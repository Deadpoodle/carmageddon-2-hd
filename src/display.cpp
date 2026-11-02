// [Display] Resolution / Widescreen.
//
// The game draws a 640x480 frame (re/display.md). dgVoodoo2 renders its 3D at
// the forced resolution and scales the frame to it. On a wider shape that
// would stretch the picture, so the main camera's aspect is set to the output
// shape instead of 4/3: BRender's field of view is vertical, so the view keeps
// its height and widens at the sides (Hor+), and the track culling and sky,
// which read the same field, widen with it (re/camera-hud.md, plan A). The
// aspect is written once, after the cameras are created and before the first
// frame; nothing in the game rewrites it in the external views.
//
// The HUD and menus are still drawn in 640x480 and so are stretched in this
// mode; Widescreen=0 keeps the whole frame 4:3 with bars at the sides.
#include "display.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "dgvconf.h"
#include "game.h"
#include "hook.h"
#include "loader.h"
#include "log.h"
#include "settings.h"

namespace display {
namespace {

float g_aspect = 640.0f / 480.0f;

float* CameraAspect(int i) {
    uintptr_t actor = ((uintptr_t*)game::CameraList)[i];
    if (!actor) return nullptr;
    uintptr_t cam = *(uintptr_t*)(actor + game::ActorTypeData);
    return cam ? (float*)(cam + game::CameraAspect) : nullptr;
}

void InitialiseCameras() {
    reinterpret_cast<void (*)()>(game::InitialiseCameras)();
    for (int i = 0; i < 2; i++)
        if (float* a = CameraAspect(i)) {
            Log("display: camera %d aspect %.4f -> %.4f", i, *a, g_aspect);
            *a = g_aspect;
        }
}

// ---------------------------------------------------------------------------
// A wider device (re/d3d-driver.md). The D3D driver offers four fixed screen
// formats and its mode picker only looks for 640x480, so the 512x384 format
// row is turned into the wide size and the picker, default mode and pixel
// clip are pointed at it. Done in memory, when the driver resolves
// DirectDrawCreate: it is mapped by then and none of its code has run.

int g_deviceW, g_deviceH = 480;  // 0 = stock 640x480
bool g_driverPatched;

uint8_t* FindDriver() {
    static const uint8_t sig[] = {0xeb, 0x6f, 0x8b, 0x7e, 0x0c, 0xb8, 0x80, 0x02, 0x00,
                                  0x00, 0x3b, 0xf8, 0xb9, 0xe0, 0x01, 0x00, 0x00};
    MEMORY_BASIC_INFORMATION mbi;
    for (uint8_t* p = (uint8_t*)0x10000; p < (uint8_t*)0x7fff0000; p = (uint8_t*)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery(p, &mbi, sizeof mbi)) break;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || mbi.Type == MEM_IMAGE) continue;
        uint8_t* b = (uint8_t*)mbi.BaseAddress;
        for (size_t i = 0; i + sizeof sig <= mbi.RegionSize; i++)
            if (b[i] == sig[0] && memcmp(b + i, sig, sizeof sig) == 0) return b + i - game::DrvSigRva;
    }
    return nullptr;
}

void PatchDriver(const char* name) {
    if (g_driverPatched || !g_deviceW || strcmp(name, "DirectDrawCreate") != 0) return;
    g_driverPatched = true;
    uint8_t* base = FindDriver();
    if (!base) {
        Log("display: D3D driver not found in memory; staying at 640x480");
        return;
    }
    uintptr_t b = (uintptr_t)base;
    uint32_t w = (uint32_t)g_deviceW, h = (uint32_t)g_deviceH;
    auto at = [b](uint32_t rva) { return *(const uint32_t*)(b + rva); };
    uint8_t* mov = base + game::DrvNo2DBufferSet;
    if (at(game::DrvFormat512W) != 512 || at(game::DrvFormat512H) != 384 || at(game::DrvPickerWidth) != 640 ||
        at(game::DrvPickerHeight) != 480 || at(game::DrvModeWidth) != 640 || at(game::DrvModeHeight) != 480 ||
        at(game::DrvClipX) != 639 || mov[0] != 0x89 || mov[1] != 0x1d) {
        Log("display: D3D driver at %p has unexpected bytes; staying at 640x480", (void*)base);
        return;  // nothing changed: the game falls back to its own size
    }
    bool ok = hook::PatchU32(b + game::DrvFormat512W, 512, w) && hook::PatchU32(b + game::DrvFormat512H, 384, h) &&
              hook::PatchU32(b + game::DrvPickerWidth, 640, w) && hook::PatchU32(b + game::DrvPickerHeight, 480, h) &&
              hook::PatchU32(b + game::DrvModeWidth, 640, w) && hook::PatchU32(b + game::DrvModeHeight, 480, h) &&
              hook::PatchU32(b + game::DrvClipX, 639, w - 1);
    // Never switch on the driver's static 640x480 2D buffer (it would overflow):
    // `mov [abs32], ebx` (its address relocated) becomes six NOPs.
    uint8_t was[6];
    memcpy(was, mov, 6);
    static const uint8_t nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    ok = ok && hook::Patch((uintptr_t)mov, was, nops, 6);
    Log("display: D3D driver at %p patched for %ux%u: %s", (void*)base, w, h, ok ? "ok" : "FAILED (unexpected bytes)");
}

// The exe's width push runs before the driver is loaded, so whether the driver
// can be patched is decided from D3D.BDD on disk, before anything is changed.
bool DriverFileMatches() {
    FILE* f = fopen((GameDir() + "D3D.BDD").c_str(), "rb");
    if (!f) return false;
    std::string d;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    fclose(f);
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

}  // namespace

void Install() {
    // Always the Direct3D driver, so dgVoodoo2 renders it (the Glide driver is
    // capped at 640x480 and nGlide's 63 fps): the -d3d switch, set before the
    // game reads its command line, so the store's Play button works as is.
    if (hook::PatchU32(game::RendererDeviceName, game::NameGlide, game::NameD3D))
        Log("display: renderer set to D3D");
    else
        Log("display: renderer name pointer not at its default; left as it is");

    const settings::Display& s = settings::GetDisplay();
    int w, h;
    settings::OutputSize(&w, &h);
    std::string res;
    if (s.widescreen) {
        g_aspect = (float)w / (float)h;
        res = std::to_string(w) + "x" + std::to_string(h);
    } else {
        // The original 4:3 picture at full height, with bars at the sides.
        int w43 = h * 4 / 3;
        if (w43 > w) w43 = w, h = w * 3 / 4;
        res = std::to_string(w43) + "x" + std::to_string(h);
    }
    dgvconf::Sync({{"DirectX", "Resolution", res}, {"General", "ScalingMode", "stretched_ar"}});

    // Experiment (test switch only): a wider device buffer at 480 lines.
    char wide[16] = "";
    GetEnvironmentVariableA("C2HD_WIDE", wide, sizeof wide);
    if (int ww = atoi(wide)) {
        if (!DriverFileMatches()) {
            Log("display: EXPERIMENT: D3D.BDD is not the expected driver; staying at 640x480");
        } else if (hook::PatchU32(game::DeviceWidthImm, 640, (uint32_t)ww)) {
            g_deviceW = ww;
            loader::OnResolve(&PatchDriver);
            dgvconf::Sync({{"DirectXExt", "ExtraEnumeratedResolutions", std::to_string(ww) + "x480"}});
            Log("display: EXPERIMENT device %dx480", ww);
        }
    }

    if (s.widescreen && !hook::Redirect(game::CallInitialiseCameras, 0xe8, game::InitialiseCameras,
                                        (const void*)&InitialiseCameras))
        Log("display: camera hook FAILED (unexpected bytes at %#x)", (unsigned)game::CallInitialiseCameras);
    Log("display: output %s, view aspect %.4f%s", res.c_str(), g_aspect, s.widescreen ? " (Hor+)" : " (4:3)");
}

}  // namespace display
