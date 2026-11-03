// Keys where the stock menus leave something to the mouse. With a pad (the
// D-pad, A and B reach the menus as the arrows, Return and Esc;
// controller.cpp) or the keyboard alone, the stock game can't change the car,
// can't pick a save or leave Load Game, can't change a network game's
// starting credit or target, and has no Esc in the lobby. Every other screen
// already reaches all its items with Up/Down (a key sweep of each, WIKI_LOG).
//
// Change Car's, Load Game's and the lobby's updates activate the focused
// item when KeyPressed(Return) is true, or on a mouse click (calls at
// 0x46bb5a, 0x47062b, 0x473ab0). Those calls are redirected to ReturnPressed,
// which also answers "pressed" while a wrapper of ours asks the update to
// activate an item: so pressing an arrow button or CANCEL from a key runs the
// game's own code (sound, callback, screen change), exactly as a click on it
// does.
//
// Change Car (0x5d6410, 15 items): Left/Right press the arrow buttons (items
// 0 and 1), Up/Down move between OK (13) and CANCEL (14), Esc presses CANCEL.
//
// Load Game (0x5ed5a0, 19 items): eight rows (items 1-8, row i showing save
// offset + i - 1; offset 0x7638ac, saves 0x764e9c) with scroll arrows (items
// 17 and 18, whose callbacks 0x46fb30/0x46fb80 move the offset by one) and
// CANCEL (16). Up/Down move along the saved games, scrolling at the ends of
// the window, then on to CANCEL; Esc presses CANCEL. The update highlights
// the row whose item is focused.
//
// Network game options (0x610208, 33 items, the shared options update
// 0x470c20): the starting credit (24) and target (26) values change only by
// their small arrow buttons (27/28, 29/30), which Up/Down never reach. Left/
// Right on the value call the button's callback (0x473220, 0x4732b0) with the
// focus briefly on the button, since that is how the callback tells down from
// up; it plays the click sound when the value changed, as a click does.
//
// Options, also the in-race pause menu (0x632c60): a fault of the original.
// Every menu update, when Up or Down moves the focus onto an item inside the
// screen's scroll-list window (+0x87a4..+0x87a8), activates that item (e.g.
// 0x47154a, 0x471805 in the shared options update): that is how moving along
// a track list selects the track. Options has no scroll list, so the window
// stays 0..0 and item 0 counts as in it; in a race item 0 is Abort Race
// (callback 0x474730, race state 2), so Up onto it, or Down wrapping round
// from QUIT GAME, ended the race without Return. The mouse never did. Its
// enter is wrapped to make the window empty (-1..-1) when it has no list.
//
// The lobby (NetSync 0x61bad0): Esc moves the focus to ABORT GAME (27), and
// Esc again there presses it, so a stray Esc doesn't leave the game.
#include "menukeys.h"

#include <windows.h>

#include <cstdint>

#include "hook.h"
#include "log.h"

namespace menukeys {
namespace {

constexpr int kCount = 0x104, kUpdate = 0x110, kSavedFocus = 0x118, kItems = 0x130, kItemSize = 0x158, kCallback = 4;
// Key codes for KeyPressed (KEYNAMES.TXT line - 2)
constexpr int kUp = 0x48, kDown = 0x49, kLeft = 0x46, kRight = 0x47, kEsc = 0x3f, kPad8 = 0x59, kPad2 = 0x53,
              kPad4 = 0x55, kPad6 = 0x57;

typedef int(__fastcall* ScreenFn)(void* s);
const auto kKeyPressed = (int(__fastcall*)(int))0x482550;  // pressed this frame (or repeating)
int* const kFocus = (int*)0x688770;
int* const kMouseMode = (int*)0x79ecb8;  // focus follows the cursor; a key clears it, cursor movement sets it
uint8_t* const* const kCurrent = (uint8_t* const*)0x688abc;  // the current screen

// The update's next Return check answers "pressed". Volatile: it is only read
// by ReturnPressed, which the update reaches through a patched call GCC can't
// see. (The Load Game flake once blamed on that was GCC's indirect inlining:
// see build.sh.)
volatile bool g_activate;

int __fastcall ReturnPressed(int code) { return g_activate || kKeyPressed(code); }

bool Pressed(int a, int b) { return kKeyPressed(a) || kKeyPressed(b); }

// Run the screen's update as if the item were clicked.
int Activate(ScreenFn update, void* s, int item) {
    Log("menukeys: %s: item %d pressed", (const char*)s, item);
    *kMouseMode = 0;
    *kFocus = item;
    g_activate = true;
    int r = update(s);
    g_activate = false;
    return r;
}

uint32_t Callback(uint8_t* screen, int item) {
    return *(uint32_t*)(screen + kItems + item * kItemSize + kCallback);
}

// ---------------------------------------------------------------------------
// Change Car

uint8_t* const kCar = (uint8_t*)0x5d6410;
const ScreenFn kCarUpdate = (ScreenFn)0x46ba80;
constexpr int kCarItems = 15, kCarLeft = 0, kCarRight = 1, kCarOk = 13, kCarCancel = 14;

int CarKeys(void* s) {
    if (kKeyPressed(kEsc)) return Activate(kCarUpdate, s, kCarCancel);
    if (*kMouseMode) return kCarUpdate(s);
    int f = *kFocus;
    if (f != kCarOk && f != kCarCancel) *kFocus = f = kCarOk;
    bool left = Pressed(kLeft, kPad4);
    if (left || Pressed(kRight, kPad6)) {
        int r = Activate(kCarUpdate, s, left ? kCarLeft : kCarRight);
        if (*kCurrent == s) *kFocus = f;
        return r;
    }
    if (Pressed(kUp, kPad8) || Pressed(kDown, kPad2)) *kFocus = f == kCarOk ? kCarCancel : kCarOk;
    return kCarUpdate(s);
}

int __fastcall CarUpdate(void* s) {
    int r = CarKeys(s);
    // Leaving: the screen opens on OK next time, not where it was left
    // (the transition saved the focus in the screen's +0x118).
    if (*kCurrent != s) *(int32_t*)(kCar + kSavedFocus) = kCarOk;
    return r;
}

// ---------------------------------------------------------------------------
// Load Game

uint8_t* const kLoad = (uint8_t*)0x5ed5a0;
const ScreenFn kLoadUpdate = (ScreenFn)0x470020;
const ScreenFn kScrollUp = (ScreenFn)0x46fb30, kScrollDown = (ScreenFn)0x46fb80;
constexpr int kLoadItems = 19, kRows = 8, kLoadCancel = 16, kLoadUp = 17, kLoadDown = 18;
int* const kOffset = (int*)0x7638ac;  // the save shown in the first row
int* const kSaves = (int*)0x764e9c;   // how many saved games there are

int __fastcall LoadUpdate(void* s) {
    if (kKeyPressed(kEsc)) return Activate(kLoadUpdate, s, kLoadCancel);
    if (*kMouseMode) return kLoadUpdate(s);
    int rows = *kSaves - *kOffset;
    if (rows > kRows) rows = kRows;
    int f = *kFocus;
    if (f != kLoadCancel && (f < 1 || f > rows)) *kFocus = f = rows > 0 ? 1 : kLoadCancel;
    if (Pressed(kDown, kPad2)) {
        if (f == kLoadCancel && *kSaves > 0) {  // round to the first save
            *kOffset = 0;
            f = 1;
        } else if (f != kLoadCancel && f < rows) {
            f++;
        } else if (f != kLoadCancel && *kOffset + kRows < *kSaves) {
            kScrollDown(s);
        } else {
            f = kLoadCancel;
        }
    } else if (Pressed(kUp, kPad8)) {
        if (f == kLoadCancel && *kSaves > 0) {  // round to the last save
            *kOffset = *kSaves > kRows ? *kSaves - kRows : 0;
            f = *kSaves - *kOffset;
        } else if (f != kLoadCancel && f > 1) {
            f--;
        } else if (f != kLoadCancel && *kOffset > 0) {
            kScrollUp(s);
        } else {
            f = kLoadCancel;
        }
    }
    *kFocus = f;
    return kLoadUpdate(s);
}

// ---------------------------------------------------------------------------
// Network game options

uint8_t* const kNetOptions = (uint8_t*)0x610208;
const ScreenFn kOptionsUpdate = (ScreenFn)0x470c20;  // the update Options, Graphics, Controls... share
const ScreenFn kCreditArrow = (ScreenFn)0x473220, kTargetArrow = (ScreenFn)0x4732b0;
constexpr int kNetOptionsItems = 33, kCredit = 24, kTarget = 26, kCreditDown = 27, kCreditUp = 28, kTargetDown = 29,
              kTargetUp = 30;
const DWORD* const kArrowTime = (const DWORD*)0x686efc;  // when an arrow last changed its value
const auto kPlaySound = (int(__fastcall*)(int, int))0x455690;
const int* const kSoundOutlet = (const int*)0x6845fc;
constexpr int kClickSound = 0xbbc;

int State(uint8_t* screen, int item) { return *(int32_t*)(screen + kItems + item * kItemSize + 0x28); }

int __fastcall NetOptionsUpdate(void* s) {
    int f = *kFocus;
    if (!*kMouseMode && (f == kCredit || f == kTarget)) {
        bool left = Pressed(kLeft, kPad4);
        if (left || Pressed(kRight, kPad6)) {
            int item = f == kCredit ? (left ? kCreditDown : kCreditUp) : (left ? kTargetDown : kTargetUp);
            if (State(kNetOptions, item) > 0) {
                DWORD before = *kArrowTime;
                *kFocus = item;  // the arrows' callbacks read the focus for the direction
                ((ScreenFn)Callback(kNetOptions, item))(s);
                *kFocus = f;
                if (*kArrowTime != before) kPlaySound(*kSoundOutlet, kClickSound);  // as a click on it sounds
            }
        }
    }
    return kOptionsUpdate(s);
}

// ---------------------------------------------------------------------------
// Options (and the in-race pause menu, the same screen)

uint8_t* const kOptions = (uint8_t*)0x632c60;
const ScreenFn kOptionsEnter = (ScreenFn)0x474530;
constexpr int kEnter = 0x108, kScrollCount = 0x8790, kScrollFirst = 0x87a4, kScrollLast = 0x87a8;

int __fastcall OptionsEnter(void* s) {
    int r = kOptionsEnter(s);  // loads options.txt on the first entry
    if (*(int32_t*)(kOptions + kScrollCount) == 0) {
        *(int32_t*)(kOptions + kScrollFirst) = -1;  // an empty window: no item is "in the list"
        *(int32_t*)(kOptions + kScrollLast) = -1;
    }
    return r;
}

// ---------------------------------------------------------------------------
// The lobby (NetSync)

uint8_t* const kLobby = (uint8_t*)0x61bad0;
constexpr int kLobbyAbort = 27;
ScreenFn g_lobbyUpdate;  // the stock update, or netmenu's wrapper of it

int __fastcall LobbyUpdate(void* s) {
    if (kKeyPressed(kEsc)) {
        if (!*kMouseMode && *kFocus == kLobbyAbort) return Activate(g_lobbyUpdate, s, kLobbyAbort);
        *kMouseMode = 0;
        *kFocus = kLobbyAbort;
    }
    return g_lobbyUpdate(s);
}

}  // namespace

void Install() {
    uint32_t* carUpdate = (uint32_t*)(kCar + kUpdate);
    if (*carUpdate == (uint32_t)(uintptr_t)kCarUpdate && *(int32_t*)(kCar + kCount) == kCarItems &&
        hook::Redirect(0x46bb5a, 0xe8, 0x482550, (const void*)&ReturnPressed)) {
        *carUpdate = (uint32_t)(uintptr_t)&CarUpdate;
        Log("menukeys: Change Car takes the arrows and Esc");
    } else {
        Log("menukeys: Change Car not as expected; its keys stay as they were");
    }
    uint32_t* loadUpdate = (uint32_t*)(kLoad + kUpdate);
    if (*loadUpdate == (uint32_t)(uintptr_t)kLoadUpdate && *(int32_t*)(kLoad + kCount) == kLoadItems &&
        Callback(kLoad, kLoadUp) == (uint32_t)(uintptr_t)kScrollUp &&
        Callback(kLoad, kLoadDown) == (uint32_t)(uintptr_t)kScrollDown &&
        hook::Redirect(0x47062b, 0xe8, 0x482550, (const void*)&ReturnPressed)) {
        *loadUpdate = (uint32_t)(uintptr_t)&LoadUpdate;
        Log("menukeys: Load Game takes the arrows and Esc");
    } else {
        Log("menukeys: Load Game not as expected; its keys stay as they were");
    }
    uint32_t* netUpdate = (uint32_t*)(kNetOptions + kUpdate);
    if (*netUpdate == (uint32_t)(uintptr_t)kOptionsUpdate && *(int32_t*)(kNetOptions + kCount) == kNetOptionsItems &&
        Callback(kNetOptions, kCreditDown) == (uint32_t)(uintptr_t)kCreditArrow &&
        Callback(kNetOptions, kCreditUp) == (uint32_t)(uintptr_t)kCreditArrow &&
        Callback(kNetOptions, kTargetDown) == (uint32_t)(uintptr_t)kTargetArrow &&
        Callback(kNetOptions, kTargetUp) == (uint32_t)(uintptr_t)kTargetArrow) {
        *netUpdate = (uint32_t)(uintptr_t)&NetOptionsUpdate;
        Log("menukeys: Network game options: Left/Right change the credit and target");
    } else {
        Log("menukeys: Network game options not as expected; its keys stay as they were");
    }
    uint32_t* optionsEnter = (uint32_t*)(kOptions + kEnter);
    if (*optionsEnter == (uint32_t)(uintptr_t)kOptionsEnter && *(int32_t*)(kOptions + kScrollCount) == 0 &&
        Callback(kOptions, 0) == 0x474730) {
        *optionsEnter = (uint32_t)(uintptr_t)&OptionsEnter;
        Log("menukeys: Options: Up/Down onto Abort Race no longer abort the race");
    } else {
        Log("menukeys: Options not as expected; Up/Down onto Abort Race still abort the race");
    }
    uint32_t* lobbyUpdate = (uint32_t*)(kLobby + kUpdate);
    if (*(int32_t*)(kLobby + kCount) >= kLobbyAbort + 1 &&
        Callback(kLobby, kLobbyAbort) == (uint32_t)(uintptr_t)0x4735d0 &&
        hook::Redirect(0x473ab0, 0xe8, 0x482550, (const void*)&ReturnPressed)) {
        g_lobbyUpdate = (ScreenFn)(uintptr_t)*lobbyUpdate;
        *lobbyUpdate = (uint32_t)(uintptr_t)&LobbyUpdate;
        Log("menukeys: lobby: Esc picks ABORT GAME, a second Esc presses it");
    } else {
        Log("menukeys: lobby not as expected; its keys stay as they were");
    }
}

}  // namespace menukeys
