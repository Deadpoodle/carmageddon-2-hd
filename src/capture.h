#pragma once
// Screenshots of the game's own back buffer, taken at the page flip.

namespace capture {

// Hooks DirectDrawCreate (through the loader). Shots are taken on request.
void Install();

// Save the next presented frame to shots\NNNN.bmp in the game folder.
void Request();

// Tests: every Lock fails for this long, as they do while the game is minimised.
void FailLocks(unsigned ms);

}  // namespace capture
