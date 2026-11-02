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

#include <string>

#include "dgvconf.h"
#include "game.h"
#include "hook.h"
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

    if (s.widescreen && !hook::Redirect(game::CallInitialiseCameras, 0xe8, game::InitialiseCameras,
                                        (const void*)&InitialiseCameras))
        Log("display: camera hook FAILED (unexpected bytes at %#x)", (unsigned)game::CallInitialiseCameras);
    Log("display: output %s, view aspect %.4f%s", res.c_str(), g_aspect, s.widescreen ? " (Hor+)" : " (4:3)");
}

}  // namespace display
