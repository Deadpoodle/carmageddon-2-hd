// The main menu's race groups.
//
// The main menu (0x5a80f0) lists one group of RACES.TXT, three races and a
// mission (items 4-7, its scroll window), under "GROUP n" (item 2). The group
// is the game's group pointer 0x74d5e4, an entry of the table at [0x68b8a0]
// (0xc each: race count, first race, mission). It moves on only when that
// group's mission is won (RaceResult 0x4e3440), so a finished group can't be
// raced again. Its up/down arrows (items 3 and 8, callbacks 0x46a010/
// 0x46a080) move the pointer to any group, but the update (0x46a0e0) shows
// them only once the game is complete (0x75bc04). Here they become left/right
// arrows either side of GROUP and page through the groups up to the one in
// progress; GROUP takes the focus on Up/Down, and Left/Right there change the
// group. The update's per-frame states for the arrows and the mission row
// (0x46a0fc..0x46a152, which grey the mission until it is due) are skipped
// (jne -> jmp at 0x46a0fc) and set here instead. Paging fills the list with
// the game's own 0x467b30, which reads the pointer, so it is lent the shown
// group, and then picks a race of that group as a click on it would.
//
// The pointer is the player's progress, so it stays on the group in progress.
// Picking a race (0x469ea0) sets it to that race's group, so it is put back
// afterwards. A save keeps only the race, so entering the menu sets it from
// the missions won. A race of an earlier group counts as a replay:
// RaceResult sees it with its mission flag cleared, so the next race is again
// one of the group in progress, and an old mission doesn't move the player on.
// Once the game is complete (0x75bc04) the stock game picks races at random
// across all groups; then every group can be shown and the pointer is left
// alone.
//
// Auto Load (the Graphics screen's row, 0x68b864, OPTIONS.TXT AutoLoad) loads
// the latest save when the game starts. It follows [Game] AutoLoad (default
// on), and the row writes it back.
#include "groups.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hook.h"
#include "log.h"
#include "options.h"
#include "settings.h"

namespace groups {
namespace {

uint8_t* const kMain = (uint8_t*)0x5a80f0;
constexpr int kCount = 0x104, kEnter = 0x108, kUpdate = 0x110, kSavedFocus = 0x118, kItems = 0x130, kItemSize = 0x158,
              kScrollFirst = 0x87a4;
constexpr int kMainItems = 26, kLabel = 2, kLeft = 3, kRight = 8, kRows = 4, kMissionRow = 3, kStart = 21;
constexpr int kLeftImage = 18, kRightImage = 19, kImageW = 16, kImageH = 32, kArrowW = 10, kArrowH = 20,
              kGap = 4, kGlow = 9;

typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kMainEnter = (ScreenFn)0x469a40, kMainUpdate = (ScreenFn)0x46a0e0, kPick = (ScreenFn)0x469ea0;
const ScreenFn kStockUp = (ScreenFn)0x46a010, kStockDown = (ScreenFn)0x46a080;
const auto kShowList = (void(__fastcall*)(void*))0x467b30;
const auto kAutoSize = (void(__fastcall*)(void*))0x4666f0;
const auto kRaceResult = (int(__fastcall*)(int))0x4e3440;
const auto kKeyPressed = (int(__fastcall*)(int))0x482550;
const auto kPlaySound = (int(__fastcall*)(int, int))0x455690;
const int* const kSoundOutlet = (const int*)0x6845fc;
constexpr int kClickSound = 0xbbc;
// Key codes for KeyPressed (KEYNAMES.TXT line - 2)
constexpr int kKeyLeft = 0x46, kKeyRight = 0x47, kPad4 = 0x55, kPad6 = 0x57;

int* const kFocus = (int*)0x688770;
int* const kMouseMode = (int*)0x79ecb8;
uint8_t* const* const kCurrent = (uint8_t* const*)0x688abc;
int* const kRace = (int*)0x75bbf8;  // the race START starts
uint8_t** const kGroupPtr = (uint8_t**)0x74d5e4;
uint8_t* const* const kGroupTable = (uint8_t* const*)0x68b8a0;
const int* const kGroupCount = (const int*)0x7634ec;
const int* const kRaceCount = (const int*)0x74d4a0;
uint8_t* const* const kRaceInfo = (uint8_t* const*)0x762438;  // the race being run: its RACES.TXT entry
const int* const kGameDone = (const int*)0x75bc04;
const int* const kMissionPending = (const int*)0x761a60;
int* const kAutoLoad = (int*)0x68b864;

uint8_t* const kRaceList = (uint8_t*)0x74d660;
constexpr int kRaceSize = 0x244, kRaceWins = 0x184, kRaceMission = 0x190, kRaceGroup = 0x240;
constexpr int kGroupSize = 0xc, kGroupMission = 8;

struct Item {
    int32_t text, callback, target, result, font, fontLit, x, y, w, h, state, visible, pixelmap, greyed, selectable,
        selected, group, levelBar, wrap, glow, origX, origY;
    uint8_t flags;
    char name[255];
};
static_assert(sizeof(Item) == kItemSize, "item record");

Item& At(int i) { return *(Item*)(kMain + kItems + i * kItemSize); }
int Window() { return *(int32_t*)(kMain + kScrollFirst); }

bool g_on;      // [Game] EarlierGroups, read on entering the menu
int g_view;     // the group shown
int g_lastPick = -1;  // the race last picked in the group in progress
DWORD g_lastPage;

uint8_t* Group(int g) { return *kGroupTable + g * kGroupSize; }
uint8_t* Race(int r) { return kRaceList + r * kRaceSize; }
int GroupOf(int race) {
    if (race < 0 || race >= *kRaceCount) return 0;
    return (int)((*(uint8_t**)(Race(race) + kRaceGroup) - *kGroupTable) / kGroupSize);
}
bool Done() { return *kGameDone != 0; }

// The group in progress: the first whose mission hasn't been won.
int Progress() {
    int n = *kGroupCount;
    for (int g = 0; g < n; g++) {
        uint8_t* mission = *(uint8_t**)(Group(g) + kGroupMission);
        if (mission && *(int32_t*)(mission + kRaceWins) == 0) return g;
    }
    return n - 1;
}
int Last() { return Done() ? *kGroupCount - 1 : Progress(); }

// The race the game would offer in the group in progress (as RaceResult
// chooses: a race not yet won, else the mission).
int Suggested(int g) {
    if (g_lastPick >= 0 && GroupOf(g_lastPick) == g) return g_lastPick;
    if (!*kMissionPending)
        for (int i = 0; i < kRows - 1; i++)
            if (*(int32_t*)(Race(g * kRows + i) + kRaceWins) == 0) return g * kRows + i;
    return g * kRows + kMissionRow;
}

void KeepProgress() {
    if (!Done()) *kGroupPtr = Group(Progress());
}

// The arrow pictures (menu images 18/19, Change Car's, 16x32 RGBA 4444) are
// taller than GROUP's letters. A button maps its picture 1:1 (UVs = size /
// power-of-two size, FE_CreateButton 0x46c5c0) and the driver stretches a
// picture of any other size to a power of two, so neither a smaller quad nor
// a smaller picture shows the whole arrow. Instead each copy is 16x32 with the
// arrow drawn 10x20 in its middle (each pixel the average of the source area,
// per 4-bit channel) and the rest the source's most transparent pixel (alpha
// is the top nibble; the right arrow's top-left corner is part of its edge, so
// a corner won't do). The menu-image table holds the copies while the main
// menu's buttons are built, right after its enter.
const auto kAllocate = (uint8_t*(__cdecl*)(int, int, int, void*, int))0x538d80;  // BrPixelmapAllocate
uint8_t** const kMenuImages = (uint8_t**)0x686850;
constexpr int kRgba4444 = 0x12;
constexpr int kInsetX = (kImageW - kArrowW) / 2, kInsetY = (kImageH - kArrowH) / 2;  // the arrow in its picture
uint8_t* g_small[2];  // the copies with the smaller arrows
uint8_t* g_lent[2];   // the table's own pictures while ours are lent, else null

uint8_t* Shrink(uint8_t* src) {
    if (!src || src[0x2c] != kRgba4444 || *(uint16_t*)(src + 0x34) != kImageW || *(uint16_t*)(src + 0x36) != kImageH)
        return nullptr;
    uint8_t* dst = kAllocate(kRgba4444, kImageW, kImageH, nullptr, 0);
    if (!dst) return nullptr;
    const uint8_t* sp = *(uint8_t**)(src + 8);
    int srow = *(int16_t*)(src + 0x28), drow = *(int16_t*)(dst + 0x28);
    uint16_t empty = 0xffff;
    for (int y = 0; y < kImageH; y++)
        for (int x = 0; x < kImageW; x++) {
            uint16_t p = *(const uint16_t*)(sp + y * srow + x * 2);
            if (p >> 12 < empty >> 12) empty = p;
        }
    for (int y = 0; y < kImageH; y++)
        for (int x = 0; x < kImageW; x++) *(uint16_t*)(*(uint8_t**)(dst + 8) + y * drow + x * 2) = empty;
    uint8_t* dp = *(uint8_t**)(dst + 8) + kInsetY * drow + kInsetX * 2;
    constexpr int kSub = 4;  // samples per pixel each way
    for (int y = 0; y < kArrowH; y++)
        for (int x = 0; x < kArrowW; x++) {
            int sum[4] = {};
            for (int j = 0; j < kSub; j++)
                for (int i = 0; i < kSub; i++) {
                    int sx = (x * kSub + i) * kImageW / (kArrowW * kSub);
                    int sy = (y * kSub + j) * kImageH / (kArrowH * kSub);
                    uint16_t p = *(const uint16_t*)(sp + sy * srow + sx * 2);
                    for (int c = 0; c < 4; c++) sum[c] += (p >> (c * 4)) & 15;
                }
            uint16_t q = 0;
            for (int c = 0; c < 4; c++) q |= (uint16_t)((sum[c] + kSub * kSub / 2) / (kSub * kSub)) << (c * 4);
            *(uint16_t*)(dp + y * drow + x * 2) = q;
        }
    return dst;
}

void LendSmall(bool lend) {
    for (int k = 0; k < 2; k++) {
        int slot = k ? kRightImage : kLeftImage;
        if (lend && g_small[k] && !g_lent[k]) {
            g_lent[k] = kMenuImages[slot];
            kMenuImages[slot] = g_small[k];
        } else if (!lend && g_lent[k]) {
            kMenuImages[slot] = g_lent[k];
            g_lent[k] = nullptr;
        }
    }
}

// The quads are built at the enter from the items' rects, and only the list
// rows' follow later changes (0x466ce0). GROUP's width changes with its number
// (GROUP 1 is narrower than GROUP 2), and the hit test uses the quads, so the
// label's quad is resized and the right arrow's moved to match; otherwise the
// label's old quad would take clicks meant for the arrow.
uint8_t* const kButtons = (uint8_t*)0x687248;
const int* const kButtonCount = (const int*)0x68683c;
constexpr int kButtonSize = 0x2c;
const auto kModelUpdate = (void(__cdecl*)(void*, int))0x51f950;
int g_quad[3][4];  // the rect each quad (label, left, right) was last given (0 = not yet)
bool g_built;      // the main menu's buttons exist (its update has run since the enter)

void MoveQuad(int slot, int item) {
    Item& a = At(item);
    int rect[4] = {a.x, a.y, a.w, a.h};
    if (!g_built || *kCurrent != kMain || *kButtonCount <= item || !memcmp(g_quad[slot], rect, sizeof rect)) return;
    uint8_t* model = *(uint8_t**)(kButtons + item * kButtonSize + 4);
    float* v = model ? *(float**)(model + 8) : nullptr;
    if (!v) return;
    float l = (float)a.x, r = (float)(a.x + a.w), t = -(float)a.y, b = -(float)(a.y + a.h);
    v[0] = v[10] = l;   // vertices 0..3 (0x28 apart): top left, bottom left, bottom right, top right
    v[20] = v[30] = r;
    v[1] = v[31] = t;
    v[11] = v[21] = b;
    kModelUpdate(model, 0x7fff);
    memcpy(g_quad[slot], rect, sizeof rect);
}

void PlaceArrows() {
    Item& label = At(kLabel);
    Item& left = At(kLeft);
    Item& right = At(kRight);
    int inset = g_small[0] && g_small[1] ? kInsetX : 0;  // the arrow's edge inside its picture
    left.x = left.origX = label.x - kGap - left.w + inset;
    right.x = right.origX = label.x + label.w + kGap - inset;
    left.y = left.origY = right.y = right.origY = label.y + (label.h - left.h) / 2;
    MoveQuad(0, kLabel);
    MoveQuad(1, kLeft);
    MoveQuad(2, kRight);
}

// The mission row and the arrows for group g (already in the list): a won
// mission can be raced again, the one in progress once it is due. (A greyed
// image button looks the same as a live one, so an arrow with nowhere to go
// is hidden.)
void Dress(int g) {
    bool past = Done() || g < Progress();
    At(Window() + kMissionRow).state = past || *kMissionPending ? 1 : -1;
    At(kLeft).state = At(kRight).state = 1;
    At(kLeft).visible = g > 0;  // an arrow shows only where there is a group to go to
    At(kRight).visible = g < Last();
    PlaceArrows();
}

void Show(int g) {
    uint8_t* keep = *kGroupPtr;
    *kGroupPtr = Group(g);
    kShowList(kMain);
    *kGroupPtr = keep;
    g_view = g;
    Dress(g);
}

int __fastcall Pick(void* s) {
    int r = kPick(s);
    if (g_on) {
        KeepProgress();
        g_view = GroupOf(*kRace);
        if (!Done() && g_view == Progress()) g_lastPick = *kRace;
        Dress(g_view);
    }
    return r;
}

// Show group g with one of its races picked, as a click on that race does.
bool Page(int d) {
    int g = g_view + d;
    if (g < 0 || g > Last()) return false;
    DWORD now = GetTickCount();
    if (now - g_lastPage < 250) return false;  // a held mouse button activates the arrow every frame
    g_lastPage = now;
    Show(g);
    int race = !Done() && g == Progress() ? Suggested(g) : g * kRows;
    int focus = *kFocus, mouse = *kMouseMode;
    *kFocus = Window() + race - g * kRows;
    *kMouseMode = 0;  // the pick writes the race's description only for keys
    Pick(kMain);
    *kFocus = focus;
    *kMouseMode = mouse;
    Show(g);  // marks the picked race
    Log("groups: group %d shown, race %d picked (group in progress %d)", g + 1, race, Progress() + 1);
    return true;
}

int __fastcall LeftArrow(void*) {
    Page(-1);
    return 0;
}
int __fastcall RightArrow(void*) {
    Page(1);
    return 0;
}

// The list refresh after Change Car and the like (0x467a70) shows the group
// being looked at, not the pointer's.
void __fastcall Refresh(void* s) {
    if (g_on) Show(g_view);
    else kShowList(s);
}

// The update's arrow and mission-row states: the stock ones, or ours.
const uint8_t kStockStates[] = {0x75, 0x1a}, kOurStates[] = {0xeb, 0x54};  // jne 0x46a118 / jmp 0x46a152
bool g_ourStates;
void OurStates(bool on) {
    if (on != g_ourStates &&
        hook::Patch(0x46a0fc, on ? kStockStates : kOurStates, on ? kOurStates : kStockStates, 2))
        g_ourStates = on;
}

Item g_stock[2];  // the stock arrows (3 and 8) as the screen file has them
bool g_saved;

int __fastcall Enter(void* s) {
    g_on = settings::Get("Game", "EarlierGroups", "1") != "0";
    OurStates(g_on);
    g_built = false;
    if (!g_on) {
        int r = kMainEnter(s);
        if (g_saved) {
            At(kLeft) = g_stock[0];
            At(kRight) = g_stock[1];
        }
        At(kLeft).callback = (int32_t)(uintptr_t)kStockUp;
        At(kRight).callback = (int32_t)(uintptr_t)kStockDown;
        At(kLabel).state = 0;
        At(kLabel).glow = 0;
        int32_t& saved = *(int32_t*)(kMain + kSavedFocus);  // the focus the screen reopens on
        if (saved == kLabel || saved == kLeft || saved == kRight) saved = kStart;
        return r;
    }
    KeepProgress();  // after a load the pointer is the saved race's group
    int g = GroupOf(*kRace);
    if (g > Last()) g = Last();
    uint8_t* keep = *kGroupPtr;
    *kGroupPtr = Group(g);  // the enter fills the list from the pointer
    int r = kMainEnter(s);
    *kGroupPtr = keep;
    if (!g_saved) {
        g_stock[0] = At(kLeft);
        g_stock[1] = At(kRight);
        g_saved = true;
    }
    g_view = g;
    if (!Done() && g == Progress()) g_lastPick = *kRace;

    // Items 3 and 8 become image buttons either side of GROUP, which takes
    // the focus; the three pulse together while it has it. The buttons are
    // built after the enter, from these fields.
    for (int k = 0; k < 2; k++)
        if (!g_small[k]) g_small[k] = Shrink(kMenuImages[k ? kRightImage : kLeftImage]);
    Item& label = At(kLabel);
    label.state = 1;
    label.glow = kGlow;
    for (int i : {kLeft, kRight}) {
        Item& a = At(i);
        a.callback = (int32_t)(uintptr_t)(i == kLeft ? &LeftArrow : &RightArrow);
        a.pixelmap = i == kLeft ? kLeftImage : kRightImage;
        a.w = kImageW;
        a.h = kImageH;
        a.glow = kGlow;
    }
    Dress(g);
    int32_t& saved = *(int32_t*)(kMain + kSavedFocus);
    if (saved == kLeft || saved == kRight) saved = kLabel;  // an arrow may be hidden now
    if (g_small[0] && g_small[1]) LendSmall(true);  // given back once the buttons are built
    memset(g_quad, 0, sizeof g_quad);
    return r;
}

// Up/Down pass over the arrows (GROUP stands for them): on to the next item
// the same way. The stock walk wraps only between NETWORK and the far end of
// the screen, never onto an arrow, so the direction is plain.
void Skip(void* s, int before) {
    int f = *kFocus;
    if (f != kLeft && f != kRight) return;
    int n = *(int32_t*)(kMain + kCount);
    int step = f > before ? 1 : -1;
    for (int i = 0, t = f; i < n; i++) {
        t = (t + step + n) % n;
        Item& it = At(t);
        if (t == kLeft || t == kRight || it.state <= 0 || !it.visible) continue;
        *kFocus = t;
        if (t >= Window() && t < Window() + kRows) ((ScreenFn)(uintptr_t)it.callback)(s);  // picked on the way, as the update does
        return;
    }
}

int __fastcall Update(void* s) {
    if (g_on && !*kMouseMode && *kFocus == kLabel) {
        bool left = kKeyPressed(kKeyLeft) || kKeyPressed(kPad4);
        if ((left || kKeyPressed(kKeyRight) || kKeyPressed(kPad6)) && Page(left ? -1 : 1))
            kPlaySound(*kSoundOutlet, kClickSound);
    }
    g_built = true;
    LendSmall(false);
    if (g_on) PlaceArrows();  // the first frame after the buttons were built, or a new label width
    int before = *kFocus;
    int r = kMainUpdate(s);
    if (g_on && *kCurrent == s && !*kMouseMode) Skip(s, before);
    return r;
}

// A won race from an earlier group is a replay: RaceResult sees it without
// its mission flag, so it doesn't move the player on a group and the next
// race is one of the group in progress.
int __fastcall RaceResult(int result) {
    uint8_t* race = *kRaceInfo;
    if (result != 3 || Done() || !race || *(uint8_t**)(race + kRaceGroup) == *kGroupPtr) return kRaceResult(result);
    int32_t mission = *(int32_t*)(race + kRaceMission);
    *(int32_t*)(race + kRaceMission) = 0;
    int r = kRaceResult(result);
    *(int32_t*)(race + kRaceMission) = mission;
    Log("groups: race %d of group %d won again; next race %d (group %d)", (int)((race - kRaceList) / kRaceSize),
        (int)((*(uint8_t**)(race + kRaceGroup) - *kGroupTable) / kGroupSize) + 1, *kRace, GroupOf(*kRace) + 1);
    return r;
}

void ApplyAutoLoad() {
    *kAutoLoad = settings::Get("Game", "AutoLoad", "1") != "0";
    Log("groups: Auto Load %s", *kAutoLoad ? "on (the latest save at start-up)" : "off");
}

uint32_t Callback(int item) { return *(uint32_t*)(kMain + kItems + item * kItemSize + 4); }
void SetCallback(int item, const void* fn) { *(uint32_t*)(kMain + kItems + item * kItemSize + 4) = (uint32_t)(uintptr_t)fn; }

}  // namespace

void SaveAutoLoad() { settings::Set("Game", "AutoLoad", *kAutoLoad ? "1" : "0"); }

void Install() {
    options::OnLoaded(&ApplyAutoLoad);
    uint32_t* enter = (uint32_t*)(kMain + kEnter);
    uint32_t* update = (uint32_t*)(kMain + kUpdate);
    bool rows = true;
    for (int i = 4; i < 4 + kRows; i++) rows = rows && Callback(i) == (uint32_t)(uintptr_t)kPick;
    if (*(int32_t*)(kMain + kCount) != kMainItems || *enter != (uint32_t)(uintptr_t)kMainEnter ||
        *update != (uint32_t)(uintptr_t)kMainUpdate || Callback(kLeft) != (uint32_t)(uintptr_t)kStockUp ||
        Callback(kRight) != (uint32_t)(uintptr_t)kStockDown || !rows || !hook::Matches(0x46a0fc, kStockStates, 2) ||
        !hook::Redirect(0x467a87, 0xe8, 0x467b30, (const void*)&Refresh) ||
        !hook::Redirect(0x504025, 0xe8, 0x4e3440, (const void*)&RaceResult)) {
        Log("groups: main menu not as expected; groups as the original");
        return;
    }
    *enter = (uint32_t)(uintptr_t)&Enter;
    *update = (uint32_t)(uintptr_t)&Update;
    for (int i = 4; i < 4 + kRows; i++) SetCallback(i, (const void*)&Pick);
    Log("groups: installed (GROUP arrows, replays of earlier groups)");
}

}  // namespace groups
