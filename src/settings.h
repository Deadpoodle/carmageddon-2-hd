#pragma once
// Carma2HD.ini next to the exe: written with comments on first run, never
// overwritten; missing keys fall back to the defaults below.
#include <string>

namespace settings {

struct Display {
    int width = 0, height = 0;  // Resolution; 0 = auto (the desktop's)
    bool widescreen = true;     // Hor+ view at the resolution's shape; false = original 4:3, pillarboxed
    double hudAspect = 0;       // HudWidth: the shape the HUD spreads across; 0 = auto (the screen's, up to 21:9)
};

void Load();
const Display& GetDisplay();

// The size the game is shown at: the configured one, or the desktop's.
void OutputSize(int* w, int* h);

}  // namespace settings
