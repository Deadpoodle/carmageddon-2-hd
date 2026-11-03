#pragma once
// Key set E (the modern controls) beside the game's A-D on the Controls screen.

namespace keysets {

// Before the game runs.
void Install();

// Set E is the chosen set (the modern scheme).
bool Modern();

// Choose set E, or go back to the classic set chosen before (HD tab).
void SetModern(bool modern);

}  // namespace keysets
