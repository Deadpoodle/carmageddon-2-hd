// Back-buffer screenshots. The Direct3D driver gets DirectDraw from
// DirectDrawCreate (resolved through the loader); the primary surface's Flip
// is swapped so a requested shot reads the back buffer just before it is
// shown. Blts onto the primary are counted too, in case a screen presents
// that way.
#include "capture.h"

#include <ddraw.h>
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "driver.h"
#include "hook.h"
#include "loader.h"
#include "log.h"

namespace capture {
namespace {

// IDirectDraw(2) and IDirectDrawSurface vtable slots
enum { kCreateSurface = 6 };
enum { kBlt = 5, kFlip = 11, kGetAttachedSurface = 12, kLock = 25, kUnlock = 32 };

typedef HRESULT(WINAPI* DirectDrawCreate_t)(GUID*, LPDIRECTDRAW*, IUnknown*);
typedef HRESULT(WINAPI* CreateSurface_t)(void*, DDSURFACEDESC*, LPDIRECTDRAWSURFACE*, IUnknown*);
typedef HRESULT(WINAPI* Flip_t)(LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE, DWORD);
typedef HRESULT(WINAPI* Blt_t)(LPDIRECTDRAWSURFACE, RECT*, LPDIRECTDRAWSURFACE, RECT*, DWORD, DDBLTFX*);

DirectDrawCreate_t o_DirectDrawCreate;
CreateSurface_t o_CreateSurface, o_CreateSurface2;
Flip_t o_Flip;
Blt_t o_Blt;

LPDIRECTDRAWSURFACE g_primary;
volatile LONG g_pending;
int g_shots;
LONG g_flips, g_primaryBlts;

uintptr_t Slot(void* obj, int i) { return (uintptr_t)(*(void***)obj + i); }

int Channel(DWORD v, DWORD mask) {
    if (!mask) return 0;
    while (!(mask & 1)) mask >>= 1, v >>= 1;
    return (int)((v & mask) * 255 / mask);
}

void Save(LPDIRECTDRAWSURFACE surf) {
    DDSURFACEDESC d = {};
    d.dwSize = sizeof d;
    HRESULT hr = surf->Lock(nullptr, &d, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    if (FAILED(hr)) {
        Log("capture: Lock failed %#lx", (unsigned long)hr);
        return;
    }
    int w = d.dwWidth, h = d.dwHeight, bpp = d.ddpfPixelFormat.dwRGBBitCount;
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;  // BMP rows are padded to 4 bytes
    std::vector<BYTE> rgb(stride * h);
    for (int y = 0; y < h; y++) {
        const BYTE* row = (const BYTE*)d.lpSurface + (size_t)y * d.lPitch;
        for (int x = 0; x < w; x++) {
            DWORD v = bpp == 16 ? ((const WORD*)row)[x] : ((const DWORD*)row)[x];
            BYTE* o = &rgb[(size_t)(h - 1 - y) * stride + (size_t)x * 3];  // bottom-up BGR
            o[0] = (BYTE)Channel(v, d.ddpfPixelFormat.dwBBitMask);
            o[1] = (BYTE)Channel(v, d.ddpfPixelFormat.dwGBitMask);
            o[2] = (BYTE)Channel(v, d.ddpfPixelFormat.dwRBitMask);
        }
    }
    surf->Unlock(nullptr);

    std::string dir = GameDir() + "shots";
    CreateDirectoryA(dir.c_str(), nullptr);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%04d.bmp", dir.c_str(), g_shots++);
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (DWORD)rgb.size();
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(rgb.data(), 1, rgb.size(), f);
    fclose(f);
    Log("capture: %s (%dx%d, %d bpp)", path, w, h, bpp);
}

HRESULT WINAPI h_Flip(LPDIRECTDRAWSURFACE s, LPDIRECTDRAWSURFACE target, DWORD flags) {
    if (s == g_primary) {
        LONG n = InterlockedIncrement(&g_flips);
        if (n == 1) Log("capture: first Flip on the primary");
        static DWORD since;
        static LONG sinceFlips;
        DWORD now = GetTickCount();
        if (!since) since = now;
        if (now - since >= 5000) {  // frames presented per second, every 5 s
            Log("capture: %.1f fps", (n - sinceFlips) * 1000.0 / (now - since));
            since = now;
            sinceFlips = n;
        }
        if (InterlockedExchange(&g_pending, 0)) {
            DDSCAPS caps = {DDSCAPS_BACKBUFFER};
            LPDIRECTDRAWSURFACE back = nullptr;
            if (target) Save(target);
            else if (SUCCEEDED(s->GetAttachedSurface(&caps, &back))) {
                Save(back);
                back->Release();
            }
        }
    }
    return o_Flip(s, target, flags);
}

HRESULT WINAPI h_Blt(LPDIRECTDRAWSURFACE dst, RECT* dr, LPDIRECTDRAWSURFACE src, RECT* sr, DWORD flags, DDBLTFX* fx) {
    if (dst == g_primary && src) {
        if (InterlockedIncrement(&g_primaryBlts) == 1) Log("capture: first Blt onto the primary");
        if (InterlockedExchange(&g_pending, 0)) Save(src);
    }
    return o_Blt(dst, dr, src, sr, flags, fx);
}

// Locking the back buffer fails while the game is minimised, and nothing copes.
// The D3D driver's lock count 0x1001bbd8 goes up before Lock and stays up if it
// fails, in the rectangle copy 0x10001b00 and in DirectLock 0x10001d70. The next
// copy then finds the buffer "already locked" and writes through an unset local
// (crash at 0x10001c2c, on the first full-screen copy after Alt+Tab), and the
// game's own 2D code writes through the pixels DirectLock didn't set (0x47d21f).
// So that Lock can't fail: a failure hands back a private buffer laid out like the
// last real lock, and its Unlock does nothing. What's drawn there is never shown.
typedef HRESULT(WINAPI* Lock_t)(LPDIRECTDRAWSURFACE, RECT*, DDSURFACEDESC*, DWORD, HANDLE);
typedef HRESULT(WINAPI* Unlock_t)(LPDIRECTDRAWSURFACE, void*);
Lock_t o_Lock;
Unlock_t o_Unlock;
volatile DWORD g_failLocksUntil;
bool g_stockLocks;
DDSURFACEDESC g_backDesc;  // the back buffer's last real lock
std::vector<BYTE> g_standIn;
bool g_standInLocked;

bool IsBackBuffer(LPDIRECTDRAWSURFACE s) {
    uint8_t* base = driver::Base();
    return base && s == *(LPDIRECTDRAWSURFACE*)(base + 0x1bc3c);
}

HRESULT WINAPI h_Lock(LPDIRECTDRAWSURFACE s, RECT* r, DDSURFACEDESC* d, DWORD flags, HANDLE e) {
    bool forced = g_failLocksUntil && (int)(g_failLocksUntil - GetTickCount()) > 0;
    HRESULT hr = forced ? DDERR_SURFACELOST : o_Lock(s, r, d, flags, e);
    bool back = !g_stockLocks && !r && IsBackBuffer(s);
    if (SUCCEEDED(hr) && d && d->lpSurface) {
        if (back) g_backDesc = *d;
        return hr;
    }
    static LONG logged;
    if (InterlockedIncrement(&logged) <= 8)
        Log("capture: Lock failed: %#lx%s", (unsigned long)hr, back && g_backDesc.lpSurface ? ", drawing to a stand-in" : "");
    if (!back || !d || !g_backDesc.lpSurface || g_standInLocked) return hr;
    if (SUCCEEDED(hr)) o_Unlock(s, nullptr);  // locked, but with nowhere to draw
    g_standIn.resize((size_t)g_backDesc.lPitch * g_backDesc.dwHeight);
    *d = g_backDesc;
    d->lpSurface = g_standIn.data();
    g_standInLocked = true;
    return DD_OK;
}

HRESULT WINAPI h_Unlock(LPDIRECTDRAWSURFACE s, void* rect) {
    if (g_standInLocked && IsBackBuffer(s)) {
        g_standInLocked = false;
        return DD_OK;
    }
    return o_Unlock(s, rect);
}

HRESULT CreateSurfaceCommon(CreateSurface_t real, void* dd, DDSURFACEDESC* desc, LPDIRECTDRAWSURFACE* out,
                            IUnknown* outer) {
    HRESULT hr = real(dd, desc, out, outer);
    if (SUCCEEDED(hr) && out && *out && desc && (desc->dwFlags & DDSD_CAPS) &&
        (desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)) {
        g_primary = *out;
        Log("capture: primary surface %p (caps %#lx, back buffers %lu)", (void*)g_primary,
            (unsigned long)desc->ddsCaps.dwCaps, (unsigned long)((desc->dwFlags & DDSD_BACKBUFFERCOUNT) ? desc->dwBackBufferCount : 0));
        if (!o_Flip) {
            hook::Import(Slot(g_primary, kFlip), (Flip_t)&h_Flip, &o_Flip);
            hook::Import(Slot(g_primary, kBlt), (Blt_t)&h_Blt, &o_Blt);
            char stock[4] = "";
            g_stockLocks = GetEnvironmentVariableA("C2HD_STOCK_LOCKS", stock, sizeof stock) && stock[0] == '1';
            Log("capture: back buffer always locks: %s",
                hook::Import(Slot(g_primary, kLock), (Lock_t)&h_Lock, &o_Lock) &&
                        hook::Import(Slot(g_primary, kUnlock), (Unlock_t)&h_Unlock, &o_Unlock)
                    ? (g_stockLocks ? "left as the original (C2HD_STOCK_LOCKS)" : "on")
                    : "FAILED");
        }
    }
    return hr;
}

HRESULT WINAPI h_CreateSurface(void* dd, DDSURFACEDESC* desc, LPDIRECTDRAWSURFACE* out, IUnknown* outer) {
    return CreateSurfaceCommon(o_CreateSurface, dd, desc, out, outer);
}
HRESULT WINAPI h_CreateSurface2(void* dd, DDSURFACEDESC* desc, LPDIRECTDRAWSURFACE* out, IUnknown* outer) {
    return CreateSurfaceCommon(o_CreateSurface2, dd, desc, out, outer);
}

// IDirectDraw::SetDisplayMode (slot 21) and IDirectDraw2's (slot 21, with
// refresh rate and flags): logged, to see what the driver asks for.
typedef HRESULT(WINAPI* SetDisplayMode_t)(void*, DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* SetDisplayMode2_t)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
SetDisplayMode_t o_SetDisplayMode;
SetDisplayMode2_t o_SetDisplayMode2;
HRESULT WINAPI h_SetDisplayMode(void* dd, DWORD w, DWORD h, DWORD bpp) {
    HRESULT hr = o_SetDisplayMode(dd, w, h, bpp);
    Log("ddraw: SetDisplayMode(%lu x %lu x %lu) = %#lx", w, h, bpp, (unsigned long)hr);
    return hr;
}
HRESULT WINAPI h_SetDisplayMode2(void* dd, DWORD w, DWORD h, DWORD bpp, DWORD rate, DWORD flags) {
    HRESULT hr = o_SetDisplayMode2(dd, w, h, bpp, rate, flags);
    Log("ddraw: SetDisplayMode2(%lu x %lu x %lu, %lu Hz) = %#lx", w, h, bpp, rate, (unsigned long)hr);
    return hr;
}

HRESULT WINAPI h_DirectDrawCreate(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer) {
    HRESULT hr = o_DirectDrawCreate(guid, out, outer);
    Log("capture: DirectDrawCreate = %#lx", (unsigned long)hr);
    if (SUCCEEDED(hr) && out && *out && !o_CreateSurface) {
        hook::Import(Slot(*out, kCreateSurface), (CreateSurface_t)&h_CreateSurface, &o_CreateSurface);
        hook::Import(Slot(*out, 21), (SetDisplayMode_t)&h_SetDisplayMode, &o_SetDisplayMode);
        IDirectDraw2* dd2 = nullptr;
        if (SUCCEEDED((*out)->QueryInterface(IID_IDirectDraw2, (void**)&dd2))) {
            if (*(void***)dd2 != *(void***)*out) {
                hook::Import(Slot(dd2, kCreateSurface), (CreateSurface_t)&h_CreateSurface2, &o_CreateSurface2);
                hook::Import(Slot(dd2, 21), (SetDisplayMode2_t)&h_SetDisplayMode2, &o_SetDisplayMode2);
            }
            dd2->Release();
        }
    }
    return hr;
}

}  // namespace

void Install() { loader::Redirect("DirectDrawCreate", (void*)&h_DirectDrawCreate, (void**)&o_DirectDrawCreate); }

void FailLocks(unsigned ms) { g_failLocksUntil = GetTickCount() + ms; }

void Request() { InterlockedExchange(&g_pending, 1); }

}  // namespace capture
