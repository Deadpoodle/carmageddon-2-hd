// The "HD" tab on the Graphics options screen (re/hd-tab.md).
//
// The screen is a compiled-in struct; its enter/exit pointers are swapped for
// ours, which call the originals. On first entry our items are appended after
// the stock 53 (a "HD" tab, a clickable "GRAPHICS" tab clone, and six rows of
// radio words in the game's own fonts), so drawing, hover, mouse, keyboard
// focus and selection are all the game's own. A tab click shows one page and
// hides the other and stays on the screen. On exit our rows are written to
// Carma2HD.ini; draw distance is applied live through the game's SetYon, and
// the stock Scenery Cut-off row (which would reset it) is greyed while ours
// owns it.
#include "hdtab.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "groups.h"
#include "input.h"
#include "keysets.h"
#include "log.h"
#include "options.h"
#include "settings.h"

namespace hdtab {
namespace {

uint8_t* const kScreen = (uint8_t*)0x63e528;  // the Graphics screen struct
constexpr int kCount = 0x104, kEnter = 0x108, kExit = 0x10c, kUpdate = 0x110, kSavedFocus = 0x118;
constexpr int kItems = 0x130, kItemSize = 0x158, kRadios = 0x9be8, kRadioSize = 0x2c, kRadioCount = 0x9be4;
constexpr int kStockItems = 53, kTextOwn = 0x404, kTextGraphics = 0xf9, kStub = 0x466450;
int* const kFocus = (int*)0x688770;

typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kGraphicsEnter = (ScreenFn)0x4747e0, kGraphicsExit = (ScreenFn)0x474850;
const ScreenFn kGraphicsUpdate = (ScreenFn)0x470c20;
const auto kAutoSize = (void(__fastcall*)(void*))0x4666f0;
const auto kGetSelected = (int(__fastcall*)(void*, int))0x484f40;
const auto kSetSelected = (void(__fastcall*)(void*, int, int))0x484f70;
const auto kSetYon = (void(__stdcall*)(float))0x446b70;

struct Item {
    int32_t text, callback, target, result, font, fontLit, x, y, w, h, state, visible, pixelmap, greyed, selectable,
        selected, group, levelBar, wrap, glow, origX, origY;
    uint8_t flags;
    char name[255];
};
static_assert(sizeof(Item) == kItemSize, "item record");

Item& At(int i) { return *(Item*)(kScreen + kItems + i * kItemSize); }
int32_t& Field(int off) { return *(int32_t*)(kScreen + off); }

// Items: 53 HD tab, 54 GRAPHICS tab, then each row: label + words.
constexpr int kHdTab = 53, kGfxTab = 54, kFirstRow = 55;
// The HD tab panel, beside the GRAPHICS tab (640x480 menu space).
constexpr float kTabLeft = 253.61f, kTabRight = 322, kTabTop = 51, kTabBottom = 83.47f;  // screen, 640x480

constexpr int kStockYonRow = 7, kStockYonFirst = 8, kStockYonLast = 11;  // Scenery Cut-off: label, words

struct Row {
    const char* label;
    int y;
    const char* words[5];
    int x[5];
};
const Row kRows[] = {
    {"Resolution:", 100, {"Auto", "1280x720", "1920x1080", "2560x1440", "3440x1440"}, {170, 221, 305, 395, 491}},
    {"Widescreen:", 120, {"On", "Off"}, {250, 310}},
    {"HUD width:", 140, {"Auto", "Full", "16:9", "4:3"}, {250, 330, 410, 490}},
    {"Controls:", 160, {"Classic", "Modern"}, {250, 330}},
    {"Pedestrians:", 180, {"Humans", "Zombies", "Aliens"}, {250, 330, 410}},
    {"Draw distance:", 200, {"Original", "Far", "Further", "Max"}, {250, 330, 410, 490}},
    {"Anti-aliasing:", 220, {"Off", "2x", "4x", "8x"}, {250, 330, 410, 490}},
    {"Textures:", 240, {"Original", "Smooth", "Sharpest"}, {250, 330, 410}},
    {"Colour:", 260, {"16-bit", "Smooth"}, {250, 330}},
    {"Frame rate:", 280, {"60", "Auto"}, {250, 330}},
    {"Earlier groups:", 300, {"On", "Off"}, {250, 330}},
};
constexpr int kRowCount = 11, kFirstGroup = 15;  // 99 items in all (the struct has room for 100)
enum { kResolution, kWidescreen, kHudWidth, kControls, kPedestrians, kDrawDistance, kAntialiasing, kTextures,
       kColour, kFrameRate, kEarlierGroups };
const float kYon[4] = {0, 50.0f, 70.0f, 100.0f};  // Original keeps the game's own

int g_first[kRowCount], g_words[kRowCount];  // first word item and word count per row
int g_lastItem;
bool g_hdPage;
int g_atStart[kRowCount];  // each row's value when the game started
bool g_started;

// "Takes effect after a restart", shown on the HD page while a row differs from
// what the game started with. The screen has no item to spare (99 and the
// cursor's), so it borrows a stock row's label, hidden on that page anyway.
constexpr int kNote = 2, kNoteX = 60, kNoteY = 318, kNoteFont = 3;
Item g_noteStock;

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

int __fastcall TabClick(void* s);

void Append() {
    Item& title = At(0);
    Fill(At(kHdTab), kTextOwn, "HD", 6, 7, title.x + title.w + 22, title.y, 1, (int)(uintptr_t)&TabClick);
    Fill(At(kGfxTab), kTextGraphics, nullptr, 6, 7, title.x, title.y, 1, (int)(uintptr_t)&TabClick);
    int i = kFirstRow;
    for (int r = 0; r < kRowCount; r++) {
        const Row& row = kRows[r];
        Fill(At(i++), kTextOwn, row.label, 3, 3, 60, row.y, 0, kStub);
        g_first[r] = i;
        g_words[r] = 0;
        for (int w = 0; w < 5 && row.words[w]; w++, g_words[r]++) {
            Item& it = At(i++);
            Fill(it, kTextOwn, row.words[w], 0, 1, row.x[w], row.y, 1, kStub);
            it.selectable = 1;
            it.group = kFirstGroup + r;
        }
        uint8_t* radio = kScreen + kRadios + (kFirstGroup + r - 1) * kRadioSize;
        memset(radio, 0, kRadioSize);
        ((int32_t*)radio)[0] = g_first[r] - 1;   // id: the label
        ((int32_t*)radio)[1] = g_words[r];      // total
        ((int32_t*)radio)[2] = g_first[r];      // first
        ((int32_t*)radio)[3] = i - 1;           // last
    }
    g_lastItem = i - 1;
    Field(kCount) = i;
    Field(kRadioCount) = kFirstGroup + kRowCount - 1;
    kAutoSize(kScreen);
    Item& hd = At(kHdTab);  // centred in its tab, level with GRAPHICS
    hd.x = hd.origX = (int)(kTabLeft + (kTabRight - kTabLeft - hd.w) / 2);
    Log("hdtab: %d items appended (count %d)", i - kStockItems, i);
}

void SetRowState(int r, int state) {
    At(g_first[r] - 1).state = state < 0 ? -1 : 0;
    for (int w = 0; w < g_words[r]; w++) At(g_first[r] + w).state = state;
}

// ---------------------------------------------------------------------------
// Values <-> ini

int Index(const std::string& v, std::initializer_list<const char*> names) {
    int i = 0;
    for (const char* n : names) {
        if (_stricmp(v.c_str(), n) == 0) return i;
        i++;
    }
    return -1;
}

int Current(int r) {
    switch (r) {
    case kResolution:
        return Index(settings::Get("Display", "Resolution", "auto"),
                     {"auto", "1280x720", "1920x1080", "2560x1440", "3440x1440"});
    case kWidescreen: return settings::Get("Display", "Widescreen", "1") == "0" ? 1 : 0;
    case kHudWidth: return Index(settings::Get("Display", "HudWidth", "auto"), {"auto", "full", "16:9", "4:3"});
    case kControls: return keysets::Modern() ? 1 : 0;  // key set E
    case kPedestrians: return Index(settings::Get("Game", "Pedestrians", "humans"), {"humans", "zombies"});
    case kDrawDistance:
        return Index(settings::Get("Display", "DrawDistance", "max"), {"original", "far", "further", "max"});
    case kAntialiasing: return Index(settings::Get("Visuals", "Antialiasing", "4x"), {"off", "2x", "4x", "8x"});
    case kTextures: return Index(settings::Get("Visuals", "Textures", "sharpest"), {"original", "smooth", "sharpest"});
    case kColour: return Index(settings::Get("Visuals", "Colour", "smooth"), {"16-bit", "smooth"});
    case kFrameRate: return Index(settings::Get("Visuals", "FrameRate", "auto"), {"60", "auto"});
    case kEarlierGroups: return settings::Get("Game", "EarlierGroups", "1") == "0" ? 1 : 0;
    }
    return -1;
}

void Store(int r, int v) {
    static const char* res[] = {"auto", "1280x720", "1920x1080", "2560x1440", "3440x1440"};
    static const char* hud[] = {"auto", "full", "16:9", "4:3"};
    static const char* dd[] = {"original", "far", "further", "max"};
    switch (r) {
    case kResolution: settings::Set("Display", "Resolution", res[v]); break;
    case kWidescreen: settings::Set("Display", "Widescreen", v == 0 ? "1" : "0"); break;
    case kHudWidth: settings::Set("Display", "HudWidth", hud[v]); break;
    case kControls: keysets::SetModern(v == 1); break;
    case kDrawDistance: settings::Set("Display", "DrawDistance", dd[v]); break;
    case kAntialiasing: settings::Set("Visuals", "Antialiasing", kRows[r].words[v]); break;
    case kTextures: settings::Set("Visuals", "Textures", kRows[r].words[v]); break;
    case kColour: settings::Set("Visuals", "Colour", kRows[r].words[v]); break;
    case kFrameRate: settings::Set("Visuals", "FrameRate", kRows[r].words[v]); break;
    case kPedestrians: settings::Set("Game", "Pedestrians", v == 1 ? "zombies" : "humans"); break;
    case kEarlierGroups: settings::Set("Game", "EarlierGroups", v == 0 ? "1" : "0"); break;
    }
}

int Selected(int r) {
    int item = kGetSelected(kScreen, kFirstGroup + r);
    return item < 0 ? -1 : item - g_first[r];
}

// The stock Scenery Cut-off row would reset Yon on exit; while our Draw
// distance isn't Original it is greyed and left unselected (then the stock
// exit skips SetYon).
void LockStockYon(bool ours) {
    for (int i = kStockYonRow; i <= kStockYonLast; i++) At(i).state = ours ? -1 : (i == kStockYonRow ? 0 : 1);
    if (ours)
        for (int i = kStockYonFirst; i <= kStockYonLast; i++) At(i).selected = 0;
}

void ShowPage(bool hd) {
    g_hdPage = hd;
    for (int i = 0; i < kStockItems; i++) At(i).visible = hd ? (i == 1) : 1;  // OK (1) stays
    Item& tab = At(kHdTab);
    tab.font = hd ? 5 : 6;
    tab.fontLit = hd ? 5 : 7;
    tab.state = hd ? 0 : 1;
    tab.visible = 1;
    for (int i = kGfxTab; i <= g_lastItem; i++) At(i).visible = hd ? 1 : 0;
    At(kNote) = g_noteStock;
    if (hd) {
        Fill(At(kNote), kTextOwn, "Settings will take effect after the game restarts", kNoteFont, kNoteFont, kNoteX,
             kNoteY, 0, kStub);
        At(kNote).wrap = 1;  // a fixed size: the text isn't measured
        At(kNote).w = 500;
        At(kNote).h = 16;
        At(kNote).visible = 0;
    }
}

int __fastcall TabClick(void*) {
    bool toHd = !g_hdPage;
    ShowPage(toHd);
    if (!toHd) LockStockYon(Selected(kDrawDistance) > 0);
    *kFocus = toHd ? kGfxTab : kHdTab;
    return 4;  // stay on the screen
}

// ---------------------------------------------------------------------------
// The HD tab's panel: spare menu model C-5 (used by no screen) scaled into a
// tab beside the GRAPHICS tab of panel A-8, so it has the panel's own tinted
// material and fades in with it (re/menus.md §5). Menu models are in a space
// where screen x = 300 + 1.25x and screen y = 250 - 1.25y. The model is a box
// rounded at all four corners. It shares panel A's edges exactly, so the two
// read as one panel: both are translucent, and any overlap shows as a dark line.
// Its bottom right corner is squared off to meet the panel's straight edge; the
// ten vertices of its bottom left corner are put on the ten of panel A's curve
// there. Spare model B-10 (no screen uses it) is squeezed into a straight strip
// over the join, as the separator between the tabs.

uintptr_t* const kModelsC = (uintptr_t*)0x687058;  // C models, stride 0x10 (br_model* first)
uintptr_t* const kModelsB = (uintptr_t*)0x687188;
constexpr int kTabModel = 5, kScreenModelC = 0x12c, kStripModel = 10, kScreenModelB = 0x128;
constexpr float kStripLeft = 252.6f, kStripRight = 254.7f, kStripTop = 58;  // down to kTabBottom

// The model's bottom left corner, from the bottom edge up the left side, and
// panel A-8's curve there (vertices 8, 262, 257, 261, 255, 256, 260, 258, 259, 7).
const int kCorner[10] = {11, 34, 30, 35, 28, 29, 33, 31, 32, 10};
const float kCurve[10][2] = {{263.19f, 83.47f}, {261.76f, 83.27f}, {260.17f, 82.89f}, {258.76f, 82.29f},
                             {257.09f, 81.16f}, {255.92f, 80.01f}, {255.00f, 78.60f}, {254.21f, 76.89f},
                             {253.85f, 75.62f}, {253.61f, 73.84f}};
const auto kModelUpdate = reinterpret_cast<void(__cdecl*)(void*, int)>(0x51f950);  // BrModelUpdate

void ShapeStrip() {
    uint8_t* model = (uint8_t*)kModelsB[kStripModel * 4];
    if (!model) return;
    float* v = *(float**)(model + 8);
    int n = *(uint16_t*)(model + 0x10);
    if (!v || n <= 0) return;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        float x = v[i * 10], y = v[i * 10 + 1];
        x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
    }
    float left = (kStripLeft - 300) / 1.25f, right = (kStripRight - 300) / 1.25f;
    float bottom = (250 - kTabBottom) / 1.25f, top = (250 - kStripTop) / 1.25f;
    if (x0 == left && x1 == right) return;  // already shaped
    for (int i = 0; i < n; i++) {  // every vertex to a corner: the model's outline is no rectangle
        v[i * 10] = v[i * 10] < (x0 + x1) / 2 ? left : right;
        v[i * 10 + 1] = v[i * 10 + 1] < (y0 + y1) / 2 ? bottom : top;
    }
    kModelUpdate(model, 0x7fff);
    Log("hdtab: tab separator shaped from model B-10 (%d vertices)", n);
}

void ShapeTabModel() {
    uint8_t* model = (uint8_t*)kModelsC[kTabModel * 4];
    if (!model) return;
    float* v = *(float**)(model + 8);
    int n = *(uint16_t*)(model + 0x10);
    if (!v || n <= 0) return;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        float x = v[i * 10], y = v[i * 10 + 1];
        x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
    }
    float tx0 = (kTabLeft - 300) / 1.25f, tx1 = (kTabRight - 300) / 1.25f;
    float ty0 = (250 - kTabBottom) / 1.25f, ty1 = (250 - kTabTop) / 1.25f;
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    if (x0 == tx0 && x1 == tx1) return;  // already shaped (the table persists until the front end reloads it)
    float corner = 0.22f;  // of the model's height: the rounded part
    for (int i = 0; i < n; i++) {
        float x = v[i * 10], y = v[i * 10 + 1], r = corner * (y1 - y0);
        v[i * 10] = tx0 + (x - x0) * (tx1 - tx0) / (x1 - x0);
        v[i * 10 + 1] = ty0 + (y - y0) * (ty1 - ty0) / (y1 - y0);
        if (y < y0 + r && x > x1 - r) {
            v[i * 10] = tx1;
            v[i * 10 + 1] = ty0;
        }
    }
    if (n == 44)
        for (int i = 0; i < 10; i++) {
            v[kCorner[i] * 10] = (kCurve[i][0] - 300) / 1.25f;
            v[kCorner[i] * 10 + 1] = (250 - kCurve[i][1]) / 1.25f;
        }
    kModelUpdate(model, 1);
    Log("hdtab: tab panel shaped from model C-5 (%d vertices)", n);
}

int __fastcall Enter(void* s) {
    ShapeTabModel();
    ShapeStrip();
    int r = kGraphicsEnter(s);
    if (Field(kCount) == kStockItems) {
        g_noteStock = At(kNote);
        Append();
    }
    ShowPage(false);
    for (int row = 0; row < kRowCount; row++) {
        int v = Current(row);
        if (!g_started) g_atStart[row] = v;
        kSetSelected(kScreen, kFirstGroup + row, v >= 0 ? g_first[row] + v : -1);
        SetRowState(row, 1);
    }
    At(g_first[kPedestrians] + 2).state = -1;  // Aliens wait for the original disc data
    g_started = true;
    LockStockYon(Current(kDrawDistance) > 0);
    int saved = Field(kSavedFocus);
    if (saved < 0 || saved > g_lastItem || !At(saved).visible || At(saved).state <= 0) Field(kSavedFocus) = 1;
    return r;
}

// Controls and Earlier groups apply at once. dgVoodoo2 reads its settings as the
// game starts, and the display, draw distance and pedestrian patches are laid
// down then, so a change to any other row waits for a restart.
int __fastcall Update(void* s) {
    int r = kGraphicsUpdate(s);
    if (g_hdPage) {
        bool differs = false;
        for (int row = 0; row < kRowCount; row++) {
            int v = Selected(row);
            if (v >= 0 && v != g_atStart[row] && row != kControls && row != kEarlierGroups) differs = true;
        }
        At(kNote).visible = differs;
    }
    return r;
}

int __fastcall Exit(void* s) {
    int dd = Selected(kDrawDistance);
    if (dd > 0) {
        kSetYon(kYon[dd]);
        for (int i = kStockYonFirst; i <= kStockYonLast; i++) At(i).selected = 0;
    }
    At(kNote) = g_noteStock;  // the stock exit reads the screen's rows
    int r = kGraphicsExit(s);
    for (int row = 0; row < kRowCount; row++) {
        int v = Selected(row);
        if (v >= 0) Store(row, v);
    }
    groups::SaveAutoLoad();  // the stock Auto Load row
    input::LoadBindings();  // Controls applies at once
    Log("hdtab: saved (draw distance %d)", dd);
    return r;
}

// At start-up, right after OPTIONS.TXT: our draw distance, unless Original.
// The cameras don't exist yet, so gYon (which they are created from) is set.
void ApplyDrawDistance() {
    int dd = Current(kDrawDistance);
    if (dd <= 0) return;
    *(float*)0x761f4c = kYon[dd];  // gYon
    Log("hdtab: draw distance %s (Yon %.0f)", kRows[kDrawDistance].words[dd], kYon[dd]);
}

}  // namespace

void Install() {
    options::OnLoaded(&ApplyDrawDistance);
    uint32_t* enter = (uint32_t*)(kScreen + kEnter);
    uint32_t* exit = (uint32_t*)(kScreen + kExit);
    if (*enter != 0x4747e0 || *exit != 0x474850) {
        Log("hdtab: Graphics screen pointers not as expected; no HD tab");
        return;
    }
    if (*(int32_t*)(kScreen + kScreenModelC) == 0) *(int32_t*)(kScreen + kScreenModelC) = kTabModel;
    if (*(int32_t*)(kScreen + kScreenModelB) == 0) *(int32_t*)(kScreen + kScreenModelB) = kStripModel;
    *enter = (uint32_t)(uintptr_t)&Enter;
    *exit = (uint32_t)(uintptr_t)&Exit;
    *(uint32_t*)(kScreen + kUpdate) = (uint32_t)(uintptr_t)&Update;
    Log("hdtab: installed");
}

}  // namespace hdtab
