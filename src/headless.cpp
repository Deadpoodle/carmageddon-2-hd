// C2HD_HEADLESS=1: for automated test runs on a PC someone is using.
//
// The game runs through dgVoodoo2 in windowed mode, in a window that is
// created off-screen, can't be activated or brought to the front, and believes
// it has the focus. Its audio session is muted (the sound code still runs).
// Message boxes are logged and answered. The single-instance check is
// skipped, so tests run beside a copy being played.
//
// Safety: the game's own window is always a full-screen-style popup; full
// screen comes from the renderer. So the run ends before any renderer that
// could change the display mode loads: Glide (nGlide is full screen), a
// DirectDraw that isn't dgVoodoo2's in the game folder, or a dgVoodoo.conf
// that doesn't say FullScreenMode = false.
#include "headless.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <mmsystem.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "hook.h"
#include "loader.h"
#include "log.h"

#include <algorithm>

namespace headless {
namespace {

bool g_active;
bool g_visible;  // C2HD_HEADLESS=2: an on-screen test (scripted, but the window is left alone)
HWND g_hwnd;
constexpr int kOff = -32000;
const char* kWindowClass = "Carma2MainWndClass";

decltype(&CreateWindowExA) o_CreateWindowExA;
decltype(&SetWindowPos) o_SetWindowPos;
decltype(&MoveWindow) o_MoveWindow;
decltype(&SetForegroundWindow) o_SetForegroundWindow;
decltype(&GetForegroundWindow) o_GetForegroundWindow;
decltype(&GetActiveWindow) o_GetActiveWindow;
decltype(&SetFocus) o_SetFocus;
decltype(&FindWindowA) o_FindWindowA;
decltype(&MessageBoxA) o_MessageBoxA;
WNDPROC o_WndProc;

[[noreturn]] void Abort(const char* why) {
    Log("headless: %s; ending the run so the player's display is never touched", why);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) {}
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Whatever moves or shows the window, it stays off-screen and inactive.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_WINDOWPOSCHANGING) {
        auto* wpos = (WINDOWPOS*)lp;
        wpos->x = kOff;
        wpos->y = kOff;
        wpos->flags = (wpos->flags & ~SWP_NOMOVE) | SWP_NOACTIVATE | SWP_NOZORDER;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return CallWindowProcA(o_WndProc, hwnd, msg, wp, lp);
}

// The game exits at start-up if a window of its class exists (FindWindowA at
// 0x51ad11), so a test window under the stock class would make the player's
// own launch quit. Tests register and use a class of their own.
const char* kTestClass = "Carma2MainWndClass_C2HDTest";
decltype(&RegisterClassA) o_RegisterClassA;
decltype(&SendMessageA) o_SendMessageA;

const char* TestClass(LPCSTR cls) { return HIWORD(cls) && strcmp(cls, kWindowClass) == 0 ? kTestClass : cls; }

ATOM WINAPI h_RegisterClassA(const WNDCLASSA* wc) {
    WNDCLASSA copy = *wc;
    copy.lpszClassName = TestClass(wc->lpszClassName);
    return o_RegisterClassA(&copy);
}

// Shutdown broadcasts WM_SHOWWINDOW to every top-level window (0x51c1e4).
LRESULT WINAPI h_SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (hwnd == HWND_BROADCAST) {
        Log("headless: dropped a broadcast of message %#x", msg);
        return 0;
    }
    return o_SendMessageA(hwnd, msg, wp, lp);
}

HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent,
                              HMENU menu, HINSTANCE inst, LPVOID param) {
    bool game = !parent && HIWORD(cls) && strcmp(cls, kWindowClass) == 0;
    cls = TestClass(cls);
    if (g_visible) {
        HWND hwnd = o_CreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
        if (hwnd && game) g_hwnd = hwnd;
        Log("headless (on screen): window %p", (void*)hwnd);
        return hwnd;
    }
    HWND hwnd = o_CreateWindowExA(ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, name, style & ~WS_VISIBLE, kOff, kOff,
                                  w, h, parent, menu, inst, param);
    if (hwnd && game) {
        g_hwnd = hwnd;
        o_WndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&WndProc);
    }
    Log("headless: window %p \"%s\" %dx%d style %#lx, hidden off-screen", (void*)hwnd, HIWORD(cls) ? cls : "(atom)",
        w, h, (unsigned long)style);
    return hwnd;
}

BOOL WINAPI h_SetWindowPos(HWND hwnd, HWND after, int, int, int w, int h, UINT flags) {
    flags = (flags & ~SWP_SHOWWINDOW) | SWP_NOACTIVATE | SWP_NOZORDER;
    return o_SetWindowPos(hwnd, after, kOff, kOff, w, h, flags);
}

BOOL WINAPI h_MoveWindow(HWND hwnd, int, int, int w, int h, BOOL repaint) {
    return o_MoveWindow(hwnd, kOff, kOff, w, h, repaint);
}

BOOL WINAPI h_SetForegroundWindow(HWND) { return TRUE; }
HWND WINAPI h_GetForegroundWindow() { return g_hwnd ? g_hwnd : o_GetForegroundWindow(); }
HWND WINAPI h_GetActiveWindow() { return g_hwnd ? g_hwnd : o_GetActiveWindow(); }
HWND WINAPI h_SetFocus(HWND) { return g_hwnd; }

// The game looks for a running copy by window class; a test never finds one
// (and so never brings the player's game to the front).
HWND WINAPI h_FindWindowA(LPCSTR cls, LPCSTR name) {
    Log("headless: FindWindowA(%s, %s) answered: none", cls && HIWORD(cls) ? cls : "-", name ? name : "-");
    return nullptr;
}

decltype(&ShowWindow) o_ShowWindow;
decltype(&SetActiveWindow) o_SetActiveWindow;
BOOL WINAPI h_ShowWindow(HWND, int) { return FALSE; }  // stays hidden
HWND WINAPI h_SetActiveWindow(HWND) { return g_hwnd; }

// Ends the run if the test window ever becomes the foreground window or
// reaches the visible desktop.
DWORD WINAPI Watchdog(void*) {
    for (;;) {
        Sleep(100);
        if (!g_hwnd) continue;
        if (o_GetForegroundWindow() == g_hwnd) Abort("the test window became the foreground window");
        RECT r, screen = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0}, both;
        screen.right = screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        screen.bottom = screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (IsWindowVisible(g_hwnd) && GetWindowRect(g_hwnd, &r) && IntersectRect(&both, &r, &screen))
            Abort("the test window reached the visible desktop");
    }
}

// CD music goes to the store's winmm.dll (ogg player) through mciSendCommandA:
// logged in tests to see that music starts.
typedef MCIERROR(WINAPI* MciSend_t)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
MciSend_t o_mciSendCommandA;
MCIERROR WINAPI h_mciSendCommandA(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR param) {
    MCIERROR r = o_mciSendCommandA(id, msg, flags, param);
    if (msg == MCI_PLAY && param && (flags & MCI_FROM)) {
        DWORD from = ((MCI_PLAY_PARMS*)param)->dwFrom;
        Log("music: play track %lu (result %lu)", (unsigned long)(from & 0xff), (unsigned long)r);
    } else if (msg == MCI_OPEN && param) {
        MCI_OPEN_PARMSA* op = (MCI_OPEN_PARMSA*)param;
        HMODULE m = nullptr;
        char path[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(void*)o_mciSendCommandA, &m))
            GetModuleFileNameA(m, path, MAX_PATH);
        Log("music: open flags %#lx type %s element %s = %lu (handled by %s)", (unsigned long)flags,
            (flags & MCI_OPEN_TYPE_ID) ? "(id)" : (op->lpstrDeviceType ? op->lpstrDeviceType : "-"),
            (flags & MCI_OPEN_ELEMENT) && op->lpstrElementName ? op->lpstrElementName : "-", (unsigned long)r, path);
    }
    return r;
}

int WINAPI h_MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT) {
    Log("headless: MessageBox \"%s\": %s", caption ? caption : "", text ? text : "");
    return IDOK;
}

// Mute this process's audio session on the default output. DirectSound streams
// the game opens later join the same session.
DWORD WINAPI MuteThread(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioSessionManager* mgr = nullptr;
    ISimpleAudioVolume* vol = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&en);
    if (SUCCEEDED(hr)) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, (void**)&mgr);
    if (SUCCEEDED(hr)) hr = mgr->GetSimpleAudioVolume(nullptr, FALSE, &vol);
    if (SUCCEEDED(hr)) hr = vol->SetMute(TRUE, nullptr);
    BOOL muted = FALSE;
    if (vol) vol->GetMute(&muted);
    Log("headless: audio session mute %s (hr %#lx)", SUCCEEDED(hr) && muted ? "on" : "FAILED", (unsigned long)hr);
    return 0;  // the session stays referenced for the life of the process
}

bool ConfigIsWindowed() {
    FILE* f = fopen((GameDir() + "dgVoodoo.conf").c_str(), "rb");
    if (!f) return false;
    char line[512];
    bool windowed = false;
    while (fgets(line, sizeof line, f)) {
        std::string s = Lower(line);
        s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return isspace((unsigned char)c); }), s.end());
        if (s.rfind("fullscreenmode=", 0) == 0) windowed = s == "fullscreenmode=false";
    }
    fclose(f);
    return windowed;
}

template <typename T>
bool HookUser32(HMODULE m, const char* name, T fn, T* orig) {
    uintptr_t slot = hook::FindImport(m, "USER32.dll", name);
    if (!slot) return true;  // this module doesn't use it
    T saved;
    if (!hook::Import(slot, fn, &saved)) return false;
    if (!*orig) *orig = saved;
    return true;
}

}  // namespace

bool Active() { return g_active; }
HWND Window() { return g_hwnd; }

void OnLoadLibrary(const char* name, HMODULE module) {
    if (!g_active || !name) return;
    std::string n = Lower(name);
    if (n.find("glide") != std::string::npos || n.find("3dfx") != std::string::npos) Abort("the Glide renderer was requested (run with -d3d)");
    if (n.find("ddraw") != std::string::npos && module) {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        if (Lower(path) != Lower(GameDir() + "ddraw.dll")) Abort("DirectDraw is not dgVoodoo2's copy in the game folder");
    }
    // dgVoodoo2 manages the window of a windowed game itself: keep its calls
    // inactive and off-screen too. (Its DLLs are packed, with no import names:
    // then nothing is found here and the window subclass and the watchdog
    // keep it off-screen and inactive.)
    if (module && (n.find("ddraw") != std::string::npos || n.find("d3dimm") != std::string::npos)) {
        bool ok = HookUser32(module, "ShowWindow", &h_ShowWindow, &o_ShowWindow) &&
                  HookUser32(module, "SetWindowPos", &h_SetWindowPos, &o_SetWindowPos) &&
                  HookUser32(module, "MoveWindow", &h_MoveWindow, &o_MoveWindow) &&
                  HookUser32(module, "SetForegroundWindow", &h_SetForegroundWindow, &o_SetForegroundWindow) &&
                  HookUser32(module, "SetActiveWindow", &h_SetActiveWindow, &o_SetActiveWindow) &&
                  HookUser32(module, "SetFocus", &h_SetFocus, &o_SetFocus);
        Log("headless: window hooks in %s: %s", name, ok ? "ok" : "FAILED");
        if (!ok) Abort("dgVoodoo2's window hooks failed");
    }
}

bool Install(bool visible) {
    g_active = true;
    g_visible = visible;
    if (!ConfigIsWindowed()) Abort("dgVoodoo.conf in the game folder must say FullScreenMode = false");
    if (visible) {  // scripted input, test class, mute and logged message boxes; the window is left alone
        bool ok = HookUser32(nullptr, "CreateWindowExA", &h_CreateWindowExA, &o_CreateWindowExA) &&
                  HookUser32(nullptr, "MessageBoxA", &h_MessageBoxA, &o_MessageBoxA) &&
                  HookUser32(nullptr, "RegisterClassA", &h_RegisterClassA, &o_RegisterClassA) &&
                  HookUser32(nullptr, "SendMessageA", &h_SendMessageA, &o_SendMessageA);
        Log("headless (on screen): hooks %s", ok ? "ok" : "FAILED");
        if (ok) CloseHandle(CreateThread(nullptr, 0, &MuteThread, nullptr, 0, nullptr));
        return ok;
    }
    bool ok = HookUser32(nullptr, "CreateWindowExA", &h_CreateWindowExA, &o_CreateWindowExA) &&
              HookUser32(nullptr, "SetWindowPos", &h_SetWindowPos, &o_SetWindowPos) &&
              HookUser32(nullptr, "SetForegroundWindow", &h_SetForegroundWindow, &o_SetForegroundWindow) &&
              HookUser32(nullptr, "GetForegroundWindow", &h_GetForegroundWindow, &o_GetForegroundWindow) &&
              HookUser32(nullptr, "SetFocus", &h_SetFocus, &o_SetFocus) &&
              HookUser32(nullptr, "FindWindowA", &h_FindWindowA, &o_FindWindowA) &&
              HookUser32(nullptr, "MessageBoxA", &h_MessageBoxA, &o_MessageBoxA) &&
              HookUser32(nullptr, "RegisterClassA", &h_RegisterClassA, &o_RegisterClassA) &&
              HookUser32(nullptr, "SendMessageA", &h_SendMessageA, &o_SendMessageA);
    // The renderer drivers' own window calls (they import these by name).
    loader::Redirect("SetWindowPos", (void*)&h_SetWindowPos, (void**)&o_SetWindowPos);
    loader::Redirect("MoveWindow", (void*)&h_MoveWindow, (void**)&o_MoveWindow);
    loader::Redirect("SetForegroundWindow", (void*)&h_SetForegroundWindow, (void**)&o_SetForegroundWindow);
    loader::Redirect("GetActiveWindow", (void*)&h_GetActiveWindow, (void**)&o_GetActiveWindow);
    loader::OnLoad(&OnLoadLibrary);
    uintptr_t mci = hook::FindImport(nullptr, "winmm.dll", "mciSendCommandA");
    if (mci) hook::Import(mci, &h_mciSendCommandA, &o_mciSendCommandA);
    Log("headless: exe hooks %s; music log %s", ok ? "ok" : "FAILED", mci ? "on" : "off");
    if (!o_GetForegroundWindow) o_GetForegroundWindow = &GetForegroundWindow;
    if (ok) {
        CloseHandle(CreateThread(nullptr, 0, &MuteThread, nullptr, 0, nullptr));
        CloseHandle(CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr));
    }
    return ok;
}

}  // namespace headless
