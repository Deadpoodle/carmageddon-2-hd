#pragma once
// XInput pads, presented to the game as its own DirectInput joystick.
#define DIRECTINPUT_VERSION 0x0500
#include <dinput.h>
#include <windows.h>

namespace controller {

// [Controller] Enabled (default 1). Before the game runs.
void Install();

// IDirectInputA::EnumDevices: true if the joystick enumeration was answered
// with the virtual pad (the real joysticks are then not reported).
bool OnEnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKA cb, void* ref);

// IDirectInputA::CreateDevice: our pad for its GUID, else nullptr.
void* CreateDevice(REFGUID guid);

// The keyboard state the game is about to see: pad buttons as the game's
// keys for their actions (race) or as arrows/Return/Esc (menus).
void InjectKeys(BYTE* state, BYTE (*targetScan)(int line));

}  // namespace controller
