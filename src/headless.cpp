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
    if (msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW || msg == WM_STYLECHANGED) {
        RECT r = {};
        GetWindowRect(hwnd, &r);
        Log("headless: msg %#x wp %#lx: window at %ld,%ld..%ld,%ld, style %#lx, ex %#lx, %s", msg, (unsigned long)wp,
            r.left, r.top, r.right, r.bottom, (unsigned long)GetWindowLongA(hwnd, GWL_STYLE),
            (unsigned long)GetWindowLongA(hwnd, GWL_EXSTYLE), IsWindowVisible(hwnd) ? "visible" : "hidden");
    }
    if (msg == WM_WINDOWPOSCHANGING) {
        auto* wpos = (WINDOWPOS*)lp;
        wpos->x = kOff;
        wpos->y = kOff;
        wpos->flags = (wpos->flags & ~SWP_NOMOVE) | SWP_NOACTIVATE | SWP_NOZORDER;
    }
    // The graphics stack (DXGI) rewrites the extended style when the renderer
    // starts, dropping WS_EX_TOOLWINDOW: the window then got a taskbar button
    // and an Alt+Tab entry on the player's desktop for each test run.
    if (msg == WM_STYLECHANGING && wp == (WPARAM)GWL_EXSTYLE) {
        auto* st = (STYLESTRUCT*)lp;
        st->styleNew = (st->styleNew | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW;
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
        if (IsWindowVisible(g_hwnd) && GetWindowRect(g_hwnd, &r) && IntersectRect(&both, &r, &screen)) {
            Log("headless: window at %ld,%ld..%ld,%ld, style %#lx, ex %#lx", r.left, r.top, r.right, r.bottom,
                (unsigned long)GetWindowLongA(g_hwnd, GWL_STYLE), (unsigned long)GetWindowLongA(g_hwnd, GWL_EXSTYLE));
            Abort("the test window reached the visible desktop");
        }
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

// C2HD_HEADLESS=3: a real full-screen run for on-screen checks (the mouse,
// Alt+F4, the picture), driven by real input from outside. Only the test
// class (so a copy the player has open doesn't make it quit at start-up, nor
// it the player's), logged message boxes and mute; nothing else is touched.
bool InstallScreen() {
    g_visible = true;
    bool ok = HookUser32(nullptr, "CreateWindowExA", &h_CreateWindowExA, &o_CreateWindowExA) &&
              HookUser32(nullptr, "RegisterClassA", &h_RegisterClassA, &o_RegisterClassA) &&
              HookUser32(nullptr, "FindWindowA", &h_FindWindowA, &o_FindWindowA) &&
              HookUser32(nullptr, "MessageBoxA", &h_MessageBoxA, &o_MessageBoxA);
    char mute[4] = "";  // C2HD_MUTE=0: audible (for sound checks with the player there)
    bool audible = GetEnvironmentVariableA("C2HD_MUTE", mute, sizeof mute) && mute[0] == '0';
    Log("headless (full screen): hooks %s%s", ok ? "ok" : "FAILED", audible ? ", sound on" : "");
    if (ok && !audible) CloseHandle(CreateThread(nullptr, 0, &MuteThread, nullptr, 0, nullptr));
    return ok;
}

// The calls that bring a window forward, blocked at user32 itself for this
// process's own windows: the game's and its drivers' imports are hooked
// above, but dgVoodoo2's DLLs are packed (their calls can't be found), and on
// a PC where the foreground lock timeout is 0 any of them takes the focus
// from the player at once. Each must start with the hot-patch prologue or be a
// jmp [abs32] forwarding stub (as all four are on Windows 10's 32-bit user32).
void* o_SetForegroundWindowApi;
void* o_BringWindowToTopApi;
void* o_SetActiveWindowApi;

bool OwnWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}
BOOL WINAPI SetForegroundWindowApi(HWND hwnd) {
    if (OwnWindow(hwnd)) return TRUE;
    return reinterpret_cast<decltype(&SetForegroundWindow)>(o_SetForegroundWindowApi)(hwnd);
}
BOOL WINAPI BringWindowToTopApi(HWND hwnd) {
    if (OwnWindow(hwnd)) return TRUE;
    return reinterpret_cast<decltype(&BringWindowToTop)>(o_BringWindowToTopApi)(hwnd);
}
HWND WINAPI SetActiveWindowApi(HWND hwnd) {
    if (OwnWindow(hwnd)) return g_hwnd;
    return reinterpret_cast<decltype(&SetActiveWindow)>(o_SetActiveWindowApi)(hwnd);
}

// Moves of the test window, from anywhere: dgVoodoo2 sometimes sized it to
// its forced resolution at 0,0 with a call that skipped WM_WINDOWPOSCHANGING
// (so the window procedure above couldn't keep it off-screen) and the
// watchdog ended the run. Any move of it now goes to kOff, and the first call
// from each module is logged.
void* o_SetWindowPosApi;
void* o_MoveWindowApi;
void* o_SetWindowPlacementApi;

void LogMover(const char* api, void* from) {
    static HMODULE seen[8];
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)from, &m))
        return;
    for (HMODULE& s : seen) {
        if (s == m) return;
        if (!s) {
            s = m;
            char path[MAX_PATH] = "?";
            GetModuleFileNameA(m, path, MAX_PATH);
            const char* base = strrchr(path, 92);  // the last backslash
            Log("headless: %s on the test window from %s, kept off-screen", api, base ? base + 1 : path);
            return;
        }
    }
}
BOOL WINAPI SetWindowPosApi(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
    if (hwnd == g_hwnd && g_hwnd) {
        LogMover("SetWindowPos", __builtin_return_address(0));
        x = y = kOff;
        flags = (flags & ~SWP_NOMOVE) | SWP_NOACTIVATE | SWP_NOZORDER;
    }
    return reinterpret_cast<decltype(&SetWindowPos)>(o_SetWindowPosApi)(hwnd, after, x, y, w, h, flags);
}
BOOL WINAPI MoveWindowApi(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
    if (hwnd == g_hwnd && g_hwnd) {
        LogMover("MoveWindow", __builtin_return_address(0));
        x = y = kOff;
    }
    return reinterpret_cast<decltype(&MoveWindow)>(o_MoveWindowApi)(hwnd, x, y, w, h, repaint);
}
BOOL WINAPI SetWindowPlacementApi(HWND hwnd, const WINDOWPLACEMENT* wp) {
    if (hwnd == g_hwnd && g_hwnd && wp) {
        LogMover("SetWindowPlacement", __builtin_return_address(0));
        WINDOWPLACEMENT copy = *wp;
        OffsetRect(&copy.rcNormalPosition, kOff - copy.rcNormalPosition.left, kOff - copy.rcNormalPosition.top);
        if (copy.showCmd == SW_MAXIMIZE || copy.showCmd == SW_SHOWMAXIMIZED) copy.showCmd = SW_SHOWNOACTIVATE;
        return reinterpret_cast<decltype(&SetWindowPlacement)>(o_SetWindowPlacementApi)(hwnd, &copy);
    }
    return reinterpret_cast<decltype(&SetWindowPlacement)>(o_SetWindowPlacementApi)(hwnd, wp);
}

bool BlockForegroundCalls() {
    static const uint8_t hot[5] = {0x8b, 0xff, 0x55, 0x8b, 0xec};  // mov edi,edi; push ebp; mov ebp,esp
    HMODULE user32 = GetModuleHandleA("user32.dll");
    struct {
        const char* name;
        void* fn;
        void** orig;
    } apis[] = {{"SetForegroundWindow", (void*)&SetForegroundWindowApi, &o_SetForegroundWindowApi},
                {"BringWindowToTop", (void*)&BringWindowToTopApi, &o_BringWindowToTopApi},
                {"SetActiveWindow", (void*)&SetActiveWindowApi, &o_SetActiveWindowApi},
                {"SetWindowPos", (void*)&SetWindowPosApi, &o_SetWindowPosApi},
                {"MoveWindow", (void*)&MoveWindowApi, &o_MoveWindowApi},
                {"SetWindowPlacement", (void*)&SetWindowPlacementApi, &o_SetWindowPlacementApi}};
    bool ok = true;
    for (auto& a : apis) {
        uintptr_t at = (uintptr_t)GetProcAddress(user32, a.name);
        *a.orig = at ? hook::Detour(at, hot, 5, a.fn) : nullptr;
        // Or an export that is a forwarding stub, jmp dword ptr [abs32] (ff 25):
        // absolute, so it runs unchanged from the trampoline.
        uint8_t stub[6];
        if (!*a.orig && at && *(const uint8_t*)at == 0xff && *(const uint8_t*)(at + 1) == 0x25) {
            memcpy(stub, (const void*)at, 6);
            *a.orig = hook::Detour(at, stub, 6, a.fn);
        }
        if (!*a.orig) {
            const uint8_t* b = (const uint8_t*)at;
            Log("headless: %s not blockable (starts %02x %02x %02x %02x %02x %02x %02x %02x)", a.name, b ? b[0] : 0,
                b ? b[1] : 0, b ? b[2] : 0, b ? b[3] : 0, b ? b[4] : 0, b ? b[5] : 0, b ? b[6] : 0, b ? b[7] : 0);
        }
        ok = ok && *a.orig;
    }
    return ok;
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
    bool blocked = BlockForegroundCalls();
    Log("headless: foreground calls %s", blocked ? "blocked at user32" : "NOT all blocked (see above)");
    if (ok) {
        CloseHandle(CreateThread(nullptr, 0, &MuteThread, nullptr, 0, nullptr));
        CloseHandle(CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr));
    }
    return ok;
}

}  // namespace headless
