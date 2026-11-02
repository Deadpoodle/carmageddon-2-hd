#pragma once
// The D3D renderer driver (D3D.BDD), mapped by BRender's own loader.
#include <cstdint>

namespace driver {

// Its image base, or nullptr if it isn't the known driver or isn't mapped yet.
// Call it first from a loader::OnResolve callback for "DirectDrawCreate": the
// driver is mapped then and nothing has patched it.
uint8_t* Base();

}  // namespace driver
