// Zombie pedestrians of our own ([Game] Pedestrians=zombies, re/zombies.md).
//
// The game's -zombie switch only swaps data folders for art that came on the
// UK disc. This makes zombies from the normal pedestrians instead. The setting
// is read at start-up (blood art loads once, at the start), and nothing here
// is installed for Humans.
//
// - They shamble: drunk is one global, 0x7447b8, read in SetPedMove (0x4cbd48)
//   and the per-frame ped check (0x4d4d64). Both reads are diverted to answer
//   "drunk" unless Groovin' (0x7447a8) or Panic (0x7447f8) is running, so those
//   power-ups still take over. No power-up entry is touched: no icon, no timer.
// - They moan: PlayPedSound 0x4cd880 takes pitch from 65536.0 / ped scale. The
//   two fdivr operands (the double at 0x58aa58) point at our own double, set
//   lower for the voice groups (table 0x65d758), so thuds stay as they are.
// - They're grubby: pedestrian skins (PEDS\PEEPS, RGB565) are recoloured while
//   LoadPedPeepsArt 0x4cbc10 runs, at the BrMapAdd in the folder loader
//   (0x502641), before the first upload. tools/zombify.py holds the same
//   formula, for tuning on stills. <NAME>B is the bloodied copy of <NAME>:
//   texels that differ and are red are blood. Not every copy lines up with
//   its skin, hence the red test.
// - Their blood is green: the same BrMapAdd during LoadGiblets 0x4cb1e0 covers
//   PEDS\GIBLETS (GIBS2/3, the BIGBL clouds, GIBSLICK, PEDSMEAR); tyre trails'
//   GIBSMEAR comes through LoadPixelmap at 0x4e9ce1 (as OILSMEAR does, which
//   stays black); and the chunks' material colours, set from SETTINGS.TXT, are
//   overwritten after LoadGiblets.
// - The wording follows the official zombie patch: "pedestrian" becomes ZOMBIE
//   in TEXT.TXT's strings (table 0x6b5f40, null-ended) and the power-up names
//   (entry +0x50), rewritten in place, once they are loaded (the main menu's
//   first enter).
#include "zombies.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hook.h"
#include "log.h"
#include "settings.h"

namespace zombies {
namespace {

// ---------------------------------------------------------------------------
// Recolour (the numbers of tools/zombify.py)

constexpr float kDesaturate = 0.70f, kGrime = 0.45f, kBloodRed = 1.6f;
constexpr float kTint[3] = {0.80f, 0.92f, 0.68f}, kSkin[3] = {0.50f, 0.62f, 0.40f};
constexpr int kBloodDiff = 40;

float Hash(int i, int j, uint32_t seed) {
    uint32_t n = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u + seed * 2246822519u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)((n ^ (n >> 16)) & 0xffff) / 65535.0f;
}

float Noise(int x, int y, int cell, uint32_t seed) {
    int gx = x / cell, gy = y / cell;
    float fx = (float)(x % cell) / cell, fy = (float)(y % cell) / cell;
    float a = Hash(gx, gy, seed) * (1 - fx) + Hash(gx + 1, gy, seed) * fx;
    float b = Hash(gx, gy + 1, seed) * (1 - fx) + Hash(gx + 1, gy + 1, seed) * fx;
    return a * (1 - fy) + b * fy;
}

float Grime(int x, int y) {
    float n = 0.6f * Noise(x, y, 16, 1) + 0.3f * Noise(x, y, 6, 2) + 0.1f * Noise(x, y, 2, 3);
    n = (n - 0.35f) / 0.35f;
    n = n < 0 ? 0 : n > 1 ? 1 : n;
    return 1 - kGrime * (1 - n);
}

bool IsSkin(float r, float g, float b) {
    return r > g && g > b && r - b > 0.08f && r - g < 0.9f * (r - b) && r > 0.15f && g > 0.5f * r;
}

void Zombie(float* c, int x, int y) {
    float lum = 0.30f * c[0] + 0.59f * c[1] + 0.11f * c[2];
    bool skin = IsSkin(c[0], c[1], c[2]);
    float k = Grime(x, y);
    for (int i = 0; i < 3; i++) {
        float v = skin ? kSkin[i] * (0.35f + 1.1f * lum) : (c[i] * (1 - kDesaturate) + lum * kDesaturate) * kTint[i];
        v *= k;
        c[i] = v > 1 ? 1 : v;
    }
}

void GreenBlood(float* c) {
    float r = c[0], g = c[1], b = c[2];
    c[0] = g * 0.7f + 0.05f;
    c[1] = r * 0.72f;
    c[2] = b * 0.4f;
}

void From565(uint16_t p, float* c) {
    c[0] = (float)((p >> 11) & 31) / 31;
    c[1] = (float)((p >> 5) & 63) / 63;
    c[2] = (float)(p & 31) / 31;
}
uint16_t To565(const float* c) {
    return (uint16_t)((int)(c[0] * 31 + 0.5f) << 11 | (int)(c[1] * 63 + 0.5f) << 5 | (int)(c[2] * 31 + 0.5f));
}

// ---------------------------------------------------------------------------
// Pixelmaps

constexpr int kRgb565 = 5, kArgb4444 = 0x12;
inline const char* Name(uint8_t* pm) { return *(const char**)(pm + 4); }
inline uint16_t* Pixels(uint8_t* pm) { return *(uint16_t**)(pm + 8); }
inline int Row(uint8_t* pm) { return *(int16_t*)(pm + 0x28) / 2; }
inline int Type(uint8_t* pm) { return pm[0x2c]; }
inline int Width(uint8_t* pm) { return *(uint16_t*)(pm + 0x34); }
inline int Height(uint8_t* pm) { return *(uint16_t*)(pm + 0x36); }

// The skin last seen, to find the blood in its <NAME>B.
std::string g_lastName;
std::vector<uint16_t> g_last;
int g_skins, g_bloodied, g_bloodArt;

bool BloodTexel(uint16_t now, uint16_t was, const float* c) {
    float o[3];
    From565(was, o);
    int diff = 0;
    for (int i = 0; i < 3; i++) diff += abs((int)(c[i] * 255) - (int)(o[i] * 255));
    float other = c[1] > c[2] ? c[1] : c[2];
    return now != was && diff > kBloodDiff && c[0] > 0.2f && c[0] > kBloodRed * other;
}

void Skin(uint8_t* pm) {
    uint16_t* px = Pixels(pm);
    int w = Width(pm), h = Height(pm), row = Row(pm);
    if (!px || w <= 0 || h <= 0) return;
    std::string name = Name(pm) ? Name(pm) : "";
    bool hit = name.size() == g_lastName.size() + 1 && _strnicmp(name.c_str(), g_lastName.c_str(), g_lastName.size()) == 0 &&
               (name.back() == 'B' || name.back() == 'b') && g_last.size() == (size_t)w * h;
    std::vector<uint16_t> before;
    if (!hit) before.resize((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t& p = px[y * row + x];
            float c[3];
            From565(p, c);
            if (hit && BloodTexel(p, g_last[(size_t)y * w + x], c)) {
                GreenBlood(c);
            } else {
                if (!hit) before[(size_t)y * w + x] = p;
                Zombie(c, x, y);
            }
            p = To565(c);
        }
    if (hit) {
        g_bloodied++;
    } else {
        g_lastName = name;
        g_last.swap(before);
        g_skins++;
    }
}

void Blood(uint8_t* pm) {
    uint16_t* px = Pixels(pm);
    int w = Width(pm), h = Height(pm), row = Row(pm), type = Type(pm);
    if (!px || (type != kRgb565 && type != kArgb4444)) {
        Log("zombies: %s left as it is (type %d)", Name(pm) ? Name(pm) : "?", type);
        return;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t& p = px[y * row + x];
            float c[3];
            if (type == kRgb565) {
                From565(p, c);
                GreenBlood(c);
                p = To565(c);
            } else {
                c[0] = (float)((p >> 8) & 15) / 15, c[1] = (float)((p >> 4) & 15) / 15, c[2] = (float)(p & 15) / 15;
                GreenBlood(c);
                p = (uint16_t)((p & 0xf000) | (int)(c[0] * 15 + 0.5f) << 8 | (int)(c[1] * 15 + 0.5f) << 4 |
                               (int)(c[2] * 15 + 0.5f));
            }
        }
    g_bloodArt++;
    Log("zombies: green blood in %s (%dx%d, type %d)", Name(pm) ? Name(pm) : "?", w, h, type);
}

enum Art { kNone, kPeeps, kGibs };
Art g_art;

const auto kMapAdd = (void*(__cdecl*)(void*))0x51efa0;  // BrMapAdd
void* __cdecl MapAdd(uint8_t* pm) {
    if (g_art == kPeeps && Type(pm) == kRgb565) Skin(pm);
    else if (g_art == kGibs) Blood(pm);
    return kMapAdd(pm);
}

const auto kLoadPeeps = (void(__fastcall*)(void*))0x4cbc10;
void __fastcall LoadPeeps(void* name) {
    g_art = kPeeps;
    kLoadPeeps(name);
    g_art = kNone;
}

constexpr uint32_t kGibColour = 0x0040a030;
const auto kMaterialUpdate = (void(__cdecl*)(void*, int))0x520e70;

void GreenChunk(uint8_t* model) {
    uint8_t* face = model ? *(uint8_t**)(model + 0xc) : nullptr;
    uint8_t* material = face ? *(uint8_t**)(face + 8) : nullptr;
    if (!material) return;
    *(uint32_t*)(material + 8) = kGibColour;
    kMaterialUpdate(material, 0x7fff);
}

const auto kLoadGiblets = (void(__fastcall*)(void*))0x4cb1e0;
void __fastcall LoadGiblets(void* file) {
    g_art = kGibs;
    kLoadGiblets(file);
    g_art = kNone;
    uint8_t* table = *(uint8_t**)0x694274;
    int count = *(int*)0x69bc18, chunks = 0;
    for (int i = 0; i < count; i++) {
        uint8_t* e = table + i * 0x18;
        for (int k = 0; k < e[2] && k < 3; k++, chunks++) GreenChunk(*(uint8_t**)(e + 0xc + k * 4));
    }
    GreenChunk(*(uint8_t**)0x694278);  // the spurting stump
    Log("zombies: %d giblet chunks green", chunks);
}

const auto kLoadPixelmap = (uint8_t*(__fastcall*)(void*))0x48ec00;
uint8_t* __fastcall LoadTrail(void* name) {
    uint8_t* pm = kLoadPixelmap(name);
    if (pm && Name(pm) && _stricmp(Name(pm), "GIBSMEAR") == 0) Blood(pm);
    return pm;
}

// ---------------------------------------------------------------------------
// Moans

constexpr double kPitch = 65536.0, kMoan = 0.7;
double g_pitch = kPitch;
const uint8_t* const kVoice = (const uint8_t*)0x65d758;  // per sound group; 1 = a voice

typedef int(__fastcall* PlaySoundFn)(void*, int, int, void*);
PlaySoundFn o_playPedSound;
int __fastcall PlayPedSound(void* ped, int group, int loop, void* car) {
    g_pitch = group >= 0 && group < 11 && kVoice[group] ? kPitch * kMoan : kPitch;
    static int logged;
    if (g_pitch != kPitch && logged++ < 3) Log("zombies: a moan (sound group %d) at %.0f%% pitch", group, kMoan * 100);
    return o_playPedSound(ped, group, loop, car);
}

// ---------------------------------------------------------------------------
// Wording

// "pedestrian(s)" in any case -> ZOMBIE(s), "Peds" -> Zombies, as the official
// patch has them. Empty if nothing changes.
std::string Reworded(const char* s) {
    std::string out;
    bool changed = false;
    for (const char* p = s; *p;) {
        if (_strnicmp(p, "pedestrian", 10) == 0) {
            out += "ZOMBIE";
            p += 10;
            if (*p == 's' || *p == 'S') out += 's', p++;
            changed = true;
        } else if (strncmp(p, "Peds", 4) == 0 && !isalpha((unsigned char)p[4]) && (p == s || !isalpha((unsigned char)p[-1]))) {
            out += "Zombies";
            p += 4;
            changed = true;
        } else {
            out += *p++;
        }
    }
    return changed ? out : std::string();
}

int Reword(char* s, size_t room) {
    if (!s) return 0;
    std::string now = Reworded(s);
    if (now.empty() || now.size() >= room) return 0;
    Log("zombies: \"%s\" -> \"%s\"", s, now.c_str());
    memcpy(s, now.c_str(), now.size() + 1);
    return 1;
}

char** const kText = (char**)0x6b5f40;  // TEXT.TXT, a line each, then null
constexpr int kTextRoom = 1024;
uint8_t* const* const kPowerups = (uint8_t* const*)0x6a0a54;
const int* const kPowerupCount = (const int*)0x6a0ad0;
constexpr int kPowerupSize = 0xac, kPowerupName = 0x50, kPowerupNameRoom = 0x40;

uint8_t* const kMain = (uint8_t*)0x5a80f0;
constexpr int kEnter = 0x108;
typedef int(__fastcall* ScreenFn)(void*);
ScreenFn g_mainEnter;
bool g_worded;

int __fastcall MainEnter(void* s) {
    if (!g_worded) {
        g_worded = true;
        int n = 0;
        for (int i = 0; i < kTextRoom && kText[i]; i++) n += Reword(kText[i], strlen(kText[i]) + 1);
        for (int i = 0; *kPowerups && i < *kPowerupCount; i++)
            n += Reword((char*)(*kPowerups + i * kPowerupSize + kPowerupName), kPowerupNameRoom);
        Log("zombies: %d strings reworded", n);
    }
    Log("zombies: %d skins, %d bloodied, %d blood pictures so far", g_skins, g_bloodied, g_bloodArt);
    return g_mainEnter(s);
}

// ---------------------------------------------------------------------------
// The shamble

// The tail of a stub after the site's own read of the drunk flag, which leaves
// ZF set for "not drunk": ZF clear (drunk) unless Groovin' or Panic is running.
void Shamble(uint8_t* stub) {
    static const uint8_t tail[] = {
        0x75, 0x19,                                // jne exit (really drunk)
        0x83, 0x3d, 0xa8, 0x47, 0x74, 0x00, 0x00,  // cmp dword [0x7447a8], 0 (Groovin')
        0x75, 0x0e,                                // jne plain
        0x83, 0x3d, 0xf8, 0x47, 0x74, 0x00, 0x00,  // cmp dword [0x7447f8], 0 (Panic)
        0x75, 0x05,                                // jne plain
        0x83, 0xfc, 0x00,                          // cmp esp, 0 (ZF clear: drunk)
        0xeb, 0x02,                                // jmp exit
        0x39, 0xc0,                                // plain: cmp eax, eax (ZF set)
        0xe9, 0,    0,    0,    0,                 // exit: jmp back to the je
    };
    memcpy(stub, tail, sizeof tail);
}
constexpr size_t kTailSize = 32, kTailJump = 28;

}  // namespace

void Install() {
    std::string mode = settings::Get("Game", "Pedestrians", "humans");
    if (_stricmp(mode.c_str(), "zombies") != 0) return;

    // 0x4cbd48: cmp [drunk], ebx (ebx = 0), then je at 0x4cbd4e.
    static const uint8_t moveOld[6] = {0x39, 0x1d, 0xb8, 0x47, 0x74, 0x00};
    uint8_t moveStub[6 + kTailSize];
    memcpy(moveStub, moveOld, 6);
    Shamble(moveStub + 6);
    // 0x4d4d64: mov ecx, [drunk]; test ecx, ecx, then je at 0x4d4d6c.
    static const uint8_t frameOld[8] = {0x8b, 0x0d, 0xb8, 0x47, 0x74, 0x00, 0x85, 0xc9};
    uint8_t frameStub[8 + kTailSize];
    memcpy(frameStub, frameOld, 8);
    Shamble(frameStub + 8);
    bool shamble = hook::Divert(0x4cbd48, moveOld, 6, moveStub, sizeof moveStub, {{6 + kTailJump, 0x4cbd4e}}) &&
                   hook::Divert(0x4d4d64, frameOld, 8, frameStub, sizeof frameStub, {{8 + kTailJump, 0x4d4d6c}});

    static const uint8_t soundStart[5] = {0x53, 0x55, 0x56, 0x8b, 0xf1};
    o_playPedSound = (PlaySoundFn)hook::Detour(0x4cd880, soundStart, 5, (const void*)&PlayPedSound);
    uint32_t pitch = (uint32_t)(uintptr_t)&g_pitch;
    bool moans = o_playPedSound && hook::PatchU32(0x4cd953, 0x58aa58, pitch) && hook::PatchU32(0x4cd999, 0x58aa58, pitch);

    bool art = hook::Redirect(0x502641, 0xe8, 0x51efa0, (const void*)&MapAdd) &&
               hook::Redirect(0x406ba1, 0xe8, 0x4cbc10, (const void*)&LoadPeeps) &&
               hook::Redirect(0x4caf39, 0xe8, 0x4cb1e0, (const void*)&LoadGiblets) &&
               hook::Redirect(0x4e9ce1, 0xe8, 0x48ec00, (const void*)&LoadTrail);

    uint32_t* enter = (uint32_t*)(kMain + kEnter);
    g_mainEnter = (ScreenFn)(uintptr_t)*enter;
    *enter = (uint32_t)(uintptr_t)&MainEnter;
    Log("zombies: on: shamble %s, moans %s, skins and blood %s", shamble ? "on" : "FAILED", moans ? "on" : "FAILED",
        art ? "on" : "FAILED");
}

}  // namespace zombies
