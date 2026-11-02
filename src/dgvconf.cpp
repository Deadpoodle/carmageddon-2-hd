#include "dgvconf.h"

#include <windows.h>

#include <cctype>
#include <cstdio>

#include "log.h"

namespace dgvconf {
namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

}  // namespace

bool Sync(const std::vector<Key>& keys) {
    std::string path = GameDir() + "dgVoodoo.conf";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        Log("dgvconf: no dgVoodoo.conf next to the exe; display keys left to dgVoodoo2's defaults");
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);

    bool crlf = text.find("\r\n") != std::string::npos;
    std::vector<std::string> lines;
    for (size_t i = 0; i <= text.size();) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(i, e - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        i = e + 1;
    }

    bool changed = false;
    for (const Key& k : keys) {
        std::string want = "[" + Lower(k.section) + "]", section;
        size_t sectionEnd = std::string::npos;
        bool found = false;
        for (size_t i = 0; i < lines.size() && !found; ++i) {
            std::string t = Trim(lines[i]);
            if (!t.empty() && t[0] == '[') {
                if (section == want) sectionEnd = i;
                section = Lower(t);
                continue;
            }
            if (section != want || t.empty() || t[0] == ';') continue;
            size_t eq = t.find('=');
            if (eq == std::string::npos || Lower(Trim(t.substr(0, eq))) != Lower(k.name)) continue;
            found = true;
            if (Lower(Trim(t.substr(eq + 1))) != Lower(k.value)) {
                lines[i] = std::string(k.name) + " = " + k.value;
                changed = true;
            }
        }
        if (found) continue;
        changed = true;
        if (section == want && sectionEnd == std::string::npos) sectionEnd = lines.size();
        if (sectionEnd == std::string::npos) {  // no such section: add it
            lines.push_back("");
            lines.push_back("[" + std::string(k.section) + "]");
            sectionEnd = lines.size();
        }
        while (sectionEnd > 0 && Trim(lines[sectionEnd - 1]).empty()) --sectionEnd;
        lines.insert(lines.begin() + sectionEnd, std::string(k.name) + " = " + k.value);
    }

    if (changed) {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i) out += lines[i] + (i + 1 < lines.size() ? (crlf ? "\r\n" : "\n") : "");
        if (FILE* w = fopen(path.c_str(), "wb")) {
            fwrite(out.data(), 1, out.size(), w);
            fclose(w);
        }
    }
    for (const Key& k : keys) Log("dgvconf: [%s] %s = %s", k.section, k.name, k.value.c_str());
    if (changed) Log("dgvconf: dgVoodoo.conf updated");
    return true;
}

}  // namespace dgvconf
