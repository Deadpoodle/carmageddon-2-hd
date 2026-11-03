// The D3D renderer driver (D3D.BDD) as BRender has mapped it. Found once, by
// the mode picker's code signature, before anything patches it, and cached;
// and its texture cache, enlarged for the features that need bigger textures.
#include "driver.h"

#include <windows.h>

#include <cstring>

#include "game.h"
#include "hook.h"

namespace driver {
namespace {

// True if every patch site of a driver mapped at base holds what we expect.
bool LooksLikeDriver(const uint8_t* base) {
    auto at = [base](uint32_t rva) {
        uint32_t v;
        memcpy(&v, base + rva, 4);
        return v;
    };
    const uint8_t* mov = base + game::DrvNo2DBufferSet;
    return at(game::DrvFormat512W) == 512 && at(game::DrvFormat512H) == 384 && at(game::DrvPickerWidth) == 640 &&
           at(game::DrvPickerHeight) == 480 && at(game::DrvModeWidth) == 640 && at(game::DrvModeHeight) == 480 &&
           at(game::DrvClipX) == 639 && mov[0] == 0x89 && mov[1] == 0x1d;
}

// The mapped driver, found by the mode picker's code signature. Copies of
// the file (our own check reads it; so does BRender's loader) hold the same
// code at other offsets, so each match is checked as a whole mapped image.
uint8_t* FindDriver() {
    static const uint8_t sig[] = {0xeb, 0x6f, 0x8b, 0x7e, 0x0c, 0xb8, 0x80, 0x02, 0x00,
                                  0x00, 0x3b, 0xf8, 0xb9, 0xe0, 0x01, 0x00, 0x00};
    MEMORY_BASIC_INFORMATION mbi;
    for (uint8_t* p = (uint8_t*)0x10000; p < (uint8_t*)0x7fff0000; p = (uint8_t*)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery(p, &mbi, sizeof mbi)) break;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || mbi.Type == MEM_IMAGE) continue;
        uint8_t* b = (uint8_t*)mbi.BaseAddress;
        for (size_t i = 0; i + sizeof sig <= mbi.RegionSize; i++) {
            if (b[i] != sig[0] || memcmp(b + i, sig, sizeof sig) != 0) continue;
            uint8_t* base = b + i - game::DrvSigRva;
            uintptr_t end = (uintptr_t)base + game::DrvFormat512H + 4;
            if (base < b || end > (uintptr_t)b + mbi.RegionSize) continue;  // the image must lie in this region
            if (LooksLikeDriver(base)) return base;
        }
    }
    return nullptr;
}

}  // namespace

uint8_t* Base() {
    static uint8_t* base = FindDriver();
    return base;
}

// Entries are {batches, work, slots per batch, w, h, allocated}; the slot
// builder (0x10004610) makes batches x per-batch surfaces of w x h, and a
// texture binds to the slot size that fits it best (0x10003bd0), halving until
// one does, so the 16x16 class must stay. 0x10004610 copies batches into work
// and counts allocated itself: only the first five fields are set. The slot
// array has room for 2048 slots; this makes 544 (64 + 160 + 126 + 96 + 2 + 32),
// about 15 MB of 16-bit surfaces.
bool PatchTextureCache() {
    static int state = -1;
    if (state >= 0) return state == 1;
    state = 0;
    uint8_t* base = Base();
    if (!base) return false;
    const uintptr_t sq128 = (uintptr_t)base + 0x158e0, wide128 = (uintptr_t)base + 0x15910;
    static const uint32_t sqWas[5] = {2, 0, 6, 128, 128}, sqNow[5] = {16, 0, 6, 128, 128};
    static const uint32_t wideWas[5] = {1, 0, 2, 128, 64}, wideNow[5] = {16, 0, 2, 256, 256};
    // Both or neither.
    if (!hook::Matches(sq128, (const uint8_t*)sqWas, 20) || !hook::Matches(wide128, (const uint8_t*)wideWas, 20))
        return false;
    state = hook::Patch(sq128, (const uint8_t*)sqWas, (const uint8_t*)sqNow, 20) &&
            hook::Patch(wide128, (const uint8_t*)wideWas, (const uint8_t*)wideNow, 20);
    return state == 1;
}

}  // namespace driver
