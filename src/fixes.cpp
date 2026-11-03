// Guards against the original game's own faults.
//
// - StopPowerupEffect 0x4d8250 (fastcall slot) has no check for slot -1
//   ("none"). The front end's main loop calls it on entry for the slots in
//   0x655e48/4c/50 (0x46d900..0x46d922), and 0x655e50 is never written, so it
//   is always -1: the routine then reads table entry -1, the memory just
//   before 0x705c80, and if that happens to be non-zero writes through a
//   pointer taken from it. After a long race that crashed the game on the way
//   back to the menus (write at 0x4d826e). A negative slot now does nothing.
#include "fixes.h"

#include <cstdint>

#include "hook.h"
#include "log.h"

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
}

}  // namespace fixes
