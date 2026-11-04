#pragma once
// The game restarting itself, so HD tab settings that are only read at start-up
// apply without the player doing it.

namespace restart {

// First thing at start-up: a restarted copy waits here for the old one to end.
void WaitForOldCopy();

// Before the game runs: a restarted copy skips the opening films.
void Install();

// Restart now, or, in a race, the next time the main menu is up.
void Request();
bool Pending();
void Now();

// This copy was started by a restart, until the main menu first opens (Back).
bool Restarted();
void Back();

}  // namespace restart
