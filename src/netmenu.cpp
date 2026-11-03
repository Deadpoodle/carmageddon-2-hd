// The join address on the Network screen (re/network.md §4).
//
// The screen is a compiled-in struct; its enter pointer is swapped for ours,
// which calls the original and, on first entry, appends an "IP:" label and a
// text field (on the game's own blue field box) along the bottom of the panel
// under the game list, below the selected game's details. The field is edited with the game's own text
// entry, the one the driver name uses (typed characters, Backspace, Return,
// Escape): a click starts it, Return or Escape ends it. The screen's update
// would copy whatever field was being edited into the driver name, so its
// call to the text entry is redirected here and ends our field itself. The
// address is saved to [Network] JoinAddress and handed to the network layer
// at once, so the host's game shows in the list within a few seconds.
#include "netmenu.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "hook.h"
#include "log.h"
#include "netshim.h"
#include "settings.h"

namespace netmenu {
namespace {

uint8_t* const kScreen = (uint8_t*)0x5b39b8;  // the Network screen struct
constexpr int kCount = 0x104, kEnter = 0x108, kItems = 0x130, kItemSize = 0x158;
constexpr int kStockItems = 31, kTextOwn = 0x404, kTextNone = 0x402, kStub = 0x466450, kNiceBlue = 22;
constexpr int kLabel = 31, kBox = 32, kField = 33;  // appended after the stock 31
constexpr int kMaxLength = 40;

typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kNetworkEnter = (ScreenFn)0x467ef0;
const auto kAutoSize = (void(__fastcall*)(void*))0x4666f0;
const auto kTextEntry = (int (*)())0x46e470;  // < 0 when Return or Escape ends the edit
const auto kClearChars = (void (*)())0x51b0a0;
const auto kKeysOff = (void (*)())0x484610, kWaitKeys = (void (*)())0x4833b0, kKeysOn = (void (*)())0x484600;

// The game's text-entry state (0x466ec0 starts and ends it for the driver name).
int* const kEditing = (int*)0x68b858;
int* const kEditItem = (int*)0x68723c;
int* const kEditFlag = (int*)0x688b20;
char** const kEditText = (char**)0x688af4;
int* const kEditMax = (int*)0x59b0d4;
int* const kEditPos = (int*)0x59b0d8;
char* const kEditBackup = (char*)0x763900;  // 0x60 bytes; Escape (and Return on an empty field) restore it
constexpr int kBackupSize = 0x60;

struct Item {
    int32_t text, callback, target, result, font, fontLit, x, y, w, h, state, visible, pixelmap, greyed, selectable,
        selected, group, levelBar, wrap, glow, origX, origY;
    uint8_t flags;
    char name[255];
};
static_assert(sizeof(Item) == kItemSize, "item record");

Item& At(int i) { return *(Item*)(kScreen + kItems + i * kItemSize); }
int32_t& Field(int off) { return *(int32_t*)(kScreen + off); }

// One line along the bottom of the panel under the game list (640x480 menu
// space), clear of the selected game's details (item 30 at y 350, up to
// seven lines): the label, then the field to the panel's right edge, room
// for about 20 characters. The field starts 8 past the label and its box sits
// 2 left of and 1 below its text, as the driver name's do.
constexpr int kLeft = 414, kRight = 610, kY = 428, kFieldH = 16;

void Fill(Item& it, int text, const char* name, int font, int fontLit, int x, int y, int state, int callback) {
    memset(&it, 0, sizeof it);
    it.text = text;
    it.callback = callback;
    it.font = font;
    it.fontLit = fontLit;
    it.x = it.origX = x;
    it.y = it.origY = y;
    it.state = state;
    it.visible = 1;
    if (name) snprintf(it.name, sizeof it.name, "%s", name);
}

int __fastcall FieldClick(void* s);

void Append() {
    Item& label = At(kLabel);
    Fill(label, kTextOwn, "IP:", 4, 4, kLeft, kY + 1, 0, kStub);
    Item& box = At(kBox);
    Item& field = At(kField);
    Fill(box, kTextNone, nullptr, 0, 1, 0, kY + 1, 0, kStub);
    Fill(field, kTextOwn, nullptr, 0, 1, 0, kY, 1, (int)(uintptr_t)&FieldClick);
    field.wrap = 1;  // keeps the game's auto-size off: a fixed hit rect, so an empty field can be clicked
    Field(kCount) = kField + 1;
    kAutoSize(kScreen);  // the label's width
    field.x = field.origX = label.x + label.w + 8;
    field.w = kRight - field.x;
    field.h = kFieldH;
    box.x = box.origX = field.x - 2;
    box.w = field.w + 4;
    box.h = kFieldH;
    box.pixelmap = kNiceBlue;
    Log("netmenu: join address field appended (count %d)", kField + 1);
}

void SetText(const std::string& text) { snprintf(At(kField).name, kMaxLength + 1, "%s", text.c_str()); }

int __fastcall FieldClick(void*) {
    if (*kEditing) return 0;
    Item& field = At(kField);
    *kEditFlag = 1;
    *kEditing = 1;
    *kEditItem = kField;
    field.font = 2;  // the game's editing look
    field.fontLit = 3;
    snprintf(kEditBackup, kBackupSize, "%s", field.name);
    kClearChars();
    *kEditPos = -1;
    *kEditMax = kMaxLength;
    *kEditText = field.name;
    Sleep(200);  // as the game does: the click or Return that started the edit doesn't end it
    return 0;
}

void Finish(bool clear) {
    Item& field = At(kField);
    kKeysOff();
    kWaitKeys();
    kKeysOn();
    *kEditFlag = 0;
    *kEditing = 0;
    *kEditText = nullptr;
    field.font = 0;
    field.fontLit = 1;
    kEditBackup[0] = 0;
    std::string text = clear ? "" : field.name;
    size_t a = text.find_first_not_of(' '), b = text.find_last_not_of(' ');
    text = a == std::string::npos ? "" : text.substr(a, b - a + 1);
    SetText(text);
    settings::Set("Network", "JoinAddress", text.c_str());
    netshim::SetJoinAddress(text);
    Log("netmenu: join address \"%s\"", text.c_str());
}

// The update's call to the text entry. For our field it ends the edit here
// and tells the update to carry on as while typing (>= 0), so it never
// reaches the copy into the driver name.
int TextEntry() {
    if (!*kEditing || *kEditItem != kField || *kEditText != At(kField).name) return kTextEntry();
    bool wasEmpty = At(kField).name[0] == 0;
    int r = kTextEntry();
    if (r >= 0) return r;
    // Return on an empty field (-1) restores the old text in the game's code;
    // here it clears the address. Escape (-2) keeps the old one.
    Finish(r == -1 && wasEmpty);
    return 0;
}

int __fastcall Enter(void* s) {
    int r = kNetworkEnter(s);
    if (Field(kCount) == kStockItems) Append();
    SetText(settings::Get("Network", "JoinAddress", ""));
    return r;
}

}  // namespace

void Install() {
    uint32_t* enter = (uint32_t*)(kScreen + kEnter);
    if (*enter != (uint32_t)(uintptr_t)kNetworkEnter || Field(kCount) != kStockItems) {
        Log("netmenu: Network screen not as expected; no join address field");
        return;
    }
    if (!hook::Redirect(0x469357, 0xe8, 0x46e470, (const void*)&TextEntry)) {
        Log("netmenu: text entry call not as expected; no join address field");
        return;
    }
    *enter = (uint32_t)(uintptr_t)&Enter;
    Log("netmenu: installed");
}

}  // namespace netmenu
