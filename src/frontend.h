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

// Test runs: the cursor goes through the game's real mouse path (GetCursorPos,
// ScreenToClient, the mapping above) in the client of an emulated full-screen
// window, the output size. Before the game runs.
void InstallTestCursor();
// Diagnostic: log the main menu's outcome (game state) each time it returns.
void InstallFlowLog();
// The cursor at a point in 640x480 menu space...
void SetVirtualCursor(int x, int y);
// ...or at a client pixel.
void SetTestCursor(int x, int y);
// Hover every focusable item of the current screen and check the game focuses
// it, and check the client's corners reach the frame's; results to the log.
void Sweep();

}  // namespace frontend
