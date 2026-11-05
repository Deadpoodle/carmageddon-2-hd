// Closing the game: Alt+F4, and Close from the taskbar or Task Manager.
//
// The game's window procedure (0x51b0c0) quits only on WM_SYSCOMMAND
// SC_CLOSE: DestroyWindow, then its shutdown (0x51c110), which releases
// DirectDraw and the sound system, pumps the remaining messages
// (Win32ServiceMessages 0x51cad0) and ends in ExitProcess. That pump waits in
// GetMessage while the game is inactive (its Alt+Tab pause), and after
// DestroyWindow it is inactive with no window left to wake it: in full
// screen, Alt+F4 hung there for good (hidden test windows always count as
// active, so tests never saw it). The shutdown's call to it (0x51c1c8) now
// drains without waiting. Next it broadcast WM_SHOWWINDOW to every top-level
// window with SendMessage (0x51c1e4), which waits on each of them, so any
// window not answering held it up too; the message does nothing (DefWindowProc
// ignores it without a reason code), so that call is gone. WM_CLOSE went to
// DefWindowProc, which destroys the
// window and leaves the game running without one. So the class's procedure
// (the pointer stored at 0x51ad40) is ours: Alt+F4 and WM_CLOSE become
// SC_CLOSE; and a guard ends the process, logging where the game was, if its
// shutdown still hasn't finished within a few seconds.
#include "quit.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gamestate.h"
#include "hook.h"
#include "log.h"
#include "publicaddr.h"

namespace quit {
namespace {

const WNDPROC kGameProc = (WNDPROC)0x51b0c0;
constexpr uintptr_t kClassProc = 0x51ad40;  // mov [esp+0x20], 0x51b0c0: WNDCLASSA.lpfnWndProc
constexpr DWORD kGraceMs = 3000;
HANDLE g_mainThread;  // Install runs on it (DllMain)

}  // namespace

// Where the main thread is: its EIP and the code addresses on its stack.
void LogMainThread() {
    if (!g_mainThread || SuspendThread(g_mainThread) == (DWORD)-1) return;
    CONTEXT c = {};
    c.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(g_mainThread, &c)) {
        auto name = [](DWORD a, char* out, size_t n) {
            HMODULE m = nullptr;
            char path[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(uintptr_t)a, &m) && m)
                GetModuleFileNameA(m, path, MAX_PATH);
            const char* base = strrchr(path, '\\');
            snprintf(out, n, "%s+%#lx", base ? base + 1 : path, m ? a - (DWORD)(uintptr_t)m : a);
        };
        char where[MAX_PATH + 16];
        name(c.Eip, where, sizeof where);
        Log("quit: main thread at %s", where);
        const DWORD* sp = (const DWORD*)(uintptr_t)c.Esp;
        for (int i = 0, shown = 0; i < 1024 && shown < 12; i++) {
            MEMORY_BASIC_INFORMATION mbi;
            DWORD v;
            if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, 4, nullptr)) break;
            if (v < 0x10000 || !VirtualQuery((void*)(uintptr_t)v, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
                !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                continue;
            name(v, where, sizeof where);
            Log("quit:   [esp+%03x] %s", i * 4, where);
            shown++;
        }
    }
    ResumeThread(g_mainThread);
}

namespace {

// A hang: the game's window stops answering messages (the game pumps them
// every frame, menus and races alike) for a few seconds. Then what the game was
// doing and the main thread's stack go to the log, three samples of the stack a
// second apart, once per hang; a line says when it answers again, which tells a
// long load from a freeze.
HWND* const kWindow = (HWND*)0x6ad4c8;

DWORD WINAPI HangWatch(void*) {
    bool logged = false;
    DWORD hungAt = 0;
    for (;;) {
        Sleep(1000);
        gamestate::Poll();
        HWND hwnd = *kWindow;
        if (!hwnd || IsIconic(hwnd)) continue;  // the game waits by design while minimised
        DWORD_PTR r;
        if (SendMessageTimeoutA(hwnd, WM_NULL, 0, 0, SMTO_NORMAL, 3000, &r)) {
            if (logged) Log("hang: the game answers again, after about %lu s", (unsigned long)((GetTickCount() - hungAt) / 1000));
            logged = false;
            continue;
        }
        if (logged) continue;
        logged = true;
        hungAt = GetTickCount() - 3000;
        gamestate::LogSnapshot();
        for (int i = 0; i < 3; i++) {
            Log("hang: the game hasn't answered its window for 3 s (sample %d)", i + 1);
            LogMainThread();
            Sleep(1000);
        }
    }
}

DWORD WINAPI Guard(void*) {
    Sleep(kGraceMs);
    Log("quit: the game's shutdown didn't finish in %lu ms; ending the process", (unsigned long)kGraceMs);
    gamestate::LogSnapshot();
    LogMainThread();
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

// Alt+Tab. Windows' own compatibility database shims this exe, and its shim
// library (AcGenral.dll) installs a global low-level keyboard hook that
// swallows Alt+Tab (the Tab never reaches the game and no switcher appears),
// a fix for old DirectDraw games that broke when switched away from. Through
// dgVoodoo2 the game handles losing the focus (it minimises and pauses, and
// resumes on return), so that one hook is declined: a low-level keyboard
// hook asked for by a Windows compatibility module. Everything else passes.
void* o_SetWindowsHookExA;

bool FromCompatShim(void* ret) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "";
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)ret, &m) || !m)
        return false;
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    return _strnicmp(base, "AcGenral", 8) == 0 || _strnicmp(base, "AcLayers", 8) == 0 ||
           _strnicmp(base, "AcSpecfc", 8) == 0 || _strnicmp(base, "AcXtrnal", 8) == 0;
}

HHOOK WINAPI SetWindowsHookExA_NoAltTabBlock(int id, HOOKPROC fn, HINSTANCE mod, DWORD thread) {
    if (id == WH_KEYBOARD_LL && FromCompatShim(__builtin_return_address(0))) {
        Log("quit: declined the compatibility shim's keyboard hook that blocks Alt+Tab");
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return reinterpret_cast<decltype(&SetWindowsHookExA)>(o_SetWindowsHookExA)(id, fn, mod, thread);
}

// The shutdown's message pump: what is waiting, without waiting for more.
void ShutdownDrain() {
    static bool once;
    if (!once) {
        once = true;
        publicaddr::Shutdown();  // remove the router's port mapping while hosting
    }
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE))
        if (m.message != WM_QUIT) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
}

bool g_logFocus;  // diagnostic: focus-related messages to the log

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_logFocus && (msg == WM_ACTIVATEAPP || msg == WM_ACTIVATE || msg == WM_SYSCOMMAND || msg == WM_SYSKEYDOWN ||
                       msg == WM_SYSKEYUP || msg == WM_SIZE || msg == WM_KILLFOCUS || msg == WM_SETFOCUS ||
                       msg == WM_WINDOWPOSCHANGED))
        Log("window: msg %#x wp %#lx lp %#lx (foreground %p, iconic %d)", msg, (unsigned long)wp, (unsigned long)lp,
            (void*)GetForegroundWindow(), IsIconic(hwnd));
    const char* why = nullptr;
    if (msg == WM_SYSKEYDOWN && wp == VK_F4 && (lp & (1 << 29))) why = "Alt+F4";
    else if (msg == WM_CLOSE) why = "WM_CLOSE";
    else if (msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE) why = "SC_CLOSE";
    if (!why) return CallWindowProcA(kGameProc, hwnd, msg, wp, lp);
    static LONG once;
    if (InterlockedExchange(&once, 1)) return 0;  // already quitting
    Log("quit: %s", why);
    if (HANDLE t = CreateThread(nullptr, 0, &Guard, nullptr, 0, nullptr)) CloseHandle(t);
    return CallWindowProcA(kGameProc, hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
}

}  // namespace

void LogFocusMessages() { g_logFocus = true; }

void Install() {
    g_mainThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, GetCurrentThreadId());
    if (HANDLE t = CreateThread(nullptr, 0, &HangWatch, nullptr, 0, nullptr)) CloseHandle(t);
    // call [SendMessageA] -> add esp, 16 (its four pushed arguments); xor eax, eax
    static const uint8_t broadcast[6] = {0xff, 0x15, 0x14, 0x92, 0x58, 0x00};
    static const uint8_t none[6] = {0x83, 0xc4, 0x10, 0x33, 0xc0, 0x90};
    bool ok = hook::PatchU32(kClassProc, (uint32_t)(uintptr_t)kGameProc, (uint32_t)(uintptr_t)&WndProc) &&
              hook::Redirect(0x51c1c8, 0xe8, 0x51cad0, (const void*)&ShutdownDrain) &&
              hook::Patch(0x51c1e4, broadcast, none, 6);
    Log("quit: Alt+F4 and Close quit the game: %s", ok ? "ok" : "FAILED (unexpected bytes)");
    static const uint8_t hotPatch[5] = {0x8b, 0xff, 0x55, 0x8b, 0xec};  // mov edi,edi; push ebp; mov ebp,esp
    uintptr_t hookApi = (uintptr_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowsHookExA");
    o_SetWindowsHookExA =
        hookApi ? hook::Detour(hookApi, hotPatch, 5, (const void*)&SetWindowsHookExA_NoAltTabBlock) : nullptr;
    Log("quit: Alt+Tab: %s", o_SetWindowsHookExA ? "a compatibility shim's blocking keyboard hook will be declined"
                                                   : "FAILED (SetWindowsHookExA not as expected)");
}

}  // namespace quit
