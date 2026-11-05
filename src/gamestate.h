#pragma once
// What the game was doing, for the log: the race, the player and the opponents.

namespace gamestate {

// A snapshot to the log, for a hang or a crash. Safe on any thread and on
// broken game state: every read is checked, and nothing in the game is called.
void LogSnapshot();

// Call about once a second: logs each race as it starts and ends, so the log
// names the track even when the end comes too suddenly for a snapshot.
void Poll();

}  // namespace gamestate
