#pragma once
// The game's IPX networking carried over UDP by the patch (replaces the
// store's ipxwrapper for the game): several copies per PC, join by IP.

namespace netshim {

// Before the game runs (after the test harness, which decides loopback).
void Install();

}  // namespace netshim
