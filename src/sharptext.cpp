// [Visuals] SharpText=on: the poly-font pages and the HUD art drawn from
// textures twice their size, enlarged with Scale2x (re/textures.md §1-§3, §6).
//
// dgVoodoo2 draws the font and HUD quads at the screen's resolution, but their
// textures are tiny (64x64 pages of ARGB4444 glyphs, 32-128 px HUD art), so
// text and HUD look soft at 1440p. Enlarging those pixelmaps in place would
// break the game, which uses their sizes for layout and CPU blits. Instead
// each one, P, gets a twin T at 2w x 2h that only the renderer sees:
// BrMapUpdate (0x523160) is detoured, and for a twinned P it runs on T holding
// P's stored slot (+0x40), so the driver's texture object is built from T's
// pixels and size, while every material still points at P and picks up
// P->stored. UVs are fractions of the texture (pages /64, HUD quads
// w/pow2(w)), so nothing else changes. All CPU drawing (glyph packing, rev
// digits, damage icons, the armour/power/offence bars) stays on P, and T is
// rebuilt from P on each BrMapUpdate, which is cheap at these sizes.
//
// Only a whitelist gets twins: the font and car-icon pages (marked at their
// BrMapAdd) and the HUD textures loaded by InitHeadups. Race and world
// textures, buttons and the movie tiles go through BrMapUpdate untouched.
#include "sharptext.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "driver.h"
#include "hook.h"
#include "loader.h"
#include "log.h"
#include "settings.h"

namespace sharptext {
namespace {

// br_pixelmap fields (BRender 1.3 layout, re/textures.md §3).
inline uint16_t* Pixels(uintptr_t pm) { return *(uint16_t**)(pm + 8); }
inline int RowBytes(uintptr_t pm) { return *(int16_t*)(pm + 0x28); }
inline int MipOffset(uintptr_t pm) { return *(uint16_t*)(pm + 0x2a); }
inline int Type(uintptr_t pm) { return *(uint8_t*)(pm + 0x2c); }
inline int BaseX(uintptr_t pm) { return *(uint16_t*)(pm + 0x30); }
inline int BaseY(uintptr_t pm) { return *(uint16_t*)(pm + 0x32); }
inline int Width(uintptr_t pm) { return *(uint16_t*)(pm + 0x34); }
inline int Height(uintptr_t pm) { return *(uint16_t*)(pm + 0x36); }
inline uintptr_t& Stored(uintptr_t pm) { return *(uintptr_t*)(pm + 0x40); }

// BRender and game functions.
using MapUpdateFn = void(__cdecl*)(uintptr_t, int);
using FreeFn = void(__cdecl*)(uintptr_t);
using MapAddFn = uintptr_t(__cdecl*)(uintptr_t);
using CopyFn = void(__fastcall*)(uintptr_t, int, int, uintptr_t, int, int, int, int);
MapUpdateFn g_mapUpdate;  // trampoline of BrMapUpdate 0x523160
FreeFn g_free;            // trampoline of BrPixelmapFree 0x537dc0
const auto kAllocate = (uintptr_t(__cdecl*)(int, int, int, void*, int))0x538d80;  // BrPixelmapAllocate
const auto kResAdd = (uintptr_t(__cdecl*)(uintptr_t, uintptr_t))0x5278e0;         // BrResAdd(parent, child)
const auto kMapAdd = (MapAddFn)0x51efa0;                                           // BrMapAdd
const auto kPageCopy = (CopyFn)0x515690;  // DRPixelmapRectangleCopy (fastcall dst, dx; dy, src, sx, sy, w, h)
uintptr_t* const kRenderer = (uintptr_t*)0x79efec;  // BRender's current renderer (0 before the device exists)

// The HUD textures (InitHeadups 0x449090 stores each global before its
// BrMapAdd; it runs once, at start-up).
const uintptr_t kHudGlobals[] = {
    0x74ca80, 0x74ca44,            // timer left/right (64x64, static)
    0x67c4f8, 0x67fcfc, 0x67f87c,  // hudcurve1..3 (static)
    0x74cf00,                      // statbar5: greytop5 + damage icons, redrawn every other frame
    0x74cf84, 0x74cab4, 0x74cf5c,  // armour / power / offence bars, redrawn on change
    // rev1..rev13: the rev counter frames; the current one gets the gear digit
    0x67fd00, 0x67fd04, 0x67fd08, 0x67fd0c, 0x67fd10, 0x67fd14, 0x67fd18, 0x67fd1c, 0x67fd20, 0x67fd24,
    0x67fd28, 0x67fd2c, 0x67fd30,
};

struct Twin {
    uintptr_t t;      // the twin pixelmap: a BRender resource child of P, freed with it
    uint16_t* src;    // P's pixels, w, h and type when twinned: an address reused
    int w, h, type;   // by a new pixelmap must not find the old twin
    int gx, gy;       // cell grid (glyph cells on a page): Scale2x never looks across it
};
std::unordered_map<uintptr_t, Twin> g_twins;
std::unordered_set<uintptr_t> g_pages;     // font and car-icon pages, marked at their BrMapAdd
std::unordered_set<uintptr_t> g_rejected;  // the driver took no texture from the twin: left original
size_t g_bytes;                            // twin pixel memory
int g_made, g_pagesMade;

// ---------------------------------------------------------------------------
// Scale2x (the EPX / AdvanceMAME rule) on whole 16-bit texels, alpha
// included. Each output texel is a copy of a source texel, so transparent
// texels keep their colour (fonts 0x0FFF, HUD 0x0004) and nothing fringes.
// At the edges of the texture and of each gx x gy cell the missing neighbour
// is the centre texel, so neighbouring glyphs never bleed into each other.

void Scale2x(const uint16_t* s, int srow, int w, int h, int gx, int gy, uint16_t* d) {
    const int drow = 2 * w;
    for (int y = 0; y < h; y++) {
        int cy0 = y - y % gy, cy1 = (cy0 + gy < h ? cy0 + gy : h) - 1;
        const uint16_t* row = s + y * srow;
        const uint16_t* up = y > cy0 ? row - srow : row;
        const uint16_t* dn = y < cy1 ? row + srow : row;
        uint16_t* d0 = d + 2 * y * drow;
        uint16_t* d1 = d0 + drow;
        for (int x = 0; x < w; x++) {
            int cx0 = x - x % gx, cx1 = (cx0 + gx < w ? cx0 + gx : w) - 1;
            uint16_t e = row[x], b = up[x], hh = dn[x];
            uint16_t dl = x > cx0 ? row[x - 1] : e, f = x < cx1 ? row[x + 1] : e;
            uint16_t e0 = e, e1 = e, e2 = e, e3 = e;
            if (b != hh && dl != f) {
                if (dl == b) e0 = dl;
                if (b == f) e1 = f;
                if (dl == hh) e2 = dl;
                if (hh == f) e3 = f;
            }
            d0[2 * x] = e0;
            d0[2 * x + 1] = e1;
            d1[2 * x] = e2;
            d1[2 * x + 1] = e3;
        }
    }
}

// ---------------------------------------------------------------------------
// Which pixelmaps get twins

// 16-bit types the driver takes (0x100081e0): 4 RGB555, 5 RGB565, 0x12
// ARGB4444. The twin must stay within its 256 limit, and P must be a plain
// pixelmap (no mipmaps, no sub-rectangle).
bool Suitable(uintptr_t pm) {
    int type = Type(pm), w = Width(pm), h = Height(pm);
    return (type == 4 || type == 5 || type == 0x12) && w >= 8 && h >= 8 && 2 * w <= 256 && 2 * h <= 256 &&
           Pixels(pm) && RowBytes(pm) >= 2 * w && !MipOffset(pm) && !BaseX(pm) && !BaseY(pm);
}

bool Wanted(uintptr_t pm) {
    if (g_rejected.count(pm) || !Suitable(pm)) return false;
    if (g_pages.count(pm)) return true;
    for (uintptr_t g : kHudGlobals)
        if (*(uintptr_t*)g == pm) return true;
    return false;
}

bool Same(const Twin& tw, uintptr_t pm) {
    return tw.src == Pixels(pm) && tw.w == Width(pm) && tw.h == Height(pm) && tw.type == Type(pm);
}

Twin* Make(uintptr_t pm) {
    int w = Width(pm), h = Height(pm), type = Type(pm);
    uintptr_t t = kAllocate(type, 2 * w, 2 * h, nullptr, 0);
    if (!t) return nullptr;
    // The driver copies rows w*2 bytes apart (0x100081e0): the twin must be tight.
    if (!Pixels(t) || RowBytes(t) != 4 * w || Width(t) != 2 * w || Height(t) != 2 * h || Type(t) != type) {
        g_free(t);
        return nullptr;
    }
    kResAdd(pm, t);
    Stored(t) = 0;
    g_bytes += (size_t)4 * w * h * 2;
    g_made++;
    bool page = g_pages.count(pm) != 0;
    if (page) g_pagesMade++;
    else Log("sharptext: twin %dx%d -> %dx%d (type %#x) for HUD pixelmap %p", w, h, 2 * w, 2 * h, type, (void*)pm);
    return &(g_twins[pm] = Twin{t, Pixels(pm), w, h, type, w, h});
}

// ---------------------------------------------------------------------------
// Cost, logged every few seconds while twins are being rebuilt

LARGE_INTEGER g_freq;
int64_t g_ticks;
int g_rescales;
DWORD g_lastLog;

void Rescale(uintptr_t pm, const Twin& tw) {
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    Scale2x(tw.src, RowBytes(pm) / 2, tw.w, tw.h, tw.gx, tw.gy, Pixels(tw.t));
    QueryPerformanceCounter(&b);
    g_ticks += b.QuadPart - a.QuadPart;
    g_rescales++;
    DWORD now = GetTickCount();
    if (now - g_lastLog >= 5000) {
        if (g_lastLog && g_freq.QuadPart) {
            double ms = g_ticks * 1000.0 / g_freq.QuadPart;
            Log("sharptext: %d twins (%d pages), %u KB; %d rescales in %.1f s: %.1f ms (%.1f us each)", g_made,
                g_pagesMade, (unsigned)(g_bytes / 1024), g_rescales, (now - g_lastLog) / 1000.0, ms,
                ms * 1000.0 / g_rescales);
        }
        g_lastLog = now;
        g_ticks = 0;
        g_rescales = 0;
    }
}

// ---------------------------------------------------------------------------
// Hooks

// BrMapUpdate(pm, flags). For a twin the original runs on T holding P's
// stored slot: the first time it builds the driver's texture from T (pixels,
// size, format), later it only marks it dirty and the driver re-reads T's
// pixels at the next bind. T's buffer never moves while P lives.
void __cdecl MapUpdate(uintptr_t pm, int flags) {
    Twin* tw = nullptr;
    if (pm) {
        auto it = g_twins.find(pm);
        if (it != g_twins.end()) {
            // No stored texture: P was BrMapRemoved, or this is a new pixelmap
            // at a freed one's address. Either way, start again.
            if (Stored(pm) && Same(it->second, pm)) tw = &it->second;
            else g_twins.erase(it);
        }
        if (!tw && !Stored(pm) && *kRenderer && Wanted(pm)) {
            tw = Make(pm);
            if (!tw) {
                g_rejected.insert(pm);
                Log("sharptext: no twin for %p (%dx%d, type %#x): left as it is", (void*)pm, Width(pm), Height(pm),
                    Type(pm));
            }
        }
    }
    if (!tw) return g_mapUpdate(pm, flags);
    Rescale(pm, *tw);
    uintptr_t t = tw->t;
    Stored(t) = Stored(pm);
    g_mapUpdate(t, flags);
    Stored(pm) = Stored(t);
    Stored(t) = 0;
    if (!Stored(pm)) {
        // The driver made no texture from the twin: P gets its own, as before.
        g_rejected.insert(pm);
        g_twins.erase(pm);
        Log("sharptext: the driver refused the twin of %p: left as it is", (void*)pm);
        g_mapUpdate(pm, flags);
    }
}

// BrPixelmapFree(pm): forget P (its twin is freed with it, as its child).
void __cdecl PixelmapFree(uintptr_t pm) {
    if (pm) {
        auto it = g_twins.find(pm);
        if (it != g_twins.end()) {
            g_bytes -= (size_t)4 * it->second.w * it->second.h * 2;
            g_twins.erase(it);
        }
        g_pages.erase(pm);
        g_rejected.erase(pm);
    }
    g_free(pm);
}

// The BrMapAdd of a new font page (0x4646d5) or car-icon page (0x4974ad):
// mark it, so the BrMapUpdate inside makes its twin.
uintptr_t __cdecl MapAddPage(uintptr_t pm) {
    if (pm) g_pages.insert(pm);
    return kMapAdd(pm);
}

int Gcd(int a, int b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b) {
        int r = a % b;
        a = b;
        b = r;
    }
    return a;
}

// A glyph or car icon copied into a page (0x464742, 0x4974d7): the page's cell
// grid becomes the common divisor of every rect copied into it, so Scale2x
// keeps each cell to itself. The BrMapUpdate that follows rebuilds the twin.
void __fastcall PageCopy(uintptr_t dst, int dx, int dy, uintptr_t src, int sx, int sy, int w, int h) {
    kPageCopy(dst, dx, dy, src, sx, sy, w, h);
    auto it = g_twins.find(dst);
    if (it == g_twins.end()) return;
    Twin& tw = it->second;
    int gx = Gcd(Gcd(tw.gx, (int16_t)dx), w & 0xffff), gy = Gcd(Gcd(tw.gy, (int16_t)dy), h & 0xffff);
    tw.gx = gx > 0 ? gx : 1;
    tw.gy = gy > 0 ? gy : 1;
}

void PatchDriver(const char* name) {
    static bool done;
    if (done || strcmp(name, "DirectDrawCreate") != 0) return;
    done = true;
    if (!driver::Base()) {
        Log("sharptext: D3D driver not found; its texture cache stays as it is");
        return;
    }
    // Without more slots the 128x128 page twins would share 12 and thrash
    // (correct, but slower), and 256x256 HUD twins would be stored at 128.
    Log("sharptext: driver texture cache (96 x 128x128, 32 x 256x256): %s",
        driver::PatchTextureCache() ? "ok" : "not patched");
}

}  // namespace

void Install() {
    std::string mode = settings::Get("Visuals", "SharpText", "on");
    if (_stricmp(mode.c_str(), "off") == 0 || mode == "0") {
        Log("sharptext: off");
        return;
    }
    QueryPerformanceFrequency(&g_freq);
    // Every site is checked before anything is patched.
    static const uint8_t mapUpdate[10] = {0x83, 0xec, 0x18, 0x56, 0x8b, 0x35, 0xec, 0xef, 0x79, 0x00};
    static const uint8_t pixelmapFree[5] = {0x56, 0x8b, 0x74, 0x24, 0x08};
    auto isCall = [](uintptr_t site, uintptr_t target) {
        return *(uint8_t*)site == 0xe8 && site + 5 + *(int32_t*)(site + 1) == target;
    };
    if (!hook::Matches(0x523160, mapUpdate, 10) || !hook::Matches(0x537dc0, pixelmapFree, 5) ||
        !isCall(0x4646d5, 0x51efa0) || !isCall(0x4974ad, 0x51efa0) || !isCall(0x464742, 0x515690) ||
        !isCall(0x4974d7, 0x515690)) {
        Log("sharptext: code not as expected; text and HUD stay original");
        return;
    }
    g_free = (FreeFn)hook::Detour(0x537dc0, pixelmapFree, 5, (const void*)&PixelmapFree);
    g_mapUpdate = g_free ? (MapUpdateFn)hook::Detour(0x523160, mapUpdate, 10, (const void*)&MapUpdate) : nullptr;
    bool ok = g_mapUpdate && hook::Redirect(0x4646d5, 0xe8, 0x51efa0, (const void*)&MapAddPage) &&
              hook::Redirect(0x4974ad, 0xe8, 0x51efa0, (const void*)&MapAddPage) &&
              hook::Redirect(0x464742, 0xe8, 0x515690, (const void*)&PageCopy) &&
              hook::Redirect(0x4974d7, 0xe8, 0x515690, (const void*)&PageCopy);
    loader::OnResolve(&PatchDriver);
    Log("sharptext: 2x text and HUD textures %s", ok ? "on" : "hook FAILED");
}

}  // namespace sharptext
