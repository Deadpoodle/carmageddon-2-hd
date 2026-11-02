#pragma once
// The game's OPTIONS.TXT load at start-up (LoadOptions 0x48d8f0, called once
// from InitialiseDeathRace at 0x47de15, before the cameras exist).

namespace options {

// Run fn after the game has loaded OPTIONS.TXT. Install() places the hook.
void OnLoaded(void (*fn)());
bool Install();

}  // namespace options
