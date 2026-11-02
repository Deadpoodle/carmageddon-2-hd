#pragma once
// The exe's LoadLibraryA and GetProcAddress. BRender loads the renderer drivers
// (.bdd) with its own image loader, which resolves their Windows imports
// through these two, so redirecting a name here reaches the drivers too.
#include <windows.h>

namespace loader {

// Hook the exe's imports. Call once, before the game runs.
bool Install();

// GetProcAddress(any module, name) returns fn instead; the real address goes
// to *orig (set the first time the name is asked for).
void Redirect(const char* name, void* fn, void** orig);

// Called for each name looked up through GetProcAddress (before redirects).
// A driver's imports are resolved after BRender has mapped it and before any
// of its code runs, so this is the moment to patch a driver.
void OnResolve(void (*fn)(const char* name));

// Called after each LoadLibraryA by the game or a driver: (name, result).
void OnLoad(void (*fn)(const char* name, HMODULE module));

}  // namespace loader
