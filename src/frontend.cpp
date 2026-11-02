// The front end (menus, the in-race pause menu, loading and message screens,
// Smacker movies) centred in a wide back buffer, with bars at the sides
// (re/frontend-centre.md). Its three drawing primitives all take their x from
// the back screen's origin_x, so inside a "centring scope" each draw onto the
// back screen runs with origin_x += xo, xo = (W - 640) / 2. Cameras that were
// 640.0 wide become W wide, so 640 menu units stay 1:1 pixels. The bars need
// nothing: the driver clears the whole mode every frame.
//
// The mouse: client pixels are mapped to the W x 480 frame the way dgVoodoo2
// shows it (stretched_ar), then into menu space inside a scope.
#include "frontend.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "hook.h"
#include "log.h"

namespace frontend {

int g_w = 640, g_xo;
volatile LONG g_scope;  // > 0 while a front-end loop or screen runs

namespace {

uintptr_t* const kBackScreen = (uintptr_t*)0x74d360;  // br_pixelmap* gBack_screen
constexpr int kOriginX = 0x38;                        // br_pixelmap origin_x (int16)
HWND* const kWindow = (HWND*)0x6ad4c8;

int16_t& BackOriginX() { return *(int16_t*)(*kBackScreen + kOriginX); }

}  // namespace
}  // namespace frontend

// ---------------------------------------------------------------------------
// Scope thunks: one per wrapped function, whatever its calling convention.
// On entry the real return address is pushed onto a private stack and
// replaced by the exit path; the exit path pops it and jumps back. Registers
// and stack arguments pass through untouched; the game is single-threaded
// here, and nested scopes (a pause menu over a Smacker...) stack up.
extern "C" {
uintptr_t c2hd_scopeRets[64];
volatile LONG c2hd_scopeTop;
uintptr_t c2hd_scopeJump;
void* c2hd_scopeTramp[4];
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
        "  inc dword ptr [_c2hd_scopeTop]\n"                              \
        "  lock inc dword ptr [__ZN8frontend7g_scopeE]\n"                 \
        "  mov dword ptr [esp+8], offset _c2hd_scopeExit\n"               \
        "  pop ecx\n"                                                     \
        "  pop eax\n"                                                     \
        "  jmp dword ptr [_c2hd_scopeTramp+" #n "*4]\n"                   \
        ".att_syntax\n");

SCOPE_THUNK(0)
SCOPE_THUNK(1)
SCOPE_THUNK(2)
SCOPE_THUNK(3)

asm(".intel_syntax noprefix\n"
    "_c2hd_scopeExit:\n"
    "  push eax\n"
    "  push ecx\n"
    "  lock dec dword ptr [__ZN8frontend7g_scopeE]\n"
    "  dec dword ptr [_c2hd_scopeTop]\n"
    "  mov ecx, dword ptr [_c2hd_scopeTop]\n"
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

template <int I>
int __cdecl Render3D(void* world, void* camera, void* pm, void* depth) {
    bool shift = g_scope > 0 && (uintptr_t)pm == *kBackScreen;
    if (shift) BackOriginX() += (int16_t)g_xo;
    int r = reinterpret_cast<Render3D_t>(g_sites3D[I].target)(world, camera, pm, depth);
    if (shift) BackOriginX() -= (int16_t)g_xo;
    return r;
}

int __fastcall Copy2D(void* dst, int dx, int dy, void* src, int sx, int sy, int w, int h) {
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

void MapMouse(int* x, int* y) {
    RECT rc;
    if (!GetClientRect(*kWindow, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    double cw = rc.right, ch = rc.bottom;
    double s = cw / g_w < ch / 480.0 ? cw / g_w : ch / 480.0;
    double ox = (cw - g_w * s) / 2, oy = (ch - 480 * s) / 2;
    int fx = (int)((*x - ox) / s), fy = (int)((*y - oy) / s);
    if (g_scope > 0) fx -= g_xo;
    if (!InterlockedExchange(&g_mouseLogged, 1))
        Log("frontend: client %ldx%ld, mouse %d,%d -> %d,%d", rc.right, rc.bottom, *x, *y, fx, fy);
    *x = fx;
    *y = fy;
}

void __fastcall GetMouse(int* x, int* y) {
    o_GetMouse(x, y);
    MapMouse(x, y);
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

void Install(int w) {
    g_w = w;
    g_xo = (w - 640) / 2;

    // Scopes: the front-end loop (also the pause menu and key capture), Smacker
    // movies, the loading screen, the message screen.
    struct Scope {
        uintptr_t fn;
        uint8_t bytes[8];
        size_t n;
        void (*thunk)();
    } scopes[4] = {
        {0x46d8e0, {0xa1, 0x28, 0x23, 0x76, 0x00}, 5, &c2hd_scope0},
        {0x43e770, {0x81, 0xec, 0x2c, 0x01, 0x00, 0x00}, 6, &c2hd_scope1},
        {0x47b990, {0x53, 0x56, 0x57, 0x8b, 0xf9}, 5, &c2hd_scope2},
        {0x4a0080, {0x83, 0xec, 0x10, 0x53, 0x55}, 5, &c2hd_scope3},
    };
    for (int i = 0; i < 4; i++) {
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

    // Mouse.
    static const uint8_t getMouse[5] = {0xa1, 0xe0, 0x21, 0x66, 0x00};
    static const uint8_t getDouble[7] = {0x56, 0x8b, 0x35, 0x54, 0xd4, 0x6a, 0x00};
    o_GetMouse = (GetMouse_t)hook::Detour(0x51c900, getMouse, 5, (const void*)&GetMouse);
    o_GetDoubleClick = (GetDoubleClick_t)hook::Detour(0x51db00, getDouble, 7, (const void*)&GetDoubleClick);
    Check(o_GetMouse && o_GetDoubleClick, "mouse detours");

    Log("frontend: centred at +%d in %dx480 (%d failures)", g_xo, w, g_failures);
}

}  // namespace frontend
