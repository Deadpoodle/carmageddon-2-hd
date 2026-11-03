// Power-up icons and menu pictures without lines along their edges.
//
// The HUD's power-up icons are textured quads ("Billboard" models, built by
// 0x4e09d0 for each icon in LoadPowerups 0x4d96c0) under the 2D camera, with
// UVs 0..1. Their material (flags 0x2800) asks for no filtering, so the game
// drew them with hard pixels. With [Visuals] Textures smooth or sharpest
// dgVoodoo2 filters every texture, and the driver leaves D3D on WRAP
// addressing: at the quad's edges the filter blends in the icon's opposite
// column, a thin line down each side. Pulling the UVs in by half a texel keeps
// every sample inside the icon.
//
// The front end's image buttons have the same fault. FE_CreateButton
// 0x46c5c0 (fastcall: ecx = button record of g_buttons 0x687248, edx = x;
// stack: y, w, h, a value for the material (+8), pixelmap, one more;
// ret 0x18) gives each a quad with UVs from 0 to w / pow2(image width),
// material flags 0x2800, and a copy of the image (record +0xc) as its
// texture. The main menu's car picture is six of them, 64x64 tiles
// <car>A..F.TIF (0x46aa20 copies them into the buttons' textures), so with
// smoothing a 3x2 grid of lines showed across the car. After the game builds
// an image button, its UVs are pulled in by half a texel too.
#include "icons.h"

#include <windows.h>

#include <cstdint>
#include <string>

#include "hook.h"
#include "log.h"
#include "settings.h"

namespace icons {
namespace {

using BuildFn = uintptr_t(__fastcall*)(uintptr_t);
const auto kBuild = (BuildFn)0x4e09d0;  // builds a Billboard actor for a pixelmap (ecx)
const auto kModelUpdate = (void(__cdecl*)(uintptr_t, int))0x51f950;  // BrModelUpdate

// Pull the quad's UVs in by half a texel of the w x h texture: the edges at
// 0 move up, the others (1 for the icons, w / pow2 for the buttons) down.
void Inset(uintptr_t model, int w, int h) {
    float du = 0.5f / w, dv = 0.5f / h;
    uint8_t* v = *(uint8_t**)(model + 8);  // 4 br_vertex of 0x28 bytes, map u,v at +0xc
    for (int i = 0; i < 4; i++) {
        float* uv = (float*)(v + i * 0x28 + 0xc);
        uv[0] = uv[0] > 0 ? uv[0] - du : du;
        uv[1] = uv[1] > 0 ? uv[1] - dv : dv;
    }
    kModelUpdate(model, 0x7fff);
}

uintptr_t __fastcall Build(uintptr_t pm) {
    uintptr_t actor = kBuild(pm);
    uintptr_t model = actor ? *(uintptr_t*)(actor + 0x18) : 0;
    int w = *(uint16_t*)(pm + 0x34), h = *(uint16_t*)(pm + 0x36);
    if (model && w && h) Inset(model, w, h);
    return actor;
}

using CreateButtonFn = void(__fastcall*)(uintptr_t, int, int, int, int, int, uintptr_t, int);
CreateButtonFn o_CreateButton;

void __fastcall CreateButton(uintptr_t rec, int x, int y, int w, int h, int colour, uintptr_t pm, int a6) {
    o_CreateButton(rec, x, y, w, h, colour, pm, a6);
    uintptr_t model = *(uintptr_t*)(rec + 4), tex = *(uintptr_t*)(rec + 0xc);
    if (!pm || !model || !tex) return;  // no image: a plain 8x8 colour quad
    int tw = *(uint16_t*)(tex + 0x34), th = *(uint16_t*)(tex + 0x36);
    if (tw && th) Inset(model, tw, th);
}

}  // namespace

void Install() {
    std::string tex = settings::Get("Visuals", "Textures", "sharpest");
    if (_stricmp(tex.c_str(), "original") == 0) return;  // unfiltered: nothing bleeds
    bool ok = hook::Redirect(0x4d9a53, 0xe8, 0x4e09d0, (const void*)&Build);
    Log("icons: power-up icon edges %s", ok ? "inset" : "hook FAILED");
    // push ecx; push ebp; push esi; xor ebp,ebp; push edi
    static const uint8_t start[6] = {0x51, 0x55, 0x56, 0x33, 0xed, 0x57};
    o_CreateButton = (CreateButtonFn)hook::Detour(0x46c5c0, start, 6, (const void*)&CreateButton);
    Log("icons: menu image edges %s", o_CreateButton ? "inset" : "hook FAILED");
}

}  // namespace icons
