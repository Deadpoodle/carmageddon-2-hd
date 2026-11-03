// The join address on the Network screen (re/network.md §4).
//
// The screen is a compiled-in struct; its enter pointer is swapped for ours,
// which calls the original and, on first entry, appends an "IP:" label and a
// text field (on the game's own blue field box) along the bottom of the panel
// under the game list, below the selected game's details, and a status line
// under them. The field is edited with the game's own text entry, the one the
// driver name uses (typed characters, Backspace, Return, Escape): a click
// starts it, Return or Escape ends it. The screen's update would copy whatever
// field was being edited into the driver name, so its call to the text entry
// is redirected here and ends our field itself. The address is saved to
// [Network] JoinAddress and handed to the network layer at once, so the
// host's game shows in the list within a few seconds.
//
// The game draws no cursor in a field being edited (nor for the driver name),
// so ours blinks one after the text; and the status line says what is
// happening: type, searching, not found, found. The screen's update is
// wrapped for that. When the host at the address answers and its game is
// listed, the game's own row click (GameList_Click 0x4682c0) selects it, which
// fills in its details, and the focus goes to JOIN.
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
constexpr int kCount = 0x104, kEnter = 0x108, kUpdate = 0x110, kItems = 0x130, kItemSize = 0x158;
constexpr int kStockItems = 31, kTextOwn = 0x404, kTextNone = 0x402, kStub = 0x466450, kNiceBlue = 22;
constexpr int kLabel = 31, kBox = 32, kField = 33, kStatus = 34;  // appended after the stock 31
constexpr int kJoin = 29, kListFirst = 0x880c, kListLast = 0x8810;  // JOIN; the game list's first/last row items
constexpr int kMaxLength = 40;

typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kNetworkEnter = (ScreenFn)0x467ef0, kNetworkUpdate = (ScreenFn)0x469320;
const ScreenFn kGameListClick = (ScreenFn)0x4682c0;  // selects the game of the focused row, if joinable
const auto kAutoSize = (void(__fastcall*)(void*))0x4666f0;
const auto kTextEntry = (int (*)())0x46e470;  // < 0 when Return or Escape ends the edit
const auto kClearChars = (void (*)())0x51b0a0;
const auto kKeysOff = (void (*)())0x484610, kWaitKeys = (void (*)())0x4833b0, kKeysOn = (void (*)())0x484600;
int* const kFocus = (int*)0x688770;
uintptr_t* const kListGames = (uintptr_t*)0x688718;  // 6 x {game details*, time listed}; details+6 = host node
int* const kSelectedGame = (int*)0x688760;
int* const kMouseMode = (int*)0x79ecb8;  // focus follows the cursor; a key clears it, cursor movement sets it

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
// 2 left of and 1 below its text, as the driver name's do. The status line
// below, in the details' small font (11, about 7 pixels a letter), fits the
// panel's 196 pixels at 26 letters or fewer.
constexpr int kLeft = 414, kRight = 610, kY = 428, kFieldH = 16;
constexpr int kStatusY = kY + 19, kStatusFont = 11;

char g_edit[kMaxLength + 2];  // the text being typed (the field shows it with the cursor)
std::string g_address;        // the address in use
DWORD g_since;                // when the search for it started
bool g_picked;                // its game has been selected once (then the player's choices stand)

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
    // A fixed width (wrap on keeps the game's auto-size off), as the game's
    // own details text has: the item draw word-wraps to the item's width
    // (0x46e020 -> 0x463ad0), and an auto-sized item last measured empty has
    // width 0, which no letter fits: the wrap then never advances (it froze
    // the game the first time the status was set while typing).
    Item& status = At(kStatus);
    Fill(status, kTextOwn, "", kStatusFont, kStatusFont, kLeft, kStatusY, 0, kStub);
    status.wrap = 1;
    status.w = kRight + 2 - kLeft;
    status.h = 12;
    Field(kCount) = kStatus + 1;
    kAutoSize(kScreen);  // the label's width
    field.x = field.origX = label.x + label.w + 8;
    field.w = kRight - field.x;
    field.h = kFieldH;
    box.x = box.origX = field.x - 2;
    box.w = field.w + 4;
    box.h = kFieldH;
    box.pixelmap = kNiceBlue;
    Log("netmenu: join address field appended (count %d)", kStatus + 1);
}

void SetText(const std::string& text) { snprintf(At(kField).name, kMaxLength + 1, "%s", text.c_str()); }

void SetStatus(const char* text) {
    char* name = At(kStatus).name;
    if (strcmp(name, text) == 0) return;
    snprintf(name, sizeof At(kStatus).name, "%s", text);
    if (*text) Log("netmenu: status \"%s\"", text);
}

// The field while typing: the text and a bar cursor blinking twice a second
// (the fonts' "_" is a short dash at the baseline that reads as a full stop).
void ShowEditing() { snprintf(At(kField).name, kMaxLength + 2, "%s%s", g_edit, GetTickCount() / 250 % 2 ? "|" : ""); }

int __fastcall FieldClick(void*) {
    if (*kEditing) return 0;
    Item& field = At(kField);
    *kEditFlag = 1;
    *kEditing = 1;
    *kEditItem = kField;
    field.font = 2;  // the game's editing look
    field.fontLit = 3;
    snprintf(g_edit, sizeof g_edit, "%s", g_address.c_str());
    snprintf(kEditBackup, kBackupSize, "%s", g_edit);
    kClearChars();
    *kEditPos = -1;
    *kEditMax = kMaxLength;
    *kEditText = g_edit;
    ShowEditing();
    SetStatus("TYPE, THEN PRESS RETURN");
    Log("netmenu: editing the join address");
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
    std::string text = clear ? "" : g_edit;
    size_t a = text.find_first_not_of(' '), b = text.find_last_not_of(' ');
    text = a == std::string::npos ? "" : text.substr(a, b - a + 1);
    SetText(text);
    settings::Set("Network", "JoinAddress", text.c_str());
    netshim::SetJoinAddress(text);
    g_address = text;
    g_since = GetTickCount();
    g_picked = false;
    Log("netmenu: join address \"%s\"", text.c_str());
}

// The update's call to the text entry. For our field it ends the edit here
// and tells the update to carry on as while typing (>= 0), so it never
// reaches the copy into the driver name.
int TextEntry() {
    if (!*kEditing || *kEditItem != kField || *kEditText != g_edit) return kTextEntry();
    bool wasEmpty = g_edit[0] == 0;
    int r = kTextEntry();
    if (r >= 0) {
        ShowEditing();
        return r;
    }
    // Return on an empty field (-1) restores the old text in the game's code;
    // here it clears the address. Escape (-2) keeps the old one.
    Finish(r == -1 && wasEmpty);
    return 0;
}

// The host at the address has answered: select its game once it is listed.
// Returns the status to show.
const char* PickHostGame(const uint8_t host[6]) {
    int first = Field(kListFirst), last = Field(kListLast);
    for (int i = 0; i < 6 && first + i <= last; i++) {
        const uint8_t* details = (const uint8_t*)kListGames[i * 2];
        if (!details || memcmp(details + 6, host, 6) != 0) continue;
        int focus = *kFocus;
        *kFocus = first + i;
        kGameListClick(kScreen);
        g_picked = true;
        if (*kSelectedGame == i) {
            *kFocus = kJoin;
            *kMouseMode = 0;  // as a key press does, so a cursor left on the field doesn't take the focus back
            Log("netmenu: the host's game is row %d: selected, focus on JOIN", i);
            return "FOUND: PRESS JOIN";
        }
        *kFocus = focus;
        Log("netmenu: the host's game is row %d but can't be joined now", i);
        return "THAT GAME CANNOT BE JOINED";
    }
    return "HOST FOUND, READING GAME";
}

// After each update of the screen: the status line.
void Tick() {
    if (*kEditing && *kEditItem == kField) return;  // "type it" stays while typing
    if (g_address.empty()) {
        SetStatus("");
        return;
    }
    uint8_t host[6];
    switch (netshim::GetJoinState(host)) {
    case netshim::kJoinNone:
        SetStatus("");
        break;
    case netshim::kJoinResolving:
        SetStatus("SEARCHING...");
        break;
    case netshim::kJoinBadAddress:
        SetStatus("ADDRESS NOT FOUND");
        break;
    case netshim::kJoinLooking:
        SetStatus(GetTickCount() - g_since > 10000 ? "NO GAME FOUND YET, TRYING" : "SEARCHING...");
        break;
    case netshim::kJoinAnswered:
        if (!g_picked) SetStatus(PickHostGame(host));
        break;
    }
}

int __fastcall Update(void* s) {
    int r = kNetworkUpdate(s);
    Tick();
    return r;
}


int __fastcall Enter(void* s) {
    int r = kNetworkEnter(s);
    if (Field(kCount) == kStockItems) Append();
    g_address = settings::Get("Network", "JoinAddress", "");
    SetText(g_address);
    SetStatus("");
    g_since = GetTickCount();
    g_picked = false;
    return r;
}

}  // namespace

void Install() {
    uint32_t* enter = (uint32_t*)(kScreen + kEnter);
    uint32_t* update = (uint32_t*)(kScreen + kUpdate);
    if (*enter != (uint32_t)(uintptr_t)kNetworkEnter || *update != (uint32_t)(uintptr_t)kNetworkUpdate ||
        Field(kCount) != kStockItems) {
        Log("netmenu: Network screen not as expected; no join address field");
        return;
    }
    if (!hook::Redirect(0x469357, 0xe8, 0x46e470, (const void*)&TextEntry)) {
        Log("netmenu: text entry call not as expected; no join address field");
        return;
    }
    *enter = (uint32_t)(uintptr_t)&Enter;
    *update = (uint32_t)(uintptr_t)&Update;
    Log("netmenu: installed");
}

}  // namespace netmenu
