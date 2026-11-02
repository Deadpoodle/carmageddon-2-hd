// DirectInput keyboard hooks and the key translation layer.
//
// The proxy hands us the game's IDirectInputA; its CreateDevice slot is
// swapped so every device the game makes gets these slots swapped too
// (devices of one kind share a vtable, so each slot is swapped once).
//
// The game reads the whole keyboard once per frame with GetDeviceState(256)
// (PDSetKeyArray 0x51cef0, re/input.md), so remapping happens there, on the
// key state it sees:
// - each key bound to an action in the scheme or [Keys] is hidden from the
//   game ("does only that action": S no longer toggles the sound, Q no
//   longer quits with Ctrl...);
// - in a race, a bound key held down presses whatever key the game has for
//   that action (gKey_mapping line -> game key code -> gScan_code), so the
//   game's own keymap and Controls screen keep working underneath;
// - in a menu (the menus only know the arrows/keypad, Return and Esc), the
//   keys bound to Accelerate/Brake/Left/Right become the arrows;
// - while the Controls screen waits for a key to bind, nothing is changed.
#define DIRECTINPUT_VERSION 0x0500
#include "input.h"

#include <dinput.h>
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "frontend.h"
#include "headless.h"
#include "hook.h"
#include "log.h"
#include "settings.h"

namespace input {
namespace {

// IDirectInputA / IDirectInputDeviceA vtable slots
enum { kCreateDevice = 3 };
enum { kAcquire = 7, kGetDeviceState = 9, kGetDeviceData = 10, kSetCooperativeLevel = 13 };

typedef HRESULT(WINAPI* CreateDevice_t)(void*, REFGUID, void**, IUnknown*);
typedef HRESULT(WINAPI* GetDeviceState_t)(void*, DWORD, void*);
typedef HRESULT(WINAPI* GetDeviceData_t)(void*, DWORD, DIDEVICEOBJECTDATA*, DWORD*, DWORD);
typedef HRESULT(WINAPI* SetCooperativeLevel_t)(void*, HWND, DWORD);
typedef HRESULT(WINAPI* Acquire_t)(void*);

CreateDevice_t o_CreateDevice;
GetDeviceState_t o_GetDeviceState;
GetDeviceData_t o_GetDeviceData;
SetCooperativeLevel_t o_SetCooperativeLevel;
Acquire_t o_Acquire;

volatile BYTE g_script[256];  // scripted key state (0x80 = down), test runs only

uintptr_t Slot(void* obj, int i) { return (uintptr_t)(*(void***)obj + i); }

// ---------------------------------------------------------------------------
// Key names (DirectInput's, without DIK_)

struct KeyName {
    const char* name;
    BYTE dik;
    UINT vk;  // for scripted taps posted to the window; 0 = none
};
const KeyName kKeys[] = {
    {"ESCAPE", DIK_ESCAPE, VK_ESCAPE}, {"1", DIK_1, '1'}, {"2", DIK_2, '2'}, {"3", DIK_3, '3'}, {"4", DIK_4, '4'},
    {"5", DIK_5, '5'}, {"6", DIK_6, '6'}, {"7", DIK_7, '7'}, {"8", DIK_8, '8'}, {"9", DIK_9, '9'}, {"0", DIK_0, '0'},
    {"MINUS", DIK_MINUS, 0}, {"EQUALS", DIK_EQUALS, 0}, {"BACK", DIK_BACK, VK_BACK}, {"TAB", DIK_TAB, VK_TAB},
    {"Q", DIK_Q, 'Q'}, {"W", DIK_W, 'W'}, {"E", DIK_E, 'E'}, {"R", DIK_R, 'R'}, {"T", DIK_T, 'T'}, {"Y", DIK_Y, 'Y'},
    {"U", DIK_U, 'U'}, {"I", DIK_I, 'I'}, {"O", DIK_O, 'O'}, {"P", DIK_P, 'P'}, {"LBRACKET", DIK_LBRACKET, 0},
    {"RBRACKET", DIK_RBRACKET, 0}, {"RETURN", DIK_RETURN, VK_RETURN}, {"LCONTROL", DIK_LCONTROL, VK_CONTROL},
    {"A", DIK_A, 'A'}, {"S", DIK_S, 'S'}, {"D", DIK_D, 'D'}, {"F", DIK_F, 'F'}, {"G", DIK_G, 'G'}, {"H", DIK_H, 'H'},
    {"J", DIK_J, 'J'}, {"K", DIK_K, 'K'}, {"L", DIK_L, 'L'}, {"SEMICOLON", DIK_SEMICOLON, 0},
    {"APOSTROPHE", DIK_APOSTROPHE, 0}, {"GRAVE", DIK_GRAVE, 0}, {"LSHIFT", DIK_LSHIFT, VK_SHIFT},
    {"BACKSLASH", DIK_BACKSLASH, 0}, {"Z", DIK_Z, 'Z'}, {"X", DIK_X, 'X'}, {"C", DIK_C, 'C'}, {"V", DIK_V, 'V'},
    {"B", DIK_B, 'B'}, {"N", DIK_N, 'N'}, {"M", DIK_M, 'M'}, {"COMMA", DIK_COMMA, 0}, {"PERIOD", DIK_PERIOD, 0},
    {"SLASH", DIK_SLASH, 0}, {"RSHIFT", DIK_RSHIFT, 0}, {"MULTIPLY", DIK_MULTIPLY, 0}, {"LMENU", DIK_LMENU, 0},
    {"LALT", DIK_LMENU, 0}, {"SPACE", DIK_SPACE, VK_SPACE}, {"CAPITAL", DIK_CAPITAL, 0}, {"F1", DIK_F1, VK_F1},
    {"F2", DIK_F2, VK_F2}, {"F3", DIK_F3, VK_F3}, {"F4", DIK_F4, VK_F4}, {"F5", DIK_F5, VK_F5}, {"F6", DIK_F6, VK_F6},
    {"F7", DIK_F7, VK_F7}, {"F8", DIK_F8, VK_F8}, {"F9", DIK_F9, VK_F9}, {"F10", DIK_F10, VK_F10},
    {"NUMLOCK", DIK_NUMLOCK, 0}, {"SCROLL", DIK_SCROLL, 0}, {"NUMPAD7", DIK_NUMPAD7, 0}, {"NUMPAD8", DIK_NUMPAD8, 0},
    {"NUMPAD9", DIK_NUMPAD9, 0}, {"SUBTRACT", DIK_SUBTRACT, 0}, {"NUMPAD4", DIK_NUMPAD4, 0},
    {"NUMPAD5", DIK_NUMPAD5, 0}, {"NUMPAD6", DIK_NUMPAD6, 0}, {"ADD", DIK_ADD, 0}, {"NUMPAD1", DIK_NUMPAD1, 0},
    {"NUMPAD2", DIK_NUMPAD2, 0}, {"NUMPAD3", DIK_NUMPAD3, 0}, {"NUMPAD0", DIK_NUMPAD0, 0},
    {"DECIMAL", DIK_DECIMAL, 0}, {"F11", DIK_F11, VK_F11}, {"F12", DIK_F12, VK_F12},
    {"NUMPADENTER", DIK_NUMPADENTER, 0}, {"RCONTROL", DIK_RCONTROL, 0}, {"DIVIDE", DIK_DIVIDE, 0},
    {"RMENU", DIK_RMENU, 0}, {"RALT", DIK_RMENU, 0}, {"HOME", DIK_HOME, VK_HOME}, {"UP", DIK_UP, VK_UP},
    {"PRIOR", DIK_PRIOR, VK_PRIOR}, {"PGUP", DIK_PRIOR, VK_PRIOR}, {"LEFT", DIK_LEFT, VK_LEFT},
    {"RIGHT", DIK_RIGHT, VK_RIGHT}, {"END", DIK_END, VK_END}, {"DOWN", DIK_DOWN, VK_DOWN},
    {"NEXT", DIK_NEXT, VK_NEXT}, {"PGDN", DIK_NEXT, VK_NEXT}, {"INSERT", DIK_INSERT, VK_INSERT},
    {"DELETE", DIK_DELETE, VK_DELETE},
};

const KeyName* FindKey(const std::string& name) {
    for (const KeyName& k : kKeys)
        if (name == k.name) return &k;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Actions: keymap lines (re/input.md §2) and the modern scheme's keys

struct Action {
    const char* name;
    int line;          // index into gKey_mapping
    const char* modern;  // the modern scheme's keys ("" = the game's own)
};
const Action kActions[] = {
    {"Accelerate", 49, "W,UP"},   {"Brake", 50, "S,DOWN"},       {"Left", 47, "A,LEFT"},
    {"Right", 48, "D,RIGHT"},     {"Handbrake", 54, "SPACE"},     {"Wheelspin", 56, ""},
    {"Repair", 45, "F"},          {"Recover", 60, "R"},           {"UsePowerup", 61, "E"},
    {"PowerupPrev", 63, ""},      {"PowerupNext", 64, "Q"},       {"PowerupList", 62, ""},
    {"Camera", 75, "C"},          {"Map", 57, ""},                {"MiniMap", 35, ""},
    {"Horn", 59, ""},             {"Replay", 58, "P"},            {"LookUp", 31, "I"},
    {"LookDown", 32, "K"},        {"LookLeft", 33, "J"},          {"LookRight", 34, "L"},
    {"HudDetail", 76, ""},        {"Chat", 71, ""},               {"TargetLock", 72, ""},
    {"CycleTarget", 73, ""},      {"BuyArmour", 67, ""},          {"BuyPower", 68, ""},
    {"BuyOffence", 69, ""},       {"NetPlayers", 70, ""},
};
constexpr int kAccelerate = 0, kBrake = 1, kLeft = 2, kRight = 3;  // indices into kActions

int* const kKeyMapping = (int*)0x74b5e0;  // gKey_mapping[77]: game key code per line
int* const kScanCode = (int*)0x6b34a0;    // gScan_code[151]: DIK code per game key code

struct Binding {
    int action;
    std::vector<BYTE> keys;
};
std::vector<Binding> g_bindings;
bool g_consumed[256];
bool g_menuArrows;  // modern scheme: driving keys move around the menus

std::vector<BYTE> ParseKeys(const std::string& list, const char* action) {
    std::vector<BYTE> keys;
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t end = list.find(',', pos);
        if (end == std::string::npos) end = list.size();
        std::string name;
        for (char c : list.substr(pos, end - pos))
            if (!isspace((unsigned char)c)) name += (char)toupper((unsigned char)c);
        if (!name.empty()) {
            if (name.rfind("DIK_", 0) == 0) name = name.substr(4);
            if (const KeyName* k = FindKey(name)) keys.push_back(k->dik);
            else Log("input: [Keys] %s: unknown key \"%s\"", action, name.c_str());
        }
        pos = end + 1;
    }
    return keys;
}

// The DIK code of the key the game currently has for an action, or 0.
BYTE TargetScan(int line) {
    int code = kKeyMapping[line];
    if (code < 0 || code > 150) return 0;
    int scan = kScanCode[code];
    return scan > 0 && scan < 256 ? (BYTE)scan : 0;
}

void Translate(BYTE* state) {
    if (g_bindings.empty() || frontend::InKeyCapture()) return;
    BYTE in[256];
    memcpy(in, state, 256);
    for (int i = 0; i < 256; i++)
        if (g_consumed[i]) state[i] = 0;
    if (frontend::InMenu()) {
        if (!g_menuArrows) return;
        static const BYTE arrows[4] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT};  // Accelerate, Brake, Left, Right
        for (const Binding& b : g_bindings)
            if (b.action <= kRight)
                for (BYTE k : b.keys)
                    if (in[k] & 0x80) state[arrows[b.action]] = 0x80;
        return;
    }
    static bool logged;
    if (!logged && kKeyMapping[kActions[kAccelerate].line]) {
        logged = true;
        Log("input: in race, Accelerate is game key %d = DIK %#x", kKeyMapping[kActions[kAccelerate].line],
            TargetScan(kActions[kAccelerate].line));
    }
    for (const Binding& b : g_bindings) {
        BYTE target = TargetScan(kActions[b.action].line);
        if (!target) continue;
        for (BYTE k : b.keys)
            if (in[k] & 0x80) state[target] = 0x80;
    }
}

// ---------------------------------------------------------------------------
// DirectInput hooks

HRESULT WINAPI h_SetCooperativeLevel(void* dev, HWND hwnd, DWORD flags) {
    DWORD use = flags;
    if (headless::Active()) use = DISCL_BACKGROUND | DISCL_NONEXCLUSIVE;  // never needs the foreground
    HRESULT hr = o_SetCooperativeLevel(dev, hwnd, use);
    Log("dinput: SetCooperativeLevel(%p, flags %#lx -> %#lx) = %#lx", dev, (unsigned long)flags,
        (unsigned long)use, (unsigned long)hr);
    return hr;
}

HRESULT WINAPI h_Acquire(void* dev) {
    HRESULT hr = o_Acquire(dev);
    if (headless::Active() && FAILED(hr)) return DI_OK;
    return hr;
}

HRESULT WINAPI h_GetDeviceState(void* dev, DWORD size, void* data) {
    HRESULT hr = o_GetDeviceState(dev, size, data);
    if (size != 256) return hr;  // not the keyboard
    if (headless::Active()) {    // test runs: the script only, never the real keyboard
        for (int i = 0; i < 256; i++) ((BYTE*)data)[i] = g_script[i];
        hr = DI_OK;
    }
    if (SUCCEEDED(hr)) Translate((BYTE*)data);
    return hr;
}

HRESULT WINAPI h_GetDeviceData(void* dev, DWORD size, DIDEVICEOBJECTDATA* data, DWORD* count, DWORD flags) {
    if (headless::Active()) {  // no buffered events from the real devices in tests
        if (count) *count = 0;
        return DI_OK;
    }
    return o_GetDeviceData(dev, size, data, count, flags);
}

HRESULT WINAPI h_CreateDevice(void* di, REFGUID guid, void** out, IUnknown* outer) {
    HRESULT hr = o_CreateDevice(di, guid, out, outer);
    bool keyboard = IsEqualGUID(guid, GUID_SysKeyboard);
    Log("dinput: CreateDevice(%s) = %#lx, device %p", keyboard ? "keyboard" : IsEqualGUID(guid, GUID_SysMouse) ? "mouse" : "other",
        (unsigned long)hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out && !o_GetDeviceState) {
        void* dev = *out;
        hook::Import(Slot(dev, kSetCooperativeLevel), (SetCooperativeLevel_t)&h_SetCooperativeLevel, &o_SetCooperativeLevel);
        hook::Import(Slot(dev, kAcquire), (Acquire_t)&h_Acquire, &o_Acquire);
        hook::Import(Slot(dev, kGetDeviceState), (GetDeviceState_t)&h_GetDeviceState, &o_GetDeviceState);
        hook::Import(Slot(dev, kGetDeviceData), (GetDeviceData_t)&h_GetDeviceData, &o_GetDeviceData);
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Test script

struct Step {
    DWORD ms;
    const KeyName* key;  // nullptr: a mouse step
    DWORD hold;
    int x, y;  // MOUSE=x;y moves the virtual cursor; CLICK presses the left button
    bool click;
};
std::vector<Step> g_steps;

DWORD WINAPI ScriptThread(void*) {
    DWORD start = GetTickCount();
    for (const Step& s : g_steps) {
        DWORD now = GetTickCount() - start;
        if (s.ms > now) Sleep(s.ms - now);
        HWND hwnd = headless::Window();
        if (!s.key) {
            if (s.click) {
                Log("script: click");
                if (hwnd) PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
                Sleep(s.hold);
                if (hwnd) PostMessageA(hwnd, WM_LBUTTONUP, 0, 0);
            } else {
                Log("script: mouse %d,%d", s.x, s.y);
                frontend::SetVirtualCursor(s.x, s.y);
            }
            continue;
        }
        LPARAM scan = (LPARAM)s.key->dik << 16;
        Log("script: %s (%lu ms)", s.key->name, s.hold);
        g_script[s.key->dik] = 0x80;
        if (hwnd && s.key->vk) PostMessageA(hwnd, WM_KEYDOWN, s.key->vk, scan | 1);
        Sleep(s.hold);
        g_script[s.key->dik] = 0;
        if (hwnd && s.key->vk) PostMessageA(hwnd, WM_KEYUP, s.key->vk, scan | 0xC0000001);
    }
    Log("script: done");
    return 0;
}

// ---------------------------------------------------------------------------
// The Controls screen refuses any key held by a hidden keymap line in 28..76
// (re/input.md §2): mostly unused leftovers (q, w, e, F1-F3, p, KP-) and the
// sound toggle on S. With the modern scheme, after every keymap load the
// unused ones get key code 80 ("KP =", which can never be pressed) and the
// sound toggle moves from S to F9, so W/S/Q/E can be bound there.

constexpr int kNeverPressed = 80, kKeyS = 39, kKeyF9 = 99, kSoundLine = 37;
const int kUnusedLines[] = {28, 29, 30, 36, 51, 52, 53, 55};
void* g_loadKeyMapping;

void LoadKeyMapping() {
    reinterpret_cast<void (*)()>(g_loadKeyMapping)();
    for (int line : kUnusedLines) kKeyMapping[line] = kNeverPressed;
    if (kKeyMapping[kSoundLine] == kKeyS) kKeyMapping[kSoundLine] = kKeyF9;
}

}  // namespace

void OnDirectInput(void* di) {
    if (!di || o_CreateDevice) return;
    hook::Import(Slot(di, kCreateDevice), (CreateDevice_t)&h_CreateDevice, &o_CreateDevice);
    Log("dinput: IDirectInputA %p hooked", di);
}

void LoadBindings() {
    std::string scheme = settings::Get("Controls", "Scheme", "classic");
    for (char& c : scheme) c = (char)tolower((unsigned char)c);
    bool modern = scheme == "modern";
    g_menuArrows = modern;
    int n = 0;
    for (int i = 0; i < (int)(sizeof kActions / sizeof kActions[0]); i++) {
        const Action& a = kActions[i];
        std::string keys = settings::Get("Keys", a.name, "\x01");  // \x01 = not set
        if (keys == "\x01") keys = modern ? a.modern : "";
        Binding b{i, ParseKeys(keys, a.name)};
        if (b.keys.empty()) continue;
        for (BYTE k : b.keys) g_consumed[k] = true;
        g_bindings.push_back(b);
        n++;
    }
    Log("input: %s scheme, %d actions with their own keys", modern ? "modern" : "classic", n);
    if (modern) {
        static const uint8_t start[6] = {0x81, 0xec, 0x00, 0x01, 0x00, 0x00};  // sub esp, 0x100
        g_loadKeyMapping = hook::Detour(0x487e10, start, 6, (const void*)&LoadKeyMapping);
        Log("input: unused keymap lines freed, sound toggle on F9: %s", g_loadKeyMapping ? "ok" : "FAILED");
    }
}

void StartScript(const char* script) {
    // "2.5:RETURN,4:W/2000": key at 2.5 s; an optional /ms holds it (default 150).
    std::string s = script;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        std::string item = s.substr(pos, end - pos);
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
            DWORD ms = (DWORD)(atof(item.c_str()) * 1000);
            std::string name = item.substr(colon + 1);
            DWORD hold = 150;
            size_t slash = name.find('/');
            if (slash != std::string::npos) {
                hold = (DWORD)atoi(name.c_str() + slash + 1);
                name = name.substr(0, slash);
            }
            int mx, my;
            if (sscanf(name.c_str(), "MOUSE=%d;%d", &mx, &my) == 2) g_steps.push_back({ms, nullptr, 0, mx, my, false});
            else if (name == "CLICK") g_steps.push_back({ms, nullptr, hold, 0, 0, true});
            else if (const KeyName* k = FindKey(name)) g_steps.push_back({ms, k, hold, 0, 0, false});
            else Log("script: unknown key %s", name.c_str());
        }
        pos = end + 1;
    }
    Log("script: %u steps", (unsigned)g_steps.size());
    if (!g_steps.empty()) CloseHandle(CreateThread(nullptr, 0, &ScriptThread, nullptr, 0, nullptr));
}

}  // namespace input
