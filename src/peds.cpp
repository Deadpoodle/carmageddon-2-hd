// Pedestrians without a twitching torso (re/peds.md 5b, 5c).
//
// An animated ped's root bone (the torso) has a physics body that sleeps
// while the ped is animated. The physics step (0x4b6630) runs in fixed 40 ms
// ticks; on each tick it copies every body's actor into the body (0x4baa00).
// On frames with no tick due it instead copies every body back onto its actor
// (0x4c2600), asleep or not. For an animated ped that copy is the pose of the
// last tick, so every key-frame step since then is undone until the next one:
// the torso flicks between its current and previous position and the ped
// seems to run on the spot. At the ~25 fps of the time every frame had a tick
// and it never showed; at 100 fps most frames have none.
//
// Between ticks an asleep ped body now belongs to the animation: physics
// leaves its actor alone. Two spots, each diverted to a stub that tests the
// body (asleep +0xec, and a ped's: low byte of +0x238 is 2; cars are 1):
// - the copy-back loop in 0x4c2600 (0x4c260e) goes on to the next body;
// - the no-tick loop at 0x4b6721, which moves a body's actor by its velocity
//   (0x4c2830) and relied on the copy-back to undo it, skips the body.
// Everything else runs the moved instructions and returns, as before.
// Skipping every asleep body is not enough: a car waiting on the grid is
// asleep too.
//
// Pedestrians in network races ([Network] Pedestrians, re/network-peds.md):
// the original spawns none online. Its only network check in the ped code is
// in the track spawner 0x4d2cc0 (jne at 0x4d2cd7 when gNet_mode 0x68b918 is
// set), so that jump goes: each machine then runs its own pedestrians, which
// are never sent (no ped messages exist). One more change keeps races going:
// in race type 0, the only type network races use, killing the last ped
// (0x4cd160: count 0x7447cc reaches 0x7447d4) shows "Every pedestrian
// murdered" and calls RaceCompleted(1), which on the host would end the race
// for everyone. In network games that test now takes the "peds left" path
// (0x4cd1cd). Time bonuses, the HUD kill counter, ped power-ups and scoring
// already ignore peds online.
#include "peds.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "log.h"
#include "settings.h"

namespace peds {
namespace {

constexpr uint8_t kPedBody = 2;

}  // namespace

void Install() {
    // 0x4c260e: cmp [edi+0xf0], ebx (edi = body); then 0x4c2614: jne.
    static const uint8_t copyOld[6] = {0x39, 0x9f, 0xf0, 0x00, 0x00, 0x00};
    static const uint8_t copyStub[] = {
        0x80, 0xbf, 0xec, 0x00, 0x00, 0x00, 0x00,      // cmp byte [edi+0xec], 0   (asleep?)
        0x74, 0x0e,                                    // je awake
        0x80, 0xbf, 0x38, 0x02, 0x00, 0x00, kPedBody,  // cmp byte [edi+0x238], 2 (a ped's?)
        0x75, 0x05,                                    // jne awake
        0xe9, 0, 0, 0, 0,                              // jmp next body (0x4c265a)
        // awake:
        0x39, 0x9f, 0xf0, 0x00, 0x00, 0x00,  // cmp [edi+0xf0], ebx (moved)
        0xe9, 0, 0, 0, 0,                    // jmp 0x4c2614 (its jne)
    };
    bool copy = hook::Divert(0x4c260e, copyOld, 6, copyStub, sizeof copyStub, {{19, 0x4c265a}, {30, 0x4c2614}});
    // 0x4b6721: cmp [eax+8], edi (eax = body+0x240, edi = 2; esi = body);
    // jne 0x4b6737 (next body); else 0x4b6726 calls 0x4c2830.
    static const uint8_t moveOld[5] = {0x39, 0x78, 0x08, 0x75, 0x11};
    static const uint8_t moveStub[] = {
        0x39, 0x78, 0x08,                              // cmp [eax+8], edi (moved)
        0x75, 0x17,                                    // jne skip
        0x80, 0xbe, 0xec, 0x00, 0x00, 0x00, 0x00,      // cmp byte [esi+0xec], 0   (asleep?)
        0x74, 0x09,                                    // je go
        0x80, 0xbe, 0x38, 0x02, 0x00, 0x00, kPedBody,  // cmp byte [esi+0x238], 2 (a ped's?)
        0x74, 0x05,                                    // je skip
        // go:
        0xe9, 0, 0, 0, 0,  // jmp 0x4b6726 (the call)
        // skip:
        0xe9, 0, 0, 0, 0,  // jmp 0x4b6737 (next body)
    };
    bool move = hook::Divert(0x4b6721, moveOld, 5, moveStub, sizeof moveStub, {{24, 0x4b6726}, {29, 0x4b6737}});
    Log("peds: asleep ped bodies keep their animated pose: copy-back %s, drift %s", copy ? "on" : "FAILED",
        move ? "on" : "FAILED");
    if (settings::Get("Network", "Pedestrians", "1") == std::string("1")) {
        // 0x4d2cd7: jne 0x4d334e (network: no spawning) -> six nops.
        static const uint8_t gateOld[6] = {0x0f, 0x85, 0x71, 0x06, 0x00, 0x00};
        static const uint8_t gateNew[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        bool gate = hook::Patch(0x4d2cd7, gateOld, gateNew, 6);
        // 0x4cd1a3: mov ecx, 0x1a (the all-dead message, then RaceCompleted).
        static const uint8_t endOld[5] = {0xb9, 0x1a, 0x00, 0x00, 0x00};
        static const uint8_t endStub[] = {
            0x83, 0x3d, 0x18, 0xb9, 0x68, 0x00, 0x00,  // cmp dword [0x68b918], 0 (gNet_mode)
            0x0f, 0x85, 0, 0, 0, 0,                    // jne 0x4cd1cd (peds left: carry on)
            0xb9, 0x1a, 0x00, 0x00, 0x00,              // mov ecx, 0x1a (moved)
            0xe9, 0, 0, 0, 0,                          // jmp 0x4cd1a8
        };
        bool end = hook::Divert(0x4cd1a3, endOld, 5, endStub, sizeof endStub, {{9, 0x4cd1cd}, {19, 0x4cd1a8}});
        Log("peds: pedestrians in network races: spawning %s, no race end when all are killed %s",
            gate ? "on" : "FAILED", end ? "on" : "FAILED");
    }
}

}  // namespace peds
