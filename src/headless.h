#pragma once
// Hidden, muted test runs that never touch the player's screen or keyboard.
#include <windows.h>

namespace headless {

// C2HD_HEADLESS=1. Installs the window, focus and safety hooks on the exe.
// Returns false (and the caller ends the process) if a hook can't be placed.
bool Install(bool visible = false);

// C2HD_HEADLESS=3: a real full-screen run on the player's screen (allowed by
// the player), driven from outside: only the test class, mute and message boxes.
bool InstallScreen();

// A hidden or windowed test run (not 3).
bool Active();
HWND Window();

}  // namespace headless
