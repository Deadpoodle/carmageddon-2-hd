#pragma once
// The game's DirectSound use.

namespace audio {

// Diagnostic (C2HD_GAMELOG=1): log the cooperative level, the buffers and
// every failed Play/Lock. Before the game runs.
void InstallLog();

}  // namespace audio
