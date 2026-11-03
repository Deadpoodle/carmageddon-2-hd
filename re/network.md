# Network layer (CARMA2_HW.EXE, Steam build)

Static analysis only: the Ghidra export plus capstone/pefile on the real bytes, and a read of the ipxwrapper 0.4.0 source (`testrun/ipxwrapper/ipxwrapper-0.4.0-src.zip`). Tags: **H** = read from the instructions or the static data; **M** = from the decompiler, or an inference from it; **L** = to be checked at run time.

Ghidra missed several functions in this area. PDNetInitialise 0x519560, NetInitialise 0x49d210, Network_Enter 0x467ef0, GameList_Click 0x4682c0, Network_Update 0x469320 and PDNetSendMessageToAllPlayers 0x51a2e0 have no entry in `functions.tsv`, and Ghidra folds their xrefs into the neighbouring function. Every address below was checked against the disassembly.

Fastcall is the norm: `ecx` holds the first argument and `edx` the second.

## Summary

- The game speaks **IPX only**. It makes one `socket(AF_IPX=6, SOCK_DGRAM, NSPROTO_IPX=1000)` and has no AF_INET path. Windows 10 has no IPX stack, so LAN play works only through the shipped ipxwrapper 0.4.0, which tunnels IPX over UDP (H).
- **ipxwrapper cannot do either of our goals** (H from its source):
  - It cannot run two copies on one PC. The second bind of IPX socket 0x2FFE fails, and the two copies share one node number, so each drops the other's packets anyway.
  - It cannot join by typed IP. A unicast goes to a cached address or is sent as a LAN broadcast, and incoming packets from a different subnet are dropped.
- **Recommendation:** our dinput proxy replaces the 11 WSOCK32 import slots with a small **IPX-over-UDP shim**. That gives:
  - a node number per instance and one UDP port per instance;
  - routing by node, so NAT is handled;
  - join-by-IP by also sending the game's own discovery packet ("XXXXCAR2MSG1") to the typed address.

  Two-player internet play then needs no patch to the game's code. The host forwards one UDP port.

## 1. Address family, ports, broadcast, address format

- **PDNetInitialise 0x519560** (called from NetInitialise 0x49d210 at 0x49d305) (H):
  1. Reads `NETWORK.INI` from the **current directory**: section `[Network]`, keys `IPXSocket` (hex) and `Network1`..`Network15` (hex network numbers). The path is built at startup in 0x51aaa0 into 0x6b3380; flag 0x6ad4c4. The Steam folder has no NETWORK.INI.
  2. `WSAStartup(0x101)`.
  3. `socket(6, 2, 1000)` → 0x6619b0.
  4. `setsockopt(SOL_SOCKET, SO_BROADCAST, &1)`.
  5. `setsockopt(SOL_SOCKET, SO_LINGER, {1,0})`.
  6. `ioctlsocket(FIONBIO, &1)`, which makes the socket **non-blocking**. The constant 1 is at 0x6619b4.
  7. `bind(&own, 14)`.
  8. `getsockname(&own)`.
  9. Copies the address into 0x6abf08 and formats it as a string into 0x6ac320.
  10. Sets 0x6ac578=1 if the string contains the developer node "00a0240f9fac".
  11. Returns 0 on success.
- **No SO_REUSEADDR** is set (H).
- **Own address** 0x6ac310 is a `sockaddr_ipx` of 14 bytes:
  - family 6;
  - net 0, node 0, which lets the stack choose;
  - **socket 0x2FFE**. The word 0xFE2F is stored at 0x6ac12c, which gives the bytes 2F FE in network order. `IPXSocket=` in the INI overrides it.
  - Pointer to it: 0x6ac358.
- **Broadcast address** 0x6ac300 (pointer 0x6abf00):
  - node FF:FF:FF:FF:FF:FF;
  - the same socket number as the own address;
  - the net is rewritten for each entry of the network list 0x6ac130 (count 0x6ac128). Entry 0 is the own net; the INI adds `Network1..15` when they differ from it.
  - One broadcast is sent per network (H).
- **Fixed numbers:**
  - The game uses only IPX socket 0x2FFE.
  - ipxwrapper carries it on UDP: port **54792** is shared and receives broadcasts only. Each process also binds a **random private UDP port** on 0.0.0.0 for sending and unicast (from ipxwrapper's `router.c` and `config.h`) (H).
- **The "%s" format** (string at 0x661a34): `%2.2x×4 : %2.2x×6 , %2.2x×2`, which is net:node,socket in hex. Example: `00000001:0a1b2c3d4e5f,2ffe` (H).

## 2. Discovery (finding games) and joining

- **The joiner's request** comes from PDNetGetNextJoinGame 0x519ab0 when pIndex==0 (H):
  - At most every **3000 ms** (next-time 0x6ac580) it sprintfs `"XXXX%s%0.1d"` with "CAR2MSG" and 1, giving **`XXXXCAR2MSG1\0` (13 bytes)** in 0x6ac360.
  - It `sendto`s that to the broadcast address for each network. The call site is **0x519c62**, which calls the sendto thunk 0x51deca.
- **The host's answer** comes from PDNetGetNextMessage 0x51a380 (H):
  - It checks `strncmp(buf+4, "CAR2MSG", 7)` (the length 7 is stored in 0x6abf04), then reads the digit.
  - Digit 1, while `gNet_mode` 0x68b918 == 2 (host): it sends `XXXXCAR2MSG2\0` back to the recvfrom source 0x6ac118, at 0x51a58d.
  - Digit 2 is ignored by the host.
  - Anything else is a game message and is passed up.
- **The joiner's list** (H):
  - 0x519ab0 drains the socket with recvfrom into 0x6abf18 (512 bytes), source in 0x6ac118 / pointer 0x6ac340.
  - It discards packets from its own node: the comparison covers the **6 node bytes only**, not net or socket.
  - It discards any packet that is not "CAR2MSG2".
  - A host reply is stored in the PD list 0x6ac574: 16 entries × 0x14 (sockaddr_ipx 14 bytes, pad, u32 last-seen at +0x10). The count is in 0x6ac35c. Duplicates are matched by node. **There is no bounds check at 16.**
  - Entries not seen for **10 s** are pruned.
  - `pIndex` returns entry n, which the caller copies (14 bytes) into a new game-details struct at +0.
  - **So the address used for the game is the UDP/IPX source of the host's reply.**
- **The details poll** is DoNextJoinPoll 0x49ec70, called from NetService (H for the flow, M for the names):
  1. Allocates a game-details struct (0x78 bytes).
  2. Calls PDNetGetNextJoinGame.
  3. Sends message **type 0 (SENDMEDETAILS)** to that address through PDNetSendMessageToAddress 0x51a630.
  4. Waits up to **10 s** (0x2711) for **type 1 (DETAILS)**. The handler in ReceivedMessage copies 106 bytes (host name, options, etc.) into details+0x0e and sets 0x68d978.
  5. Calls the list callback 0x690230 = **AddToJoinList 0x4681d0**. That dedupes by game ID (PDNetExtractGameID 0x51a0d0 = node bytes 2..5) into the menu table **0x688718**: 6 entries × 8 bytes (details pointer, time added).
- **Joining:** the JOIN item calls 0x468a50, then **NetJoinGame 0x49e970** (`ecx` = details) (H):
  1. Sets gNet_mode = 3 (client).
  2. Sets the local ID 0x74b7dc = PDNetExtractPlayerID 0x51a130, which is **own node bytes 2..5**.
  3. Sends **type 2 (JOIN)** as a guaranteed message to `details+0` (call at 0x49ea80). JOIN carries the joiner's 0xd4-byte player record.
  4. Spins NetService for up to **30 s** until a player list arrives, or until it is refused (0x68d988) or the host is lost.
- **The host's handling of JOIN** is in ReceivedMessage 0x4a0260, case 2 (H):
  - Refuses with type 3 and count 0 if the game is closed while racing, already has ≥12 players, or 0x68d960 is set. Returns −1 if the chosen car is taken.
  - Otherwise copies the joiner's record and then **overwrites its address with the recvfrom source** (PDNetSetPlayerSystemInfo 0x51a5f0, called at 0x4a095f with `edx` = sender).
  - Gives a blank name "PLAYER %d".
  - Sends **type 3 (NEWPLAYERLIST)**, one guaranteed message per player, to every player.
- **After the join**, every message goes to addresses in the player table. The client's table is the host's list, copied verbatim (ReceivedNewPlayerList 0x4a48d0). **The host's own entry holds the host's getsockname address**, set at 0x49e313, not the address the client used (H).

## 3. Message layer

- **Message header** (0x18 bytes; PDNetGetHeaderSize 0x51a6a0 returns 0) (H for the offsets, M for the names):

| Off | Field |
|---|---|
| +0x00 | pd scratch (unused) |
| +0x04 | magic 0x763a5059; the receive loop 0x4a4a40 drops mismatches |
| +0x08 | sender player ID |
| +0x0c | guarantee field (alloc sets 1) |
| +0x10 | sender time (ms) |
| +0x14 | u16 num_contents (0xff = guaranteed template, not freed) |
| +0x16 | u16 overall size |

- **Contents** start at +0x18. Each is `[u8 size][u8 type]` followed by its data. The first content's type is at **+0x19**, which is what the "message type %d" log prints.
- **Size limits:** at most **512 bytes** per datagram. Messages are **batched per destination** in player+0xcc and flushed when full. NetService flushes every 200 ms, or every service call while not racing (0x49fcf0).
- **Types seen** (H for the numbers, M for the names):
  - 0 SENDMEDETAILS, 1 DETAILS, 2 JOIN, 3 NEWPLAYERLIST (count 0 = refused, −1 = car taken)
  - 4 GUARANTEE ACK (matches by u16 number)
  - 5 guarantee trailer (u16 number; receiver dedupes via 0x74a6a0, 15 slots, and acks)
  - 6 → 7 car-details request and reply
  - 0x0b client status to the host, every 1 s
  - 0x0d head-up text
  - 0x2e host heartbeat with player statuses, every 1 s (0x4a4ac0)
  - The switch runs to 0x33.
- **Reliability** (0x4a5ca0 / 0x4a6080) (H):
  - Guaranteed sends go into a table at 0x68d990: 200 entries × 0x34 bytes, number 0x659d30.
  - The first resend is after **100 ms**, then each interval is ×1.2 (double at 0x58a428).
  - A send is dropped after **10 s** unless it has a fail callback.
  - Entries are sent either by player ID or by address (flag at +0x30).
- **Timeouts** (H):
  - Host kicks a player not heard from for **60 s** (0x4a4da0).
  - Client declares the host lost after **60 s**.
  - The menu list hides a game after **15 s** (0x4682c0, 0x3a98).
  - The time base is QPC milliseconds since start (0x51d410).
  - **All of these tolerate internet latency.**
- **Players:** **12 slots** (H: NetSync shows 12 slots, the host refuses at ≥12, and the GameList click needs fewer than 12 players).
  - Table **0x74bcc0**, stride 0xd4.
  - Count 0x74b7d4, own index 0x74b7d0, local ID 0x74b7dc, current game 0x74b784.
  - Record fields: +0 sockaddr_ipx, +0x14 last heard, +0x24 ID, +0x28 name, +0x48 status, +0x4c car, +0xc8 car spec, +0xcc pending batched message.
- **Authority:**
  - The **host** owns membership, game details, start race, the heartbeat and the player list.
  - In-race traffic is a **mesh**. "Broadcast" contents (0x49f9c0 / 0x49fb70, used from gameplay code at 0x436170, 0x43d1d0, 0x497900, …) and head-ups (0x49d430) are copied into **every other player's batch and sent directly to that player's address** (H for the loops).
  - **With 3+ players, clients must reach each other.**

## 4. Menus (for a test script)

All coordinates are in 640x480 space; clicks are the easiest route. The screens are described in `re/menus.md`.

- **Main 0x5a80f0:**
  - item 1 "NETWORK" at (214,95), callback 0x467eb0. That calls NetInitialise, which is the **first winsock use**; 0x74c6b4 = success.
  - Target: Network.
- **Network 0x5b39b8** (31 items; enter **0x467ef0**, exit 0x468260, update **0x469320**; pointers at 0x5b3ac0/c4/c8):
  - left: game type rows 3-6 (26,123..178), track rows 10-13 (26,248..302), Options 15 → NetworkOptions, Cancel 16 (24,380), **HOST 17 (286,380)** → 0x468e80;
  - right: **game list rows 20-23 (399,126/144/162/180)** → GameList_Click 0x4682c0, **Driver name 26 (478,217)** → 0x4670c0, **JOIN 29 (536,260)** → 0x468a50, status line 30.
  - Enter starts the join list with NetStartProducingJoinList 0x49dbd0 (callback 0x4681d0). So **just sitting on this screen broadcasts**.
- **The HOST quirk (M; check L):** HOST and JOIN both return early unless the selected-game index **0x688760 ≥ 0**.
  - Its initial value is 1 (.data).
  - It drops to −1 after a failed or cancelled host/join, or after clicking an empty or closed list row.
  - After that, HOST does nothing until a valid game row is clicked.
  - For tests, write 0 to 0x688760 before clicking HOST, or NOP the `jl` at 0x468e9c.
  - The host picks a **random allowed car** (0x468d70); there is no UI for it.
- **Text entry: none required.** The driver name is 0x75d590.
  - It is saved to OPTIONS.TXT by 0x48d190 (key `NetName`, M).
  - Clicking item 26 toggles edit mode (0x466ec0, flag 0x68b858) fed by the WM_CHAR buffer.
  - An empty name becomes "HOST" or "PLAYER n".
- **NetSync 0x61bad0**, entered by mode 3 from the race flow 0x503c50:
  - shows 12 player slots;
  - host's **START 26 (440,430)** → 0x4735a0 needs **>1 player**;
  - ABORT 27 (110,430) → 0x4735d0;
  - clients get no Start.
- **NetworkSummary 0x627398:** times out after 30 s; "continue" is item 79.
- **NetworkOptions 0x610208:** radio rows only, no text.

## 5. What breaks on modern Windows and with two copies on one PC

- **No IPX in Windows (H):** without ipxwrapper, `socket(AF_IPX)` fails and the log shows "Failed to create socket".
- **Two copies under ipxwrapper (H from the code; L at run time):**
  1. Its bind check is cross-process through shared memory, and it compares **only the socket number** (`addrtable.c`). The game does not set SO_REUSEADDR, so the second copy's bind fails with **10048** and network init fails.
  2. Even with a reuse hack, both copies get the **same node**. The node is per machine, from the registry; the wildcard interface has a random node. The game drops packets from its own node in both 0x519ab0 and 0x51a380, so the copies are invisible to each other.
  3. A different `IPXSocket` per copy does not help, because discovery is sent to the sender's own socket number.
- **Internet under ipxwrapper (H):**
  - `sendto` uses a learned address cache (TTL 30 s); otherwise it broadcasts on UDP 54792 to each interface's subnet broadcast.
  - The receive side **drops packets whose source IP is not in the bound interface's subnets** (`router.c`, `source_ok`).
  - So a typed IP can be neither addressed nor heard.
- **Firewall and registry side effects (M):**
  - The stub `wsock32.dll` loads `ipxwrapper.dll` lazily, on the first winsock call, i.e. the Network menu (`stubdll.c`, `common.c`).
  - It then binds **0.0.0.0:54792 and 0.0.0.0:random**. The first time, this is likely to raise the Windows Defender Firewall "allow access" prompt **on the player's screen**.
  - It also writes `HKCU\Software\IPXWrapper` (the random wildcard node).
  - The stub truncates `ipxwrapper.log` at every launch (H), which explains the 0-byte log.
  - Tests must not let ipxwrapper load. Our shim should bind **127.0.0.1** in test mode; a loopback bind does not trigger the prompt (M).
- **Broadcast:**
  - ipxwrapper sends to the subnet broadcast of each adapter (VPN and virtual adapters included).
  - Windows does not deliver 255.255.255.255 to other loopback sockets reliably, so copies on the same PC need unicast to 127.0.0.1 (M).
- **Timers and loops:**
  - QPC milliseconds per process: fine.
  - Name edit and similar spots busy-wait for 200 ms: harmless.
  - **Risk (L):** if an unfocused or hidden copy stalls its main loop (WM_ACTIVATEAPP handling, `re/input.md`), the other side waits up to 60 s and then drops it. The harness must keep both loops running.

## 6. Join by IP: hook points and the minimal change

- **Do not keep ipxwrapper for this** (§5). Replace the game's IAT slots in **.rdata** (VirtualProtect first; DINPUT is a static import, so our proxy loads before any winsock call). The thunks are at 0x51de8e..0x51decc:

| Slot | Function | Thunk |
|---|---|---|
| 0x589244 | socket | 0x51deb8 |
| 0x589248 | sendto | 0x51deca |
| 0x58924c | WSAStartup | 0x51debe |
| 0x589250 | closesocket | 0x51de9a |
| 0x589254 | bind | 0x51de94 |
| 0x589258 | getsockname | 0x51de8e |
| 0x58925c | recvfrom | 0x51dec4 |
| 0x589260 | WSAGetLastError | 0x51deb2 |
| 0x589264 | WSACleanup | 0x51deac |
| 0x589268 | ioctlsocket | 0x51dea0 |
| 0x58926c | setsockopt | 0x51dea6 |

  Redirect all 11; then ipxwrapper.dll is never loaded, because the stub only loads it on demand (M).
- **What the shim does:**
  - `socket(6,2,1000)` returns a handle backed by a real UDP socket from **system** `ws2_32.dll`.
  - `bind`: UDP **port P**, configurable, e.g. 12286 (= 0x2FFE). Use 0.0.0.0, or 127.0.0.1 in test mode. Fall back to an ephemeral port if P is taken, so a second local copy still starts.
  - `getsockname`: net 00000001, a **random node per instance** (never 00a0240f9fac), socket 0x2FFE.
  - **Framing:** each datagram carries a small header (magic, source node, destination node) and then the payload.
  - `recvfrom` (H):
    - strips the header and **learns node → UDP source endpoint**;
    - returns a sockaddr_ipx with the sender's node;
    - is non-blocking: returns −1 with **WSAEWOULDBLOCK 10035 (0x2733)**, the value 0x519ab0 tests;
    - delivers at most 512 bytes.
  - `sendto(node)` goes to the learned endpoint. For node FF×6 it sends to the LAN broadcast on P, to **127.0.0.1** at the local instances' ports (a tiny shared-memory list, or P plus an ephemeral list), **and to each typed join address**.
- **Join by IP is then purely in the shim:**
  1. The joiner's discovery broadcast (call site 0x519c62, payload `XXXXCAR2MSG1`) is also unicast to `host:P`.
  2. The host's game replies `CAR2MSG2` from P. The joiner's shim learns host node → host IP:P.
  3. The game adds the entry, fetches DETAILS, and lists it by host name. The player clicks the row, then JOIN.
  - The host's self-reported player-0 address is reachable too, because the shim **routes by node, not by IP**.
  - **The host forwards UDP P (one port).** Joiners need no forwarding; replies go to the address the host observed.
- **Typed address input:**
  - Start with `Carma2HD.ini` `[Net] Join=host[:port]` and/or a command-line switch.
  - Later: an "Address" field on the Network screen, added by wrapping Network_Enter 0x467ef0 (struct +0x108 at 0x5b3ac0). It can reuse the name-edit mechanism (0x466ec0 and the WM_CHAR buffer), as in `re/hd-tab.md`.
- **Alternatives considered:**
  - A fake row in the PD list 0x6ac574 or the menu table 0x688718. It still needs DETAILS and a transport underneath; no gain.
  - Patching 0x519c62 to add a second `sendto`. Moot with ipxwrapper underneath; unnecessary with the shim.
- **3+ players over the internet:** clients send to each other (§3). A client may never have heard from a peer's node.
  - **v1:** the shim relays unknown-node packets through the host's shim (the joined address). The host knows every client's public endpoint.
  - **Later:** hole punching.
  - LAN and same-PC games are direct.

## 7. Risks and plan

**Risks**
- Shim fidelity: non-blocking semantics, the 14-byte sockaddr, the error codes the game checks (0x2733), and 512-byte payloads (H for the requirements).
- Interop: our framing is not ipxwrapper-compatible, so stock-ipxwrapper players can't join. ipxwrapper's 30-byte `ipx_packet` header could be adopted later if wanted (L).
- NAT: symmetric NATs on the joiner side could break the client↔client mesh. This is why the relay is needed.
- The HOST guard on 0x688760 (§4).
- The unbounded PD list (more than 16 hosts on one LAN; L).
- The firewall prompt in user mode on the first non-loopback bind (expected for hosting; document it, never auto-add rules).
- Hidden copies stalling (§5).

**Plan**
1. Shim behind `[Net] Shim=1`: IAT redirect, per-instance node, loopback test mode, logging to our log.
2. Harness test: two copies; host on P, joiner on an ephemeral port. Script: Main (214,95) → Network; host poke 0x688760=0 → HOST (286,380) → NetSync. Joiner waits for the row (399,126) → JOIN (536,260). Then host START (440,430). Assert from our log and the game's state: 0x68b918 = 2/3, 0x74b7d4 = 2.
3. Join-by-IP via `Join=` (unicast discovery), tested over loopback with a non-default P, then across a real WAN.
4. Relay for 3+ players.
5. Address field in the Network menu.
6. Update the README and WIKI_LOG.

## names.tsv

```
0x519560	PDNetInitialise	AF_IPX socket, SO_BROADCAST, SO_LINGER, FIONBIO, bind 0x2FFE, getsockname; NETWORK.INI
0x519a10	PDNetShutdown	closesocket
0x519a40	PDNetStartProducingJoinList	alloc 16x0x14 PD game list
0x519a80	PDNetEndJoinList
0x519ab0	PDNetGetNextJoinGame	broadcast XXXXCAR2MSG1 every 3 s; collect CAR2MSG2 replies; 10 s expiry
0x51a010	PDNetFormatAddress	sockaddr_ipx -> "net:node,sock"
0x51a080	PDNetHostGame	*out = &own address
0x51a0a0	PDNetJoinGame	returns 0
0x51a0b0	PDNetLeaveGame
0x51a0c0	PDNetHostFinishGame
0x51a0d0	PDNetExtractGameID	node bytes 2..5
0x51a130	PDNetExtractPlayerID	own node bytes 2..5
0x51a190	PDNetObtainSystemUserName	GetComputerNameA
0x51a210	PDNetSendMessageToPlayer	unused
0x51a2e0	PDNetSendMessageToAllPlayers	unused
0x51a380	PDNetGetNextMessage	recvfrom; host answers CAR2MSG1 with CAR2MSG2; drops own node
0x51a5f0	PDNetSetPlayerSystemInfo	copy 14-byte address
0x51a630	PDNetSendMessageToAddress	sendto(len = msg+0x16)
0x51a6a0	PDNetGetHeaderSize	returns 0
0x5194f0	PDNetFindPlayerByID	unused
0x51d410	PDGetTotalTime	QPC ms since start
0x49d210	NetInitialise	message pools 3x200, PDNetInitialise
0x49d370	NetShutdown
0x49d430	NetSendHeadupToAllPlayers	type 0x0d, per-player batches
0x49dbd0	NetStartProducingJoinList	ecx = add-game callback
0x49dc00	NetEndJoinList
0x49dc80	NetDisposeGameDetails
0x49dc90	NetLeaveGame
0x49e210	NetHostGame	gNet_mode=2; player 0 address = own getsockname
0x49e3a0	NetInitPlayerInfo
0x49e970	NetJoinGame	gNet_mode=3; guaranteed JOIN to details+0; waits 30 s
0x49ec70	DoNextJoinPoll	SENDMEDETAILS, 10 s wait for DETAILS
0x49ee50	NetSendMessageToAddress
0x49eea0	NetSendMessageToPlayer	by ID via player table address
0x49f440	NetGetMessageSize
0x49f9c0	NetBeginBroadcastContents	to every other player (mesh)
0x49fb70	NetEndBroadcastContents
0x49fcf0	NetFlushBatches
0x49fdb0	NetAllocateMessage	magic 0x763a5059
0x49ff50	NetDisposeMessage
0x49ff70	NetGetNextMessage
0x4a0260	ReceivedMessage	switch on content type 0..0x33
0x4a48d0	ReceivedNewPlayerList
0x4a4a40	NetReceiveMessages
0x4a4ac0	NetSendHeartbeat	host 0x2e / client 0x0b, every 1 s
0x4a4da0	NetCheckTimeouts	60 s
0x4a5ca0	NetRegisterGuaranteed	100 ms, x1.2, 200 slots
0x4a5e20	NetGuaranteedSendToPlayer
0x4a6080	NetResendGuaranteed	10 s give-up
0x467eb0	Main_NetworkClick	NetInitialise
0x467ef0	Network_Enter	starts join list (callback 0x4681d0)
0x4681d0	AddToJoinList	menu table 0x688718
0x468260	Network_Exit
0x4682c0	GameList_Click	sets 0x688760; status text
0x468a50	Network_JoinClick
0x468e80	Network_HostClick	needs 0x688760>=0
0x468d70	NetPickRandomCar
0x469280	Network_CancelClick	NetShutdown
0x469320	Network_Update
0x4670c0	Network_NameClick	edit 0x75d590
0x4735a0	NetSync_StartClick	needs >1 player
0x4735d0	NetSync_AbortClick
0x473610	NetSync_Enter
0x6619b0	gSocket
0x6ac310	gOwn_ipx_addr	ptr 0x6ac358
0x6ac300	gBroadcast_ipx_addr	ptr 0x6abf00
0x6ac118	gLast_from_addr	ptr 0x6ac340; copy 0x6ac348
0x6ac12c	gIPX_socket_no	0x2FFE (BE)
0x6ac130	gNetwork_numbers	count 0x6ac128
0x6ac574	gPD_join_list	16 x 0x14; count 0x6ac35c
0x68b918	gNet_mode	0 none, 2 host, 3 client
0x690c54	gProducing_join_list
0x688718	gJoin_menu_games	6 x 8
0x688760	gJoin_selected	initial 1
0x74bcc0	gNet_players	12 x 0xd4
0x74b7d4	gNumber_of_net_players
0x74b7d0	gThis_net_player_index
0x74b7dc	gLocal_net_ID
0x74b784	gCurrent_net_game	0x78 bytes
0x75d590	gNet_player_name
0x68d990	gGuaranteed_list	200 x 0x34
```
