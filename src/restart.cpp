// The game restarting itself. Several HD tab settings can't change in a running
// game: dgVoodoo2 reads its config as it starts, and the display, draw distance
// and pedestrian patches are laid down at start-up. So leaving the HD tab with
// one of those changed starts a new copy and quits this one through QuitGame
// 0x491f70, which saves the options and any unsaved progress. The new copy waits
// for the old one to end, skips the opening films (0x68b894, -NOCUTSCENES) and
// loads the latest save, then puts both back at the first main menu. In a race
// nothing is lost: the restart waits for the main menu.
#include "restart.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log.h"

namespace restart {
namespace {

constexpr char kVariable[] = "C2HD_RESTART_OF";  // the old copy's process id
int* const kNoFilms = (int*)0x68b894;
const int* const kInRace = (const int*)0x75bba8;
const auto kQuitGame = (void (*)())0x491f70;

bool g_restarted, g_pending;

}  // namespace

void WaitForOldCopy() {
    char id[16] = "";
    if (!GetEnvironmentVariableA(kVariable, id, sizeof id)) return;
    SetEnvironmentVariableA(kVariable, nullptr);
    g_restarted = true;
    if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)atoi(id))) {
        WaitForSingleObject(old, 15000);
        CloseHandle(old);
    }
}

void Install() {
    if (!g_restarted) return;
    *kNoFilms = 1;
    Log("restart: started by the last copy; no opening films, the latest save");
}

bool Restarted() { return g_restarted; }

void Back() {
    if (!g_restarted) return;
    g_restarted = false;
    char line[1024];
    snprintf(line, sizeof line, "%s", GetCommandLineA());
    _strupr(line);
    if (!strstr(line, "NOCUTSCENE")) *kNoFilms = 0;
}

bool Pending() { return g_pending; }

void Now() {
    g_pending = false;
    char path[MAX_PATH], line[1024], id[16];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    snprintf(line, sizeof line, "%s", GetCommandLineA());
    snprintf(id, sizeof id, "%lu", GetCurrentProcessId());
    SetEnvironmentVariableA(kVariable, id);
    SetEnvironmentVariableA("C2HD_KEYS", nullptr);  // a test's script is for this copy
    std::string dir = GameDir();
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    bool ok = CreateProcessA(path, line, nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi);
    SetEnvironmentVariableA(kVariable, nullptr);
    Log("restart: settings changed; new copy %s", ok ? "started" : "FAILED");
    if (!ok) return;  // they apply at the next start, as they would have
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    kQuitGame();
}

void Request() {
    if (*kInRace) {
        g_pending = true;
        Log("restart: waits for the end of the race");
    } else {
        Now();
    }
}

}  // namespace restart
