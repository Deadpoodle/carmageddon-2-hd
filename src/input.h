#pragma once
// The game's DirectInput keyboard, seen through our dinput.dll proxy.

namespace input {

// Called by the proxy with each IDirectInputA the game creates.
void OnDirectInput(void* di);

// The scheme and [Keys] from Carma2HD.ini. Before the game runs.
void LoadBindings();

// Test runs: the keyboard reads only scripted keys (never the real keyboard).
// C2HD_KEYS="2.5:RETURN,4:W/2000,..." presses each key (DIK name) at that
// many seconds after start, for 150 ms or the given ms; steps run in order. Also posted to the window as WM_KEYDOWN/UP.
void StartScript(const char* script);

}  // namespace input
