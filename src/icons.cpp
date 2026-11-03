// Power-up icons without lines down their sides.
//
// The HUD's power-up icons are textured quads ("Billboard" models, built by
// 0x4e09d0 for each icon in LoadPowerups 0x4d96c0) under the 2D camera, with
// UVs 0..1. Their material (flags 0x2800) asks for no filtering, so the game
// drew them with hard pixels. With [Visuals] Textures smooth or sharpest
// dgVoodoo2 filters every texture, and the driver leaves D3D on WRAP
// addressing: at the quad's edges the filter blends in the icon's opposite
// column, a thin line down each side. Pulling the UVs in by half a texel keeps
// every sample inside the icon.
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

uintptr_t __fastcall Build(uintptr_t pm) {
    uintptr_t actor = kBuild(pm);
    uintptr_t model = actor ? *(uintptr_t*)(actor + 0x18) : 0;
    int w = *(uint16_t*)(pm + 0x34), h = *(uint16_t*)(pm + 0x36);
    if (!model || !w || !h) return actor;
    float du = 0.5f / w, dv = 0.5f / h;
    uint8_t* v = *(uint8_t**)(model + 8);  // 4 br_vertex of 0x28 bytes, map u,v at +0xc
    for (int i = 0; i < 4; i++) {
        float* uv = (float*)(v + i * 0x28 + 0xc);
        uv[0] = uv[0] > 0.5f ? 1 - du : du;
        uv[1] = uv[1] > 0.5f ? 1 - dv : dv;
    }
    kModelUpdate(model, 0x7fff);
    return actor;
}

}  // namespace

void Install() {
    std::string tex = settings::Get("Visuals", "Textures", "sharpest");
    if (_stricmp(tex.c_str(), "original") == 0) return;  // unfiltered: nothing bleeds
    bool ok = hook::Redirect(0x4d9a53, 0xe8, 0x4e09d0, (const void*)&Build);
    Log("icons: power-up icon edges %s", ok ? "inset" : "hook FAILED");
}

}  // namespace icons
