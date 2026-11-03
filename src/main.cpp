// Carmageddon II HD: entry point. Loaded as dinput.dll (proxy.cpp) at process
// start, before the game's own code runs.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "audio.h"
#include "capture.h"
#include "controller.h"
#include "display.h"
#include "frontend.h"
#include "hdtab.h"
#include "icons.h"
#include "headless.h"
#include "hook.h"
#include "input.h"
#include "keysets.h"
#include "loader.h"
#include "movie.h"
#include "music.h"
#include "netmenu.h"
#include "netshim.h"
#include "options.h"
#include "quit.h"
#include "log.h"
#include "settings.h"
#include "sharptext.h"
#include "version.h"
#include "wrecks.h"

namespace {

const char* kVersion = C2HD_VERSION;

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
    // Errors only (0xC...: access violation, stack overflow, illegal
    // instruction...), not warnings or the debugger's and C++'s own codes.
    if ((code & 0xF0000000) != 0xC0000000 || code == 0xE06D7363 || InterlockedExchange(&once, 1))
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    Log("CRASH: exception %#lx at %p (%s address %p) eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx "
        "ebp=%08lx esp=%08lx",
        (unsigned long)code, ep->ExceptionRecord->ExceptionAddress,
        ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
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

// C2HD_GAMELOG=1 (diagnostic): the game's own debug print, compiled out to a
// bare ret at 0x44c590 (printf-style, cdecl), goes to our log.
void __cdecl GameDebugPrint(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    size_t n = strlen(line);
    while (n && (line[n - 1] == 10 || line[n - 1] == 13)) line[--n] = 0;  // trailing newlines
    Log("game: %s", line);
}

// C2HD_GAMELOG=1 also: who in the process installs Windows hooks or registers
// raw input (either can take keys such as Alt+Tab's Tab before the system
// sees them), with the calling module. Only hooked when the API starts with
// the usual hot-patch prologue (mov edi,edi; push ebp; mov ebp,esp).
void* o_SetWindowsHookExA;
void* o_SetWindowsHookExW;
void* o_RegisterRawInputDevices;

void LogCaller(const char* what, void* ret) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)ret, &m) && m)
        GetModuleFileNameA(m, path, MAX_PATH);
    const char* base = strrchr(path, 92);
    Log("%s, called from %s+%#lx", what, base ? base + 1 : path, m ? (DWORD)((uintptr_t)ret - (uintptr_t)m) : 0ul);
}

HHOOK WINAPI LogHookA(int id, HOOKPROC fn, HINSTANCE mod, DWORD thread) {
    char what[96];
    snprintf(what, sizeof what, "hooks: SetWindowsHookExA(id %d, thread %lu)", id, thread);
    LogCaller(what, __builtin_return_address(0));
    return reinterpret_cast<decltype(&SetWindowsHookExA)>(o_SetWindowsHookExA)(id, fn, mod, thread);
}
HHOOK WINAPI LogHookW(int id, HOOKPROC fn, HINSTANCE mod, DWORD thread) {
    char what[96];
    snprintf(what, sizeof what, "hooks: SetWindowsHookExW(id %d, thread %lu)", id, thread);
    LogCaller(what, __builtin_return_address(0));
    return reinterpret_cast<decltype(&SetWindowsHookExW)>(o_SetWindowsHookExW)(id, fn, mod, thread);
}
BOOL WINAPI LogRawInput(PCRAWINPUTDEVICE devs, UINT n, UINT size) {
    for (UINT i = 0; i < n; i++) {
        char what[128];
        snprintf(what, sizeof what, "hooks: RegisterRawInputDevices(page %#x usage %#x flags %#lx)",
                 devs[i].usUsagePage, devs[i].usUsage, (unsigned long)devs[i].dwFlags);
        LogCaller(what, __builtin_return_address(0));
    }
    return reinterpret_cast<decltype(&RegisterRawInputDevices)>(o_RegisterRawInputDevices)(devs, n, size);
}

void LogHooks() {
    static const uint8_t hot[5] = {0x8b, 0xff, 0x55, 0x8b, 0xec};
    HMODULE user32 = GetModuleHandleA("user32.dll");
    struct {
        const char* name;
        void* fn;
        void** orig;
    } apis[] = {{"SetWindowsHookExA", (void*)&LogHookA, &o_SetWindowsHookExA},
                {"SetWindowsHookExW", (void*)&LogHookW, &o_SetWindowsHookExW},
                {"RegisterRawInputDevices", (void*)&LogRawInput, &o_RegisterRawInputDevices}};
    for (auto& a : apis) {
        uintptr_t at = (uintptr_t)GetProcAddress(user32, a.name);
        const uint8_t* b = (const uint8_t*)at;
        *a.orig = at ? hook::Detour(at, hot, 5, a.fn) : nullptr;
        Log("hooks: %s %s (starts %02x %02x %02x %02x %02x)", a.name, *a.orig ? "logged" : "NOT logged", b ? b[0] : 0,
            b ? b[1] : 0, b ? b[2] : 0, b ? b[3] : 0, b ? b[4] : 0);
    }
}

// C2HD_PEDLOG=1 (diagnostic): every pedestrian move change, through
// SetPedMove 0x4cbcd0 (fastcall ped, move; then rate, blend, end flag, time,
// state; callee pops 0x14), with the caller (re/peds.md).
using SetPedMoveFn = int(__fastcall*)(uintptr_t, int, int, int, int, int, int);
SetPedMoveFn o_SetPedMove;

int __fastcall LogSetPedMove(uintptr_t ped, int move, int rate, int blend, int end, int time, int state) {
    Log("ped %08lx move %d rate %d blend %d end %d time %d state %d from %p", (unsigned long)ped, move, rate, blend,
        end, time, state, __builtin_return_address(0));
    return o_SetPedMove(ped, move, rate, blend, end, time, state);
}

void StartTests() {
    AddVectoredExceptionHandler(0, &CrashLogger);
    if (Env("C2HD_GAMELOG") == "1") {
        uint8_t jmp[5] = {0xe9};
        int32_t rel = (int32_t)((uintptr_t)&GameDebugPrint - (0x44c590 + 5));
        memcpy(jmp + 1, &rel, 4);
        static const uint8_t stub[5] = {0xc3, 0x90, 0x90, 0x90, 0x90};  // ret, then padding
        Log("test: game debug print to the log: %s", hook::Patch(0x44c590, stub, jmp, 5) ? "on" : "FAILED");
        quit::LogFocusMessages();
        LogHooks();
        audio::InstallLog();
    }
    if (Env("C2HD_FLOWLOG") == "1") frontend::InstallFlowLog();
    if (Env("C2HD_PEDLOG") == "1") {
        static const uint8_t prologue[6] = {0x81, 0xec, 0x28, 0x01, 0x00, 0x00};  // sub esp, 0x128
        o_SetPedMove = (SetPedMoveFn)hook::Detour(0x4cbcd0, prologue, 6, (const void*)&LogSetPedMove);
        Log("test: pedestrian moves to the log: %s", o_SetPedMove ? "on" : "FAILED");
    }
    if (Env("C2HD_TEST_EXIT") == "window") {
        bool ok = hook::Import(hook::FindImport(nullptr, "USER32.dll", "CreateWindowExA"), &ExitAtCreateWindow,
                               &real_CreateWindowExA);
        Log("test: exit at CreateWindowExA (%s)", ok ? "hooked" : "hook FAILED");
        if (!ok) TerminateProcess(GetCurrentProcess(), 1);  // never let a test reach the screen
    }
    std::string mode = Env("C2HD_HEADLESS");
    if (mode == "3") {
        if (!headless::InstallScreen()) TerminateProcess(GetCurrentProcess(), 1);
        int quit = atoi(Env("C2HD_QUIT").c_str());
        CloseHandle(CreateThread(nullptr, 0, &QuitThread, (void*)(uintptr_t)(quit > 0 ? quit : 120), 0, nullptr));
        capture::Install();  // the frame-rate log
        return;
    }
    if (mode != "1" && mode != "2") return;
    if (!headless::Install(mode == "2")) {
        Log("test: headless hooks failed; ending the run");
        TerminateProcess(GetCurrentProcess(), 1);
    }
    capture::Install();
    frontend::InstallTestCursor();
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
    quit::Install();
    input::LoadBindings();
    options::Install();
    hdtab::Install();
    keysets::Install();
    controller::Install();
    display::Install();
    movie::Install();
    sharptext::Install();
    icons::Install();
    wrecks::Install();
    music::Install();
    StartTests();
    if (netshim::Install()) netmenu::Install();  // after the test harness: tests stay on loopback
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
