#pragma once
// The game's DirectInput keyboard, seen through our dinput.dll proxy.

namespace input {

// Called by the proxy with each IDirectInputA the game creates.
void OnDirectInput(void* di);

// Test runs: the keyboard reads only scripted keys (never the real keyboard).
// C2HD_KEYS="2.5:RETURN,4:DOWN,..." taps each key (DIK name) at that many
// seconds after start, for 150 ms. Also posted to the window as WM_KEYDOWN/UP.
void StartScript(const char* script);

}  // namespace input
