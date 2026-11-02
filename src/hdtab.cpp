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

#include "input.h"
#include "log.h"
#include "options.h"
#include "settings.h"

namespace hdtab {
namespace {

uint8_t* const kScreen = (uint8_t*)0x63e528;  // the Graphics screen struct
constexpr int kCount = 0x104, kEnter = 0x108, kExit = 0x10c, kSavedFocus = 0x118;
constexpr int kItems = 0x130, kItemSize = 0x158, kRadios = 0x9be8, kRadioSize = 0x2c, kRadioCount = 0x9be4;
constexpr int kStockItems = 53, kTextOwn = 0x404, kTextGraphics = 0xf9, kStub = 0x466450;
int* const kFocus = (int*)0x688770;

typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kGraphicsEnter = (ScreenFn)0x4747e0, kGraphicsExit = (ScreenFn)0x474850;
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
constexpr int kTabLeft = 252, kTabRight = 322, kTabTop = 51, kTabBottom = 84;  // screen, 640x480

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
};
constexpr int kRowCount = 10, kFirstGroup = 15;
enum { kResolution, kWidescreen, kHudWidth, kControls, kPedestrians, kDrawDistance, kAntialiasing, kTextures,
       kColour, kFrameRate };
const float kYon[4] = {0, 50.0f, 70.0f, 100.0f};  // Original keeps the game's own

int g_first[kRowCount], g_words[kRowCount];  // first word item and word count per row
int g_lastItem;
bool g_hdPage;

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
    hd.x = hd.origX = kTabLeft + (kTabRight - kTabLeft - hd.w) / 2;
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
    case kControls: return Index(settings::Get("Controls", "Scheme", "classic"), {"classic", "modern"});
    case kPedestrians: return 0;
    case kDrawDistance:
        return Index(settings::Get("Display", "DrawDistance", "max"), {"original", "far", "further", "max"});
    case kAntialiasing: return Index(settings::Get("Visuals", "Antialiasing", "4x"), {"off", "2x", "4x", "8x"});
    case kTextures: return Index(settings::Get("Visuals", "Textures", "sharpest"), {"original", "smooth", "sharpest"});
    case kColour: return Index(settings::Get("Visuals", "Colour", "smooth"), {"16-bit", "smooth"});
    case kFrameRate: return Index(settings::Get("Visuals", "FrameRate", "auto"), {"60", "auto"});
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
    case kControls: settings::Set("Controls", "Scheme", v ? "modern" : "classic"); break;
    case kDrawDistance: settings::Set("Display", "DrawDistance", dd[v]); break;
    case kAntialiasing: settings::Set("Visuals", "Antialiasing", kRows[r].words[v]); break;
    case kTextures: settings::Set("Visuals", "Textures", kRows[r].words[v]); break;
    case kColour: settings::Set("Visuals", "Colour", kRows[r].words[v]); break;
    case kFrameRate: settings::Set("Visuals", "FrameRate", kRows[r].words[v]); break;
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
// where screen x = 300 + 1.25x and screen y = 250 - 1.25y.

uintptr_t* const kModelsC = (uintptr_t*)0x687058;  // C models, stride 0x10 (br_model* first)
constexpr int kTabModel = 5, kScreenModelC = 0x12c;

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
    for (int i = 0; i < n; i++) {
        v[i * 10] = tx0 + (v[i * 10] - x0) * (tx1 - tx0) / (x1 - x0);
        v[i * 10 + 1] = ty0 + (v[i * 10 + 1] - y0) * (ty1 - ty0) / (y1 - y0);
    }
    reinterpret_cast<void(__cdecl*)(void*, int)>(0x51f950)(model, 1);  // BrModelUpdate
    Log("hdtab: tab panel shaped from model C-5 (%d vertices)", n);
}

int __fastcall Enter(void* s) {
    ShapeTabModel();
    int r = kGraphicsEnter(s);
    if (Field(kCount) == kStockItems) Append();
    ShowPage(false);
    for (int row = 0; row < kRowCount; row++) {
        int v = Current(row);
        kSetSelected(kScreen, kFirstGroup + row, v >= 0 ? g_first[row] + v : -1);
        SetRowState(row, 1);
    }
    SetRowState(kPedestrians, -1);  // Zombies/Aliens wait for the original disc data
    LockStockYon(Current(kDrawDistance) > 0);
    int saved = Field(kSavedFocus);
    if (saved < 0 || saved > g_lastItem || !At(saved).visible || At(saved).state <= 0) Field(kSavedFocus) = 1;
    return r;
}

int __fastcall Exit(void* s) {
    int dd = Selected(kDrawDistance);
    if (dd > 0) {
        kSetYon(kYon[dd]);
        for (int i = kStockYonFirst; i <= kStockYonLast; i++) At(i).selected = 0;
    }
    int r = kGraphicsExit(s);
    for (int row = 0; row < kRowCount; row++) {
        int v = Selected(row);
        if (v >= 0 && row != kPedestrians) Store(row, v);
    }
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
    *enter = (uint32_t)(uintptr_t)&Enter;
    *exit = (uint32_t)(uintptr_t)&Exit;
    Log("hdtab: installed");
}

}  // namespace hdtab
