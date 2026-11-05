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
//   indexes), so they can go up. Traffic stays at twice the original at every
//   setting (boxes 30 and 36, caps 20 and 24), and fades at its box's edge
//   (trafficfade.cpp). Following k put 64 cars in a box of 103 at Max, far more
//   than the roads can take.
// - Opponents are culled with the yon already, but swap their simple model
//   for the full one at sqrt(8 x 50) = 20 units: the car files' min_dist_squared
//   times the factor row for the hardware renderer (0x58f6c4..0x58f6d0: 50, 5,
//   2.5, 1.5 by car detail level). The row is scaled by k^2.
//
// k follows [Display] DrawDistance: Yon / 35 (Original 1, Far 2, Further 2.86,
// Max 5.71); pedestrians use a gentler 1, 2, 2.5, 5 (their cost grows with the
// area). Original changes nothing. A hidden race at Max held 100 fps; so did one
// at Yon 400.
//
// Traffic that has been hit stays where it stops until it leaves the box it moves
// in. That box was 18 units, so a wreck went as soon as the player drove off; in a
// wider box it stays in the road, the next car hits it and stops too (at 103 units
// and 64 moving, over half the cars in New City were stuck in piles after a minute).
// So a car that has been knocked about or stopped for a few seconds is sent home as
// soon as the player can't see it: see ClearWreck.
//
// Translucent faces (water, glass, tree leaves) aren't drawn as they come: BRender
// keeps them for the end of the frame, in the primitive heap the game gives
// BrZbBegin 0x525d20, 300,000 bytes at 0x6baa40 (pushed at 0x4e4af1 and 0x4e4af6).
// When it fills, the rest are dropped. That was plenty at Yon 35, but at Max the
// far cells use it up: the sea beside the road vanished and trees near the player
// were bare trunks. So the heap is ours. New City 3 needs 400 to 800 KB at Max
// (a sweep of sizes, rendered against this one), and 16 MB leaves room for the
// busiest track. The old buffer stays with its other user, 0x4b5ca0.
#include "actors.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "log.h"
#include "settings.h"
#include "trafficfade.h"

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

// A drone: +4 its type (+0x2c: 2 can reset, 4 always moves), +0x30 when it took
// its state, +0x38 the state, +0x44 moving, +0x45 in the drawn box, +0xec actor.
// A state's routine is called with ecx = drone and edx = 1 for each frame.
using StateFn = void(__fastcall*)(uint8_t*, int);
constexpr uintptr_t kStateTable = 0x594738;
constexpr int kKnockedAbout = 3, kStopped = 4, kStateRun = 1;
const StateFn kKnockedAboutRun = (StateFn)0x44eb70, kStoppedRun = (StateFn)0x44ec50;
const auto kProcessingOff = (void(__fastcall*)(uint8_t*, int))0x451710;  // edx = 1 even for a car being knocked about
constexpr unsigned kWreckMs = 4000;
constexpr float kWreckNearest = 15;  // the original's drawn box: never closer, whatever the camera is doing
constexpr float kFovMargin = 0.2f;   // radians past the edge of the view, for the car's own size
int g_wrecksCleared;

// In front of the camera, within its field of view (Hor+ widens it) and near
// enough to matter. It ignores buildings, so a car behind one still counts as seen.
bool PlayerCanSee(const float* at) {
    const float* camera = (const float*)0x74d460;  // the camera's matrix: rows x, y, z, position
    float dx = at[0] - camera[9], dy = at[1] - camera[10], dz = at[2] - camera[11];
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    if (distance < kWreckNearest) return true;
    const uint8_t* data = *(uint8_t**)(*(uintptr_t*)0x74d35c + 0x5c);
    float halfFov = *(const uint16_t*)(data + 6) * (3.14159265f / 65536);  // a BrAngle, halved
    float halfWidth = atanf(*(const float*)(data + 0x10) * tanf(halfFov));
    float ahead = -(dx * camera[6] + dy * camera[7] + dz * camera[8]) / distance;
    return ahead > cosf(halfWidth + kFovMargin);
}

// Switching a car off is what happens when it leaves its box, and the state's own
// routine then sends it back to its start. It can only start again outside the
// drawn box, so neither end of this is seen.
void ClearWreck(uint8_t* drone) {
    uint8_t flags = *(uint8_t*)(*(uintptr_t*)(drone + 4) + 0x2c);
    if (!drone[0x44] || !(flags & 2) || (flags & 4)) return;
    if (*(unsigned*)0x79efb8 - *(unsigned*)(drone + 0x30) < kWreckMs) return;
    if (drone[0x45] && PlayerCanSee((const float*)(*(uintptr_t*)(drone + 0xec) + 0x50))) return;
    kProcessingOff(drone, 1);
    g_wrecksCleared++;
}

void __fastcall KnockedAbout(uint8_t* drone, int command) {
    if (command == kStateRun) ClearWreck(drone);
    kKnockedAboutRun(drone, command);
}

void __fastcall Stopped(uint8_t* drone, int command) {
    if (command == kStateRun) ClearWreck(drone);
    kStoppedRun(drone, command);
}

constexpr uint32_t kStockHeap = 300000, kHeap = 16 * 1024 * 1024;

void LargerPrimitiveHeap() {
    void* heap = VirtualAlloc(nullptr, kHeap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = heap && hook::PatchU32(0x4e4af2, kStockHeap, kHeap) &&
              hook::PatchU32(0x4e4af7, 0x6baa40, (uint32_t)(uintptr_t)heap);
    Log("actors: primitive heap %lu MB (was 300 KB): %s", (unsigned long)(kHeap >> 20), ok ? "on" : "FAILED");
}

}  // namespace

void Install() {
    char stock[4] = "";
    if (!(GetEnvironmentVariableA("C2HD_STOCK_HEAP", stock, sizeof stock) && stock[0] == '1')) LargerPrimitiveHeap();
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

    // Traffic stays at twice the original at every setting, the rule PCGamingWiki
    // gives for Yon and pedestrians. It used to follow k, but each car in the
    // wider box is one more to collide with: at Max that was 64 of them.
    constexpr float kt = 2;
    constexpr int kMoving = 24, kDrawn = 20;
    g_droneNear[0] = 15 * kt, g_droneNear[1] = 12 * kt;
    g_droneFar[0] = 18 * kt, g_droneFar[1] = 13 * kt;
    bool drones = Operand(0x44ca9a, 0x589d98, &g_droneNear[0]) && Operand(0x44cab2, 0x589d9c, &g_droneNear[1]) &&
                  Operand(0x44caca, 0x589d98, &g_droneNear[0]) && Operand(0x44cb8a, 0x589db0, &g_droneFar[0]) &&
                  Operand(0x44cba2, 0x589db4, &g_droneFar[1]) && Operand(0x44cbba, 0x589db0, &g_droneFar[0]);
    bool caps = Cap(0x44f7ee, 12, kMoving) && Cap(0x45152e, 12, kMoving) && Cap(0x451847, 10, kDrawn) &&
                Cap(0x452712, 10, kDrawn);

    float* lod = (float*)0x58f6c4;  // hardware row, car detail levels 0..3
    bool models = lod[0] == 50 && lod[1] == 5 && lod[2] == 2.5f && lod[3] == 1.5f;
    DWORD protect;
    if (models && VirtualProtect(lod, 16, PAGE_READWRITE, &protect)) {
        for (int i = 0; i < 4; i++) lod[i] *= k * k;
        VirtualProtect(lod, 16, protect, &protect);
    } else {
        models = false;
    }
    trafficfade::Install(g_droneNear[0], g_droneNear[1]);
    char stockTraffic[4] = "";
    if (GetEnvironmentVariableA("C2HD_STOCK_TRAFFIC", stockTraffic, sizeof stockTraffic) && stockTraffic[0] == '1') {
        Log("actors: wrecked traffic left as the original (C2HD_STOCK_TRAFFIC)");
    } else {
        bool wrecks = hook::PatchU32(kStateTable + kKnockedAbout * 4, (uint32_t)(uintptr_t)kKnockedAboutRun,
                                     (uint32_t)(uintptr_t)&KnockedAbout) &&
                      hook::PatchU32(kStateTable + kStopped * 4, (uint32_t)(uintptr_t)kStoppedRun, (uint32_t)(uintptr_t)&Stopped);
        Log("actors: wrecked traffic cleared out of sight: %s", wrecks ? "on" : "FAILED");
    }
    Log("actors: draw distance %s (x%.2f): pedestrians %s, traffic boxes %.0f/%.0f %s, caps %d/%d %s, opponents' full "
        "models out to %.0f units %s",
        dd.c_str(), k, peds ? "on" : "FAILED", g_droneNear[0],
        g_droneFar[0], drones ? "on" : "FAILED", kDrawn, kMoving, caps ? "on" : "FAILED", 20 * k, models ? "on" : "FAILED");
}

}  // namespace actors
