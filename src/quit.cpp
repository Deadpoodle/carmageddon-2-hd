// Closing the game: Alt+F4, and Close from the taskbar or Task Manager.
//
// The game's window procedure (0x51b0c0) quits only on WM_SYSCOMMAND
// SC_CLOSE: DestroyWindow, then its shutdown (0x51c110), which releases
// DirectDraw and the sound system, broadcasts WM_SHOWWINDOW to every top-level
// window (a SendMessage that waits on each of them) and ends in ExitProcess.
// WM_CLOSE goes to DefWindowProc, which destroys the window and leaves the
// game running without one. So the class's procedure (the pointer stored at
// 0x51ad40) is ours: Alt+F4 and WM_CLOSE become SC_CLOSE, and a guard ends the
// process if the game's own shutdown hasn't within a few seconds.
#include "quit.h"

#include <windows.h>

#include <cstdint>

#include "hook.h"
#include "log.h"

namespace quit {
namespace {

const WNDPROC kGameProc = (WNDPROC)0x51b0c0;
constexpr uintptr_t kClassProc = 0x51ad40;  // mov [esp+0x20], 0x51b0c0: WNDCLASSA.lpfnWndProc
constexpr DWORD kGraceMs = 3000;

DWORD WINAPI Guard(void*) {
    Sleep(kGraceMs);
    Log("quit: the game's shutdown didn't finish in %lu ms; ending the process", (unsigned long)kGraceMs);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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

void Install() {
    bool ok = hook::PatchU32(kClassProc, (uint32_t)(uintptr_t)kGameProc, (uint32_t)(uintptr_t)&WndProc);
    Log("quit: Alt+F4 and Close quit the game: %s", ok ? "ok" : "FAILED (unexpected bytes)");
}

}  // namespace quit
