#include "loader.h"

#include <cstring>
#include <vector>

#include "hook.h"
#include "log.h"

namespace loader {
namespace {

struct Redirection {
    const char* name;
    void* fn;
    void** orig;
};
std::vector<Redirection> g_redirects;
std::vector<void (*)(const char*, HMODULE)> g_onLoad;
std::vector<void (*)(const char*)> g_onResolve;

decltype(&LoadLibraryA) o_LoadLibraryA;
decltype(&GetProcAddress) o_GetProcAddress;

HMODULE WINAPI h_LoadLibraryA(LPCSTR name) {
    HMODULE m = o_LoadLibraryA(name);
    Log("LoadLibraryA(%s) = %p", name ? name : "(null)", (void*)m);
    for (auto fn : g_onLoad) fn(name, m);
    return m;
}

FARPROC WINAPI h_GetProcAddress(HMODULE m, LPCSTR name) {
    FARPROC p = o_GetProcAddress(m, name);
    if (!p || !HIWORD(name)) return p;
    for (auto fn : g_onResolve) fn(name);
    // Drivers resolve their own imports here: keep them on our loader too.
    if (strcmp(name, "LoadLibraryA") == 0) return (FARPROC)&h_LoadLibraryA;
    if (strcmp(name, "GetProcAddress") == 0) return (FARPROC)&h_GetProcAddress;
    for (auto& r : g_redirects) {
        if (strcmp(r.name, name) != 0) continue;
        if (!*r.orig) *r.orig = (void*)p;
        return (FARPROC)r.fn;
    }
    return p;
}

}  // namespace

bool Install() {
    return hook::Import(hook::FindImport(nullptr, "KERNEL32.dll", "LoadLibraryA"), &h_LoadLibraryA, &o_LoadLibraryA) &&
           hook::Import(hook::FindImport(nullptr, "KERNEL32.dll", "GetProcAddress"), &h_GetProcAddress,
                        &o_GetProcAddress);
}

void Redirect(const char* name, void* fn, void** orig) { g_redirects.push_back({name, fn, orig}); }

void OnLoad(void (*fn)(const char*, HMODULE)) { g_onLoad.push_back(fn); }

void OnResolve(void (*fn)(const char*)) { g_onResolve.push_back(fn); }

}  // namespace loader
