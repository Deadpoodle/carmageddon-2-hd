// What the game was doing when it hung or crashed. A stack says where the code
// was; this says which track, where the cars were and what each opponent was
// up to, which is what it took a player's memory to supply for the hang fixed
// in fixes.cpp ("the second race of the second group; a car went in the water").
//
// It runs from the hang watch's thread and from the crash handler, so the game
// may be mid-write or its memory bad: everything goes through ReadProcessMemory,
// which fails rather than faults, and no game code is called.
#include "gamestate.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "log.h"

namespace gamestate {
namespace {

constexpr uintptr_t kRaceInfo = 0x762438;   // the race being run: its entry in the race list, 0 outside a race
constexpr uintptr_t kRaceList = 0x74d660;   // RACES.TXT
constexpr uint32_t kRaceSize = 0x244;
constexpr uint32_t kRaceName = 0, kRaceFile = 0x60, kRaceGroup = 0x240;
constexpr uintptr_t kGroupTable = 0x68b8a0;  // pointer to the groups, 0xc each
constexpr uint32_t kGroupSize = 0xc;
constexpr uintptr_t kGameTime = 0x79efb8;    // gTime_stamp_for_this_munging, ms
constexpr uintptr_t kTimer = 0x761ee8;       // time left, ms
constexpr uintptr_t kCredits = 0x75bb80;
constexpr uintptr_t kYon = 0x761f4c;
constexpr uintptr_t kInViewDistance = 0x691764;
constexpr uintptr_t kPlayerActor = 0x75bc3c;
constexpr uintptr_t kOpponentCount = 0x75d7a0;
constexpr uintptr_t kOpponents = 0x75d8a0;
constexpr uint32_t kOpponentSize = 0x1a4;
constexpr int kMaxOpponents = 32;  // far above any race: a bad count can't flood the log
// An opponent: +4 objective, +8 car, +0x18 distance to the player, +0xb0 flags.
// A car: +0x10 actor, +0xec name. An actor: +0x50 position.
constexpr uint32_t kObjective = 4, kCar = 8, kDistance = 0x18, kFlags = 0xb0;
constexpr uint32_t kCarActor = 0x10, kCarName = 0xec, kActorPosition = 0x50;
constexpr uint32_t kFlagPhysics = 8, kFlagCheating = 0x20;
// Traffic: 0x5d8 bytes each, +0x38 state (2 driving, 3 knocked about, 4 stopped),
// +0x44 processed (moving), +0x45 inside the drawn box, +0xec actor.
constexpr uintptr_t kDrones = 0x684504, kDroneCount = 0x6820d0;
constexpr uint32_t kDroneSize = 0x5d8, kDroneState = 0x38, kDroneProcessed = 0x44, kDroneNear = 0x45, kDroneActor = 0xec;
constexpr int kMaxDrones = 200;

template <typename T>
bool Read(uintptr_t address, T* out) {
    return ReadProcessMemory(GetCurrentProcess(), (const void*)address, out, sizeof(T), nullptr) != 0;
}

// A string of the game's, always terminated; "?" if it can't be read.
void ReadText(uintptr_t address, char* out, size_t size) {
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (const void*)address, out, size - 1, &got) || !got) {
        snprintf(out, size, "?");
        return;
    }
    out[got] = 0;
}

struct Position {
    float x, y, z;
};

bool ReadPosition(uintptr_t actor, Position* out) { return actor && Read(actor + kActorPosition, out); }

// "CITY OF THE LOST GIBLETS" (NEWCITY3.TXT), race 6 of the list, group 2.
void DescribeRace(uintptr_t race, char* out, size_t size) {
    char name[64], file[32];
    ReadText(race + kRaceName, name, sizeof name);
    ReadText(race + kRaceFile, file, sizeof file);
    uintptr_t group = 0, groups = 0;
    Read(race + kRaceGroup, &group);
    Read(kGroupTable, &groups);
    int number = (int)((race - kRaceList) / kRaceSize) + 1;
    if (group >= groups && groups)
        snprintf(out, size, "\"%s\" (%s), race %d of the list, group %d", name, file, number,
                 (int)((group - groups) / kGroupSize) + 1);
    else
        snprintf(out, size, "\"%s\" (%s), race %d of the list", name, file, number);
}

void LogTraffic() {
    uintptr_t drones = 0;
    int count = 0;
    if (!Read(kDrones, &drones) || !drones || !Read(kDroneCount, &count) || count <= 0 || count > kMaxDrones) return;
    static Position stopped[kMaxDrones];
    int states[6] = {}, moving = 0, drawn = 0, wrecks = 0;
    for (int i = 0; i < count; i++) {
        uintptr_t d = drones + (uintptr_t)i * kDroneSize, actor = 0;
        int state = 0;
        uint8_t processed = 0, drawnBox = 0;
        Read(d + kDroneState, &state);
        Read(d + kDroneProcessed, &processed);
        Read(d + kDroneNear, &drawnBox);
        if (state >= 0 && state < 6) states[state]++;
        if (processed) moving++;
        if (drawnBox) drawn++;
        if (processed && (state == 3 || state == 4) && Read(d + kDroneActor, &actor) && ReadPosition(actor, &stopped[wrecks]))
            wrecks++;
    }
    int piled = 0;  // stopped cars with another stopped car touching them
    for (int i = 0; i < wrecks; i++)
        for (int j = 0; j < wrecks; j++) {
            float dx = stopped[i].x - stopped[j].x, dy = stopped[i].y - stopped[j].y, dz = stopped[i].z - stopped[j].z;
            if (i != j && dx * dx + dy * dy + dz * dz < 9) {
                piled++;
                break;
            }
        }
    Log("state: traffic: %d in all, %d moving, %d in the drawn box; driving %d, knocked about %d, stopped %d, %d of them piled up",
        count, moving, drawn, states[2], states[3], states[4], piled);
}

}  // namespace

void LogSnapshot() {
    uintptr_t race = 0;
    if (!Read(kRaceInfo, &race) || !race) {
        Log("state: not in a race");
        return;
    }
    char text[160];
    DescribeRace(race, text, sizeof text);
    Log("state: in %s", text);

    unsigned now = 0;
    int timer = 0, credits = 0;
    float yon = 0, inView = 0;
    Read(kGameTime, &now);
    Read(kTimer, &timer);
    Read(kCredits, &credits);
    Read(kYon, &yon);
    Read(kInViewDistance, &inView);
    Log("state: game time %u ms, timer %d ms, credits %d, yon %.0f, opponents' view distance %.0f", now, timer, credits,
        yon, inView);

    uintptr_t playerActor = 0;
    Position player;
    if (Read(kPlayerActor, &playerActor) && ReadPosition(playerActor, &player))
        Log("state: player at %.1f, %.1f, %.1f", player.x, player.y, player.z);

    LogTraffic();

    int count = 0;
    Read(kOpponentCount, &count);
    if (count < 0 || count > kMaxOpponents) {
        Log("state: opponent count %d doesn't look right", count);
        return;
    }
    for (int i = 0; i < count; i++) {
        uintptr_t opponent = kOpponents + (uintptr_t)i * kOpponentSize, car = 0, actor = 0;
        int objective = -1;
        uint32_t flags = 0;
        float distance = 0;
        Position at = {};
        char name[32] = "?";
        Read(opponent + kObjective, &objective);
        Read(opponent + kDistance, &distance);
        Read(opponent + kFlags, &flags);
        if (Read(opponent + kCar, &car) && car) {
            ReadText(car + kCarName, name, sizeof name);
            Read(car + kCarActor, &actor);
            ReadPosition(actor, &at);
        }
        Log("state: opponent %d %s: objective %d, flags %#x (%s%s), at %.1f, %.1f, %.1f, %.1f from the player", i, name,
            objective, flags, flags & kFlagPhysics ? "physics" : "no physics", flags & kFlagCheating ? ", cheating" : "",
            at.x, at.y, at.z, distance);
    }
}

void Poll() {
    static uintptr_t last;
    uintptr_t race = 0;
    if (!Read(kRaceInfo, &race) || race == last) return;
    last = race;
    if (!race) {
        Log("state: race over");
        return;
    }
    char text[160];
    DescribeRace(race, text, sizeof text);
    Log("state: race started: %s", text);
}

}  // namespace gamestate
