// Sound: the game's DirectSound use, logged for diagnosis (C2HD_GAMELOG=1).
//
// The exe imports DirectSoundCreate (slot 0x589018). The device's
// SetCooperativeLevel and CreateSoundBuffer, and the buffers' Play, Lock and
// Restore (vtable slots shared by every object of a class), are wrapped: the
// level, each buffer's flags and size, and every failure with its code go to
// the log, so what happens to the sound when the game loses and regains the
// focus is on record.
#include "audio.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <mmsystem.h>

#include <cstdint>

#include "hook.h"
#include "log.h"

namespace audio {
namespace {

struct DsBufferDesc {  // DSBUFFERDESC
    DWORD size, flags, bytes, reserved;
    void* format;
};

enum { kSetCooperativeLevel = 6, kCreateSoundBuffer = 3 };                  // IDirectSound
enum { kGetStatus = 9, kLock = 11, kPlay = 12, kSetVolume = 15, kStop = 18, kRestore = 20 };  // IDirectSoundBuffer

typedef HRESULT(WINAPI* DirectSoundCreate_t)(const GUID*, void**, void*);
typedef HRESULT(WINAPI* SetCooperativeLevel_t)(void*, HWND, DWORD);
typedef HRESULT(WINAPI* CreateSoundBuffer_t)(void*, const DsBufferDesc*, void**, void*);
typedef HRESULT(WINAPI* Play_t)(void*, DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* Lock_t)(void*, DWORD, DWORD, void**, DWORD*, void**, DWORD*, DWORD);
typedef HRESULT(WINAPI* Restore_t)(void*);
typedef HRESULT(WINAPI* SetVolume_t)(void*, LONG);
typedef MCIERROR(WINAPI* Mci_t)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);

DirectSoundCreate_t o_DirectSoundCreate;
SetCooperativeLevel_t o_SetCooperativeLevel;
CreateSoundBuffer_t o_CreateSoundBuffer;
Play_t o_Play;
Lock_t o_Lock;
Restore_t o_Restore;
SetVolume_t o_SetVolume;
Mci_t o_Mci;
void* g_primary;                                 // the primary buffer
volatile LONG g_volMin = 1, g_volMax = -100000;  // secondary buffers' volumes set in the tally's 2 s (1: none)
LONG g_buffers, g_failures, g_plays;

uintptr_t Slot(void* obj, int i) { return (uintptr_t)(*(void***)obj + i); }

void Failed(const char* what, void* buffer, HRESULT hr) {
    if (InterlockedIncrement(&g_failures) <= 200)
        Log("audio: %s(%p) failed: %#lx%s", what, buffer, (unsigned long)hr,
            hr == (HRESULT)0x88780096 ? " (DSERR_BUFFERLOST)" : "");
}

HRESULT WINAPI h_Play(void* b, DWORD r1, DWORD prio, DWORD flags) {
    HRESULT hr = o_Play(b, r1, prio, flags);
    if (FAILED(hr)) Failed("Play", b, hr);
    else InterlockedIncrement(&g_plays);
    return hr;
}

HRESULT WINAPI h_SetVolume(void* b, LONG vol) {
    HRESULT hr = o_SetVolume(b, vol);
    if (b == g_primary) {
        Log("audio: primary buffer volume %ld = %#lx", vol, (unsigned long)hr);
    } else {
        if (g_volMin == 1 || vol < g_volMin) g_volMin = vol;
        if (vol > g_volMax) g_volMax = vol;
    }
    return hr;
}

MCIERROR WINAPI h_Mci(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR params) {
    MCIERROR r = o_Mci(id, msg, flags, params);
    if (msg != MCI_STATUS) Log("audio: mci device %u message %#x flags %#lx = %lu", id, msg, (unsigned long)flags, r);
    return r;
}

// What this process actually sends to the speakers: its audio session's peak
// meter, sampled every 100 ms (the highest over the tally's 2 s).
volatile LONG g_peak = -1;  // x1000; -1 = no meter

// IAudioMeterInformation (MinGW's endpointvolume.h lacks it): GetPeakValue is
// vtable slot 3, called as a plain stdcall with the object first.
typedef HRESULT(WINAPI* GetPeakValue_t)(void* meter, float* peak);
const GUID kIidMeter = {0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};

DWORD WINAPI Meter(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioSessionManager* mgr = nullptr;
    IAudioSessionControl* session = nullptr;
    void* meter = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&en);
    if (SUCCEEDED(hr)) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, (void**)&mgr);
    if (SUCCEEDED(hr)) hr = mgr->GetAudioSessionControl(nullptr, 0, &session);
    if (SUCCEEDED(hr)) hr = session->QueryInterface(kIidMeter, &meter);
    Log("audio: session peak meter %s (hr %#lx)", SUCCEEDED(hr) ? "on" : "unavailable", (unsigned long)hr);
    if (FAILED(hr)) return 0;
    GetPeakValue_t getPeak = (GetPeakValue_t)(*(void***)meter)[3];
    for (;;) {
        float peak = 0;
        if (SUCCEEDED(getPeak(meter, &peak))) {
            LONG v = (LONG)(peak * 1000), cur;
            while ((cur = g_peak) < v && InterlockedCompareExchange(&g_peak, v, cur) != cur) {}
        }
        Sleep(100);
    }
}

DWORD WINAPI Tally(void*) {  // how many sounds the game starts, and their volumes, every 2 s
    for (;;) {
        Sleep(2000);
        LONG peak = InterlockedExchange(&g_peak, 0);
        Log("audio: output peak %.3f", peak / 1000.0);
        LONG n = InterlockedExchange(&g_plays, 0);
        LONG lo = InterlockedExchange(&g_volMin, 1), hi = InterlockedExchange(&g_volMax, -100000);
        if (n || lo != 1) Log("audio: %ld sounds started in 2 s; volumes set %ld..%ld", n, lo == 1 ? 0 : lo, hi);
    }
}

HRESULT WINAPI h_Lock(void* b, DWORD off, DWORD n, void** p1, DWORD* n1, void** p2, DWORD* n2, DWORD flags) {
    HRESULT hr = o_Lock(b, off, n, p1, n1, p2, n2, flags);
    if (FAILED(hr)) Failed("Lock", b, hr);
    return hr;
}

HRESULT WINAPI h_Restore(void* b) {
    HRESULT hr = o_Restore(b);
    Log("audio: Restore(%p) = %#lx", b, (unsigned long)hr);
    return hr;
}

HRESULT WINAPI h_CreateSoundBuffer(void* ds, const DsBufferDesc* desc, void** out, void* outer) {
    HRESULT hr = o_CreateSoundBuffer(ds, desc, out, outer);
    LONG n = InterlockedIncrement(&g_buffers);
    if (n <= 400 && desc)
        Log("audio: buffer %ld: flags %#lx, %lu bytes = %#lx (%p)", n, (unsigned long)desc->flags,
            (unsigned long)desc->bytes, (unsigned long)hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out && desc && (desc->flags & 1)) g_primary = *out;  // DSBCAPS_PRIMARYBUFFER
    if (SUCCEEDED(hr) && out && *out && !o_Play) {  // one vtable for every buffer
        hook::Import(Slot(*out, kPlay), (Play_t)&h_Play, &o_Play);
        hook::Import(Slot(*out, kLock), (Lock_t)&h_Lock, &o_Lock);
        hook::Import(Slot(*out, kRestore), (Restore_t)&h_Restore, &o_Restore);
        hook::Import(Slot(*out, kSetVolume), (SetVolume_t)&h_SetVolume, &o_SetVolume);
    }
    return hr;
}

HRESULT WINAPI h_SetCooperativeLevel(void* ds, HWND hwnd, DWORD level) {
    HRESULT hr = o_SetCooperativeLevel(ds, hwnd, level);
    Log("audio: SetCooperativeLevel(window %p, level %lu) = %#lx", (void*)hwnd, (unsigned long)level,
        (unsigned long)hr);
    return hr;
}

HRESULT WINAPI h_DirectSoundCreate(const GUID* guid, void** out, void* outer) {
    HRESULT hr = o_DirectSoundCreate(guid, out, outer);
    Log("audio: DirectSoundCreate = %#lx (%p)", (unsigned long)hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out && !o_SetCooperativeLevel) {
        hook::Import(Slot(*out, kSetCooperativeLevel), (SetCooperativeLevel_t)&h_SetCooperativeLevel,
                     &o_SetCooperativeLevel);
        hook::Import(Slot(*out, kCreateSoundBuffer), (CreateSoundBuffer_t)&h_CreateSoundBuffer, &o_CreateSoundBuffer);
    }
    return hr;
}

}  // namespace

void InstallLog() {
    bool ok = hook::Import(0x589018, (DirectSoundCreate_t)&h_DirectSoundCreate, &o_DirectSoundCreate);
    Log("audio: DirectSound log %s", ok ? "on" : "FAILED");
    uintptr_t mci = hook::FindImport(nullptr, "winmm.dll", "mciSendCommandA");  // after the music shim's repoint
    Log("audio: MCI log %s", mci && hook::Import(mci, (Mci_t)&h_Mci, &o_Mci) ? "on" : "FAILED");
    if (HANDLE t = CreateThread(nullptr, 0, &Tally, nullptr, 0, nullptr)) CloseHandle(t);
    if (HANDLE t = CreateThread(nullptr, 0, &Meter, nullptr, 0, nullptr)) CloseHandle(t);
}

}  // namespace audio
