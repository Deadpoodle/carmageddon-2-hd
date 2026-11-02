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
    std::vector<BYTE> rgb((size_t)w * h * 3);
    for (int y = 0; y < h; y++) {
        const BYTE* row = (const BYTE*)d.lpSurface + (size_t)y * d.lPitch;
        for (int x = 0; x < w; x++) {
            DWORD v = bpp == 16 ? ((const WORD*)row)[x] : ((const DWORD*)row)[x];
            BYTE* o = &rgb[((size_t)(h - 1 - y) * w + x) * 3];  // bottom-up BGR
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
    if (w * 3 % 4) {
        Log("capture: width %d not 4-aligned, skipped", w);
        return;
    }
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
        if (InterlockedIncrement(&g_flips) == 1) Log("capture: first Flip on the primary");
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

HRESULT WINAPI h_DirectDrawCreate(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer) {
    HRESULT hr = o_DirectDrawCreate(guid, out, outer);
    Log("capture: DirectDrawCreate = %#lx", (unsigned long)hr);
    if (SUCCEEDED(hr) && out && *out && !o_CreateSurface) {
        hook::Import(Slot(*out, kCreateSurface), (CreateSurface_t)&h_CreateSurface, &o_CreateSurface);
        IDirectDraw2* dd2 = nullptr;
        if (SUCCEEDED((*out)->QueryInterface(IID_IDirectDraw2, (void**)&dd2))) {
            if (*(void***)dd2 != *(void***)*out)
                hook::Import(Slot(dd2, kCreateSurface), (CreateSurface_t)&h_CreateSurface2, &o_CreateSurface2);
            dd2->Release();
        }
    }
    return hr;
}

}  // namespace

void Install() { loader::Redirect("DirectDrawCreate", (void*)&h_DirectDrawCreate, (void**)&o_DirectDrawCreate); }

void Request() { InterlockedExchange(&g_pending, 1); }

}  // namespace capture
