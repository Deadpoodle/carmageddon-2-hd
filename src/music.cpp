// CD music. The store ships an ogg player as winmm.dll in the game folder
// ("ogg-winmm virtual CD": MUSIC\TrackNN.ogg for the disc's audio tracks), and
// the exe was given an extra winmm import for its mciSendCommandA. On current
// Windows that call reaches System32's WINMM instead (a compatibility shim for
// this exe name loads the system DLL first; PCGamingWiki's fix is to rename
// the exe), so opening the CD fails (MCI error 266) and no music plays. Here
// the folder's winmm.dll is loaded by its full path and the exe's
// mciSendCommandA import is pointed at it.
#include "music.h"

#include <windows.h>

#include <cstring>
#include <string>

#include "hook.h"
#include "log.h"

namespace music {

void Install() {
    std::string path = GameDir() + "winmm.dll";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("music: no winmm.dll in the game folder; CD music left to the system");
        return;
    }
    uintptr_t slot = hook::FindImport(nullptr, "winmm.dll", "mciSendCommandA");
    if (!slot) {
        Log("music: the exe has no mciSendCommandA import");
        return;
    }
    HMODULE current = nullptr;
    char currentPath[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR) * (void**)slot, &current))
        GetModuleFileNameA(current, currentPath, MAX_PATH);
    if (_stricmp(currentPath, path.c_str()) == 0) {
        Log("music: the game folder's winmm.dll is already in use");
        return;
    }
    HMODULE ogg = LoadLibraryA(path.c_str());
    FARPROC fn = ogg ? GetProcAddress(ogg, "mciSendCommandA") : nullptr;
    if (!fn) {
        Log("music: could not load %s", path.c_str());
        return;
    }
    void* old;
    bool ok = hook::Import(slot, (void*)fn, &old);
    Log("music: CD music through the game folder's winmm.dll (was %s): %s", currentPath, ok ? "ok" : "FAILED");
}

}  // namespace music
