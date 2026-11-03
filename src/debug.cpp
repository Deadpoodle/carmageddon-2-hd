// A debug overlay in races ([Debug] Overlay, toggled with F2): a few lines
// of numbers on the right, above the minimap, for chasing problems such as
// the minimap noise.
//
// It is drawn with BRender's own bitmap text (BrPixelmapText 0x538a10, its
// built-in 7x9 font), white with a black shadow, straight into the back
// screen, as the minimap is (DoHeadups' plain text goes to the render screen
// 0x762128, which doesn't reach the display in the Direct3D build). The HUD's poly fonts were tried first:
// glyph quads from shared texture pages, which left stray bars beside some
// words. It is drawn right after the HUD's text pass: the one call of
// DoHeadups 0x449b10 (fastcall car, plain ret), at 0x4e53fe in the race's
// render 0x4e4e40, is redirected to a wrapper that calls it and then adds the
// overlay's lines. F2 is free in the game: in sets A-D it maps to keymap line
// 29, which does nothing, and set E leaves it unbound.
#include "debug.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "hook.h"
#include "log.h"
#include "settings.h"
#include "version.h"

namespace debug {
namespace {

using HeadupsFn = void(__fastcall*)(void*);
const HeadupsFn kDoHeadups = (HeadupsFn)0x449b10;
// BrPixelmapText (cdecl): pixelmap, x, y, colour, font (0 = BRender's 7x9), text.
const auto kText = (void(__cdecl*)(uintptr_t, int, int, uint32_t, void*, const char*))0x538a10;
uintptr_t* const kBackScreen = (uintptr_t*)0x74d360;  // gBack_screen, which the minimap is blitted into
const auto kGameTime = (unsigned (*)())0x514c30;
constexpr int kLineHeight = 10;
constexpr BYTE kDikF2 = 0x3c;

bool g_on;

int g_frames, g_fps;
DWORD g_fpsFrom;

// The player's car: the physics body (list 0x74a5f0, next +0x220) whose
// back-pointer +0x23c is the player's car 0x75bc2c; its actor's translation
// (+0x50) and velocity (+0x68).
uintptr_t PlayerBody() {
    for (uintptr_t b = *(uintptr_t*)0x74a5f0; b; b = *(uintptr_t*)(b + 0x220))
        if (*(uint32_t*)(b + 0x23c) == 0x75bc2c) return b;
    return 0;
}

int ActivePeds() {
    uintptr_t peds = *(uintptr_t*)0x744808;
    int count = *(int*)0x7447d4, n = 0;
    for (int i = 0; peds && i < count; i++)
        if (*(uint16_t*)(peds + i * 0x54 + 8) & 1) n++;
    return n;
}

int Bodies() {
    int n = 0;
    for (uintptr_t b = *(uintptr_t*)0x74a5f0; b && n < 1000; b = *(uintptr_t*)(b + 0x220)) n++;
    return n;
}

void Draw() {
    uintptr_t screen = *kBackScreen;
    if (!screen) return;
    char lines[10][64];
    int n = 0;
    snprintf(lines[n++], 64, "FPS %d  FRAME %d MS", g_fps, *(int*)0x74d3e4);
    snprintf(lines[n++], 64, "TIME %.1f S", kGameTime() / 1000.0);
    if (uintptr_t b = PlayerBody()) {
        const float* t = (const float*)(*(uintptr_t*)b + 0x50);
        const float* v = (const float*)(b + 0x68);
        snprintf(lines[n++], 64, "CAR %.2f %.2f %.2f", t[0], t[1], t[2]);
        snprintf(lines[n++], 64, "SPEED %.2f U/S", sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    }
    const float* cam = (const float*)0x74d484;
    snprintf(lines[n++], 64, "CAM %.2f %.2f %.2f", cam[0], cam[1], cam[2]);
    uintptr_t map = *(uintptr_t*)0x7632a4;
    snprintf(lines[n++], 64, "MAP %.1f %.1f  CUT %d %d", *(float*)0x68c868, *(float*)0x68c86c, *(int*)0x68c878,
             *(int*)0x68c874);
    snprintf(lines[n++], 64, "MAPPM %dX%d  CUT %dX%d", map ? *(uint16_t*)(map + 0x34) : 0,
             map ? *(uint16_t*)(map + 0x36) : 0, *(int*)0x74ab94, *(int*)0x74abec);
    snprintf(lines[n++], 64, "PEDS %d  BODIES %d", ActivePeds(), Bodies());
    snprintf(lines[n++], 64, "CARMA2-HD %s", C2HD_VERSION);
    int x = *(uint16_t*)(screen + 0x34) - 190;
    int y = *(int*)0x74abb0 - 6 - n * kLineHeight;  // above the minimap (HeadupMapY)
    if (y < 120) y = 120;
    for (int i = 0; i < n; i++) {
        kText(screen, x + 1, y + i * kLineHeight + 1, 0x0000, nullptr, lines[i]);  // shadow
        kText(screen, x, y + i * kLineHeight, 0xffff, nullptr, lines[i]);
    }
}

void __fastcall Headups(void* car) {
    kDoHeadups(car);
    g_frames++;
    DWORD now = GetTickCount();
    if (now - g_fpsFrom >= 1000) {
        g_fps = (int)(g_frames * 1000 / (now - g_fpsFrom ? now - g_fpsFrom : 1));
        g_frames = 0;
        g_fpsFrom = now;
    }
    if (g_on) Draw();
}

}  // namespace

void Install() {
    g_on = settings::Get("Debug", "Overlay", "0") == std::string("1");
    bool ok = hook::Redirect(0x4e53fe, 0xe8, 0x449b10, (const void*)&Headups);
    Log("debug: overlay (F2 in races) %s, %s", ok ? "ready" : "hook FAILED", g_on ? "on" : "off");
}

void OnKeys(const BYTE* keys) {
    static bool was;
    bool down = (keys[kDikF2] & 0x80) != 0;
    if (down && !was && *(uintptr_t*)0x762438) {  // in a race
        g_on = !g_on;
        settings::Set("Debug", "Overlay", g_on ? "1" : "0");
        Log("debug: overlay %s", g_on ? "on" : "off");
    }
    was = down;
}

}  // namespace debug
