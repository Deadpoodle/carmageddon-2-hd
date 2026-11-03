// Carmageddon II HD: entry point. Loaded as dinput.dll (proxy.cpp) at process
// start, before the game's own code runs.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "capture.h"
#include "controller.h"
#include "display.h"
#include "hdtab.h"
#include "headless.h"
#include "hook.h"
#include "input.h"
#include "loader.h"
#include "movie.h"
#include "music.h"
#include "options.h"
#include "log.h"
#include "settings.h"
#include "sharptext.h"

namespace {

const char* kVersion = "0.0.1";

std::string g_dir;  // game folder, with trailing backslash
FILE* g_log;
CRITICAL_SECTION g_logLock;

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
    char buf[1024];
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
    TerminateProcess(GetCurrentProcess(), 0);
    return real_CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

// C2HD_SHOTS=<ms>: a back-buffer shot every so often.
DWORD WINAPI ShotThread(void* p) {
    DWORD every = (DWORD)(uintptr_t)p;
    for (;;) {
        Sleep(every);
        capture::Request();
    }
}

// C2HD_QUIT=<seconds>: end the run.
DWORD WINAPI QuitThread(void* p) {
    Sleep((DWORD)(uintptr_t)p * 1000);
    Log("test: quit after %u s", (unsigned)(uintptr_t)p);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

// Logs the first access violation (address and registers), then lets the
// normal handling continue: a crash always leaves a line in the log.
LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* ep) {
    static LONG once;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION || InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    Log("CRASH: access violation at %p (%s address %p) eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx "
        "ebp=%08lx esp=%08lx",
        ep->ExceptionRecord->ExceptionAddress, ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
        (void*)ep->ExceptionRecord->ExceptionInformation[1], c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp,
        c->Esp);
    // Code addresses on the stack, named by module and offset (the drivers are
    // loaded by BRender's own loader, so theirs show as "?" with the address).
    const DWORD* sp = (const DWORD*)c->Esp;
    int shown = 0;
    for (int i = 0; i < 512 && shown < 16; i++) {
        DWORD v = sp[i];
        MEMORY_BASIC_INFORMATION mbi;
        if (v < 0x10000 || !VirtualQuery((void*)v, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            continue;
        HMODULE m = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)v, &m) && m) {
            GetModuleFileNameA(m, name, MAX_PATH);
            const char* base = strrchr(name, '\\');
            Log("CRASH: [esp+%03x] %s+%#lx", i * 4, base ? base + 1 : name, v - (DWORD)(uintptr_t)m);
        } else {
            Log("CRASH: [esp+%03x] %08lx (alloc base %p)", i * 4, v, mbi.AllocationBase);
        }
        shown++;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void StartTests() {
    AddVectoredExceptionHandler(0, &CrashLogger);
    if (Env("C2HD_TEST_EXIT") == "window") {
        bool ok = hook::Import(hook::FindImport(nullptr, "USER32.dll", "CreateWindowExA"), &ExitAtCreateWindow,
                               &real_CreateWindowExA);
        Log("test: exit at CreateWindowExA (%s)", ok ? "hooked" : "hook FAILED");
        if (!ok) TerminateProcess(GetCurrentProcess(), 1);  // never let a test reach the screen
    }
    std::string mode = Env("C2HD_HEADLESS");
    if (mode != "1" && mode != "2") return;
    if (!headless::Install(mode == "2")) {
        Log("test: headless hooks failed; ending the run");
        TerminateProcess(GetCurrentProcess(), 1);
    }
    capture::Install();
    if (int ms = atoi(Env("C2HD_SHOTS").c_str()))
        CloseHandle(CreateThread(nullptr, 0, &ShotThread, (void*)(uintptr_t)ms, 0, nullptr));
    int quit = atoi(Env("C2HD_QUIT").c_str());
    CloseHandle(CreateThread(nullptr, 0, &QuitThread, (void*)(uintptr_t)(quit > 0 ? quit : 60), 0, nullptr));
    std::string keys = Env("C2HD_KEYS");
    if (!keys.empty()) input::StartScript(keys.c_str());
}

void Start() {
    InitializeCriticalSection(&g_logLock);
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
    if (!loader::Install()) Log("loader hooks FAILED");
    settings::Load();
    input::LoadBindings();
    options::Install();
    hdtab::Install();
    controller::Install();
    display::Install();
    movie::Install();
    sharptext::Install();
    music::Install();
    StartTests();
}

}  // namespace

void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

const std::string& GameDir() { return g_dir; }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) Start();
    return TRUE;
}
