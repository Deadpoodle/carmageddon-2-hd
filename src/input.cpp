// DirectInput keyboard hooks. The proxy hands us the game's IDirectInputA; its
// CreateDevice slot is swapped so every device the game makes gets these
// slots swapped too (devices of one kind share a vtable, so each slot is
// swapped once).
#define DIRECTINPUT_VERSION 0x0500
#include "input.h"

#include <dinput.h>
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "headless.h"
#include "hook.h"
#include "log.h"

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
    if (headless::Active() && size == 256) {  // the keyboard: script only
        for (int i = 0; i < 256; i++) ((BYTE*)data)[i] = g_script[i];
        return DI_OK;
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

struct Key {
    const char* name;
    BYTE dik;
    UINT vk;
};
const Key kKeys[] = {
    {"ESCAPE", DIK_ESCAPE, VK_ESCAPE}, {"RETURN", DIK_RETURN, VK_RETURN}, {"SPACE", DIK_SPACE, VK_SPACE},
    {"UP", DIK_UP, VK_UP},             {"DOWN", DIK_DOWN, VK_DOWN},       {"LEFT", DIK_LEFT, VK_LEFT},
    {"RIGHT", DIK_RIGHT, VK_RIGHT},    {"TAB", DIK_TAB, VK_TAB},          {"BACK", DIK_BACK, VK_BACK},
    {"F1", DIK_F1, VK_F1},             {"F2", DIK_F2, VK_F2},             {"F3", DIK_F3, VK_F3},
    {"F4", DIK_F4, VK_F4},             {"F5", DIK_F5, VK_F5},             {"F6", DIK_F6, VK_F6},
    {"F7", DIK_F7, VK_F7},             {"F8", DIK_F8, VK_F8},             {"LSHIFT", DIK_LSHIFT, VK_SHIFT},
    {"LCONTROL", DIK_LCONTROL, VK_CONTROL}, {"A", DIK_A, 'A'}, {"S", DIK_S, 'S'}, {"D", DIK_D, 'D'},
    {"W", DIK_W, 'W'}, {"Q", DIK_Q, 'Q'}, {"Y", DIK_Y, 'Y'}, {"N", DIK_N, 'N'},
};

struct Step {
    DWORD ms;
    const Key* key;
};
std::vector<Step> g_steps;

DWORD WINAPI ScriptThread(void*) {
    DWORD start = GetTickCount();
    for (const Step& s : g_steps) {
        DWORD now = GetTickCount() - start;
        if (s.ms > now) Sleep(s.ms - now);
        HWND hwnd = headless::Window();
        LPARAM scan = (LPARAM)s.key->dik << 16;
        Log("script: %s", s.key->name);
        g_script[s.key->dik] = 0x80;
        if (hwnd) PostMessageA(hwnd, WM_KEYDOWN, s.key->vk, scan | 1);
        Sleep(150);
        g_script[s.key->dik] = 0;
        if (hwnd) PostMessageA(hwnd, WM_KEYUP, s.key->vk, scan | 0xC0000001);
    }
    Log("script: done");
    return 0;
}

}  // namespace

void OnDirectInput(void* di) {
    if (!di || o_CreateDevice) return;
    hook::Import(Slot(di, kCreateDevice), (CreateDevice_t)&h_CreateDevice, &o_CreateDevice);
    Log("dinput: IDirectInputA %p hooked", di);
}

void StartScript(const char* script) {
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
            for (const Key& k : kKeys)
                if (name == k.name) g_steps.push_back({ms, &k});
        }
        pos = end + 1;
    }
    Log("script: %u steps", (unsigned)g_steps.size());
    if (!g_steps.empty()) CloseHandle(CreateThread(nullptr, 0, &ScriptThread, nullptr, 0, nullptr));
}

}  // namespace input
