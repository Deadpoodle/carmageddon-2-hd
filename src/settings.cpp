#include "settings.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "log.h"

namespace settings {
namespace {

Display g_display;

const char* kDefaultIni =
    "; Carmageddon II HD settings. Delete this file to get the defaults back.\r\n"
    "\r\n"
    "[Display]\r\n"
    "; auto = your desktop's resolution, or WIDTHxHEIGHT (e.g. 1920x1080, 2560x1440, 3440x1440)\r\n"
    "Resolution=auto\r\n"
    "; 1 = the view widens to fill the screen (Hor+); 0 = the original 4:3 view, with black bars at the sides\r\n"
    "Widescreen=1\r\n";

std::string IniPath() { return GameDir() + "Carma2HD.ini"; }

std::string IniString(const char* sec, const char* key, const char* def) {
    char buf[256];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, IniPath().c_str());
    return buf;
}

}  // namespace

void Load() {
    std::string path = IniPath();
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (FILE* f = fopen(path.c_str(), "wb")) {
            fputs(kDefaultIni, f);
            fclose(f);
            Log("settings: wrote a default Carma2HD.ini");
        }
    }
    std::string res = IniString("Display", "Resolution", "auto");
    int w = 0, h = 0;
    if (sscanf(res.c_str(), "%dx%d", &w, &h) == 2 && w >= 640 && h >= 480) {
        g_display.width = w;
        g_display.height = h;
    }
    g_display.widescreen = atoi(IniString("Display", "Widescreen", "1").c_str()) != 0;
    int ow, oh;
    OutputSize(&ow, &oh);
    Log("settings: Resolution=%s (%dx%d), Widescreen=%d", res.c_str(), ow, oh, g_display.widescreen);
}

const Display& GetDisplay() { return g_display; }

void OutputSize(int* w, int* h) {
    if (g_display.width) {
        *w = g_display.width;
        *h = g_display.height;
        return;
    }
    // The real mode, unaffected by DPI scaling (the game isn't DPI-aware).
    DEVMODEA dm = {};
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth && dm.dmPelsHeight) {
        *w = (int)dm.dmPelsWidth;
        *h = (int)dm.dmPelsHeight;
    } else {
        *w = GetSystemMetrics(SM_CXSCREEN);
        *h = GetSystemMetrics(SM_CYSCREEN);
    }
}

}  // namespace settings
