#pragma once
// The game's IPX networking carried over UDP by the patch (replaces the
// store's ipxwrapper for the game): several copies per PC, join by IP.

#include <cstdint>
#include <string>

namespace netshim {

// Before the game runs (after the test harness, which decides loopback).
// False when the store's ipxwrapper stays (Transport=ipxwrapper) or it failed.
bool Install();

// The host to also send game searches to ("host[:port]"; empty for none),
// from the Network screen. Takes effect within the game's next search.
void SetJoinAddress(const std::string& text);

// Where joining by address stands: no address; looking the name up; not a
// valid address (malformed: no lookup is made); the name wasn't found;
// searching the address; the host there has answered (its IPX node in `host`).
enum JoinState { kJoinNone, kJoinResolving, kJoinInvalid, kJoinNotFound, kJoinLooking, kJoinAnswered };
JoinState GetJoinState(uint8_t host[6]);

// The UDP port the game's socket is on (0 before it binds), and whether this
// is a test run on loopback only.
int BoundPort();
bool Loopback();

}  // namespace netshim
