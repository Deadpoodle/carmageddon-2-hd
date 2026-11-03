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
#include "fixes.h"

#include <windows.h>

#include <cstdint>
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

namespace fixes {
namespace {

void(__fastcall* o_StopPowerupEffect)(int);

void __fastcall StopPowerupEffect(int slot) {
    if (slot >= 0) o_StopPowerupEffect(slot);
}

}  // namespace

void Install() {
    static const uint8_t start[6] = {0x8d, 0x04, 0x89, 0xc1, 0xe0, 0x06};  // lea eax,[ecx+ecx*4]; shl eax,6
    o_StopPowerupEffect = (void(__fastcall*)(int))hook::Detour(0x4d8250, start, 6, (const void*)&StopPowerupEffect);
    Log("fixes: power-up effect slot -1 ignored: %s", o_StopPowerupEffect ? "on" : "FAILED");
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
