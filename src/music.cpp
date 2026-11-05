// CD music. The store ships an ogg player as winmm.dll in the game folder
// ("ogg-winmm virtual CD": MUSIC\TrackNN.ogg for the disc's audio tracks), and
// the exe was given an extra winmm import for its mciSendCommandA. On current
// Windows that call reaches System32's WINMM instead (a compatibility shim for
// this exe name loads the system DLL first; PCGamingWiki's fix is to rename
// the exe), so opening the CD fails (MCI error 266) and no music plays. Here
// the folder's winmm.dll is loaded by its full path and the exe's
// mciSendCommandA import is pointed at it.
//
// The choice of track is ours too. The game (0x4566d0) picks at random from one
// of three lists of sound IDs and only refuses the last ID played (0x595c50), so
// A-B-A is common. Its lists are short: 9600..9603 for menus, 9604..9607 for races
// and all eight for network games, IDs that SOUND.TXT gives to CD tracks 2..9. But
// the store's music stops at Track08, so a race has three songs and a one in four
// chance of a track that isn't there. Each of its three calls to the random routine
// now comes here, for a shuffle: every track that exists once, in a random order,
// then again in another. Never the last ID, because the game's loop asks again
// until it gets a different one.
#include "music.h"

#include <windows.h>

#include <mmsystem.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

constexpr int kFirstId = 9600, kFirstTrack = 2, kIds = 8;
int* const kLastId = (int*)0x595c50;
bool g_exists[kIds];
bool g_heard[kIds];  // played in this round of the shuffle
uint32_t g_random;

uint32_t Random() {  // xorshift: the game's own is reseeded around its effects
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

// One of the list's tracks at random, among those that qualify; -1 if none do.
int Choose(const int* list, int count, bool mustExist, bool unheard) {
    int chosen = -1, seen = 0;
    for (int i = 0; i < count; i++) {
        int k = list[i] - kFirstId;
        if (list[i] == *kLastId || k < 0 || k >= kIds || (mustExist && !g_exists[k]) || (unheard && g_heard[k])) continue;
        if (Random() % (uint32_t)++seen == 0) chosen = i;
    }
    return chosen;
}

// The index in list of the track to play.
int Pick(const int* list, int count) {
    int chosen = Choose(list, count, true, true);
    if (chosen < 0) {  // the round is over
        for (int i = 0; i < count; i++)
            if (list[i] >= kFirstId && list[i] < kFirstId + kIds) g_heard[list[i] - kFirstId] = false;
        chosen = Choose(list, count, true, false);
    }
    if (chosen < 0) chosen = Choose(list, count, false, false);  // only the last one exists: anything else ends the loop
    if (chosen < 0) return 0;
    int k = list[chosen] - kFirstId;
    g_heard[k] = true;
    Log("music: track %d next", k + kFirstTrack);
    return chosen;
}

// In place of the random routine 0x513520 (fastcall lowest, highest).
template <uintptr_t list>
int __fastcall PickFrom(int, int highest) {
    return Pick((const int*)list, highest + 1);
}

void InstallPicker() {
    char stock[4] = "";
    if (GetEnvironmentVariableA("C2HD_STOCK_MUSIC", stock, sizeof stock) && stock[0] == '1') {
        Log("music: choice of track left as the original (C2HD_STOCK_MUSIC)");
        return;
    }
    int found = 0;
    for (int k = 0; k < kIds; k++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%sMUSIC\\Track%02d.ogg", GameDir().c_str(), k + kFirstTrack);
        g_exists[k] = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
        found += g_exists[k];
    }
    if (!found)  // a real CD: nothing to go on, so all of them
        for (bool& exists : g_exists) exists = true;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_random = (uint32_t)now.QuadPart | 1;
    bool ok = hook::Redirect(0x4567ae, 0xe8, 0x513520, (const void*)&PickFrom<0x595c78>) &&  // all eight
              hook::Redirect(0x4567c3, 0xe8, 0x513520, (const void*)&PickFrom<0x595c98>) &&  // menus
              hook::Redirect(0x4567d8, 0xe8, 0x513520, (const void*)&PickFrom<0x595ca8>);    // races
    Log("music: shuffling %d tracks: %s", found ? found : kIds, ok ? "on" : "FAILED");

    char test[8] = "";  // C2HD_MUSIC_TEST=n (tests): n choices from each list, to the log
    if (!GetEnvironmentVariableA("C2HD_MUSIC_TEST", test, sizeof test)) return;
    static const struct { const char* name; uintptr_t list; int count; } kLists[] = {
        {"all", 0x595c78, 8}, {"menus", 0x595c98, 4}, {"races", 0x595ca8, 4}};
    int last = *kLastId;
    for (const auto& l : kLists) {
        Log("music: test: %s", l.name);
        for (int i = 0; i < atoi(test); i++) *kLastId = ((const int*)l.list)[Pick((const int*)l.list, l.count)];
    }
    *kLastId = last;
    memset(g_heard, 0, sizeof g_heard);
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
    InstallPicker();
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
