// Pedestrians, traffic and opponents' full models as far out as the scenery
// (re/actor-distance.md). With the scenery drawn to Yon 100 they popped into
// view in plain sight, because none of their distances follow the yon:
//
// - Pedestrians come alive only between two world-aligned boxes around the
//   camera, 9 and 12 units (PEDS\SETTINGS.TXT, read once at start-up by
//   0x4cadc0, called at 0x47e28f, into 0x694118 / 0x6944bc, with the vertical
//   limits 0.6 times those in 0x69bc1c / 0x694128). They are drawn only while
//   alive. The loader call is wrapped and the four values scaled.
// - Traffic (drones) is drawn inside a 15/12/15 box around a point ahead of
//   the camera (0x44ca90) and moves inside an 18/13/18 one (0x44cb80); the
//   six fcomp operands are pointed at our own floats (0x589da8 nearby is shared
//   with another test, so the constants themselves stay). At most 12 drones
//   move and 10 are drawn at once (imm8 at 0x44f7ee, 0x45152e; 0x451847,
//   0x452712); those are plain counters (incremented and decremented, never
//   indexes), so the caps go up to 64 (at Yon 100 about 30 are drawn and 40 move in a city).
// - Opponents are culled with the yon already, but swap their simple model
//   for the full one at sqrt(8 x 50) = 20 units: the car files' min_dist_squared
//   times the factor row for the hardware renderer (0x58f6c4..0x58f6d0: 50, 5,
//   2.5, 1.5 by car detail level). The row is scaled by k^2.
//
// k follows [Display] DrawDistance: Yon / 35 (Original 1, Far 2, Further 2.86,
// Max 5.71); pedestrians use a gentler 1, 2, 2.5, 5 (their cost grows with the
// area). Original changes nothing. A hidden race at Max held 100 fps; so did one
// at Yon 400.
#include "actors.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "log.h"
#include "settings.h"

namespace actors {
namespace {

float g_pedScale = 1;
float g_droneNear[2], g_droneFar[2];  // x/z and y half-sizes of the drawn and moving boxes

void LoadPedSettings() {
    ((void (*)())0x4cadc0)();
    float* maxDist = (float*)0x694118;
    float* maxY = (float*)0x69bc1c;
    float* minDist = (float*)0x6944bc;
    float* minY = (float*)0x694128;
    *maxDist *= g_pedScale;
    *maxY *= g_pedScale;
    *minDist *= g_pedScale;
    *minY *= g_pedScale;
    Log("actors: pedestrians come alive between %.1f and %.1f units (vertical %.1f..%.1f)", *minDist, *maxDist, *minY,
        *maxY);
}

// The disp32 of an fcomp dword ptr [abs] (d8 1d <abs>) at site, from old to ours.
bool Operand(uintptr_t site, uint32_t old, const float* ours) {
    uint32_t now = (uint32_t)(uintptr_t)ours;
    return hook::Patch(site + 2, (const uint8_t*)&old, (const uint8_t*)&now, 4);
}

bool Cap(uintptr_t imm, uint8_t old, uint8_t now) { return hook::Patch(imm, &old, &now, 1); }

}  // namespace

void Install() {
    std::string dd = settings::Get("Display", "DrawDistance", "max");
    float k = 1, kp = 1;
    if (_stricmp(dd.c_str(), "far") == 0) k = 70.0f / 35, kp = 2.0f;
    else if (_stricmp(dd.c_str(), "further") == 0) k = 100.0f / 35, kp = 2.5f;
    else if (_stricmp(dd.c_str(), "max") == 0) k = 200.0f / 35, kp = 5.0f;
    if (k == 1) {
        Log("actors: draw distance %s: pedestrians, traffic and opponents as the original", dd.c_str());
        return;
    }

    g_pedScale = kp;
    bool peds = hook::Redirect(0x47e28f, 0xe8, 0x4cadc0, (const void*)&LoadPedSettings);

    g_droneNear[0] = 15 * k, g_droneNear[1] = 12 * k;
    g_droneFar[0] = 18 * k, g_droneFar[1] = 13 * k;
    bool drones = Operand(0x44ca9a, 0x589d98, &g_droneNear[0]) && Operand(0x44cab2, 0x589d9c, &g_droneNear[1]) &&
                  Operand(0x44caca, 0x589d98, &g_droneNear[0]) && Operand(0x44cb8a, 0x589db0, &g_droneFar[0]) &&
                  Operand(0x44cba2, 0x589db4, &g_droneFar[1]) && Operand(0x44cbba, 0x589db0, &g_droneFar[0]);
    bool caps = Cap(0x44f7ee, 12, 64) && Cap(0x45152e, 12, 64) && Cap(0x451847, 10, 64) && Cap(0x452712, 10, 64);

    float* lod = (float*)0x58f6c4;  // hardware row, car detail levels 0..3
    bool models = lod[0] == 50 && lod[1] == 5 && lod[2] == 2.5f && lod[3] == 1.5f;
    DWORD protect;
    if (models && VirtualProtect(lod, 16, PAGE_READWRITE, &protect)) {
        for (int i = 0; i < 4; i++) lod[i] *= k * k;
        VirtualProtect(lod, 16, protect, &protect);
    } else {
        models = false;
    }
    Log("actors: draw distance %s (x%.2f): pedestrians %s, traffic boxes %.0f/%.0f %s, caps 64 %s, opponents' full "
        "models out to %.0f units %s",
        dd.c_str(), k, peds ? "on" : "FAILED", g_droneNear[0],
        g_droneFar[0], drones ? "on" : "FAILED", caps ? "on" : "FAILED", 20 * k, models ? "on" : "FAILED");
}

}  // namespace actors
