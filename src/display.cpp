// [Display] Resolution / Widescreen / HudWidth.
//
// The game draws at 480 lines; dgVoodoo2 renders its 3D at the output
// resolution and scales the frame to it.
//
// Widescreen=1 on a screen wider than 4:3: the back buffer is made as wide as
// the screen's shape at 480 lines (854 for 16:9, 1146 for 21:9, 1706 for
// 32:9; wide.cpp). BRender's field of view is vertical, so the 3D view keeps
// its height and widens at the sides (Hor+); the HUD keeps its 640x480 art,
// unstretched, laid out across a HUD area (HudWidth) centred on the screen.
//
// If the driver isn't the one the patch knows, the buffer stays 640x480 and
// only the main camera's aspect is widened (the HUD is then stretched).
//
// Widescreen=0: the original 4:3 picture with bars at the sides.
#include "display.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "dgvconf.h"
#include "frontend.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "settings.h"
#include "wide.h"

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

int EvenAt480(double aspect) { return 2 * (int)std::lround(480.0 * aspect / 2.0); }

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// [Visuals]: dgVoodoo2's anti-aliasing, texture filtering and mipmaps, 16-bit
// dithering, and the frame cap. The game has no frame limiter and its physics
// treat any frame under 10 ms as 10 ms (UpdateFramePeriod 0x492680), so above
// 100 fps it runs fast: the cap never goes over 100.
std::vector<dgvconf::Key> VisualKeys() {
    std::string aa = Lower(settings::Get("Visuals", "Antialiasing", "4x"));
    if (aa != "2x" && aa != "4x" && aa != "8x") aa = "off";
    std::string tex = Lower(settings::Get("Visuals", "Textures", "sharpest"));
    std::string filtering = tex == "sharpest" ? "16" : tex == "smooth" ? "trilinear" : "appdriven";
    std::string mip = tex == "original" ? "appdriven" : "autogen_bilinear";
    bool smooth = Lower(settings::Get("Visuals", "Colour", "smooth")) != "16-bit";
    std::string rate = Lower(settings::Get("Visuals", "FrameRate", "auto"));
    DEVMODEA dm = {};
    dm.dmSize = sizeof dm;
    int hz = EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1
                 ? (int)dm.dmDisplayFrequency : 60;
    int cap = rate == "60" ? 60 : (hz < 100 ? hz : 100);
    bool vsync = cap == hz;  // tear-free when the screen's own rate is within the cap
    Log("display: visuals: AA %s, filtering %s, mipmaps %s, colour %s, %d fps cap (screen %d Hz, vsync %s)",
        aa.c_str(), filtering.c_str(), mip.c_str(), smooth ? "smooth" : "16-bit", cap, hz, vsync ? "on" : "off");
    return {{"DirectX", "Antialiasing", aa},
            {"DirectX", "Filtering", filtering},
            {"DirectX", "Mipmapping", mip},
            {"DirectXExt", "Dithering", smooth ? "disabled" : "appdriven"},
            {"GeneralExt", "FPSLimit", std::to_string(cap)},
            {"DirectX", "ForceVerticalSync", vsync ? "true" : "false"}};
}

// The visuals, the mouse and the kind of full screen. The mouse is mapped from
// the window's own pixels (frontend.cpp), so dgVoodoo2 must leave the
// physical cursor free in the window: by default it keeps it inside the game's
// resolution (854x480 of a 2560x1440 screen, the top-left third).
// CaptureMouse (on by default) still keeps it in the window. Full screen is a
// screen-size borderless window ("fake") unless [Display] Fullscreen says
// exclusive: over an exclusive full screen Windows can't show its Alt+Tab
// switcher (it switches, unseen, when Alt is let go).
std::vector<dgvconf::Key> With(std::vector<dgvconf::Key> keys) {
    for (auto& k : VisualKeys()) keys.push_back(k);
    keys.push_back({"GeneralExt", "FreeMouse", "true"});
    // Hidden and windowed tests (C2HD_HEADLESS=1/2; this runs before the test
    // harness starts) never want dgVoodoo2's screen-size window.
    char test[4] = "";
    GetEnvironmentVariableA("C2HD_HEADLESS", test, sizeof test);
    bool windowedTest = test[0] == '1' || test[0] == '2';
    bool exclusive = Lower(settings::Get("Display", "Fullscreen", "borderless")) == "exclusive" || windowedTest;
    keys.push_back({"GeneralExt", "FullscreenAttributes", exclusive ? "" : "fake"});
    return keys;
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
    double shape = (double)w / h;
    std::string res = std::to_string(w) + "x" + std::to_string(h);

    if (!s.widescreen || shape <= 4.0 / 3.0 + 0.01) {
        // The original 4:3 picture at full height, with bars at the sides.
        int w43 = h * 4 / 3;
        if (w43 > w) w43 = w, h = w * 3 / 4;
        res = std::to_string(w43) + "x" + std::to_string(h);
        dgvconf::Sync(With({{"DirectX", "Resolution", res}, {"General", "ScalingMode", "stretched_ar"}}));
        frontend::Install(640);
        Log("display: output %s, 640x480 (4:3)", res.c_str());
        return;
    }

    int bufW = EvenAt480(shape);
    // The HUD area: the screen's shape, up to 21:9 (beyond that the HUD would
    // sit far out at the edges of a 32:9 screen), or as set.
    double hudShape = s.hudAspect > 0 ? s.hudAspect : (shape < 21.0 / 9.0 ? shape : 21.0 / 9.0);
    int hudW = EvenAt480(hudShape > 10 ? 10 : hudShape);
    if (wide::DriverFileMatches() && wide::Install(bufW, hudW)) {
        dgvconf::Sync(With({{"DirectX", "Resolution", res},
                       {"General", "ScalingMode", "stretched_ar"},
                       {"DirectXExt", "ExtraEnumeratedResolutions", std::to_string(bufW) + "x480"}}));
        Log("display: output %s, buffer %dx480 (Hor+)", res.c_str(), bufW);
        return;  // the game sets the camera aspect from the buffer itself
    }

    // Fallback: 640x480 stretched to the screen, with the view widened.
    g_aspect = (float)shape;
    frontend::Install(640);
    dgvconf::Sync(With({{"DirectX", "Resolution", res}, {"General", "ScalingMode", "stretched_ar"}}));
    if (!hook::Redirect(game::CallInitialiseCameras, 0xe8, game::InitialiseCameras, (const void*)&InitialiseCameras))
        Log("display: camera hook FAILED (unexpected bytes at %#x)", (unsigned)game::CallInitialiseCameras);
    Log("display: D3D.BDD not the expected driver: output %s, 640x480 stretched, view aspect %.4f", res.c_str(),
        g_aspect);
}

}  // namespace display
