#pragma once
// The patch's log file (Carma2HD.log next to the exe).
#include <string>

void Log(const char* fmt, ...);

// The game folder, with a trailing backslash.
const std::string& GameDir();
