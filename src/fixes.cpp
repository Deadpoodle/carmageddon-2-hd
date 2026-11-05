// Guards against the original game's own faults.
//
// - StopPowerupEffect 0x4d8250 (fastcall slot) has no check for slot -1
//   ("none"). The front end's main loop calls it on entry for the slots in
//   0x655e48/4c/50 (0x46d900..0x46d922), and 0x655e50 is never written, so it
//   is always -1: the routine then reads table entry -1, the memory just
//   before 0x705c80, and if that happens to be non-zero writes through a
//   pointer taken from it. After a long race that crashed the game on the way
//   back to the menus (write at 0x4d826e). A negative slot now does nothing.
//
// - The time bonus after a race (in the race loop 0x492980, state 3 of
//   0x68c784) turns the time left (gTimer 0x761ee8, ms) into credits: each
//   frame, steps = (game time - last step 0x68c818) / 15, unsigned, and the
//   timer loses steps * 1000 ms, a 32-bit product; the bonus 0x68c780 gains
//   the seconds taken times the skill's credits per second. The last step
//   starts at 0, so the first frame normally takes all the time at once. If
//   the game time (0x514c30) is ever behind the last step, the subtraction
//   wraps, steps * 1000 overflows and the timer *gains* time: the bonus went
//   to -21456195 in the player's game, exactly a game time 18.6 s behind. A
//   negative timer would do the same. The comparison at 0x493813 now goes
//   through a guard: a clock behind the last step restarts the steps from
//   now, a negative timer counts as none left, and steps can't overflow. Each
//   time a guard acts it logs the clock's parts, to find what put it behind.
//
// - An opponent that leaves the world (0x442130, checked every 500 ms at
//   0x4ace97: no ground under it, as in the sea) is put back by 0x4a9180, which
//   walks a random route along the opponent paths, moving the car to each node,
//   until the car is further from the player than gIn_view_distance 0x691764.
//   That test is the loop's only way out. The distance is gYon + 10, so 45 in
//   the original but 210 at our Max draw distance, and no node may be that far:
//   Carrier, Funfair, Quarry and Ski Track span under 180 units in all, and on
//   the other tracks the middle of the map is out of reach of everything. The
//   game froze for good the moment a car went in the water. The test now goes
//   through a guard: the distance wanted eases down as the walk goes on, so the
//   car still lands about as far off as the track allows, and the walk always
//   ends. Where it lands doesn't matter to the AI: 0x4b1153 puts any car with
//   physics off and no cheating back on the road, near or far.
#include "fixes.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "hook.h"
#include "log.h"

extern "C" void __cdecl c2hd_TimeBonusOdd(int why, int timer, unsigned now, unsigned last) {
    Log("fixes: time bonus: %s (timer %d ms, game time %u, last step %u; frame start %u, period %u, lost %u, "
        "replay %d, bonus %d)",
        why == 1 ? "negative timer, counted as none" : "game time behind the last step, steps restarted", timer, now,
        last, *(unsigned*)0x79efb0, *(unsigned*)0x74d3e4, *(unsigned*)0x6abef4, *(int*)0x676914, *(int*)0x68c780);
}

// At 0x493813: eax = game time, edi = last step, esi = timer (ebx = 0).
// Stock: mov ecx,eax; sub ecx,edi; cmp ecx,0xf; jbe 0x493888 (no step yet:
// show the bonus), else 0x49381c (take the steps). 0x4938c9: no time left.
extern "C" void c2hd_timeBonusStep();
asm(".intel_syntax noprefix\n"
    "_c2hd_timeBonusStep:\n"
    "  mov ecx, eax\n"
    "  sub ecx, edi\n"
    "  test esi, esi\n"
    "  jl 1f\n"
    "  test ecx, ecx\n"
    "  jl 2f\n"
    "  cmp ecx, 0xf\n"
    "  jbe 4f\n"
    "  cmp ecx, 30000000\n"  // 2,000,000 steps at most: steps * 1000 stays in range
    "  jbe 3f\n"
    "  mov ecx, 30000000\n"
    "3:\n"
    "  push 0x49381c\n"
    "  ret\n"
    "4:\n"
    "  push 0x493888\n"
    "  ret\n"
    "1:\n"
    "  pushad\n"
    "  push edi\n"
    "  push eax\n"
    "  push esi\n"
    "  push 1\n"
    "  call _c2hd_TimeBonusOdd\n"
    "  add esp, 16\n"
    "  popad\n"
    "  mov dword ptr ds:0x761ee8, 0\n"
    "  push 0x4938c9\n"
    "  ret\n"
    "2:\n"
    "  pushad\n"
    "  push edi\n"
    "  push eax\n"
    "  push esi\n"
    "  push 2\n"
    "  call _c2hd_TimeBonusOdd\n"
    "  add esp, 16\n"
    "  popad\n"
    "  mov dword ptr ds:0x68c818, eax\n"
    "  push 0x493888\n"
    "  ret\n"
    ".att_syntax\n");

namespace {

constexpr int kEaseEvery = 64;    // path steps at each distance before asking for less
constexpr float kEase = 0.9f;     // from 210, about 45 after 15 eases
constexpr int kMostSteps = 4096;  // a step costs well under a microsecond, so this stays inside a frame
const float* const kInViewDistance = (const float*)0x691764;
int g_teleportSteps;
float g_teleportWanted;

}  // namespace

// One node of the walk: has the car got far enough? NaN never is, so a bad
// position runs to the step limit and stops there.
extern "C" int __cdecl c2hd_OpponentFarEnough(float distance) {
    int steps = ++g_teleportSteps;
    if (!(distance > g_teleportWanted) && steps < kMostSteps) {
        if (steps % kEaseEvery == 0) g_teleportWanted *= kEase;
        return 0;
    }
    if (steps > kEaseEvery)
        Log("fixes: opponent put back %.1f units from the player after %d path steps (wanted %.1f by then, %.1f at first)",
            distance, steps, g_teleportWanted, *kInViewDistance);
    return 1;
}

// At 0x4a933c, after 0x4a9550 has set the distance [esi+0x18]. Stock: the
// distance against gIn_view_distance, and [esp+0x10] = 1 if beyond it. Then
// 0x4a9354, where eax, ecx and edx are free.
extern "C" void c2hd_opponentPlaced();
asm(".intel_syntax noprefix\n"
    "_c2hd_opponentPlaced:\n"
    "  push dword ptr [esi+0x18]\n"
    "  call _c2hd_OpponentFarEnough\n"
    "  add esp, 4\n"
    "  test eax, eax\n"
    "  je 1f\n"
    "  mov dword ptr [esp+0x10], 1\n"
    "1:\n"
    "  push 0x4a9354\n"
    "  ret\n"
    ".att_syntax\n");

namespace fixes {
namespace {

void(__fastcall* o_StopPowerupEffect)(int);

void __fastcall StopPowerupEffect(int slot) {
    if (slot >= 0) o_StopPowerupEffect(slot);
}

// The route's first fill, once per teleport, just before the walk (0x4a9289).
const auto kTopUpRoute = (void(__fastcall*)(void*, int))0x4a93c0;

void __fastcall TeleportWalkStarts(void* opponent, int sections) {
    g_teleportSteps = 0;
    g_teleportWanted = *kInViewDistance;
    kTopUpRoute(opponent, sections);
}

// C2HD_FORCE_FALL=s (tests): every s seconds of a race, the next opponent to be
// checked counts as out of the world, so its teleport runs whatever the cars
// do. Moving the car doesn't do it: physics puts it straight back.
const auto kOutOfWorld = (int(__fastcall*)(void*))0x442130;
unsigned g_forceFallEvery, g_nextForcedFall;

int __fastcall OutOfWorldForced(void* car) {
    unsigned now = *(const unsigned*)0x79efb8;
    if (now < g_nextForcedFall) {
        if (g_nextForcedFall - now <= g_forceFallEvery) return kOutOfWorld(car);
        g_nextForcedFall = 0;  // the clock went back: a new race
    }
    if (!g_nextForcedFall) {
        g_nextForcedFall = now + g_forceFallEvery;
        return kOutOfWorld(car);
    }
    g_nextForcedFall = now + g_forceFallEvery;
    Log("fixes: test: forcing an opponent out of the world at game time %u", now);
    return 1;
}

}  // namespace

void Install() {
    static const uint8_t start[6] = {0x8d, 0x04, 0x89, 0xc1, 0xe0, 0x06};  // lea eax,[ecx+ecx*4]; shl eax,6
    o_StopPowerupEffect = (void(__fastcall*)(int))hook::Detour(0x4d8250, start, 6, (const void*)&StopPowerupEffect);
    Log("fixes: power-up effect slot -1 ignored: %s", o_StopPowerupEffect ? "on" : "FAILED");
    char fall[8] = "";
    if (GetEnvironmentVariableA("C2HD_FORCE_FALL", fall, sizeof fall) && atoi(fall) > 0) {
        g_forceFallEvery = (unsigned)atoi(fall) * 1000;
        Log("fixes: test: an opponent out of the world every %s s: %s", fall,
            hook::Redirect(0x4aceae, 0xe8, 0x442130, (const void*)&OutOfWorldForced) ? "on" : "FAILED");
    }
    char stock[4] = "";
    if (GetEnvironmentVariableA("C2HD_STOCK_TELEPORT", stock, sizeof stock) && stock[0] == '1') {  // tests: the stock loop
        Log("fixes: opponent teleport left as the original (C2HD_STOCK_TELEPORT)");
    } else {
        // fld dword ptr [esi+0x18]; fcomp dword ptr [0x691764]
        static const uint8_t test[9] = {0xd9, 0x46, 0x18, 0xd8, 0x1d, 0x64, 0x17, 0x69, 0x00};
        uint8_t toGuard[9] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
        int32_t to = (int32_t)((uintptr_t)&c2hd_opponentPlaced - (0x4a933c + 5));
        memcpy(toGuard + 1, &to, 4);
        bool walk = hook::Redirect(0x4a9289, 0xe8, 0x4a93c0, (const void*)&TeleportWalkStarts) &&
                    hook::Patch(0x4a933c, test, toGuard, 9);
        Log("fixes: opponent teleport always ends: %s", walk ? "on" : "FAILED");
    }
    // mov ecx,eax; sub ecx,edi; cmp ecx,0xf; jbe 0x493888
    static const uint8_t step[9] = {0x8b, 0xc8, 0x2b, 0xcf, 0x83, 0xf9, 0x0f, 0x76, 0x6c};
    uint8_t jump[9] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    int32_t rel = (int32_t)((uintptr_t)&c2hd_timeBonusStep - (0x493813 + 5));
    memcpy(jump + 1, &rel, 4);
    char v[4] = "";
    if (GetEnvironmentVariableA("C2HD_STOCK_TIMEBONUS", v, sizeof v) && v[0] == '1') {  // tests: the stock code
        Log("fixes: time bonus left as the original (C2HD_STOCK_TIMEBONUS)");
        return;
    }
    Log("fixes: time bonus guarded against a clock behind it: %s", hook::Patch(0x493813, step, jump, 9) ? "on" : "FAILED");
}

}  // namespace fixes
