// The D3D renderer driver (D3D.BDD) as BRender has mapped it. Found once, by
// the mode picker's code signature, before anything patches it, and cached.
#include "driver.h"

#include <windows.h>

#include <cstring>

#include "game.h"

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

}  // namespace driver
