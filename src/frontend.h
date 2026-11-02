#pragma once
// Menus and other front-end screens centred in a wide back buffer, and the
// mouse mapped to match.

namespace frontend {

// Before the game runs, with the buffer width (640 = stock: nothing moves,
// but the scopes and the mouse mapping are still installed).
void Install(int w);

// A front-end screen (menus, pause menu, loading...) is running.
bool InMenu();
// The Controls screen is waiting for a key to bind.
bool InKeyCapture();

// Test runs: the game's mouse position, in 640x480 menu space, from a script.
void SetVirtualCursor(int x, int y);

}  // namespace frontend
