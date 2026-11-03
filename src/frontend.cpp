// The front end (menus, the in-race pause menu, loading and message screens,
// Smacker movies) centred in a wide back buffer, with bars at the sides
// (re/frontend-centre.md). Its three drawing primitives all take their x from
// the back screen's origin_x, so inside a "centring scope" each draw onto the
// back screen runs with origin_x += xo, xo = (W - 640) / 2. Cameras that were
// 640.0 wide become W wide, so 640 menu units stay 1:1 pixels. The bars need
// nothing: the driver clears the whole mode every frame.
//
// The mouse: client pixels are mapped to the W x 480 frame the way dgVoodoo2
// shows it (stretched_ar), then into menu space inside a scope. Test runs
// drive the same path: the exe's GetCursorPos/ScreenToClient answer a
// scripted point in the client of the emulated full-screen window (the
// output size), and Sweep() hovers every item of the current screen.
#include "frontend.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "headless.h"
#include "hook.h"
#include "log.h"
#include "settings.h"
#include "version.h"

namespace frontend {

int g_w = 640, g_xo;
volatile LONG g_scope;    // > 0 while a front-end loop or screen runs
volatile LONG g_capture;  // > 0 while the Controls screen waits for a key

namespace {

uintptr_t* const kBackScreen = (uintptr_t*)0x74d360;  // br_pixelmap* gBack_screen
constexpr int kOriginX = 0x38, kOriginY = 0x3a;      // br_pixelmap origin_x, origin_y (int16)
HWND* const kWindow = (HWND*)0x6ad4c8;

int16_t& BackOriginX() { return *(int16_t*)(*kBackScreen + kOriginX); }
int16_t& BackOriginY() { return *(int16_t*)(*kBackScreen + kOriginY); }

}  // namespace
}  // namespace frontend

// ---------------------------------------------------------------------------
// Scope thunks: one per wrapped function, whatever its calling convention.
// On entry the real return address and the thunk's counter are pushed onto a
// private stack, the counter is raised and the return address is replaced by
// the exit path, which lowers the counter and jumps back. Registers and stack
// arguments pass through untouched; the game is single-threaded here, and
// nested scopes (a pause menu over a Smacker...) stack up.
extern "C" {
uintptr_t c2hd_scopeRets[64];
volatile LONG* c2hd_scopeCounters[64];
volatile LONG c2hd_scopeTop;
uintptr_t c2hd_scopeJump;
void* c2hd_scopeTramp[5];
volatile LONG* c2hd_scopeCounter[5];
}

#define SCOPE_THUNK(n)                                                    \
    asm(".intel_syntax noprefix\n"                                        \
        ".globl _c2hd_scope" #n "\n"                                      \
        "_c2hd_scope" #n ":\n"                                            \
        "  push eax\n"                                                    \
        "  push ecx\n"                                                    \
        "  mov ecx, dword ptr [_c2hd_scopeTop]\n"                         \
        "  mov eax, dword ptr [esp+8]\n"                                  \
        "  mov dword ptr [_c2hd_scopeRets+ecx*4], eax\n"                  \
        "  mov eax, dword ptr [_c2hd_scopeCounter+" #n "*4]\n"            \
        "  mov dword ptr [_c2hd_scopeCounters+ecx*4], eax\n"              \
        "  lock inc dword ptr [eax]\n"                                    \
        "  inc dword ptr [_c2hd_scopeTop]\n"                              \
        "  mov dword ptr [esp+8], offset _c2hd_scopeExit\n"               \
        "  pop ecx\n"                                                     \
        "  pop eax\n"                                                     \
        "  jmp dword ptr [_c2hd_scopeTramp+" #n "*4]\n"                   \
        ".att_syntax\n");

SCOPE_THUNK(0)
SCOPE_THUNK(1)
SCOPE_THUNK(2)
SCOPE_THUNK(3)
SCOPE_THUNK(4)

asm(".intel_syntax noprefix\n"
    "_c2hd_scopeExit:\n"
    "  push eax\n"
    "  push ecx\n"
    "  dec dword ptr [_c2hd_scopeTop]\n"
    "  mov ecx, dword ptr [_c2hd_scopeTop]\n"
    "  mov eax, dword ptr [_c2hd_scopeCounters+ecx*4]\n"
    "  lock dec dword ptr [eax]\n"
    "  mov eax, dword ptr [_c2hd_scopeRets+ecx*4]\n"
    "  mov dword ptr [_c2hd_scopeJump], eax\n"
    "  pop ecx\n"
    "  pop eax\n"
    "  jmp dword ptr [_c2hd_scopeJump]\n"
    ".att_syntax\n");

extern "C" void c2hd_scope0();
extern "C" void c2hd_scope1();
extern "C" void c2hd_scope2();
extern "C" void c2hd_scope3();
extern "C" void c2hd_scope4();

namespace frontend {
namespace {

// ---------------------------------------------------------------------------
// Draw wrappers (call sites redirected; each calls the original target)

typedef int(__cdecl* Render3D_t)(void* world, void* camera, void* pm, void* depth);
typedef int(__fastcall* Copy2D_t)(void* dst, int dx, int dy, void* src, int sx, int sy, int w, int h);
typedef void*(__cdecl* AllocateSub_t)(void* pm, int x, int y, int w, int h);

struct Site3D {
    uintptr_t site;
    uintptr_t target;
};
Site3D g_sites3D[5] = {{0x522f55, 0}, {0x46575e, 0}, {0x470988, 0}, {0x4651dc, 0}, {0x43ef64, 0}};
uintptr_t g_copy2D = 0x5191b0;
uintptr_t g_allocateSub = 0x537d60;

// Movies (site 4, PlaySmackerFile 0x43ef64: 640x384 of tiles under the 2D
// camera) are enlarged to fill the screen's height, or its width if that is
// narrower ([Visuals] MovieSize = fill, the default; "original" leaves them):
// the camera's view (br_camera +0x14 width, +0x18 height) is narrowed by that
// factor for the movie's draw only. 1.25 at 16:9: 640x384 becomes 800x480.
// The 2D camera's view starts at the menu's top-left, so the picture grows
// from there; the back screen's origin moves it back by half the growth of
// the 640x480 frame (80 left, 60 up at 1.25), centring it.
float g_movieZoom = 1;

template <int I>
int __cdecl Render3D(void* world, void* camera, void* pm, void* depth) {
    bool shift = g_scope > 0 && (uintptr_t)pm == *kBackScreen;
    float* view = nullptr;
    float saved[2] = {0, 0};
    int16_t dx = shift ? (int16_t)g_xo : 0, dy = 0;
    if (I == 4 && g_movieZoom > 1 && camera && shift) {
        view = (float*)(*(uint8_t**)((uint8_t*)camera + 0x5c) + 0x14);
        saved[0] = view[0], saved[1] = view[1];
        view[0] /= g_movieZoom, view[1] /= g_movieZoom;
        dx -= (int16_t)(320 * (g_movieZoom - 1) + 0.5f);
        dy -= (int16_t)(240 * (g_movieZoom - 1) + 0.5f);
    }
    if (shift) BackOriginX() += dx, BackOriginY() += dy;
    int r = reinterpret_cast<Render3D_t>(g_sites3D[I].target)(world, camera, pm, depth);
    if (shift) BackOriginX() -= dx, BackOriginY() -= dy;
    if (view) view[0] = saved[0], view[1] = saved[1];
    return r;
}

// Full-screen 640x480 pictures (menu backdrops, the Controls screen's, the
// loading screen) are stretched to the buffer's width instead of sitting
// between bars ([Display] Backdrops = stretch, the default; "bars" keeps the
// original look). The stretched copy is made once per picture, linear across
// for 16-bit pictures, and checked against the picture's pixels each time
// (the game frees and reloads backdrops, and a new one can take the address).
const auto kAllocate = (uintptr_t(__cdecl*)(int, int, int, void*, int))0x538d80;  // BrPixelmapAllocate
bool g_stretchBackdrops;

uint8_t* PmPixels(uintptr_t pm) { return *(uint8_t**)(pm + 0x08); }
int PmRowBytes(uintptr_t pm) { return *(int16_t*)(pm + 0x28); }
int PmType(uintptr_t pm) { return *(uint8_t*)(pm + 0x2c); }
int PmWidth(uintptr_t pm) { return *(uint16_t*)(pm + 0x34); }
int PmHeight(uintptr_t pm) { return *(uint16_t*)(pm + 0x36); }

uint32_t Sample(uintptr_t pm) {  // a fingerprint of the picture: 97 pixels across it
    const uint8_t* px = PmPixels(pm);
    int rb = PmRowBytes(pm);
    uint32_t h = 2166136261u;
    for (int i = 0; i < 97; i++) {
        uint32_t v = *(const uint16_t*)(px + (i * 37 % 480) * rb + (i * 151 % 640) * 2);
        h = (h ^ v) * 16777619u;
    }
    return h;
}

struct Stretched {
    uintptr_t pm;        // the W x 480 copy
    uint8_t* srcPixels;  // the picture it was made from
    uint32_t sample;
};
Stretched g_stretched[8];

uintptr_t StretchedCopy(uintptr_t src) {
    int type = PmType(src), bytes = PmRowBytes(src) / 640;
    uint32_t sample = Sample(src);
    for (Stretched& s : g_stretched)
        if (s.pm && s.srcPixels == PmPixels(src) && s.sample == sample) return s.pm;
    Stretched* slot = &g_stretched[0];
    for (Stretched& s : g_stretched)
        if (!s.pm) slot = &s;
    if (!slot->pm) slot->pm = kAllocate(type, g_w, 480, nullptr, 0);  // reused: the old copy is overwritten
    uintptr_t dst = slot->pm;
    if (!dst || PmType(dst) != type || PmWidth(dst) != g_w || PmRowBytes(dst) < g_w * bytes) {
        Log("frontend: no stretched backdrop (type %#x)", type);
        return 0;
    }
    for (int y = 0; y < 480; y++) {
        const uint8_t* in = PmPixels(src) + y * PmRowBytes(src);
        uint8_t* out = PmPixels(dst) + y * PmRowBytes(dst);
        for (int x = 0; x < g_w; x++) {
            int fx = x * 639 * 256 / (g_w - 1);  // 8.8 source position
            int i = fx >> 8, f = fx & 255, j = i < 639 ? i + 1 : i;
            if (bytes == 2) {
                uint32_t a = ((const uint16_t*)in)[i], b = ((const uint16_t*)in)[j];
                bool rgb565 = type == 5 || type == 0x12, rgb555 = type == 4;
                if (!rgb565 && !rgb555) {
                    ((uint16_t*)out)[x] = (uint16_t)(f < 128 ? a : b);
                    continue;
                }
                // Red, green and blue fields, blended separately.
                uint32_t gmask = rgb565 ? 0x07e0 : 0x03e0, rmask = rgb565 ? 0xf800 : 0x7c00;
                auto mix = [&](uint32_t m) { return ((((a & m) * (256 - f) + (b & m) * f) >> 8) & m); };
                ((uint16_t*)out)[x] = (uint16_t)(mix(rmask) | mix(gmask) | mix(0x001f));
            } else {
                for (int k = 0; k < bytes; k++)
                    out[x * bytes + k] = (uint8_t)((in[i * bytes + k] * (256 - f) + in[j * bytes + k] * f) >> 8);
            }
        }
    }
    slot->srcPixels = PmPixels(src);
    slot->sample = sample;
    const char* name = *(const char* const*)(src + 4);  // br_pixelmap identifier
    Log("frontend: backdrop %s stretched to %dx480 (type %#x)", name && !IsBadReadPtr(name, 1) ? name : "?", g_w,
        type);
    return dst;
}

int __fastcall Copy2D(void* dst, int dx, int dy, void* src, int sx, int sy, int w, int h) {
    uintptr_t s = (uintptr_t)src;
    // (The callers load the size with 16-bit moves: only the low halves count.)
    if (g_scope > 0 && g_stretchBackdrops && g_w > 640 && dx == 0 && dy == 0 && sx == 0 && sy == 0 &&
        (w & 0xffff) == 640 && (h & 0xffff) == 480 && s && PmWidth(s) == 640 && PmHeight(s) == 480 && PmPixels(s))
        if (uintptr_t wide = StretchedCopy(s))
            return reinterpret_cast<Copy2D_t>(g_copy2D)(dst, 0, 0, (void*)wide, 0, 0, g_w, 480);
    if (g_scope > 0) dx += g_xo;
    return reinterpret_cast<Copy2D_t>(g_copy2D)(dst, dx, dy, src, sx, sy, w, h);
}

void* __cdecl WrecksPreview(void* pm, int x, int y, int w, int h) {
    return reinterpret_cast<AllocateSub_t>(g_allocateSub)(pm, x + g_xo, y, w, h);
}

uintptr_t CallTarget(uintptr_t site) {
    if (*(uint8_t*)site != 0xe8) return 0;
    return site + 5 + *(int32_t*)(site + 1);
}

// ---------------------------------------------------------------------------
// Mouse

typedef void(__fastcall* GetMouse_t)(int* x, int* y);
typedef int(__fastcall* GetDoubleClick_t)(int* x, int* y);
GetMouse_t o_GetMouse;
GetDoubleClick_t o_GetDoubleClick;
LONG g_mouseLogged;

// The window's client size. Test windows are hidden and small, so tests use
// the client a full-screen window would have: the output size.
bool ClientSize(double* cw, double* ch) {
    if (headless::Active()) {
        int w, h;
        settings::OutputSize(&w, &h);
        *cw = w;
        *ch = h;
        return true;
    }
    RECT rc;
    if (!GetClientRect(*kWindow, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    *cw = rc.right;
    *ch = rc.bottom;
    return true;
}

// The frame (W x 480) as dgVoodoo2's stretched_ar shows it in the client:
// scale and top-left offset.
bool Frame(double* s, double* ox, double* oy) {
    double cw, ch;
    if (!ClientSize(&cw, &ch)) return false;
    *s = cw / g_w < ch / 480.0 ? cw / g_w : ch / 480.0;
    *ox = (cw - g_w * *s) / 2;
    *oy = (ch - 480 * *s) / 2;
    return true;
}

void MapMouse(int* x, int* y) {
    double s, ox, oy;
    if (!Frame(&s, &ox, &oy)) return;
    int fx = (int)((*x - ox) / s), fy = (int)((*y - oy) / s);
    if (g_scope > 0) fx -= g_xo;
    if (!InterlockedExchange(&g_mouseLogged, 1)) {
        double cw, ch;
        ClientSize(&cw, &ch);
        Log("frontend: client %.0fx%.0f, mouse %d,%d -> %d,%d", cw, ch, *x, *y, fx, fy);
    }
    *x = fx;
    *y = fy;
}

void __fastcall GetMouse(int* x, int* y) {
    o_GetMouse(x, y);
    MapMouse(x, y);
}

// The game's GetMousePos (0x483c10) clamps to 0 .. the mode's width and
// height, in buffer terms; menus use menu space (buffer - xo), where that let
// the cursor into the right bar but not the left. Same logic, with the menu
// bounds in menus: -xo .. W - xo.
void __fastcall GetMousePos(int* x, int* y) {
    GetMouse(x, y);  // 0x51c900, mapped
    const int32_t* mode = (const int32_t*)0x662214 + *(const int32_t*)0x762324 * 13;  // graf spec: width, height
    int lo = g_scope > 0 ? -g_xo : 0, hi = g_scope > 0 ? mode[0] - g_xo : mode[0];
    if (*x < lo) *x = lo;
    else if (*x > hi) *x = hi;
    if (*y < 0) *y = 0;
    else if (*y > mode[1]) *y = mode[1];
    *(int32_t*)0x6571f8 = *x;  // the last position (GetMouseButton compares with it)
    *(int32_t*)0x6571fc = *y;
}

// Test runs: the cursor, in client pixels of the emulated full-screen window.
volatile LONG g_testX = -1, g_testY = -1;
decltype(&GetCursorPos) o_GetCursorPos;
decltype(&ScreenToClient) o_ScreenToClient;

BOOL WINAPI h_GetCursorPos(POINT* p) {
    if (g_testX < 0) return o_GetCursorPos(p);
    p->x = g_testX;
    p->y = g_testY;
    return TRUE;
}

BOOL WINAPI h_ScreenToClient(HWND hwnd, POINT* p) {
    if (g_testX < 0 || hwnd != *kWindow) return o_ScreenToClient(hwnd, p);
    return TRUE;  // the scripted point is already in the client
}

// A point in menu space to the client pixel at its centre.
bool MenuToClient(int mx, int my, int* cx, int* cy) {
    double s, ox, oy;
    if (!Frame(&s, &ox, &oy)) return false;
    *cx = (int)(ox + (mx + g_xo + 0.5) * s);
    *cy = (int)(oy + (my + 0.5) * s);
    return true;
}

int __fastcall GetDoubleClick(int* x, int* y) {
    int r = o_GetDoubleClick(x, y);
    if (r) MapMouse(x, y);
    return r;
}

int g_failures;

void Check(bool ok, const char* what) {
    if (!ok) {
        Log("frontend: %s FAILED", what);
        g_failures++;
    }
}

}  // namespace

bool InMenu() { return g_scope > 0; }

void SetVirtualCursor(int x, int y) {
    int cx, cy;
    if (!MenuToClient(x, y, &cx, &cy)) return;
    SetTestCursor(cx, cy);
}

void SetTestCursor(int x, int y) {
    g_testY = y;
    g_testX = x;
}

// The focused item's name on the current screen ("" when none).
std::string FocusName() {
    const uint8_t* screen = *(const uint8_t* const*)0x688abc;
    int f = *(const volatile int*)0x688770;
    if (!screen || f < 0 || f >= 99) return "";
    return (const char*)screen + 0x130 + f * 0x158 + 0x59;
}

bool SameName(std::string a, std::string b) {
    for (std::string* s : {&a, &b})
        for (char& c : *s) c = c == '_' ? ' ' : (char)toupper((unsigned char)c);
    return a == b;
}

void KeySweep(void (*press)(BYTE dik), BYTE dik, const char* keyName) {
    const uint8_t* screen = *(const uint8_t* const*)0x688abc;
    if (!screen || g_scope <= 0) {
        Log("keysweep: no front-end screen");
        return;
    }
    char name[64];
    snprintf(name, sizeof name, "%s", (const char*)screen);
    int count = *(const int*)(screen + 0x104);
    auto item = [&](int i) { return (const int32_t*)(screen + 0x130 + i * 0x158); };
    bool visited[99] = {};
    std::string order;
    for (int n = 0; n < count + 4; n++) {
        press(dik);
        if (*(const uint8_t* const*)0x688abc != screen) {
            Log("keysweep: %s: the screen changed while pressing %s", name, keyName);
            return;
        }
        int f = *(const volatile int*)0x688770;
        if (f >= 0 && f < 99) {
            visited[f] = true;
            order += " " + std::to_string(f);
        }
    }
    // Reached: visited, or in a row (radio group) that focus reached.
    bool groupHit[64] = {};
    for (int i = 0; i < count && i < 99; i++)
        if (visited[i] && item(i)[16] > 0 && item(i)[16] < 64) groupHit[item(i)[16]] = true;
    int focusable = 0, missed = 0;
    std::string missing;
    for (int i = 0; i < count && i < 99; i++) {
        const int32_t* it = item(i);
        if (it[10] <= 0 || !it[11]) continue;
        focusable++;
        if (visited[i] || (it[16] > 0 && it[16] < 64 && groupHit[it[16]])) continue;
        missed++;
        missing += " " + std::to_string(i) + "\"" + std::string((const char*)it + 0x59) + "\"";
    }
    Log("keysweep: %s with %s: focus%s", name, keyName, order.c_str());
    Log("keysweep: %s: %d focusable, %d not reachable by %s%s%s", name, focusable, missed, keyName,
        missed ? ":" : "", missing.c_str());
}

bool GoTo(void (*press)(BYTE dik), const char* target) {
    for (int n = 0; n < 99; n++) {
        if (SameName(FocusName(), target)) {
            Log("goto: %s after %d presses", target, n);
            return true;
        }
        press(0xd0);  // DIK_DOWN
    }
    Log("goto: %s not reached by Down", target);
    return false;
}

// Diagnostic (C2HD_FLOWLOG=1): the main menu's outcome as the race flow sees it.
void (*o_MainMenu)();
void MainMenuLogged() {
    int inProgress = *(volatile int*)0x75bbac;
    o_MainMenu();
    Log("flow: main menu done: game in progress %d -> state %d (0x75bc24), g_fe_exit %d", inProgress,
        *(volatile int*)0x75bc24, *(volatile int*)0x6883a8);
}

// ...and every change of screen, focused item and mouse mode, polled.
DWORD WINAPI FocusLog(void*) {
    const volatile uintptr_t* screen = (const volatile uintptr_t*)0x688abc;
    const volatile int* focus = (const volatile int*)0x688770;
    const volatile int* mouseMode = (const volatile int*)0x79ecb8;
    uintptr_t lastScreen = 1;
    int lastFocus = -2, lastMode = -1;
    for (;;) {
        Sleep(10);
        uintptr_t sc = *screen;
        int f = *focus, m = *mouseMode;
        if (sc == lastScreen && f == lastFocus && m == lastMode) continue;
        char name[32] = "-";
        if (sc) snprintf(name, sizeof name, "%s", (const char*)sc);
        const char* item = sc && f >= 0 && f < 99 ? (const char*)(sc + 0x130 + f * 0x158 + 0x59) : "";
        Log("flow: screen %s, focus %d \"%s\", mouse mode %d", name, f, item, m);
        lastScreen = sc, lastFocus = f, lastMode = m;
    }
}

void InstallFlowLog() {
    CloseHandle(CreateThread(nullptr, 0, &FocusLog, nullptr, 0, nullptr));
    o_MainMenu = (void (*)())0x494540;
    Log("flow log: %s", hook::Redirect(0x503d68, 0xe8, 0x494540, (const void*)&MainMenuLogged) ? "on" : "FAILED");
}

void InstallTestCursor() {
    bool ok = hook::Import(hook::FindImport(nullptr, "USER32.dll", "GetCursorPos"), &h_GetCursorPos, &o_GetCursorPos) &&
              hook::Import(hook::FindImport(nullptr, "USER32.dll", "ScreenToClient"), &h_ScreenToClient,
                           &o_ScreenToClient);
    double cw = 0, ch = 0;
    ClientSize(&cw, &ch);
    SetTestCursor(0, 0);  // parked in the corner until a script moves it
    Log("frontend: test cursor in a %.0fx%.0f client (%s)", cw, ch, ok ? "ok" : "hooks FAILED");
}

// Hover the centre of every visible, focusable item of the current screen
// through the real mouse path and check the game focuses that item; then the
// client's corners, which must reach the frame's corners.
void Sweep() {
    const uint8_t* screen = *(const uint8_t* const*)0x688abc;  // the current front-end screen
    const volatile int* focus = (const volatile int*)0x688770;
    if (!screen || g_scope <= 0) {
        Log("sweep: no front-end screen");
        return;
    }
    char name[64];
    snprintf(name, sizeof name, "%s", (const char*)screen);
    int count = *(const int*)(screen + 0x104), tried = 0, failed = 0, shared = 0;
    auto item = [&](int i) { return (const int32_t*)(screen + 0x130 + i * 0x158); };
    for (int i = 0; i < count && i < 99; i++) {
        const int32_t* it = item(i);
        int x = it[6], y = it[7], w = it[8], h = it[9], state = it[10], visible = it[11];
        if (state <= 0 || !visible || w <= 0 || h <= 0) continue;
        tried++;
        int mx = x + w / 2, my = y + h / 2;
        SetVirtualCursor(mx, my);
        Sleep(150);  // a few menu frames (48 fps)
        int got = *focus;
        if (got == i) continue;
        // Another item over the same point (scroll arrows, or a volume
        // slider, whose bar is wider than its item) takes the hover: the
        // game's own layout, not a mapping fault.
        if (got >= 0 && got < count) {
            const int32_t* o = item(got);
            bool slider = o[17] != 0;  // +0x44 level bar
            if (slider || (mx >= o[6] && mx <= o[6] + o[8] && my >= o[7] && my <= o[7] + o[9])) {
                shared++;
                continue;
            }
        }
        failed++;
        Log("sweep: %s item %d \"%s\" at %d,%d (%dx%d): focus %d", name, i, (const char*)it + 0x59, mx, my, w, h,
            got);
    }
    double cw, ch, s, ox, oy;
    ClientSize(&cw, &ch);
    Frame(&s, &ox, &oy);
    // The client's corners must reach the frame's (in menu space, -xo,0 and
    // 640+xo-1,479; beyond them in the bars of a wider screen).
    int corners[2][2] = {{0, 0}, {(int)cw - 1, (int)ch - 1}};
    for (int k = 0; k < 2; k++) {
        int x = corners[k][0], y = corners[k][1];
        MapMouse(&x, &y);
        bool ok = k == 0 ? x <= -g_xo && y <= 0 : x >= 640 + g_xo - 1 && y >= 479;
        if (!ok) failed++;
        Log("sweep: client corner %d,%d -> menu %d,%d%s", corners[k][0], corners[k][1], x, y, ok ? "" : " FAILED");
    }
    Log("sweep: %s: %d items hovered, %d under another item, %d failed (client %.0fx%.0f, frame %dx480 at %.0f,%.0f, "
        "x%.3f)", name, tried, shared, failed, cw, ch, g_w, ox, oy, s);
}
bool InKeyCapture() { return g_capture > 0; }

void Install(int w) {
    g_w = w;
    g_xo = (w - 640) / 2;

    // Scopes: the front-end loop (also the pause menu and key capture), Smacker
    // movies, the loading screen, the message screen; and the Controls screen's
    // wait for a new key (the input layer passes real keys through there).
    struct Scope {
        uintptr_t fn;
        uint8_t bytes[8];
        size_t n;
        void (*thunk)();
    } scopes[5] = {
        {0x46d8e0, {0xa1, 0x28, 0x23, 0x76, 0x00}, 5, &c2hd_scope0},
        {0x43e770, {0x81, 0xec, 0x2c, 0x01, 0x00, 0x00}, 6, &c2hd_scope1},
        {0x47b990, {0x53, 0x56, 0x57, 0x8b, 0xf9}, 5, &c2hd_scope2},
        {0x4a0080, {0x83, 0xec, 0x10, 0x53, 0x55}, 5, &c2hd_scope3},
        {0x472b00, {0x83, 0xec, 0x0c, 0xa1, 0x70, 0x87, 0x68, 0x00}, 8, &c2hd_scope4},  // Controls_GetNewKey
    };
    for (int i = 0; i < 5; i++) {
        c2hd_scopeCounter[i] = i == 4 ? &g_capture : &g_scope;
        c2hd_scopeTramp[i] = hook::Detour(scopes[i].fn, scopes[i].bytes, scopes[i].n, (const void*)scopes[i].thunk);
        Check(c2hd_scopeTramp[i] != nullptr, "scope detour");
    }

    // Cameras 640.0 wide become W wide (heights stay 480).
    float wf = (float)w;
    uint32_t wbits;
    memcpy(&wbits, &wf, 4);
    for (uintptr_t a : {0x46cf4f, 0x469d5f, 0x46f7f9}) Check(hook::PatchU32(a, 0x44200000, wbits), "camera width");

    // 3D renders onto the back screen, shifted by xo.
    void* wrappers[5] = {(void*)&Render3D<0>, (void*)&Render3D<1>, (void*)&Render3D<2>, (void*)&Render3D<3>,
                         (void*)&Render3D<4>};
    for (int i = 0; i < 5; i++) {
        g_sites3D[i].target = CallTarget(g_sites3D[i].site);
        Check(g_sites3D[i].target && hook::Redirect(g_sites3D[i].site, 0xe8, g_sites3D[i].target, wrappers[i]),
              "3D call site");
    }
    // 2D copies, and the Wrecks preview window.
    for (uintptr_t site : {0x46d996, 0x472bdd, 0x47ba42})
        Check(hook::Redirect(site, 0xe8, g_copy2D, (const void*)&Copy2D), "2D call site");
    Check(hook::Redirect(0x46e928, 0xe8, g_allocateSub, (const void*)&WrecksPreview), "Wrecks preview");

    // The version label is right-aligned to 640 (menu space), not the buffer.
    Check(hook::Patch(0x46a166, (const uint8_t*)"\x66\x8b\x50\x34", (const uint8_t*)"\x66\xba\x80\x02", 4),
          "version label");
    Check(hook::Patch(0x46a1a0, (const uint8_t*)"\x66\x8b\x4a\x34", (const uint8_t*)"\x66\xb9\x80\x02", 4),
          "version label");
    // ...and names this patch after the game's own label (0x65574c "v1.02a",
    // the build the official Super Patch V2.0 shipped); the font capitalises.
    static char label[64];
    snprintf(label, sizeof label, "%s   carma2-hd-%s", (const char*)0x65574c, C2HD_VERSION);
    for (uintptr_t imm : {0x46a184, 0x46a1a5})  // mov edx, 0x65574c (width, then draw)
        Check(hook::PatchU32(imm, 0x65574c, (uint32_t)(uintptr_t)label), "version label");

    // Mouse.
    static const uint8_t getMouse[5] = {0xa1, 0xe0, 0x21, 0x66, 0x00};
    static const uint8_t getDouble[7] = {0x56, 0x8b, 0x35, 0x54, 0xd4, 0x6a, 0x00};
    o_GetMouse = (GetMouse_t)hook::Detour(0x51c900, getMouse, 5, (const void*)&GetMouse);
    o_GetDoubleClick = (GetDoubleClick_t)hook::Detour(0x51db00, getDouble, 7, (const void*)&GetDoubleClick);
    Check(o_GetMouse && o_GetDoubleClick, "mouse detours");
    static const uint8_t getMousePos[6] = {0x56, 0x57, 0x8b, 0xfa, 0x8b, 0xf1};  // push esi; push edi; mov edi,edx; mov esi,ecx
    Check(hook::Detour(0x483c10, getMousePos, 6, (const void*)&GetMousePos) != nullptr, "mouse bounds");
    std::string backdrops = settings::Get("Display", "Backdrops", "stretch");
    g_stretchBackdrops = _stricmp(backdrops.c_str(), "bars") != 0;
    if (_stricmp(settings::Get("Visuals", "MovieSize", "fill").c_str(), "original") != 0)
        g_movieZoom = 480.0f / 384 < w / 640.0f ? 480.0f / 384 : w / 640.0f;

    Log("frontend: centred at +%d in %dx480 (%d failures)", g_xo, w, g_failures);
}

}  // namespace frontend
