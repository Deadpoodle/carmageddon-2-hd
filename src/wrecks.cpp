// Keys on the Wrecks gallery, the screen after a race that shows the cars
// wrecked in it and lets you buy one.
//
// Its update (0x46eb80) reads only the mouse and Return/KP-Enter (which
// activate the focused item, OK by default), so the stock screen ignores the
// arrows: the cars scroll only by clicking the arrow buttons. The update is
// wrapped: Left and Right run the arrow buttons' own callbacks (item 3, the
// left arrow, scrolls with 0x46e720; item 4, the right one, with 0x46e6c0;
// both do nothing while the carousel moves), and Up and Down move the focus
// between BUY and OK. A/D, W/S and the pad's D-pad reach here as arrows
// (input.cpp, controller.cpp).
#include "wrecks.h"

#include <cstdint>

#include "log.h"

namespace wrecks {
namespace {

uint8_t* const kScreen = (uint8_t*)0x5f8e68;  // the Wrecks screen struct
constexpr int kCount = 0x104, kUpdate = 0x110, kItems = 0x130, kItemSize = 0x158, kCallback = 4;
constexpr int kStockItems = 12, kLeftArrow = 3, kRightArrow = 4, kBuy = 9, kOk = 10;
// Key codes for KeyPressed (KEYNAMES.TXT line - 2)
constexpr int kUp = 0x48, kDown = 0x49, kLeft = 0x46, kRight = 0x47, kPad8 = 0x59, kPad2 = 0x53, kPad4 = 0x55,
              kPad6 = 0x57;

typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kWrecksUpdate = (ScreenFn)0x46eb80;
const ScreenFn kScrollLeftArrow = (ScreenFn)0x46e720, kScrollRightArrow = (ScreenFn)0x46e6c0;
const auto kKeyPressed = (int(__fastcall*)(int))0x482550;  // pressed this frame (or repeating)
int* const kFocus = (int*)0x688770;
int* const kMouseMode = (int*)0x79ecb8;  // focus follows the cursor; a key clears it, cursor movement sets it

uint32_t Callback(int item) { return *(uint32_t*)(kScreen + kItems + item * kItemSize + kCallback); }

int __fastcall Update(void* s) {
    if (kKeyPressed(kLeft) || kKeyPressed(kPad4)) {
        kScrollLeftArrow(s);
    } else if (kKeyPressed(kRight) || kKeyPressed(kPad6)) {
        kScrollRightArrow(s);
    } else if (kKeyPressed(kUp) || kKeyPressed(kPad8) || kKeyPressed(kDown) || kKeyPressed(kPad2)) {
        *kMouseMode = 0;
        *kFocus = *kFocus == kBuy ? kOk : kBuy;
    }
    return kWrecksUpdate(s);
}

}  // namespace

void Install() {
    uint32_t* update = (uint32_t*)(kScreen + kUpdate);
    if (*update != (uint32_t)(uintptr_t)kWrecksUpdate || *(uint32_t*)(kScreen + kCount) != kStockItems ||
        Callback(kLeftArrow) != (uint32_t)(uintptr_t)kScrollLeftArrow ||
        Callback(kRightArrow) != (uint32_t)(uintptr_t)kScrollRightArrow) {
        Log("wrecks: Wrecks screen not as expected; keys stay as they were");
        return;
    }
    *update = (uint32_t)(uintptr_t)&Update;
    Log("wrecks: arrow keys on the Wrecks gallery");
}

}  // namespace wrecks
