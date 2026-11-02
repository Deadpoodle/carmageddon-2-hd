#pragma once
// Hidden, muted test runs that never touch the player's screen or keyboard.
#include <windows.h>

namespace headless {

// C2HD_HEADLESS=1. Installs the window, focus and safety hooks on the exe.
// Returns false (and the caller ends the process) if a hook can't be placed.
bool Install();

bool Active();
HWND Window();

}  // namespace headless
