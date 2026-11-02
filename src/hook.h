#pragma once
// Import-table hooks and verified code patches.
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace hook {

// Swap the exe's import slot for `fn`; the previous pointer goes to *orig, so
// hooks chain when installed in a fixed order.
template <typename T>
bool Import(uintptr_t slot, T fn, T* orig) {
    if (!slot) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = *(T*)slot;
    *(T*)slot = fn;
    VirtualProtect((void*)slot, sizeof(void*), old, &old);
    return true;
}

// The import-table slot of dll!name in `module` (the exe when null), or 0.
inline uintptr_t FindImport(HMODULE module, const char* dll, const char* name) {
    uint8_t* base = (uint8_t*)(module ? module : GetModuleHandleA(nullptr));
    auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((char*)(base + d->Name), dll) != 0) continue;
        auto* names = (IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* slots = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, name) == 0) return (uintptr_t)&slots->u1.Function;
        }
    }
    return 0;
}

inline bool Matches(uintptr_t addr, const uint8_t* bytes, size_t n) {
    return memcmp((const void*)addr, bytes, n) == 0;
}

// Write n bytes at addr, but only if `expect` (same length) is there now.
inline bool Patch(uintptr_t addr, const uint8_t* expect, const uint8_t* bytes, size_t n) {
    if (!Matches(addr, expect, n)) return false;
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, bytes, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return true;
}

// Replace a 32-bit value at addr, if `expect` is there now.
inline bool PatchU32(uintptr_t addr, uint32_t expect, uint32_t value) {
    return Patch(addr, (const uint8_t*)&expect, (const uint8_t*)&value, 4);
}

// Redirect a `call rel32` (0xe8) or `jmp rel32` (0xe9) at site from its
// current target `expect` to `target`.
inline bool Redirect(uintptr_t site, uint8_t opcode, uintptr_t expect, const void* target) {
    uint8_t want[5] = {opcode}, now[5] = {opcode};
    int32_t from = (int32_t)(expect - (site + 5)), to = (int32_t)((uintptr_t)target - (site + 5));
    memcpy(want + 1, &from, 4);
    memcpy(now + 1, &to, 4);
    return Patch(site, want, now, 5);
}

// Detour a function whose first n bytes (n >= 5) are whole instructions with
// no relative addressing, given as `expect`: they are copied to a trampoline
// followed by a jmp back, and replaced by a jmp to `fn`. Returns the
// trampoline (call it to run the original), or nullptr.
inline void* Detour(uintptr_t addr, const uint8_t* expect, size_t n, const void* fn) {
    if (n < 5 || !Matches(addr, expect, n)) return nullptr;
    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, n + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;
    memcpy(tramp, expect, n);
    tramp[n] = 0xe9;
    int32_t back = (int32_t)((addr + n) - ((uintptr_t)tramp + n + 5));
    memcpy(tramp + n + 1, &back, 4);
    uint8_t jmp[16];
    memset(jmp, 0x90, sizeof jmp);
    jmp[0] = 0xe9;
    int32_t to = (int32_t)((uintptr_t)fn - (addr + 5));
    memcpy(jmp + 1, &to, 4);
    if (!Patch(addr, expect, jmp, n)) return nullptr;
    return tramp;
}

}  // namespace hook
