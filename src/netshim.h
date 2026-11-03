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

// Where joining by address stands: no address; resolving it; not an address
// (or the name didn't resolve); searching it; the host there has answered
// (its IPX node in `host`).
enum JoinState { kJoinNone, kJoinResolving, kJoinBadAddress, kJoinLooking, kJoinAnswered };
JoinState GetJoinState(uint8_t host[6]);

}  // namespace netshim
