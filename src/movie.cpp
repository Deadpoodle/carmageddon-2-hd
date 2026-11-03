// [Visuals] Movies=enhanced: the Smacker cutscenes enlarged 4x (or 2x) with
// Lanczos-2 and a light contrast-adaptive sharpen (re/movies.md).
//
// The game decodes each 320x192 frame to an 8-bit buffer and copies it into
// 15 tiles of 64x64 (8-bit, one shared palette), drawn as quads over the
// screen. Here the tiles are allocated as RGB565 at 64k x 64k, the quads' UV
// inset is set to half a texel of the new size, and the per-frame copy is
// replaced by ours: palette -> RGB, Lanczos-2 to 320k x 192k, sharpen, pack.
// 4x needs the driver's texture cache to have 256x256 slots (its 128x64
// entry, RVA 0x15910, becomes 32 of them: driver::PatchTextureCache); without
// that, 2x fits the existing 128x128 slots. The game's mesh, render, timing and audio are untouched.
#include "movie.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "driver.h"
#include "hook.h"
#include "loader.h"
#include "log.h"
#include "settings.h"

namespace movie {
namespace {

int g_k = 1;              // scale actually used for this movie
bool g_slotsPatched;      // the driver has 256x256 slots
uintptr_t g_allocate;     // BrPixelmapAllocate (from the call at 0x43ead8)
const auto kMapUpdate = (void(__cdecl*)(void*, int))0x523160;
const auto kMaterialUpdate = (void(__cdecl*)(void*, int))0x520e70;
uint8_t** const kFrame = (uint8_t**)0x67c3b0;       // the decoded 8-bit frame, 320 pitch
uintptr_t* const kPalette = (uintptr_t*)0x79eca8;   // palette pixelmap (256 x 0x00RRGGBB at +8)
uintptr_t* const kTiles = (uintptr_t*)0x79ec60;     // 15 materials; colour_map at +0x40
constexpr int kW = 320, kH = 192;

// ---------------------------------------------------------------------------
// The driver's texture cache: 16 slots of 256x256 for 4x

void PatchSlots(const char* name) {
    if (g_slotsPatched || strcmp(name, "DirectDrawCreate") != 0) return;
    if (!driver::Base()) return;
    g_slotsPatched = driver::PatchTextureCache();  // shared with sharptext.cpp
    Log("movie: driver texture cache 256x256 slots: %s", g_slotsPatched ? "ok" : "not patched (2x)");
}

// ---------------------------------------------------------------------------
// Scaling

float Lanczos2(float x) {
    if (x == 0) return 1;
    if (x <= -2 || x >= 2) return 0;
    const float pi = 3.14159265f;
    return 2 * std::sin(pi * x) * std::sin(pi * x / 2) / (pi * pi * x * x);
}

struct Kernel {
    int first[4096];
    float w[4096][4];
};

// Weights for enlarging n samples by k (centres aligned).
void BuildKernel(Kernel& kr, int n, int k) {
    for (int o = 0; o < n * k; o++) {
        float src = (o + 0.5f) / k - 0.5f;
        int f = (int)std::floor(src) - 1;
        float sum = 0;
        for (int t = 0; t < 4; t++) sum += kr.w[o][t] = Lanczos2(src - (f + t));
        for (int t = 0; t < 4; t++) kr.w[o][t] /= sum;
        kr.first[o] = f;
    }
}

Kernel g_kx, g_ky;
int g_kernelK;
std::vector<float> g_src, g_den, g_rgb, g_tmp;
std::vector<uint16_t> g_packed;

inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void Upscale(int k, bool rgb555) {
    if (g_kernelK != k) {
        BuildKernel(g_kx, kW, k);
        BuildKernel(g_ky, kH, k);
        g_kernelK = k;
    }
    const int W = kW * k, H = kH * k;
    const uint8_t* frame = *kFrame;
    const uint32_t* pal = *(const uint32_t**)(*kPalette + 8);
    // Palette -> RGB, then a light contrast-adaptive sharpen (CAS-like, on the
    // cross of neighbours) at the source size, where it costs 1/16 of doing
    // it after the enlargement.
    g_src.resize(kW * kH * 3);
    for (int i = 0; i < kW * kH; i++) {
        uint32_t c = pal[frame[(i / kW) * 320 + i % kW]];
        g_src[i * 3] = (float)((c >> 16) & 255);
        g_src[i * 3 + 1] = (float)((c >> 8) & 255);
        g_src[i * 3 + 2] = (float)(c & 255);
    }
    // Deband: the movies were dithered to 8 bits; average each pixel with the
    // neighbours that differ from it by less than a dither step, so the
    // stipple goes and edges stay.
    g_den.resize(kW * kH * 3);
    const float step = 28.0f;
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++)
            for (int ch = 0; ch < 3; ch++) {
                float c = g_src[(y * kW + x) * 3 + ch], sum = 2 * c, wsum = 2;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!dx && !dy) continue;
                        float v = g_src[(Clamp(y + dy, 0, kH - 1) * kW + Clamp(x + dx, 0, kW - 1)) * 3 + ch];
                        if (std::fabs(v - c) < step) sum += v, wsum += 1;
                    }
                g_den[(y * kW + x) * 3 + ch] = sum / wsum;
            }
    g_rgb.resize(kW * kH * 3);
    const float sharp = -1.0f / 8.0f;
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            const float* c = &g_den[(y * kW + x) * 3];
            const float* n = &g_den[(Clamp(y - 1, 0, kH - 1) * kW + x) * 3];
            const float* s = &g_den[(Clamp(y + 1, 0, kH - 1) * kW + x) * 3];
            const float* e = &g_den[(y * kW + Clamp(x + 1, 0, kW - 1)) * 3];
            const float* w = &g_den[(y * kW + Clamp(x - 1, 0, kW - 1)) * 3];
            for (int ch = 0; ch < 3; ch++) {
                float mn = std::fmin(c[ch], std::fmin(std::fmin(n[ch], s[ch]), std::fmin(e[ch], w[ch])));
                float mx = std::fmax(c[ch], std::fmax(std::fmax(n[ch], s[ch]), std::fmax(e[ch], w[ch])));
                float amp = mx > 0 ? std::sqrt(std::fmax(0.f, std::fmin(mn, 255 - mx) / mx)) : 0;
                float wgt = amp * sharp;
                g_rgb[(y * kW + x) * 3 + ch] = (c[ch] + wgt * (n[ch] + s[ch] + e[ch] + w[ch])) / (1 + 4 * wgt);
            }
        }
    g_tmp.resize(W * kH * 3);  // Lanczos-2, horizontal pass
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < W; x++) {
            float r = 0, g = 0, b = 0;
            for (int t = 0; t < 4; t++) {
                const float* sp = &g_rgb[(y * kW + Clamp(g_kx.first[x] + t, 0, kW - 1)) * 3];
                float wt = g_kx.w[x][t];
                r += sp[0] * wt, g += sp[1] * wt, b += sp[2] * wt;
            }
            float* d = &g_tmp[(y * W + x) * 3];
            d[0] = r, d[1] = g, d[2] = b;
        }
    // Vertical pass, straight into the packed 16-bit image. 0x0000 is the
    // driver's colour key (the game turns its palette's black into 0x040404
    // for that, before we see the palette, which packs to a faint green band);
    // here anything under one 16-bit step is 0: transparent, over the screen
    // the driver clears to black.
    g_packed.resize(W * H);
    for (int y = 0; y < H; y++) {
        const float* rows[4];
        for (int t = 0; t < 4; t++) rows[t] = &g_tmp[Clamp(g_ky.first[y] + t, 0, kH - 1) * W * 3];
        const float* wt = g_ky.w[y];
        for (int x = 0; x < W; x++) {
            int px[3];
            for (int ch = 0; ch < 3; ch++) {
                int i = x * 3 + ch;
                float v = rows[0][i] * wt[0] + rows[1][i] * wt[1] + rows[2][i] * wt[2] + rows[3][i] * wt[3];
                px[ch] = Clamp((int)(v + 0.5f), 0, 255);
            }
            uint16_t p = rgb555 ? (uint16_t)(((px[0] >> 3) << 10) | ((px[1] >> 3) << 5) | (px[2] >> 3))
                                : (uint16_t)(((px[0] >> 3) << 11) | ((px[1] >> 2) << 5) | (px[2] >> 3));
            g_packed[y * W + x] = px[0] < 8 && px[1] < 8 && px[2] < 8 ? 0 : p;
        }
    }
}

// Replaces the game's per-frame tile copy (0x43eeaa .. 0x43ef4f).
void __cdecl UploadFrame() {
    if (g_k <= 1) return;
    uint8_t* base = driver::Base();
    bool rgb555 = base && *(int*)(base + 0x3818c) == 2;
    DWORD t0 = GetTickCount();
    Upscale(g_k, rgb555);
    const int W = kW * g_k, T = 64 * g_k;
    for (int i = 0; i < 15; i++) {
        int r = i / 5, c = i % 5;
        uintptr_t mat = kTiles[i];
        uintptr_t pm = *(uintptr_t*)(mat + 0x40);
        uint16_t* px = *(uint16_t**)(pm + 8);
        int rowBytes = *(int16_t*)(pm + 0x28);
        for (int y = 0; y < T; y++)
            memcpy((uint8_t*)px + y * rowBytes, &g_packed[(r * T + y) * W + c * T], T * 2);
        kMapUpdate((void*)pm, 0xfff);
        kMaterialUpdate((void*)mat, 0x7fff);
    }
    static int frames, total;
    total += GetTickCount() - t0;
    if (++frames % 150 == 0) Log("movie: %d frames at %dx, %d ms per frame", frames, g_k, total / 150), total = 0;
}

// The tile allocation (once per movie): RGB565 at the scale this movie uses,
// and the quads' UV inset to half a texel of it.
void* __cdecl AllocateTile(int, int, int, void*, int) {
    static int calls;
    if (calls++ % 15 == 0) {
        g_k = g_slotsPatched ? 4 : 2;
        uint32_t lo = g_k == 4 ? 0x3b000000 : 0x3b800000, hi = g_k == 4 ? 0x3f7f8000 : 0x3f7f0000;
        static uint32_t curLo = 0x3c000000, curHi = 0x3f7e0000;
        bool ok = true;
        for (uintptr_t a : {0x43ebc1, 0x43ec2b, 0x43ec4b}) ok &= hook::PatchU32(a, curLo, lo);
        for (uintptr_t a : {0x43ebcc, 0x43ebf3, 0x43ebfe, 0x43ec20}) ok &= hook::PatchU32(a, curHi, hi);
        curLo = lo, curHi = hi;
        Log("movie: enhanced %dx (%s)", g_k, ok ? "ok" : "UV patch FAILED");
    }
    return reinterpret_cast<void*(__cdecl*)(int, int, int, void*, int)>(g_allocate)(5, 64 * g_k, 64 * g_k, nullptr, 0);
}

}  // namespace

void Install() {
    // A key skips a film (and ends the game's other "any key" waits) through
    // AnyKeyDown 0x482d70, which reads the keyboard only when 500 ms have
    // passed since the last read: nothing else reads it during a film, so a
    // press shorter than that was often missed. It now reads every 50 ms.
    bool skip = hook::PatchU32(0x482d8a, 500, 50);
    Log("movie: any-key checks every 50 ms %s", skip ? "on" : "hook FAILED");
    std::string mode = settings::Get("Visuals", "Movies", "enhanced");
    if (_stricmp(mode.c_str(), "enhanced") != 0) {
        Log("movie: original");
        return;
    }
    // The upload block: call ours, then jump past the game's copy.
    static const uint8_t block[13] = {0xc7, 0x44, 0x24, 0x1c, 0, 0, 0, 0, 0xbd, 0x60, 0xec, 0x79, 0x00};
    uint8_t code[13];
    code[0] = 0xe8;
    int32_t rel = (int32_t)((uintptr_t)&UploadFrame - (0x43eeaa + 5));
    memcpy(code + 1, &rel, 4);
    code[5] = 0xe9;
    rel = (int32_t)(0x43ef50 - (0x43eeaa + 10));
    memcpy(code + 6, &rel, 4);
    code[10] = code[11] = code[12] = 0x90;
    uintptr_t site = 0x43ead8;
    if (*(uint8_t*)site != 0xe8 || !hook::Matches(0x43eeaa, block, 13)) {
        Log("movie: playback code not as expected; movies stay original");
        return;
    }
    g_allocate = site + 5 + *(int32_t*)(site + 1);
    bool ok = hook::Redirect(site, 0xe8, g_allocate, (const void*)&AllocateTile) &&
              hook::Patch(0x43eeaa, block, code, 13);
    loader::OnResolve(&PatchSlots);
    Log("movie: enhanced movies %s", ok ? "on" : "hook FAILED");
}

}  // namespace movie
