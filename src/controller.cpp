// [Controller] XInput pads (re/controller.md, recipe A).
//
// The game's own joystick code (DirectInput 5, analog steering on lX, one
// throttle/brake axis on lY) is fed a virtual pad: its EnumDevices sees only
// "Carma2 HD Gamepad", and CreateDevice for that hands out our object, whose
// GetDeviceState fills a DIJOYSTATE from XInput. The left stick steers and the
// right trigger accelerates (analog, the game's own feel); the left trigger
// presses the game's Brake key, so braking ramps and reverse engages as on the
// keyboard. Buttons are pressed as the game's keys for their actions; in menus
// the D-pad/left stick, A and B/Back become the arrows, Return and Esc.
//
// The retail game ships with the joystick disabled and no way to enable it,
// so it is switched on after LoadJoystickConfig 0x45c0b0.
#define DIRECTINPUT_VERSION 0x0500
#include "controller.h"

#include <dinput.h>
#include <windows.h>
#include <xinput.h>

#include <cstring>

#include "frontend.h"
#include "headless.h"
#include "hook.h"
#include "log.h"
#include "settings.h"

namespace controller {
namespace {

// {C2D0E6A1-4B1D-4E5F-9A61-3C2A5D9E0B11}, ours (instance and product)
const GUID kPadGuid = {0xc2d0e6a1, 0x4b1d, 0x4e5f, {0x9a, 0x61, 0x3c, 0x2a, 0x5d, 0x9e, 0x0b, 0x11}};
constexpr DWORD kPadType = DIDEVTYPE_JOYSTICK | (DIDEVTYPEJOYSTICK_GAMEPAD << 8);

bool g_enabled;
typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
XInputGetState_t g_xinputGetState;

XINPUT_GAMEPAD g_pad;  // the last read, cleared when disconnected
volatile LONG g_testButtons;  // test runs: a scripted pad (always connected) instead of XInput
DWORD g_lastRead, g_lastMissing;
bool g_connected;
HWND* const kWindow = (HWND*)0x6ad4c8;

void ReadPad() {
    DWORD now = GetTickCount();
    if (now - g_lastRead < 3) return;
    g_lastRead = now;
    if (!g_connected && now - g_lastMissing < 1000) return;  // a missing pad is asked about once a second
    XINPUT_STATE st = {};
    bool ok = g_xinputGetState && g_xinputGetState(0, &st) == ERROR_SUCCESS;
    if (headless::Active()) {  // never a real pad in a test
        st = {};
        st.Gamepad.wButtons = (WORD)g_testButtons;
        ok = true;
    }
    // The game reads the joystick even in the background: only the focused game drives.
    if (ok && !headless::Active() && GetForegroundWindow() != *kWindow) ok = false, st = {};
    if (ok != g_connected) Log("controller: pad %s", ok ? "connected" : "not connected");
    g_connected = ok;
    if (!ok) g_lastMissing = now;
    g_pad = ok ? st.Gamepad : XINPUT_GAMEPAD{};
}

LONG Stick(SHORT v) {  // radial-ish deadzone, rescaled to the full range
    const int dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    int m = v < 0 ? -v : v;
    if (m <= dz) return 32768;
    long s = (long)(m - dz) * 32767 / (32767 - dz);
    if (s > 32767) s = 32767;
    return 32768 + (v < 0 ? -s : s);
}

// ---------------------------------------------------------------------------
// The virtual device: one static IDirectInputDevice2A

struct Pad;
LONG g_refs = 1;
int g_acquires;

HRESULT WINAPI QueryInterface(Pad* self, REFIID iid, void** out) {
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IDirectInputDeviceA) ||
        IsEqualIID(iid, IID_IDirectInputDevice2A)) {
        *out = self;
        InterlockedIncrement(&g_refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
ULONG WINAPI AddRef(Pad*) { return InterlockedIncrement(&g_refs); }
ULONG WINAPI Release(Pad*) { return InterlockedDecrement(&g_refs) > 0 ? g_refs : 0; }  // static: never freed

HRESULT WINAPI GetCapabilities(Pad*, DIDEVCAPS* caps) {
    if (!caps) return E_POINTER;
    DWORD size = caps->dwSize;
    memset(caps, 0, size < sizeof *caps ? size : sizeof *caps);
    caps->dwSize = size;
    caps->dwFlags = DIDC_ATTACHED;
    caps->dwDevType = kPadType;
    caps->dwAxes = 2;
    caps->dwButtons = 16;
    return DI_OK;
}

HRESULT WINAPI Ok(Pad*) { return DI_OK; }
HRESULT WINAPI SetProperty(Pad*, REFGUID, const DIPROPHEADER*) { return DI_OK; }
HRESULT WINAPI GetProperty(Pad*, REFGUID, DIPROPHEADER*) { return DIERR_UNSUPPORTED; }
HRESULT WINAPI Acquire(Pad*) { return g_acquires++ ? DI_NOEFFECT : DI_OK; }
HRESULT WINAPI SetCooperativeLevel(Pad*, HWND, DWORD) { return DI_OK; }
HRESULT WINAPI SetDataFormat(Pad*, const DIDATAFORMAT* f) {
    return f && f->dwDataSize == sizeof(DIJOYSTATE) ? DI_OK : DIERR_INVALIDPARAM;
}

HRESULT WINAPI GetDeviceState(Pad*, DWORD size, void* data) {
    if (size != sizeof(DIJOYSTATE) || !data) return DIERR_INVALIDPARAM;
    ReadPad();
    DIJOYSTATE* js = (DIJOYSTATE*)data;
    memset(js, 0, sizeof *js);
    js->lX = Stick(g_pad.sThumbLX);
    // lY: up = accelerate. Only the right trigger (the left one is the Brake key).
    int rt = g_pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? g_pad.bRightTrigger : 0;
    js->lY = 32768 - rt * 32768 / 255;
    js->lZ = js->lRx = js->lRy = js->lRz = 32768;
    for (DWORD& pov : js->rgdwPOV) pov = 0xffffffff;
    return DI_OK;
}

HRESULT WINAPI GetDeviceData(Pad*, DWORD, DIDEVICEOBJECTDATA*, DWORD* count, DWORD) {
    if (count) *count = 0;
    return DI_OK;
}

HRESULT WINAPI GetObjectInfo(Pad*, DIDEVICEOBJECTINSTANCEA* info, DWORD obj, DWORD how) {
    if (how != DIPH_BYOFFSET || !info) return DIERR_OBJECTNOTFOUND;
    bool axis = obj == DIJOFS_X || obj == DIJOFS_Y;
    bool button = obj >= DIJOFS_BUTTON0 && obj < DIJOFS_BUTTON0 + 16;
    if (!axis && !button) return DIERR_OBJECTNOTFOUND;
    DWORD size = info->dwSize;
    memset(info, 0, size < sizeof *info ? size : sizeof *info);
    info->dwSize = size;
    info->guidType = axis ? (obj == DIJOFS_X ? GUID_XAxis : GUID_YAxis) : GUID_Button;
    info->dwOfs = obj;
    info->dwType = axis ? (DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(obj / 4)) : (DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(obj - DIJOFS_BUTTON0));
    lstrcpynA(info->tszName, axis ? (obj == DIJOFS_X ? "X axis" : "Y axis") : "Button", sizeof info->tszName);
    return DI_OK;
}

HRESULT WINAPI NotImpl(Pad*) { return E_NOTIMPL; }
HRESULT WINAPI Poll(Pad*) { return DI_OK; }  // exactly 0: the game ignores the read otherwise

typedef void* Slot;
Slot g_vtable[27] = {
    (Slot)&QueryInterface, (Slot)&AddRef, (Slot)&Release, (Slot)&GetCapabilities,
    (Slot)&NotImpl /* EnumObjects */, (Slot)&GetProperty, (Slot)&SetProperty, (Slot)&Acquire,
    (Slot)&Ok /* Unacquire */, (Slot)&GetDeviceState, (Slot)&GetDeviceData, (Slot)&SetDataFormat,
    (Slot)&Ok /* SetEventNotification */, (Slot)&SetCooperativeLevel, (Slot)&GetObjectInfo,
    (Slot)&NotImpl /* GetDeviceInfo */, (Slot)&NotImpl /* RunControlPanel */, (Slot)&Ok /* Initialize */,
    (Slot)&NotImpl /* CreateEffect */, (Slot)&NotImpl /* EnumEffects */, (Slot)&NotImpl /* GetEffectInfo */,
    (Slot)&NotImpl /* GetForceFeedbackState */, (Slot)&NotImpl /* SendForceFeedbackCommand */,
    (Slot)&NotImpl /* EnumCreatedEffectObjects */, (Slot)&NotImpl /* Escape */, (Slot)&Poll,
    (Slot)&NotImpl /* SendDeviceData */,
};
struct Pad {
    Slot* vtable;
} g_padObject = {g_vtable};

// ---------------------------------------------------------------------------
// Switched on after the game's joystick config load (it ships disabled)

void* g_loadJoystickConfig;
int* const kJoystickIndex = (int*)0x595f88;

void LoadJoystickConfig() {
    reinterpret_cast<void (*)()>(g_loadJoystickConfig)();
    if (*kJoystickIndex != -1) return;
    reinterpret_cast<void (*)()>(0x45a070)();  // EnableJoystick
    uintptr_t cfg = reinterpret_cast<uintptr_t (*)()>(0x459f80)();  // GetJoyConfig
    if (cfg) *(int*)(cfg + 0xe8) = 1;
    Log("controller: joystick enabled (index %d)", *kJoystickIndex);
}

// ---------------------------------------------------------------------------
// Buttons as keys

struct ButtonAction {
    WORD button;
    int line;  // keymap line (re/input.md §2)
};
const ButtonAction kRaceButtons[] = {
    {XINPUT_GAMEPAD_A, 54},           // handbrake
    {XINPUT_GAMEPAD_B, 60},           // recover
    {XINPUT_GAMEPAD_X, 61},           // use powerup
    {XINPUT_GAMEPAD_Y, 75},           // camera
    {XINPUT_GAMEPAD_LEFT_SHOULDER, 63},   // previous powerup
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, 64},  // next powerup
    {XINPUT_GAMEPAD_START, 0},        // pause menu (Esc)
    {XINPUT_GAMEPAD_BACK, 57},        // map
    {XINPUT_GAMEPAD_LEFT_THUMB, 59},  // horn
    {XINPUT_GAMEPAD_RIGHT_THUMB, 45}, // repair
};
constexpr int kBrakeLine = 50;

}  // namespace

bool OnEnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKA cb, void* ref) {
    if (!g_enabled || (type != DIDEVTYPE_JOYSTICK && type != 0)) return false;
    DIDEVICEINSTANCEA inst = {};
    inst.dwSize = sizeof inst;
    inst.guidInstance = inst.guidProduct = kPadGuid;
    inst.dwDevType = kPadType;
    lstrcpynA(inst.tszInstanceName, "Carma2 HD Gamepad", sizeof inst.tszInstanceName);
    lstrcpynA(inst.tszProductName, "Carma2 HD Gamepad", sizeof inst.tszProductName);
    cb(&inst, ref);
    Log("controller: virtual pad enumerated");
    return true;
}

void* CreateDevice(REFGUID guid) {
    if (!g_enabled || !IsEqualGUID(guid, kPadGuid)) return nullptr;
    InterlockedIncrement(&g_refs);
    Log("controller: virtual pad created");
    return &g_padObject;
}

void InjectKeys(BYTE* state, BYTE (*targetScan)(int line)) {
    if (!g_enabled || frontend::InKeyCapture()) return;
    ReadPad();
    if (!g_connected) return;
    WORD b = g_pad.wButtons;
    if (frontend::InMenu()) {
        SHORT lx = g_pad.sThumbLX, ly = g_pad.sThumbLY;
        if ((b & XINPUT_GAMEPAD_DPAD_UP) || ly > 16000) state[DIK_UP] = 0x80;
        if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -16000) state[DIK_DOWN] = 0x80;
        if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -16000) state[DIK_LEFT] = 0x80;
        if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 16000) state[DIK_RIGHT] = 0x80;
        if (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) state[DIK_RETURN] = 0x80;
        if (b & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_BACK)) state[DIK_ESCAPE] = 0x80;
        return;
    }
    for (const ButtonAction& a : kRaceButtons)
        if (b & a.button)
            if (BYTE scan = targetScan(a.line)) state[scan] = 0x80;
    if (g_pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        if (BYTE scan = targetScan(kBrakeLine)) state[scan] = 0x80;
}

void SetTestButtons(WORD buttons) { g_testButtons = buttons; }

void Install() {
    g_enabled = settings::Get("Controller", "Enabled", "1") != "0";
    if (!g_enabled) {
        Log("controller: off");
        return;
    }
    for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"})
        if (HMODULE m = LoadLibraryA(dll)) {
            g_xinputGetState = (XInputGetState_t)GetProcAddress(m, "XInputGetState");
            if (g_xinputGetState) break;
        }
    if (!g_xinputGetState) {
        g_enabled = false;
        Log("controller: no XInput on this system");
        return;
    }
    static const uint8_t start[6] = {0x81, 0xec, 0x00, 0x01, 0x00, 0x00};  // sub esp, 0x100
    g_loadJoystickConfig = hook::Detour(0x45c0b0, start, 6, (const void*)&LoadJoystickConfig);
    Log("controller: XInput pad support on (%s)", g_loadJoystickConfig ? "ok" : "enable hook FAILED");
}

}  // namespace controller
