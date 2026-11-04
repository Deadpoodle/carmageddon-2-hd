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

#include <mmsystem.h>

#include <cstdint>
#include <vector>

#include <cstring>
#include <string>

#include "hook.h"
#include "log.h"

namespace music {
namespace {

// The volume. The player's own (auxSetVolume) scales samples as it decodes, so a
// change waits behind the audio already queued, a second or more.
// waveOutSetVolume is immediate but sets the whole game's audio session, effects
// included. So the player stays at 100% and its 16-bit buffers are scaled here on
// the way to waveOutWrite, keeping the clean samples; a new volume rewrites the
// queued buffers. Windows reads a buffer as it plays it (measured with a loopback
// capture: zeroing the rest of a queued buffer silenced it at once), so the
// change is heard straight away.
MMRESULT(WINAPI* g_auxSetVolume)(UINT, DWORD);
MMRESULT(WINAPI* real_waveOutOpen)(HWAVEOUT*, UINT, const WAVEFORMATEX*, DWORD_PTR, DWORD_PTR, DWORD);
MMRESULT(WINAPI* real_waveOutWrite)(HWAVEOUT, WAVEHDR*, UINT);
MMRESULT(WINAPI* real_waveOutUnprepareHeader)(HWAVEOUT, WAVEHDR*, UINT);

struct Queued {
    WAVEHDR* header;
    std::vector<int16_t> samples;
};
std::vector<Queued> g_queued;
CRITICAL_SECTION g_lock;
int g_gain = 256;     // 256 = full
bool g_scaling;       // the hooks are in, and the stream is 16-bit

void Scale(Queued& q) {
    int16_t* out = (int16_t*)q.header->lpData;
    for (size_t i = 0; i < q.samples.size(); i++) out[i] = (int16_t)(q.samples[i] * g_gain / 256);
}

MMRESULT WINAPI Open(HWAVEOUT* out, UINT device, const WAVEFORMATEX* format, DWORD_PTR callback, DWORD_PTR instance,
                     DWORD flags) {
    if (format && format->wBitsPerSample != 16 && g_scaling) {
        Log("music: a %d-bit stream; the volume stays with the player", format->wBitsPerSample);
        g_scaling = false;
    }
    return real_waveOutOpen(out, device, format, callback, instance, flags);
}

MMRESULT WINAPI Write(HWAVEOUT out, WAVEHDR* header, UINT size) {
    if (g_scaling) {
        EnterCriticalSection(&g_lock);
        Queued* q = nullptr;
        for (Queued& each : g_queued)
            if (each.header == header) q = &each;
        if (!q) {
            g_queued.push_back({header, {}});
            q = &g_queued.back();
        }
        const int16_t* samples = (const int16_t*)header->lpData;
        static bool logged;
        if (!logged) Log("music: buffers of %lu bytes", header->dwBufferLength);
        logged = true;
        q->samples.assign(samples, samples + header->dwBufferLength / 2);
        Scale(*q);
        LeaveCriticalSection(&g_lock);
    }
    return real_waveOutWrite(out, header, size);
}

MMRESULT WINAPI Unprepare(HWAVEOUT out, WAVEHDR* header, UINT size) {
    EnterCriticalSection(&g_lock);  // the player may free the buffer after this
    for (size_t i = 0; i < g_queued.size(); i++)
        if (g_queued[i].header == header) {
            g_queued.erase(g_queued.begin() + i);
            break;
        }
    LeaveCriticalSection(&g_lock);
    return real_waveOutUnprepareHeader(out, header, size);
}

void HookVolume(HMODULE winmm) {
    g_auxSetVolume = (decltype(g_auxSetVolume))GetProcAddress(winmm, "auxSetVolume");
    InitializeCriticalSection(&g_lock);
    void* open;
    void* write;
    void* unprepare;
    g_scaling = hook::Import(hook::FindImport(winmm, "WINMM.DLL", "waveOutOpen"), (void*)&Open, &open) &&
                hook::Import(hook::FindImport(winmm, "WINMM.DLL", "waveOutUnprepareHeader"), (void*)&Unprepare, &unprepare) &&
                hook::Import(hook::FindImport(winmm, "WINMM.DLL", "waveOutWrite"), (void*)&Write, &write);
    if (g_scaling) {
        real_waveOutOpen = (decltype(real_waveOutOpen))open;
        real_waveOutUnprepareHeader = (decltype(real_waveOutUnprepareHeader))unprepare;
        real_waveOutWrite = (decltype(real_waveOutWrite))write;
    }
    Log("music: volume through %s", g_scaling ? "its queued buffers" : g_auxSetVolume ? "the player" : "nothing");
}

}  // namespace

void SetVolume(int volume) {
    static int last = -1;  // a held mouse button sets the bar every frame
    if (volume == last) return;
    last = volume;
    if (g_scaling) {
        EnterCriticalSection(&g_lock);
        g_gain = volume * 256 / 255;
        for (Queued& q : g_queued)
            if (!(q.header->dwFlags & WHDR_DONE)) Scale(q);
        LeaveCriticalSection(&g_lock);
    } else if (g_auxSetVolume) {
        DWORD word = (DWORD)volume * 257;  // 255 -> 0xffff
        g_auxSetVolume(0, word | word << 16);
    }
    Log("music: volume %d", volume);
}

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
        HookVolume(current);
        return;
    }
    HMODULE ogg = LoadLibraryA(path.c_str());
    FARPROC fn = ogg ? GetProcAddress(ogg, "mciSendCommandA") : nullptr;
    if (!fn) {
        Log("music: could not load %s", path.c_str());
        return;
    }
    HookVolume(ogg);
    void* old;
    bool ok = hook::Import(slot, (void*)fn, &old);
    Log("music: CD music through the game folder's winmm.dll (was %s): %s", currentPath, ok ? "ok" : "FAILED");
}

}  // namespace music
