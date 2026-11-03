#pragma once
// The debug overlay in races ([Debug] Overlay, F2 toggles it).

#include <windows.h>

namespace debug {

// Before the game runs.
void Install();

// Each keyboard read, with the keys as the player pressed them: F2 toggles.
void OnKeys(const BYTE* keys);

}  // namespace debug
