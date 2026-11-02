// dinput.dll proxy. CARMA2_HW.EXE statically imports DINPUT.dll, which is not a
// KnownDLL, so this DLL in the game folder is loaded at process start and gives
// the patch a place to run without modifying the exe. Every export is passed
// to the real dinput.dll from the system folder.
#include <windows.h>

static HMODULE real_dinput;

static FARPROC real(const char* name) {
    if (!real_dinput) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 for a 32-bit process
        lstrcpyA(path + n, "\\dinput.dll");
        real_dinput = LoadLibraryA(path);
    }
    return real_dinput ? GetProcAddress(real_dinput, name) : nullptr;
}

#define FORWARD(name, params, args)                       \
    extern "C" HRESULT WINAPI proxy_##name params {       \
        typedef HRESULT(WINAPI * fn_t) params;            \
        static fn_t fn = (fn_t)real(#name);               \
        return fn ? fn args : E_FAIL;                     \
    }

FORWARD(DirectInputCreateA, (HINSTANCE inst, DWORD ver, void** out, IUnknown* outer), (inst, ver, out, outer))
FORWARD(DirectInputCreateW, (HINSTANCE inst, DWORD ver, void** out, IUnknown* outer), (inst, ver, out, outer))
FORWARD(DirectInputCreateEx, (HINSTANCE inst, DWORD ver, REFIID iid, void** out, IUnknown* outer),
        (inst, ver, iid, out, outer))
FORWARD(DllCanUnloadNow, (), ())
FORWARD(DllGetClassObject, (REFCLSID clsid, REFIID iid, void** out), (clsid, iid, out))
FORWARD(DllRegisterServer, (), ())
FORWARD(DllUnregisterServer, (), ())
