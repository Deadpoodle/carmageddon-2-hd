#include "options.h"

#include <vector>

#include "hook.h"
#include "log.h"

namespace options {
namespace {

std::vector<void (*)()> g_onLoaded;

void LoadOptions() {
    reinterpret_cast<void (*)()>(0x48d8f0)();
    for (auto fn : g_onLoaded) fn();
}

}  // namespace

void OnLoaded(void (*fn)()) { g_onLoaded.push_back(fn); }

bool Install() {
    bool ok = hook::Redirect(0x47de15, 0xe8, 0x48d8f0, (const void*)&LoadOptions);
    if (!ok) Log("options: LoadOptions call not found");
    return ok;
}

}  // namespace options
