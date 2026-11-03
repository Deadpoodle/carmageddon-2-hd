#pragma once
// Widescreen: the game keeps its 640x480 layout; dgVoodoo2 renders it at the
// chosen resolution, and the 3D camera's aspect is widened to match (Hor+).

namespace display {

// Before the game runs: syncs dgVoodoo.conf and hooks camera creation.
void Install();

// Wine's version when the game runs under Wine (Proton, the Steam Deck), else nullptr.
const char* WineVersion();

}  // namespace display
