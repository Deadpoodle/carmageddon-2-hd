// The wide back buffer. Three parts:
//
// 1. The device: the exe asks for W x 480 (push at 0x51c34f), and the D3D
//    driver, which only knows four fixed sizes and picks 640x480, has its
//    512x384 format row, mode picker, default mode and pixel clip pointed at
//    W x 480 in memory once BRender has mapped it (re/d3d-driver.md).
// 2. The layout: the 640x480 logical size becomes W wide (render window, HUD
//    camera passes, overlays), and every 2D element is moved to the edge it
//    belongs to: left, centre or right of a HUD area A wide, centred in the
//    buffer (re/hud-layout.md §4). All y values and sizes stay as they are.
// 3. Hooks for what is loaded later: the HEADUP.TXT text slots (each car
//    load) and the minimap position from OPTIONS.TXT (kept in 640 space in
//    the file).
#include "wide.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "frontend.h"
#include "driver.h"
#include "game.h"
#include "hook.h"
#include "loader.h"
#include "options.h"
#include "log.h"

namespace wide {
namespace {

int g_w;                     // buffer width
int g_left, g_centre, g_right;  // shifts for left-, centre- and right-anchored elements
bool g_driverPatched;

// ---------------------------------------------------------------------------
// The driver

// When the driver resolves DirectDrawCreate it is mapped and none of its code
// has run yet.
void PatchDriver(const char* name) {
    if (g_driverPatched || strcmp(name, "DirectDrawCreate") != 0) return;
    g_driverPatched = true;
    uint8_t* base = driver::Base();
    if (!base) {
        Log("wide: D3D driver not found in memory");
        return;
    }
    uintptr_t b = (uintptr_t)base;
    uint32_t w = (uint32_t)g_w;
    uint8_t* mov = base + game::DrvNo2DBufferSet;
    uint8_t was[6];
    memcpy(was, mov, 6);
    static const uint8_t nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    bool ok = was[0] == 0x89 && was[1] == 0x1d && hook::PatchU32(b + game::DrvFormat512W, 512, w) &&
              hook::PatchU32(b + game::DrvFormat512H, 384, 480) && hook::PatchU32(b + game::DrvPickerWidth, 640, w) &&
              hook::PatchU32(b + game::DrvModeWidth, 640, w) && hook::PatchU32(b + game::DrvClipX, 639, w - 1) &&
              hook::Patch((uintptr_t)mov, was, nops, 6);
    Log("wide: D3D driver at %p patched for %ux480: %s", (void*)base, w, ok ? "ok" : "FAILED (unexpected bytes)");
}

// ---------------------------------------------------------------------------
// Layout patches

int g_failures;

void Add32(uintptr_t a, int32_t expect, int32_t delta) {
    if (!hook::PatchU32(a, (uint32_t)expect, (uint32_t)(expect + delta))) {
        Log("wide: %#x is not %d", (unsigned)a, expect);
        g_failures++;
    }
}

void Set32(uintptr_t a, int32_t expect, int32_t value) { Add32(a, expect, value - expect); }

void SetFloat(uintptr_t a, float expect, float value) {
    uint32_t e, v;
    memcpy(&e, &expect, 4);
    memcpy(&v, &value, 4);
    if (!hook::PatchU32(a, e, v)) {
        Log("wide: %#x is not %.1f", (unsigned)a, expect);
        g_failures++;
    }
}

void SetBytes(uintptr_t a, std::initializer_list<uint8_t> expect, std::initializer_list<uint8_t> value) {
    if (!hook::Patch(a, expect.begin(), value.begin(), expect.size())) {
        Log("wide: %#x has unexpected bytes", (unsigned)a);
        g_failures++;
    }
}

void PatchLayout() {
    const int W = g_w, L = g_left, C = g_centre, R = g_right;

    // The logical size and the 640x480 layout entry's width (so it still matches).
    Set32(0x662248, 640, W);  // gGraf_specs[1] total width: the render window's right edge
    Set32(0x662264, 640, W);  // physical width (front screen base stays 0)
    Set32(0x662260, 640, W);  // row
    Set32(0x656478, 640, W);  // gGraf_data[1] width
    // Passes and overlays sized 640.
    {
        uint16_t e = 640, v = (uint16_t)W;
        if (!hook::Patch(0x4651b4, (const uint8_t*)&e, (const uint8_t*)&v, 2)) g_failures++;  // poly-font pass width
    }
    Set32(0x47dfed, 640, W);  // full-window tint overlays
    Set32(0x47e006, 640, W);
    Set32(0x4e57a9, 640, W);  // network-mode overlay
    Set32(0x413934, -320, -(W / 2));  // picking centre

    // HUD quads (x of each panel).
    Add32(0x68b8f0, 0, L);      // statbar 1 (speedo, gears, APO)
    Add32(0x655eb4, 128, L);    // statbar 3
    Add32(0x655f14, 149, L);    // armour (network)
    Add32(0x655edc, 270, C);    // timer left
    Add32(0x655ee8, 334, C);    // timer right
    Add32(0x655f24, 298, C);    // power (network)
    Add32(0x655e6c, 68, C);     // bottom bar (BOTHUP)
    // The two flat-colour panels next to the timer (no texture: colour
    // 0x040e4a at 44% opacity) are widened to meet it, so the top banner
    // stays one continuous strip as in 640x480 (re/hud-gaps.md).
    Add32(0x655eac, 142, C - L);  // statbar 3 width: up to the timer
    Add32(0x655ef4, 398, C);      // statbar right starts at the timer's right edge
    Add32(0x655eec, 178, R - C);  // ... and reaches the right-anchored panels
    Add32(0x655ed0, 576, R);    // statbar 5 (damage icons)
    Add32(0x655f34, 448, R);    // offence (network)
    Add32(0x655ec4, 384, R);    // HUD square
    // Per-frame translations between panels (compact and network layouts).
    SetFloat(0x44b885, 142.0f, 142.0f + (C - L));
    SetFloat(0x44b89f, -178.0f, -178.0f - (R - C));
    SetFloat(0x44b754, -448.0f, -448.0f - (R - L));
    SetFloat(0x44b7b7, -102.0f, -102.0f - (C - L));
    SetFloat(0x44b7d5, -252.0f, -252.0f - (R - L));
    Add32(0x44b95b, 335, C);  // compact-mode centred text
    // Timer text, set every frame by DoHeadups.
    Add32(0x449bd6, 389, C);
    Add32(0x449be5, 389, C);
    Add32(0x449c19, 634, R);
    Add32(0x449c28, 634, R);

    // Action replay: panel, text, bar and buttons on the right.
    Add32(0x656778, 430, R);
    Add32(0x656780, 430, R);
    Add32(0x6567c8, 445, R);
    Add32(0x65fbe4, 443, R);
    Add32(0x65fbec, 612, R);
    for (int i = 0; i < 9; i++) {
        uintptr_t r = 0x65fcb0 + i * 16;
        int32_t x0 = *(int32_t*)r, x1 = *(int32_t*)(r + 8);
        Add32(r, x0, R);
        Add32(r + 8, x1, R);
    }

    // Full-screen map: legend, the 640x480 map image centred, dots shifted (not
    // stretched), and the small 3D window that map mode shows.
    for (uintptr_t a = 0x656950; a <= 0x656988; a += 8) Add32(a, *(int32_t*)a, C);
    SetBytes(0x496d13, {0x66, 0x8b, 0x51, 0x38}, {0x66, 0xba, (uint8_t)(C & 0xff), (uint8_t)(C >> 8)});
    SetBytes(0x496d1b, {0x66, 0xf7, 0xda}, {0x90, 0x90, 0x90});
    SetBytes(0x506171, {0x14}, {0x18});
    SetBytes(0x506179, {0xc4}, {0xc0});
    SetFloat(0x48e9ad, 80.0f, 80.0f + C);
}

// ---------------------------------------------------------------------------
// HEADUP.TXT slots: loaded with each car. Slot i of view v is at
// car + 0x728 + v*0x370 + i*0x2c: +0 x, +0x14 clear-rect left, +0x1c right.

enum Anchor { kL, kC, kR };
const Anchor kSlotAnchor[15] = {kL, kL, kR, kR, kC, kC, kC, kC, kL, kC, kC, kC, kR, kR, kR};

uintptr_t g_loadHeadupSlots = 0x48ba60;  // __fastcall(file ecx, view edx, car on the stack), ret 4

void __fastcall LoadHeadupSlots(void* file, int view, uintptr_t car) {
    reinterpret_cast<void(__fastcall*)(void*, int, uintptr_t)>(g_loadHeadupSlots)(file, view, car);
    if (!car || view < 0 || view > 1) return;
    for (int i = 0; i < 15; i++) {
        int shift = kSlotAnchor[i] == kL ? g_left : kSlotAnchor[i] == kC ? g_centre : g_right;
        int32_t* slot = (int32_t*)(car + 0x728 + view * 0x370 + i * 0x2c);
        slot[0] += shift;
        slot[0x14 / 4] += shift;
        slot[0x1c / 4] += shift;
    }
}

// ---------------------------------------------------------------------------
// Minimap: OPTIONS.TXT keeps the 640x480 position; in the game it is anchored
// to the half it sits in.

int32_t* const kHeadupMapX = (int32_t*)0x74abac;
int32_t* const kHeadupMapW = (int32_t*)0x74aba8;
int g_mapShift;
void* g_saveOptions;

void AnchorMinimap() {
    // InitMiniMap snaps x to a multiple of 4, so the shift is one too, or
    // every save would move the map a few pixels.
    g_mapShift = (*kHeadupMapX + *kHeadupMapW / 2 > 320 ? g_right : g_left) & ~3;
    *kHeadupMapX += g_mapShift;
    Log("wide: minimap x %d (+%d)", *kHeadupMapX, g_mapShift);
}

void SaveOptions() {
    *kHeadupMapX -= g_mapShift;
    reinterpret_cast<void (*)()>(g_saveOptions)();
    *kHeadupMapX += g_mapShift;
}

// ---------------------------------------------------------------------------
// D3D.BDD on disk

std::string ReadFile(const std::string& path) {
    std::string d;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return d;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    fclose(f);
    return d;
}

}  // namespace

bool DriverFileMatches() {
    std::string d = ReadFile(GameDir() + "D3D.BDD");
    if (d.size() < 0x400) return false;
    uint32_t pe = *(const uint32_t*)&d[0x3c];
    if (pe + 0x18 > d.size()) return false;
    uint16_t nsec = *(const uint16_t*)&d[pe + 6], optSize = *(const uint16_t*)&d[pe + 0x14];
    auto fileOffset = [&](uint32_t rva) -> size_t {
        for (int i = 0; i < nsec; i++) {
            size_t s = pe + 0x18 + optSize + (size_t)i * 40;
            if (s + 40 > d.size()) return 0;
            uint32_t va = *(const uint32_t*)&d[s + 12], raw = *(const uint32_t*)&d[s + 16],
                     ptr = *(const uint32_t*)&d[s + 20];
            if (rva >= va && rva < va + raw) return ptr + (rva - va);
        }
        return 0;
    };
    auto u32 = [&](uint32_t rva) -> uint32_t {
        size_t o = fileOffset(rva);
        return o && o + 4 <= d.size() ? *(const uint32_t*)&d[o] : 0;
    };
    size_t mov = fileOffset(game::DrvNo2DBufferSet);
    return u32(game::DrvFormat512W) == 512 && u32(game::DrvFormat512H) == 384 && u32(game::DrvPickerWidth) == 640 &&
           u32(game::DrvPickerHeight) == 480 && u32(game::DrvModeWidth) == 640 && u32(game::DrvModeHeight) == 480 &&
           u32(game::DrvClipX) == 639 && mov && (uint8_t)d[mov] == 0x89 && (uint8_t)d[mov + 1] == 0x1d;
}

bool Install(int w, int hud) {
    g_w = w;
    if (hud < 640) hud = 640;
    if (hud > w) hud = w;
    g_left = (w - hud) / 2;
    g_right = g_left + (hud - 640);
    g_centre = (w - 640) / 2;
    if (!hook::PatchU32(game::DeviceWidthImm, 640, (uint32_t)w)) {
        Log("wide: device width push not found; staying at 640x480");
        return false;
    }
    loader::OnResolve(&PatchDriver);
    PatchLayout();
    frontend::Install(w);
    bool ok = hook::Redirect(0x489d5e, 0xe8, 0x48ba60, (const void*)&LoadHeadupSlots) &&
              hook::Redirect(0x489d6b, 0xe8, 0x48ba60, (const void*)&LoadHeadupSlots);
    options::OnLoaded(&AnchorMinimap);
    static const uint8_t saveStart[6] = {0x81, 0xec, 0x00, 0x01, 0x00, 0x00};  // sub esp, 0x100
    g_saveOptions = hook::Detour(0x48d190, saveStart, 6, (const void*)&SaveOptions);
    if (!ok || !g_saveOptions) g_failures++;
    Log("wide: buffer %dx480, HUD %d wide (shifts left %d, centre %d, right %d); %d patch failures", w, hud, g_left,
        g_centre, g_right, g_failures);
    return true;
}

}  // namespace wide
