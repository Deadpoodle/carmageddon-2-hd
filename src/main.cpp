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
#include "peds.h"
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

// C2HD_PEDLOG=1 also: after each frame's ped update (0x4d3740), the two
// active peds nearest the camera: animation frame/fraction/rate, facing,
// torso (bone 0) and left leg (bone 2) translations, avoidance mode, physics
// (re/peds.md 5b.1).
void (*o_PedUpdate)();
const auto kBoneMatrix0 = (float*(__fastcall*)(uintptr_t))0x407aa0;
const auto kBoneMatrix = (float*(__fastcall*)(uintptr_t, int))0x407ad0;
const auto kGameTime = (unsigned(*)())0x514c30;

// C2HD_PEDWATCH=1 (with PEDLOG): the nearest ped (=m<n>: the nearest in
// move n) is watched; the three functions that write a ped's torso matrix
// (key-frame apply 0x407b30, facing 0x40a730, ground snap 0x40b0a0; each
// fastcall char, edx, one stack arg) log its torso x before and after when
// it changes, with their caller.
uintptr_t g_watchPed, g_watchChar;
const float* g_watchX;
using CharFn = int(__fastcall*)(uintptr_t, int, int);
CharFn o_ApplyFrame, o_SetFacing, o_GroundSnap;

int Watched(const char* name, CharFn fn, uintptr_t c, int edx, int arg, void* from) {
    if (!g_watchX || c != g_watchChar) return fn(c, edx, arg);
    float before = *g_watchX;
    int r = fn(c, edx, arg);
    if (*g_watchX != before)
        Log("pedw %u %s edx %d arg %d x %.4f -> %.4f fr %d from %p", kGameTime(), name, edx, arg, before, *g_watchX,
            *(int16_t*)(c + 0x1c), from);
    return r;
}
// C2HD_PEDWATCH also arms a hardware write watch (debug register 0) on the
// watched torso x, set by the handler of an exception the game thread raises,
// logging the address after each writing instruction (and, for library
// routines, the game code addresses on the stack). Slow: use C2HD_SLOWMO.
constexpr DWORD kArmWatch = 0xE0C2D001;
int g_watchHits;

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* ep) {
    CONTEXT* c = ep->ContextRecord;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == kArmWatch) {
        c->Dr0 = (uintptr_t)g_watchX;
        c->Dr7 = 1 | (1 << 16) | (3 << 18);  // L0, break on write, 4 bytes
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && (c->Dr6 & 1)) {
        c->Dr6 = 0;
        if (g_watchHits++ < 3000) {
            char callers[96] = "";
            if (c->Eip >= 0x520000) {
                const uint32_t* sp = (const uint32_t*)c->Esp;
                size_t n = 0;
                for (int i = 0; i < 24 && n < sizeof callers - 10; i++)
                    if (sp[i] >= 0x401000 && sp[i] < 0x520000) n += snprintf(callers + n, sizeof callers - n, " %08x", sp[i]);
            }
            Log("pedw %u hw eip %08lx x %.4f%s", kGameTime(), (unsigned long)c->Eip, *g_watchX, callers);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int __fastcall ApplyFrame(uintptr_t c, int edx, int arg) {
    return Watched("apply", o_ApplyFrame, c, edx, arg, __builtin_return_address(0));
}
int __fastcall SetFacing(uintptr_t c, int edx, int arg) {
    return Watched("facing", o_SetFacing, c, edx, arg, __builtin_return_address(0));
}
int __fastcall GroundSnap(uintptr_t c, int edx, int arg) {
    return Watched("ground", o_GroundSnap, c, edx, arg, __builtin_return_address(0));
}

// C2HD_SLOWMO=k (with PEDLOG): from the first race frame the game's clock
// (PDGetTotalTime 0x51d410, ms) runs k times slower, so a slow hidden run
// still sees short frames in game time (the ped twitch needs ~10 ms).
unsigned (*o_Clock)();
unsigned g_slowFrom, g_slow;

unsigned Clock() {
    unsigned r = o_Clock();
    return g_slow && r > g_slowFrom ? g_slowFrom + (r - g_slowFrom) / g_slow : r;
}

void PedFrameLog() {
    static unsigned slow = (unsigned)atoi(Env("C2HD_SLOWMO").c_str());
    if (slow > 1 && !g_slow && o_Clock) {
        g_slowFrom = o_Clock();
        g_slow = slow;
        Log("test: game clock %u times slower from now", slow);
    }
    // C2HD_PEDWATCH=1: the nearest ped; =m<n>: the nearest ped in move n
    static std::string watchWhat = Env("C2HD_PEDWATCH");
    static bool watch = !watchWhat.empty();
    static int watchMove = watchWhat[0] == 'm' ? atoi(watchWhat.c_str() + 1) : -1;
    o_PedUpdate();
    uintptr_t peds = *(uintptr_t*)0x744808;
    int count = *(int*)0x7447d4;
    const float* cam = (const float*)0x74d484;
    struct Near { float d; uintptr_t p; } best[2] = {{1e9f, 0}, {1e9f, 0}};
    for (int i = 0; peds && i < count; i++) {
        uintptr_t p = peds + i * 0x54, c = *(uintptr_t*)p;
        if (!c || !(*(uint16_t*)(p + 8) & 1)) continue;
        const float* t = kBoneMatrix0(c);
        if (!t) continue;
        float dx = t[9] - cam[0], dy = t[10] - cam[1], dz = t[11] - cam[2], d = dx * dx + dy * dy + dz * dz;
        if (d < best[0].d) best[1] = best[0], best[0] = {d, p};
        else if (d < best[1].d) best[1] = {d, p};
    }
    unsigned now = kGameTime();
    static int bodyDumps;
    static unsigned firstRace;
    if (!firstRace) firstRace = now;
    static const unsigned kDumpAt[3] = {0, 4000, 9000};  // game ms into the race
    if (bodyDumps < 3 && now >= firstRace + kDumpAt[bodyDumps]) {
        // The physics body list (0x74a5f0, next +0x220): asleep +0xec, matrix
        // source +0xf0, kind +0x238, back-pointer +0x23c, actor +0
        bodyDumps++;
        int n = 0;
        for (uintptr_t b = *(uintptr_t*)0x74a5f0; b && n < 400; b = *(uintptr_t*)(b + 0x220), n++)
            Log("body %d %08lx asleep %d f0 %d kind %d back %08lx actor %08lx watched %d", n, (unsigned long)b,
                *(uint8_t*)(b + 0xec), *(int*)(b + 0xf0), *(int*)(b + 0x238), (unsigned long)*(uint32_t*)(b + 0x23c),
                (unsigned long)*(uint32_t*)b,
                *(uint32_t*)b == (uint32_t)((uintptr_t)g_watchX - 0x24 - 0x2c) || *(uint32_t*)(b + 0x23c) == g_watchChar);
    }
    if (watch && !g_watchPed && now > 15000) {
        float d = 100;
        for (int i = 0; peds && i < count; i++) {
            uintptr_t p = peds + i * 0x54, c = *(uintptr_t*)p;
            if (!c || !(*(uint16_t*)(p + 8) & 1) || (watchMove >= 0 && *(int8_t*)(c + 7) != watchMove)) continue;
            const float* t = kBoneMatrix0(c);
            float dx = t[9] - cam[0], dz = t[11] - cam[2];
            if (dx * dx + dz * dz < d) d = dx * dx + dz * dz, g_watchPed = p;
        }
        if (g_watchPed) {
            g_watchChar = *(uintptr_t*)g_watchPed;
            g_watchX = kBoneMatrix0(g_watchChar) + 9;
            Log("pedw: watching ped %08lx (move %d)", (unsigned long)g_watchPed, *(int8_t*)(g_watchChar + 7));
            AddVectoredExceptionHandler(1, &WatchHandler);
            RaiseException(kArmWatch, 0, 0, nullptr);
        }
    }
    if (g_watchX) {
        uintptr_t c = g_watchChar;
        Log("pedwf %u fr %d frac %.2f x %.4f", now, *(int16_t*)(c + 0x1c), *(float*)(c + 0x18), *g_watchX);
    }
    for (const Near& n : best) {
        if (!n.p || n.d > 400) continue;  // within 20 units
        uintptr_t c = *(uintptr_t*)n.p, q = *(uintptr_t*)(n.p + 0xc);
        const float* t = kBoneMatrix0(c);
        const float* leg = kBoneMatrix(c, 2);
        const float* f = (const float*)(c + 0xc0);
        Log("pedf %u %08lx move %d fr %d frac %.2f msf %d face %.3f %.3f %.3f torso %.3f %.3f %.3f leg %.3f %.3f %.3f "
            "av %d phys %d state %d slot %d e8 %08lx m %p",
            now, (unsigned long)n.p, *(int8_t*)(c + 7), *(int16_t*)(c + 0x1c), *(float*)(c + 0x18),
            *(int16_t*)(c + 0x1e), f[0], f[1], f[2], t[9], t[10], t[11], leg ? leg[9] : 0.f, leg ? leg[10] : 0.f,
            leg ? leg[11] : 0.f, q ? *(int*)(q + 0x18) : -1, (*(uint8_t*)(c + 0x14) & 0x10) != 0, *(int*)(n.p + 0x10),
            *(int8_t*)(c + 4), (unsigned long)*(uint32_t*)(c + 0xe8), (const void*)t);
    }
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
        static const uint8_t update[12] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0xb8, 0x03, 0x00, 0x00};
        o_PedUpdate = (void (*)())hook::Detour(0x4d3740, update, 12, (const void*)&PedFrameLog);
        Log("test: nearest pedestrians each frame to the log: %s", o_PedUpdate ? "on" : "FAILED");
        if (atoi(Env("C2HD_SLOWMO").c_str()) > 1) {
            static const uint8_t clock[6] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x14};
            o_Clock = (unsigned (*)())hook::Detour(0x51d410, clock, 6, (const void*)&Clock);
            Log("test: slow motion clock: %s", o_Clock ? "ready" : "FAILED");
        }
        if (!Env("C2HD_PEDWATCH").empty()) {
            static const uint8_t apply[5] = {0x83, 0xec, 0x4c, 0x53, 0x55};
            static const uint8_t facing[6] = {0x81, 0xec, 0x40, 0x01, 0x00, 0x00};
            static const uint8_t ground[5] = {0x83, 0xec, 0x18, 0x53, 0x55};
            o_ApplyFrame = (CharFn)hook::Detour(0x407b30, apply, 5, (const void*)&ApplyFrame);
            o_SetFacing = (CharFn)hook::Detour(0x40a730, facing, 6, (const void*)&SetFacing);
            o_GroundSnap = (CharFn)hook::Detour(0x40b0a0, ground, 5, (const void*)&GroundSnap);
            Log("test: torso writers watched: %s", o_ApplyFrame && o_SetFacing && o_GroundSnap ? "on" : "FAILED");
        }
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
    peds::Install();
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
