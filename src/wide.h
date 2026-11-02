#pragma once
// A back buffer wider than 640 at 480 lines (16:9, 21:9, 32:9 ...), with the
// game's 640x480 HUD laid out across it. See re/d3d-driver.md, re/hud-layout.md.

namespace wide {

// True if D3D.BDD on disk is the driver the patch knows (checked before any
// change, since the exe's width push runs before the driver is loaded).
bool DriverFileMatches();

// Before the game runs. w = buffer width (> 640, even); hud = the width the
// HUD spreads across (640..w), centred in the buffer.
bool Install(int w, int hud);

}  // namespace wide
