#pragma once
// Keeps the keys we own in dgVoodoo.conf (next to the exe) in step with our
// settings. dgVoodoo2 reads its config after our DLL starts, so a change made
// at start-up applies to the same run.
#include <string>
#include <vector>

namespace dgvconf {

struct Key {
    const char* section;  // e.g. "DirectX"
    const char* name;     // e.g. "Resolution"
    std::string value;
};

// Rewrites only the listed keys (adding any that are missing); everything else
// in the file is left as it is. Returns false if there is no dgVoodoo.conf.
bool Sync(const std::vector<Key>& keys);

}  // namespace dgvconf
