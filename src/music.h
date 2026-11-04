#pragma once
// CD music through the store's ogg player (winmm.dll in the game folder).

namespace music {

// Before the game runs.
void Install();

// The music's volume, 0..255, through the ogg player (auxSetVolume; the game
// itself has no music volume). Nothing happens without the folder's winmm.dll.
void SetVolume(int volume);

}  // namespace music
