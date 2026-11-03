#pragma once
// The host's public address for friends, and the router port opened while
// hosting (UPnP, with a web-service fallback for the address).

#include <string>

namespace publicaddr {

// Before the game runs.
void Install();

// The player started or stopped hosting: start finding the address (and
// opening the port), or remove the port mapping.
void HostingChanged(bool hosting);

// The game is shutting down: remove the mapping now (waits up to 2 s).
void Shutdown();

// The lobby's lines: the address to give friends, the port's state, the LAN address.
void Lines(std::string* address, std::string* port, std::string* lan);

}  // namespace publicaddr
