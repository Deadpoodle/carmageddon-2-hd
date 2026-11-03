#pragma once
// The D3D renderer driver (D3D.BDD), mapped by BRender's own loader.
#include <cstdint>

namespace driver {

// Its image base, or nullptr if it isn't the known driver or isn't mapped yet.
// Call it first from a loader::OnResolve callback for "DirectDrawCreate": the
// driver is mapped then and nothing has patched it.
uint8_t* Base();

// The texture cache's slot table (re/textures.md §4), enlarged once for every
// feature that needs it (movie tiles at 4x, text and HUD twins at 2x): the
// 128x128 class gets 96 slots instead of 12, and the 128x64 class (2 slots)
// becomes 32 slots of 256x256. Call it from a loader::OnResolve
// ("DirectDrawCreate") callback, before the driver builds its slots; later
// calls return the first result. False if the table isn't as expected (then
// it is left as it is).
bool PatchTextureCache();

}  // namespace driver
