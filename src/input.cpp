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
#include <xinput.h>
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "controller.h"
#include "keysets.h"
#include "frontend.h"
#include "headless.h"
#include "hook.h"
#include "log.h"
#include "settings.h"

namespace input {
namespace {

// IDirectInputA / IDirectInputDeviceA vtable slots
enum { kCreateDevice = 3, kEnumDevices = 4 };
enum { kAcquire = 7, kGetDeviceState = 9, kGetDeviceData = 10, kSetCooperativeLevel = 13 };

typedef HRESULT(WINAPI* CreateDevice_t)(void*, REFGUID, void**, IUnknown*);
typedef HRESULT(WINAPI* EnumDevices_t)(void*, DWORD, LPDIENUMDEVICESCALLBACKA, void*, DWORD);
typedef HRESULT(WINAPI* GetDeviceState_t)(void*, DWORD, void*);
typedef HRESULT(WINAPI* GetDeviceData_t)(void*, DWORD, DIDEVICEOBJECTDATA*, DWORD*, DWORD);
typedef HRESULT(WINAPI* SetCooperativeLevel_t)(void*, HWND, DWORD);
typedef HRESULT(WINAPI* Acquire_t)(void*);

CreateDevice_t o_CreateDevice;
EnumDevices_t o_EnumDevices;
GetDeviceState_t o_GetDeviceState;
GetDeviceData_t o_GetDeviceData;
SetCooperativeLevel_t o_SetCooperativeLevel;
Acquire_t o_Acquire;

volatile BYTE g_script[256];  // scripted key state (0x80 = down), test runs only
volatile int g_givePowerup = -1;  // POWERUP=n: given on the game thread's next keyboard read

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
// Actions: keymap lines (re/input.md §2) and the modern scheme's extra keys.
// The modern scheme is key set E (keysets.cpp), whose own keys are W/A/S/D
// and the rest; here it only adds the arrows to the driving actions (on
// whatever keys set E has for them, so remapping on the Controls screen
// keeps them working).

struct Action {
    const char* name;
    int line;          // index into gKey_mapping
    const char* modern;  // the modern scheme's extra keys ("" = none)
};
const Action kActions[] = {
    {"Accelerate", 49, "UP"},     {"Brake", 50, "DOWN"},         {"Left", 47, "LEFT"},
    {"Right", 48, "RIGHT"},       {"Handbrake", 54, ""},          {"Wheelspin", 56, ""},
    {"Repair", 45, ""},           {"Recover", 60, ""},            {"UsePowerup", 61, ""},
    {"PowerupPrev", 63, ""},      {"PowerupNext", 64, ""},        {"PowerupList", 62, ""},
    {"Camera", 75, ""},           {"Map", 57, ""},                {"MiniMap", 35, ""},
    {"Horn", 59, ""},             {"Replay", 58, ""},             {"LookUp", 31, ""},
    {"LookDown", 32, ""},         {"LookLeft", 33, ""},           {"LookRight", 34, ""},
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

// Set while a menu text field (driver name, join address) takes typing: the
// characters come from WM_CHAR, so the keys pass through untranslated.
const volatile int* const kMenuTyping = (const volatile int*)0x68b858;

void Translate(BYTE* state) {
    if ((g_bindings.empty() && !g_menuArrows) || frontend::InKeyCapture() || *kMenuTyping) return;
    BYTE in[256];
    memcpy(in, state, 256);
    for (int i = 0; i < 256; i++)
        if (g_consumed[i]) state[i] = 0;
    if (frontend::InMenu()) {
        if (!g_menuArrows) return;
        // The driving keys (the set's own, and any extra) move around the menus.
        static const BYTE arrows[4] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT};  // Accelerate, Brake, Left, Right
        for (int a = kAccelerate; a <= kRight; a++)
            if (BYTE own = TargetScan(kActions[a].line))
                if (in[own] & 0x80) state[arrows[a]] = 0x80;
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
        if (g_givePowerup >= 0 && *(uintptr_t*)0x762438) {  // the race's info, 0 outside a race
            // GotPowerup 0x4d8d40 (ecx = car, edx = index, two stack args it pops),
            // as its developer key 0x4d8d30 calls it; 0x75bc2c is the player's car.
            int n = g_givePowerup;
            g_givePowerup = -1;
            Log("script: power-up %d", n);
            ((int(__fastcall*)(uintptr_t, int, int, int))0x4d8d40)(0x75bc2c, n, 1, 1);
        }
    }
    if (SUCCEEDED(hr)) {
        Translate((BYTE*)data);
        controller::InjectKeys((BYTE*)data, &TargetScan);
    }
    return hr;
}

HRESULT WINAPI h_GetDeviceData(void* dev, DWORD size, DIDEVICEOBJECTDATA* data, DWORD* count, DWORD flags) {
    if (headless::Active()) {  // no buffered events from the real devices in tests
        if (count) *count = 0;
        return DI_OK;
    }
    return o_GetDeviceData(dev, size, data, count, flags);
}

HRESULT WINAPI h_EnumDevices(void* di, DWORD type, LPDIENUMDEVICESCALLBACKA cb, void* ref, DWORD flags) {
    if (controller::OnEnumDevices(type, cb, ref)) return DI_OK;
    return o_EnumDevices(di, type, cb, ref, flags);
}

HRESULT WINAPI h_CreateDevice(void* di, REFGUID guid, void** out, IUnknown* outer) {
    if (void* pad = controller::CreateDevice(guid)) {
        *out = pad;
        return DI_OK;
    }
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
    DWORD ms = 0;
    const KeyName* key = nullptr;  // nullptr: not a key step
    DWORD hold = 0;
    int x = 0, y = 0;     // MOUSE=x;y moves the cursor; CLICK presses the left button
    bool click = false;
    std::string text;     // TEXT=chars types them (WM_CHAR, as a keyboard would)
    bool client = false;  // PMOUSE=x;y: x,y are client pixels, not menu space
    bool sweep = false;   // SWEEP hovers every item of the current screen
    UINT message = 0;     // ALTF4 / CLOSE post that window message
    WORD pad = 0;         // PAD=UP|DOWN|LEFT|RIGHT|A|B|X|Y|START|BACK|LB|RB holds that pad button
    const KeyName* sweepKey = nullptr;  // KEYSWEEP[=key]: press it round the screen (default DOWN)
    std::string goTo;     // GOTO=name: press Down until that item has the focus
    DWORD wait = 0;       // WAIT=s: pause (later steps' times count from the start, so a
                          // tour with long steps can use small times and WAITs)
    int powerup = -1;     // POWERUP=n: give the player power-up n (in a race)
};

// A scripted key press as a person makes it: down for a few menu frames, then
// up for a few (the menus act on the press, at 48 fps).
void Press(BYTE dik) {
    g_script[dik] = 0x80;
    Sleep(120);
    g_script[dik] = 0;
    Sleep(150);
}
std::vector<Step> g_steps;

DWORD WINAPI ScriptThread(void*) {
    DWORD start = GetTickCount();
    for (const Step& s : g_steps) {
        DWORD now = GetTickCount() - start;
        if (s.ms > now) Sleep(s.ms - now);
        HWND hwnd = headless::Window();
        if (!s.key) {
            if (s.wait) {
                Sleep(s.wait);
            } else if (s.powerup >= 0) {
                g_givePowerup = s.powerup;
            } else if (s.sweepKey) {
                frontend::KeySweep(&Press, s.sweepKey->dik, s.sweepKey->name);
            } else if (!s.goTo.empty()) {
                frontend::GoTo(&Press, s.goTo.c_str());
            } else if (s.pad) {
                Log("script: pad %#x (%lu ms)", s.pad, s.hold);
                controller::SetTestButtons(s.pad);
                Sleep(s.hold);
                controller::SetTestButtons(0);
            } else if (s.message) {
                Log("script: post %s", s.message == WM_CLOSE ? "WM_CLOSE" : "Alt+F4");
                if (hwnd && s.message == WM_CLOSE) PostMessageA(hwnd, WM_CLOSE, 0, 0);
                else if (hwnd) PostMessageA(hwnd, WM_SYSKEYDOWN, VK_F4, (0x3e << 16) | (1 << 29) | 1);
            } else if (s.sweep) {
                frontend::Sweep();
            } else if (!s.text.empty()) {
                Log("script: type \"%s\"", s.text.c_str());
                for (char c : s.text) {
                    if (hwnd) PostMessageA(hwnd, WM_CHAR, (BYTE)c, 1);
                    Sleep(60);
                }
            } else if (s.click) {
                Log("script: click");
                if (hwnd) PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
                Sleep(s.hold);
                if (hwnd) PostMessageA(hwnd, WM_LBUTTONUP, 0, 0);
            } else {
                Log("script: mouse %d,%d%s", s.x, s.y, s.client ? " (client)" : "");
                if (s.client) frontend::SetTestCursor(s.x, s.y);
                else frontend::SetVirtualCursor(s.x, s.y);
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

}  // namespace

void OnDirectInput(void* di) {
    if (!di || o_CreateDevice) return;
    hook::Import(Slot(di, kCreateDevice), (CreateDevice_t)&h_CreateDevice, &o_CreateDevice);
    hook::Import(Slot(di, kEnumDevices), (EnumDevices_t)&h_EnumDevices, &o_EnumDevices);
    Log("dinput: IDirectInputA %p hooked", di);
}

void LoadBindings() {
    bool modern = keysets::Modern();  // key set E
    g_menuArrows = modern;
    g_bindings.clear();  // may be called again when the HD tab changes the scheme
    memset(g_consumed, 0, sizeof g_consumed);
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
    Log("input: %s scheme, %d actions with extra keys", modern ? "modern (key set E)" : "classic", n);
}

void StartScript(const char* script) {
    // "2.5:RETURN,4:W/2000": key at 2.5 s; an optional /ms holds it (default 150).
    // "6:TEXT=10.0.0.5:47700" types the text after the time's colon.
    // MOUSE=x;y (menu space) and PMOUSE=x;y (client pixels) move the cursor
    // through the game's mouse path; CLICK clicks; SWEEP hovers every item.
    // ALTF4 and CLOSE post Alt+F4 and WM_CLOSE to the game's window.
    // PAD=DOWN/ms holds a button of a scripted pad (test runs never read a real one).
    // KEYSWEEP[=S] presses Down (or S) round the screen and logs what it reaches;
    // GOTO=Settings presses Down until that item has the focus.
    // POWERUP=n gives the player power-up n (a line of POWERUP.TXT, from 0).
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
            if (slash != std::string::npos && name.rfind("TEXT=", 0) != 0) {
                hold = (DWORD)atoi(name.c_str() + slash + 1);
                name = name.substr(0, slash);
            }
            Step st;
            st.ms = ms;
            st.hold = hold;
            if (name.rfind("TEXT=", 0) == 0) st.text = name.substr(5);
            else if (sscanf(name.c_str(), "MOUSE=%d;%d", &st.x, &st.y) == 2) {}
            else if (sscanf(name.c_str(), "PMOUSE=%d;%d", &st.x, &st.y) == 2) st.client = true;
            else if (name == "SWEEP") st.sweep = true;
            else if (name == "ALTF4") st.message = WM_SYSKEYDOWN;
            else if (name == "CLOSE") st.message = WM_CLOSE;
            else if (name == "CLICK") st.click = true;
            else if (name == "KEYSWEEP") st.sweepKey = FindKey("DOWN");
            else if (name.rfind("KEYSWEEP=", 0) == 0) {
                if (!(st.sweepKey = FindKey(name.substr(9)))) {
                    Log("script: unknown key %s", name.c_str());
                    pos = end + 1;
                    continue;
                }
            } else if (name.rfind("GOTO=", 0) == 0) st.goTo = name.substr(5);
            else if (name.rfind("WAIT=", 0) == 0) st.wait = (DWORD)(atof(name.c_str() + 5) * 1000);
            else if (name.rfind("POWERUP=", 0) == 0) st.powerup = atoi(name.c_str() + 8);
            else if (name.rfind("PAD=", 0) == 0) {
                static const struct {
                    const char* name;
                    WORD bit;
                } kButtons[] = {{"UP", XINPUT_GAMEPAD_DPAD_UP},    {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
                                {"LEFT", XINPUT_GAMEPAD_DPAD_LEFT}, {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
                                {"A", XINPUT_GAMEPAD_A},            {"B", XINPUT_GAMEPAD_B},
                                {"X", XINPUT_GAMEPAD_X},            {"Y", XINPUT_GAMEPAD_Y},
                                {"START", XINPUT_GAMEPAD_START},    {"BACK", XINPUT_GAMEPAD_BACK},
                                {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}};
                for (const auto& b : kButtons)
                    if (name.substr(4) == b.name) st.pad = b.bit;
                if (!st.pad) {
                    Log("script: unknown pad button %s", name.c_str());
                    pos = end + 1;
                    continue;
                }
            }
            else if (!(st.key = FindKey(name))) {
                Log("script: unknown key %s", name.c_str());
                pos = end + 1;
                continue;
            }
            g_steps.push_back(st);
        }
        pos = end + 1;
    }
    Log("script: %u steps", (unsigned)g_steps.size());
    if (!g_steps.empty()) CloseHandle(CreateThread(nullptr, 0, &ScriptThread, nullptr, 0, nullptr));
}

}  // namespace input
