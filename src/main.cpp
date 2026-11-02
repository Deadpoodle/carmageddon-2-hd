// Carmageddon II HD: entry point. Loaded as dinput.dll (proxy.cpp) at process
// start, before the game's own code runs.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "hook.h"

namespace {

const char* kVersion = "0.0.1";

std::string g_dir;  // game folder, with trailing backslash
FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// The Steam CARMA2_HW.EXE (build 266224): its command-line switch strings sit
// at fixed addresses. Anything else gets no patches.
bool IsKnownExe() {
    const char* zombie = (const char*)0x662604;
    const char* d3d = (const char*)0x66260c;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(zombie, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    return strcmp(zombie, "-ZOMBIE") == 0 && strcmp(d3d, "-D3D") == 0;
}

std::string Env(const char* name) {
    char buf[256];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
    return n && n < sizeof buf ? std::string(buf, n) : std::string();
}

// ---------------------------------------------------------------------------
// Test mode (environment variables only, never player options)

typedef HWND(WINAPI* CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
CreateWindowExA_t real_CreateWindowExA;

// C2HD_TEST_EXIT=window: log the game's window request and end the process
// before anything reaches the screen. Proves the patch loads without a display.
HWND WINAPI ExitAtCreateWindow(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                               HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    Log("CreateWindowExA class=%s title=%s style=0x%08lx ex=0x%08lx %d,%d %dx%d", cls ? cls : "(null)",
        title ? title : "(null)", style, ex, x, y, w, h);
    Log("test exit before the window is created");
    if (g_log) fclose(g_log);
    TerminateProcess(GetCurrentProcess(), 0);
    return real_CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

void Start() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    g_dir = path;
    g_dir.resize(g_dir.rfind('\\') + 1);
    g_log = fopen((g_dir + "Carma2HD.log").c_str(), "w");
    Log("Carmageddon II HD %s, exe %s", kVersion, path);
    Log("command line: %s", GetCommandLineA());
    if (!IsKnownExe()) {
        Log("unrecognised exe: no patches applied");
        return;
    }
    if (Env("C2HD_TEST_EXIT") == "window") {
        bool ok = hook::Import(hook::FindImport(nullptr, "USER32.dll", "CreateWindowExA"), &ExitAtCreateWindow,
                               &real_CreateWindowExA);
        Log("test: exit at CreateWindowExA (%s)", ok ? "hooked" : "hook FAILED");
        if (!ok) TerminateProcess(GetCurrentProcess(), 1);  // never let a test reach the screen
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) Start();
    return TRUE;
}
