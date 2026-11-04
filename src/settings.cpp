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
    "Widescreen=1\r\n"
    "; How far the HUD spreads on a wide screen: auto (the screen's width, up to 21:9), full, 4:3, 16:9 or 21:9\r\n"
    "HudWidth=auto\r\n"
    "; How far the scenery is drawn: original (the Graphics menu's own setting), far, further or max\r\n"
    "DrawDistance=max\r\n"
    "; borderless = a screen-size window (Alt+Tab shows its switcher; switching is quick)\r\n"
    "; exclusive = the screen is the game's alone (the Alt+Tab switcher isn't shown over it)\r\n"
    "Fullscreen=borderless\r\n"
    "; Menu and loading pictures on a wide screen: stretch (to the full width) or bars (the original\r\n"
    "; 4:3 picture between black bars)\r\n"
    "Backdrops=stretch\r\n"
    "\r\n"
    "[Visuals]\r\n"
    "; Edge smoothing: off, 2x, 4x or 8x\r\n"
    "Antialiasing=4x\r\n"
    "; Texture filtering: original, smooth (trilinear and mipmaps) or sharpest (16x anisotropic and mipmaps)\r\n"
    "Textures=sharpest\r\n"
    "; smooth = full-precision colour; 16-bit = the original dithered 16-bit look\r\n"
    "Colour=smooth\r\n"
    "; auto = your screen's refresh rate, never above 100 (the game runs too fast above 100); or a fixed\r\n"
    "; rate from 20 to 100, e.g. 60\r\n"
    "FrameRate=auto\r\n"
    "; Cutscenes: enhanced (enlarged 4x with Lanczos and lightly sharpened) or original\r\n"
    "Movies=enhanced\r\n"
    "; fill = cutscenes as large as the screen allows (1.25x at 16:9), original = 640x384 as the game had\r\n"
    "MovieSize=fill\r\n"
    "; Text and HUD art: on (their textures enlarged 2x with Scale2x, so they stay crisp at high resolutions) or off\r\n"
    "SharpText=on\r\n"
    "\r\n"
    "[Network]\r\n"
    "; The game's network play runs over UDP through the patch. The first copy on a PC uses Port,\r\n"
    "; further copies the next ones (up to Port+7). To host over the internet, UDP Port must reach\r\n"
    "; this PC: the game asks the router to open it (UPnP), or forward it on the router by hand.\r\n"
    "Port=47700\r\n"
    "; While hosting, the lobby shows your public address for friends. 1 = ask the router for it (UPnP);\r\n"
    "; 0 = don't talk to the router\r\n"
    "UPnP=1\r\n"
    "; 1 = also ask the router to open Port while hosting (closed again afterwards); 0 = don't\r\n"
    "OpenPort=1\r\n"
    "; The web service asked for the public address when the router doesn't say (empty = none)\r\n"
    "PublicIPService=https://api.ipify.org\r\n"
    "; To join a game over the internet: the host's address (and port if not 47700), e.g. 203.0.113.5\r\n"
    "; or 203.0.113.5:47700; the game then appears in the Network screen's list as on a LAN.\r\n"
    "; The Network screen's IP field sets this too\r\n"
    "JoinAddress=\r\n"
    "; 1 = pedestrians in network races (each player's game has its own; the original has none online)\r\n"
    "Pedestrians=1\r\n"
    "; udp (the patch) or ipxwrapper (the store's original network layer, LAN only)\r\n"
    "Transport=udp\r\n"
    "\r\n"
    "[Game]\r\n"
    "; 1 = the game loads the latest save when it starts (the Graphics screen's Auto Load); 0 = a new game\r\n"
    "AutoLoad=1\r\n"
    "; 1 = arrows beside the main menu's GROUP go back to the groups you have finished, to race them again\r\n"
    "EarlierGroups=1\r\n"
    "; humans, or zombies: they shamble and moan, grubby and green-blooded\r\n"
    "Pedestrians=humans\r\n"
    "\r\n"
    "[Controls]\r\n"
    "; Follows the key set chosen on the Controls screen (or on the HD tab):\r\n"
    "; classic = the game's sets A-D (A, its default, drives on the numeric keypad)\r\n"
    "; modern  = set E: W/A/S/D (and the arrows), Space handbrake, E/Q/Z/G power-ups, F repair,\r\n"
    ";           R recover, C camera ... remappable on the Controls screen; W/A/S/D also move\r\n"
    ";           around the menus\r\n"
    "Scheme=classic\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; 1 = the debug overlay in races (F2 toggles it): frame rate, positions, the minimap's map cut...\r\n"
    "Overlay=0\r\n"
    "\r\n"
    "[Keys]\r\n"
    "; Extra keys for any action, in either scheme: Action=KEY,KEY (DirectInput key names:\r\n"
    "; W, UP, SPACE, LSHIFT, LCONTROL, RETURN, NUMPAD8, F1 ...). An empty value removes the scheme's keys.\r\n"
    "; Each key listed here does only that action; the game's own key for the action still works.\r\n"
    "; Actions: Accelerate Brake Left Right Handbrake Wheelspin Repair Recover UsePowerup PowerupPrev\r\n"
    ";   PowerupNext PowerupList Camera Map MiniMap Horn Replay LookUp LookDown LookLeft LookRight\r\n"
    ";   HudDetail Chat TargetLock CycleTarget BuyArmour BuyPower BuyOffence NetPlayers\r\n"
    "; The modern scheme (set E) adds: Accelerate=UP  Brake=DOWN  Left=LEFT  Right=RIGHT\r\n";

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
    std::string hud = IniString("Display", "HudWidth", "auto");
    int a = 0, b = 0;
    if (hud == "full") g_display.hudAspect = 100.0;  // clamped to the buffer
    else if (sscanf(hud.c_str(), "%d:%d", &a, &b) == 2 && a > 0 && b > 0) g_display.hudAspect = (double)a / b;
    else g_display.hudAspect = 0;
    int ow, oh;
    OutputSize(&ow, &oh);
    Log("settings: Resolution=%s (%dx%d), Widescreen=%d", res.c_str(), ow, oh, g_display.widescreen);
}

const Display& GetDisplay() { return g_display; }

std::string Get(const char* section, const char* key, const char* def) { return IniString(section, key, def); }

void Set(const char* section, const char* key, const char* value) {
    WritePrivateProfileStringA(section, key, value, IniPath().c_str());
}

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
