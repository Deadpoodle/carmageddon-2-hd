// Key set E: the modern controls as the game's own fifth key set (re/input.md
// §2), on the Controls screen beside A-D, shown and remapped there like them.
//
// The game keeps each set in data\KEYMAP_<n>.TXT (77 key codes, one per
// keymap line) and the chosen one in OPTIONS.TXT KeyMapIndex; its file name is
// built as '0' + index, so index 4 is KEYMAP_4.TXT, and LoadKeyMapping,
// SaveOptions and the Controls screen's save on exit (0x472a30) all take it as
// it is. Two places need help:
// - The Controls screen's start (0x4725f0) reads sets 0-3 into a 4 x 77
//   buffer (0x6869e0) and copies buffer[index] into the live keys, which at
//   index 4 would read past it. For E the start runs as if on A, then E is
//   loaded from its file.
// - The A-D buttons (items 39-42, 0x472440) take the set from the focused
//   item's position (item - 39). E is an appended item with its own click,
//   doing what theirs does: save the current set to its file, load E's.
//
// KEYMAP_4.TXT is written once, from the factory set A (DKEYMAP0.TXT), with
// the modern keys, the unused lines that would clash set to a key that can
// never be pressed (80, "KP ="), the sound toggle on F9 and the cockpit
// toggle on V. Key codes come from KEYNAMES.TXT by name. Set E is the modern
// scheme: choosing it (here or on the HD tab) turns the scheme on.
#include "keysets.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "input.h"
#include "log.h"
#include "options.h"
#include "settings.h"

namespace keysets {
namespace {

constexpr int kLines = 77, kSetE = 4, kNeverPressed = 80;
int* const kKeyMapping = (int*)0x74b5e0;  // gKey_mapping[77]
int* const kKeyMapIndex = (int*)0x68b88c;  // OPTIONS.TXT KeyMapIndex

uint8_t* const kScreen = (uint8_t*)0x604940;  // the Controls screen
constexpr int kCount = 0x104, kEnter = 0x108, kItems = 0x130, kItemSize = 0x158;
constexpr int kStockItems = 68, kSetA = 39, kSetD = 42, kSetEItem = 68, kTextOwn = 0x404;
typedef int(__fastcall* ScreenFn)(void* s);
const ScreenFn kControlsStart = (ScreenFn)0x4725f0, kSelectSet = (ScreenFn)0x472440;
const auto kShowRows = (void(__fastcall*)(void*))0x4720e0;  // the list's names and keys
const auto kAutoSize = (void(__fastcall*)(void*))0x4666f0;
const auto kSaveOptions = (void (*)())0x48d190;

struct Item {
    int32_t text, callback, target, result, font, fontLit, x, y, w, h, state, visible, pixelmap, greyed, selectable,
        selected, group, levelBar, wrap, glow, origX, origY;
    uint8_t flags;
    char name[255];
};
static_assert(sizeof(Item) == kItemSize, "item record");
Item& At(int i) { return *(Item*)(kScreen + kItems + i * kItemSize); }

std::string SetPath(int set) {
    char name[32];
    snprintf(name, sizeof name, "data\\KEYMAP_%d.TXT", set);
    return GameDir() + name;
}

bool ReadSet(const std::string& path, int* keys) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    int n = 0;
    while (n < kLines && fscanf(f, "%d", &keys[n]) == 1) n++;
    fclose(f);
    return n == kLines;
}

bool WriteSet(const std::string& path, const int* keys) {  // as the game writes them: "%d\r\n"
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    for (int i = 0; i < kLines; i++) fprintf(f, "%d\r\n", keys[i]);
    fclose(f);
    return true;
}

// KEYMAP_4.TXT, if it isn't there yet.
void EnsureSetE() {
    std::string path = SetPath(kSetE);
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    int keys[kLines];
    if (!ReadSet(GameDir() + "data\\DKEYMAP0.TXT", keys) && !ReadSet(SetPath(0), keys)) {
        Log("keysets: no set A to build set E from");
        return;
    }
    std::vector<std::string> names;  // KEYNAMES.TXT: game key code = line - 2
    if (FILE* f = fopen((GameDir() + "data\\KEYNAMES.TXT").c_str(), "rb")) {
        char line[64];
        while (fgets(line, sizeof line, f)) {
            std::string s = line;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            names.push_back(s);
        }
        fclose(f);
    }
    auto code = [&](const char* name) {
        for (size_t i = 0; i < names.size(); i++)
            if (_stricmp(names[i].c_str(), name) == 0) return (int)i - 2;
        return -100;
    };
    static const struct {
        int line;
        const char* key;
    } kModern[] = {{49, "w"}, {50, "s"}, {47, "a"}, {48, "d"},  {54, "space"}, {45, "f"}, {60, "r"},
                   {61, "e"}, {64, "q"}, {63, "z"}, {62, "g"},  {56, "x"},     {75, "c"}, {58, "p"},
                   {31, "i"}, {32, "k"}, {33, "j"}, {34, "l"},  {37, "f9"},    {46, "v"}};
    // (61 use power-up, 64/63 next/previous, 62 the list, 56 wheelspin, 37 sound
    // on/off (was S), 46 cockpit (was C); all within reach of W/A/S/D.)
    int setA[kLines];
    memcpy(setA, keys, sizeof keys);
    for (const auto& m : kModern) {
        int c = code(m.key);
        if (c < 0) {
            Log("keysets: KEYNAMES has no \"%s\"; set E not written", m.key);
            return;
        }
        keys[m.line] = c;
    }
    // A key E newly gives an action is taken off any other line from line 28
    // on (where the game reads plain keys and the Controls screen checks
    // clashes); the lines below are unused or Ctrl/Shift combinations.
    for (const auto& m : kModern) {
        if (setA[m.line] == keys[m.line]) continue;  // the same as in set A (Space)
        for (int line = 28; line < kLines; line++) {
            bool target = false;
            for (const auto& t : kModern) target = target || t.line == line;
            if (!target && keys[line] == keys[m.line]) {
                Log("keysets: set E: \"%s\" taken off line %d", m.key, line);
                keys[line] = kNeverPressed;
            }
        }
    }
    // Unused lines holding letters E uses (and blocking them on the Controls screen).
    for (int line : {2, 3, 26, 27, 28, 29, 30, 36, 51, 52, 53, 55}) keys[line] = kNeverPressed;
    Log("keysets: set E (modern keys) written to KEYMAP_4.TXT: %s", WriteSet(path, keys) ? "ok" : "FAILED");
}

bool LoadSet(int set) {
    int keys[kLines];
    if (!ReadSet(SetPath(set), keys)) return false;
    memcpy(kKeyMapping, keys, sizeof keys);
    return true;
}

// The Controls screen's A-E look: one of them selected.
void ShowSelected() {
    for (int i = kSetA; i <= kSetD; i++) At(i).selected = *kKeyMapIndex == i - kSetA;
    if ((int)*(int32_t*)(kScreen + kCount) > kSetEItem) At(kSetEItem).selected = *kKeyMapIndex == kSetE;
}

// The scheme follows the set: E is modern; the classic set to go back to is remembered.
void SchemeFromSet() {
    bool modern = *kKeyMapIndex == kSetE;
    if (!modern) settings::Set("Controls", "ClassicSet", std::to_string(*kKeyMapIndex).c_str());
    settings::Set("Controls", "Scheme", modern ? "modern" : "classic");
    input::LoadBindings();
}

// The screen activates a clicked item on every frame the button is held, so
// both act only on a change.
int __fastcall SelectE(void* s) {
    int old = *kKeyMapIndex;
    if (old == kSetE) return 0;
    if (old != kSetE) WriteSet(SetPath(old), kKeyMapping);  // as the A-D buttons do
    *kKeyMapIndex = kSetE;
    if (!LoadSet(kSetE)) Log("keysets: KEYMAP_4.TXT unreadable");
    ShowSelected();
    kShowRows(s);
    kAutoSize(s);
    SchemeFromSet();
    Log("keysets: set E chosen");
    return 0;
}

int __fastcall SelectAtoD(void* s) {
    int before = *kKeyMapIndex;
    int r = kSelectSet(s);  // saves the current set, loads the clicked one
    ShowSelected();
    if (*kKeyMapIndex != before) SchemeFromSet();
    return r;
}

void AppendE() {
    Item& e = At(kSetEItem);
    e = At(kSetD);  // D's look, font, group and size
    e.text = kTextOwn;
    snprintf(e.name, sizeof e.name, "E");
    e.x = e.origX = At(kSetD).x + (At(kSetD).x - At(kSetD - 1).x);
    e.callback = (int32_t)(uintptr_t)&SelectE;
    e.selected = 0;
    *(int32_t*)(kScreen + kCount) = kSetEItem + 1;
    Log("keysets: set E button appended at x %d", e.x);
}

int __fastcall ControlsStart(void* s) {
    bool onE = *kKeyMapIndex == kSetE;
    if (onE) *kKeyMapIndex = 0;  // the start's 4-set buffer: run it as on A
    int r = kControlsStart(s);
    if (*(int32_t*)(kScreen + kCount) == kStockItems) AppendE();
    if (onE) {
        *kKeyMapIndex = kSetE;
        if (!LoadSet(kSetE)) Log("keysets: KEYMAP_4.TXT unreadable");
        kShowRows(s);
    }
    ShowSelected();
    kAutoSize(s);
    return r;
}

// Right after OPTIONS.TXT (KeyMapIndex), before the game loads the set.
void OnOptionsLoaded() {
    EnsureSetE();
    std::string scheme = settings::Get("Controls", "Scheme", "classic");
    if (_stricmp(scheme.c_str(), "modern") == 0 && *kKeyMapIndex != kSetE) {  // from before set E existed
        settings::Set("Controls", "ClassicSet", std::to_string(*kKeyMapIndex).c_str());
        *kKeyMapIndex = kSetE;
        Log("keysets: modern scheme: key set E");
    }
    if (*kKeyMapIndex == kSetE && GetFileAttributesA(SetPath(kSetE).c_str()) == INVALID_FILE_ATTRIBUTES)
        *kKeyMapIndex = 0;  // no file: the game would stop with "Couldn't open Key Map file"
    settings::Set("Controls", "Scheme", *kKeyMapIndex == kSetE ? "modern" : "classic");
    input::LoadBindings();
}

}  // namespace

bool Modern() { return *kKeyMapIndex == kSetE; }

void SetModern(bool modern) {
    if (modern == Modern()) return;
    if (modern) {
        settings::Set("Controls", "ClassicSet", std::to_string(*kKeyMapIndex).c_str());
        *kKeyMapIndex = kSetE;
    } else {
        int set = atoi(settings::Get("Controls", "ClassicSet", "0").c_str());
        *kKeyMapIndex = set >= 0 && set < kSetE ? set : 0;
    }
    if (!LoadSet(*kKeyMapIndex)) Log("keysets: KEYMAP_%d.TXT unreadable", *kKeyMapIndex);
    settings::Set("Controls", "Scheme", modern ? "modern" : "classic");
    kSaveOptions();  // KeyMapIndex
    input::LoadBindings();
    Log("keysets: key set %c", 'A' + *kKeyMapIndex);
}

void Install() {
    uint32_t* enter = (uint32_t*)(kScreen + kEnter);
    bool ok = *enter == (uint32_t)(uintptr_t)kControlsStart && *(int32_t*)(kScreen + kCount) == kStockItems;
    for (int i = kSetA; i <= kSetD && ok; i++) ok = At(i).callback == (int32_t)(uintptr_t)kSelectSet;
    if (!ok) {
        Log("keysets: Controls screen not as expected; no set E");
        return;
    }
    *enter = (uint32_t)(uintptr_t)&ControlsStart;
    for (int i = kSetA; i <= kSetD; i++) At(i).callback = (int32_t)(uintptr_t)&SelectAtoD;
    options::OnLoaded(&OnOptionsLoaded);
    Log("keysets: set E installed");
}

}  // namespace keysets
