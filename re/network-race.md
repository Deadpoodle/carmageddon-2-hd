# AI opponents in network races (CARMA2_HW.EXE, Steam build)

Static analysis only: the Ghidra export, capstone/pefile on `testrun/CARMA2_HW.EXE`, and the data files (`data/DATA.TWT` holds RACES.TXT, NETRACES.TXT and OPPONENT.TXT; the `data/RACES/*.TWT` archives hold the track text files). The game was not run. Tags: **H** = read from the instructions or the data files; **M** = from the decompiler, or an inference from it; **L** = a guess to check at run time.

Read `re/network.md` first; it covers the transport, the message header, batching and the guarantee layer. This report covers what happens inside a race. Names in the C1 style (SelectOpponents, MungeOpponents, ...) are labels for the C2 functions, not proof that C2 matches C1. Each one was checked against C2's code, and dethrace was not used.

Fastcall is the norm: `ecx` holds the first argument and `edx` the second.

## Summary

- **The network game turns AI off in three places:**
  1. SelectOpponents 0x5039e0;
  2. LoadOpponentsCars 0x48cda0;
  3. InitOpponents 0x4add50, which sets the opponent count 0x75d7a0 to 0 at 0x4ade78.

  The AI itself (MungeOpponents 0x4a9e10 and the objective code 0x4aa570..0x4af960) never checks gNet_mode. Opponent paths are still loaded in network games; only the cop start points are skipped, at 0x4a9a16. (H)
- **A network race is a list of players, not of cars.**
  - Every in-race car message identifies its car by the **u32 player ID**, never by a car index. That covers state, crush, wasted, powerups, respawn and scores. The two lookups are NetPlayerIDToCar 0x4a6960 (15 callers) and NetPlayerFromCar 0x4a69b0 (27 callers), plus many inline loops over `gNet_players[].ID` (H).
  - Several senders read the result of 0x4a69b0 **without a null check**: the crush broadcast at 0x43626d and the wasted message at 0x440f4b. On the host, the checkpoint code also walks the opponent category and reads the result at 0x503773. So in a network game, **a car that is not a net player's car crashes the game** as soon as it is crushed, wasted or crosses a checkpoint (H for the missing checks; M for the crash).
- **The network model is state relay with physics on every machine.**
  - Each client sends its own car's packed physics state, controls and damage to the host about every 80 ms (type 0x10).
  - The host relays every car's state to every client: type 0x10 for cars near the receiver, or the short type 0x2d for cars 40 or more units away.
  - Receivers copy the state into the car and let their own physics carry it forward. There is no interpolation.
  - A client simulates a remote car only when it is within 10 units of the client's car or camera. Otherwise the car is snapped to each message. (H for the code paths; M for the description.)
- **Capacity: 12 racers in total.** The player table (12 × 0xd4), the racer list `gCurrent_race.opponent_list` (12 × 0x10 at 0x763084) and the START RACE message (0xf6 bytes = 6 + 12 × 0x14) are each sized for 12 (H). The car-slot table has 30 slots, one of them for the local car (H), and the AI opponent array has about 30 (M). So **humans + AI ≤ 12** unless the racer list and START RACE are widened.
- **Recommendation: add the AI as "bot players"**, extra entries in gNet_players that the host creates and drives.
  - Clients then see them as ordinary remote players: their cars are loaded with driver 7 and driven by the existing 0x10/0x2d messages. Wasted, scores, respawn, powerups, the HUD and the summary all work by player ID with no new message types.
  - On the host, each bot's car is switched to driver 6 and put into the AI opponent array so that MungeOpponents drives it.
  - Clients need no game-code change: the IPX shim just discards packets sent to a bot's reserved node. The host needs about six small hooks (§6).
- **Biggest gameplay limits** (M):
  - The AI works around the **local** player car 0x75bc2c. It measures its distance to that car, goes into "cheat" mode (physics off) when that car has not been near it for 3 s, and only ever pursues that car. On the host, bots will race the route and attack the host.
  - Most NETRACES.TXT arenas have only a **2-node stub path**; Junknet and CarrierNET are the exceptions. Bots are only useful on the RACES.TXT tracks, which net game types 3, 4 and 5 use (H for the data).

## 1. How single player chooses and loads AI, and where network mode changes it

### 1.1 Flow (DoGame 0x503c50, state 4 "start race") (H)

1. 0x48c860 (`ecx` = race index 0x75bbf8, `edx` = 0x7623a0) copies the race-list entry (0x74d660 + i×0x244) into **gCurrent_race 0x7623a0**. 0x762438 = race+0x98 is the pointer back to the race-list entry; that is the "race info" pointer.
2. **SelectOpponents 0x5039e0** (`ecx` = 0x7623a0). Its whole body runs only if gNet_mode == 0 (test at 0x5039e0/0x5039ea).
   - It sets race+0x90 = list entry+0x194 (the opponent count from RACES.TXT).
   - It clears oppo-table+0x1a4 (the "chosen" flag) for all 53 entries, then copies the explicit indices from entry+0x198.
   - An entry that is < 0 gets a random opponent from 0x503b00. That picks from OPPONENT.TXT by rank (oppo+0x19c in 1..5), excludes the player's car 0x75bc0c and sets +0x1a4.
3. In single player, 0x48d0e0 copies the race name.
4. **Network only:** for car-choice modes 1 and 2 (details+0x70 ∈ {1,2}, M), and if 0x74d49c == 0, DoGame reloads every net player's car here:
   - 0x48cd80 frees car slots 1..29;
   - the loop at 0x503ea0..0x503ef9 calls LoadCar 0x488f70 with driver 8 for the own index or 7 for the others, the car spec in player+0xc8, and storage 0x761f00.
5. **LoadRace 0x481830** calls LoadTrack 0x504bf0 (opponent paths through 0x401030 → 0x4a9610), then the trace wrapper **0x401070**, which calls **LoadOpponentsCars 0x48cda0**.
6. NetSync 0x46d8e0 (network) or the grid screen 0x4e2b70 plus 0x4148d0 (single player).
7. The trace wrapper **0x4010b0** calls **InitOpponents 0x4add50** (`ecx` = 0x7623a0), then InitDrones 0x44f700. After that come 0x414410 (car placement), the race loop 0x492950 (the loop body starts around 0x492980) and RaceCompleted 0x5030b0 at the end.

### 1.2 LoadOpponentsCars 0x48cda0 (H)

- **gNet_mode == 0 only** (branches at 0x48cdb9 and 0x48ce43):
  - Clears car slots 1..29 (0x763544, 0x1d dwords).
  - Loads the player car: LoadCar(`ecx` = oppo[0x75bc0c]+0x50 (car file name), `edx` = **8**, car spec 0x75bc2c, owner 0, name 0x75d590, storage 0x75b960).
  - For each racer entry `race+0xce4 + i×0x10` with index ≥ 0:
    - allocates a car spec with BrMemAllocate(**0x1964**, class 0xaa) into entry+0xc;
    - calls LoadCar(oppo[idx]+0x50, `edx` = **6**, spec, idx, name = oppo entry or "POOR SOD %d", storage 0x761ca0).
  - For a "kill these opponents" race (list entry+0x210 == 1), it sets car+0x1c0 and counts 0x74b588.
- In network mode it only sets 0x74b4a8 = 0x24 and 0x74b588 = 0.

### 1.3 InitOpponents 0x4add50 (H)

- **0x4ade78:** `number_of_opponents` 0x75d7a0 = race+0x90 − 1 in single player, **0 in network**.
- For each racer entry (one negative entry is skipped), it fills `gOpponents[n]`:
  - base **0x75d8a0**, stride **0x1a4**;
  - +0 OPPONENT.TXT index, +4 objective, +8 car spec;
  - timers, repair interval, flags at +0xb0.
  - It sets car+0x80 (car ID) = 0x200+n and adds the car to the list 0x74c6e0 (count 0x74c7a8, the local player first).
- At the end it calls the car-list rebuild 0x4a7a80.

### 1.4 Other network gates around the AI (H)

| Where | What happens in network mode |
|---|---|
| 0x4a9a16 in LoadInOppoPaths 0x4a9610 | Cop start points (0x75d7a4, 0x75d7b0) are skipped. Nodes and sections (0x75d7a8 / 0x75d7ac) are still loaded. |
| 0x4ae790 GetCarCount | Category 1 (net players) = 0x74b7d4 − 1 in network mode, else 0. Category 2 = 0x75d7a0, category 3 (cops) = 0x691744, category 5 (non-cars) = 0x6793dc. |
| 0x4ae7e0 GetCarSpec | 0 → 0x75bc2c; 1 → gNet_players[i, or i+1 at and past the own index].car; 2 → gOpponents[i].car (0x75d8a8 + i×0x1a4); 3 → cops (0x7609e0 + i×0x1a4); 5 → 0x79eda0[i]. |
| 0x4d2cc0 (track ped spawner, called by ExtractColumns 0x40cc50 → 0x40d530) | Returns at once when gNet_mode != 0, so **network races have no pedestrians**. |
| Message 0x1d (cop info) | Its handler 0x498bb0 is a bare `ret`, so there are no cops. |

### 1.5 Which race list a network game uses (H)

- LoadRaces 0x48c1c0 (`ecx` = 0x74d660, `edx` = 0x74d4a0, stack = mode) picks the file from the table at 0x657530 by mode+1. The host passes the net game type details+0x74 (0x468f80).
  - **NETRACES.TXT** for types 0, 1, 2, 6 and 7;
  - **RACES.TXT** for types 3, 4 and 5, and for single player (−1).
- NETRACES.TXT has 11 arenas, all with "Number of opponents −1 (default 0)".
- Opponent path node counts from the track files:

  | Path | Tracks |
  |---|---|
  | Real path | CarrierNET 96 nodes, Junknet 145 nodes |
  | **2-node stub** | Newsumo, arena1, silonet1/2/03, funfairnet, quarrynet, skitracknet, timbernet |

  Every RACES.TXT track has 98..275 nodes; nuke has 0.

## 2. Network players' cars

### 2.1 Where remote cars are created (H)

**NetPlayersChanged 0x49e450** (`ecx` = count, `edx` = new player list) runs on both host and client. Callers:
- the host's own setup 0x49e210 (0x49e321);
- JOIN on the host, ReceivedMessage case 2 (0x4a096e);
- 0x4a4660 (0x4a4736);
- ReceivedNewPlayerList 0x4a48d0 on clients (0x4a49af).

For each player not already known:
- **Own ID 0x74b7dc** (call at 0x49e580): LoadCar(`ecx` = oppo-table[player+0x4c]+0x50, `edx` = **8**, car spec 0x75bc2c, storage 0x761f00).
- **Anyone else** (call at 0x49e5de):
  1. allocates a car spec of 0x1964 bytes (class 0xb6);
  2. LoadCar with `edx` = **7**;
  3. 0x414400;
  4. if the player's status +0x48 is < 6, sets car+4 = 1 ("not in race").
- Fills the racer list `gCurrent_race.opponent_list[i]` (0x763084 + i×0x10): index −1, player index, random grid rank, car spec. The racer count is 0x762430 (= race+0x90).
- **The cars are loaded when a player joins, not for each race,** except in car-choice modes 1 and 2 (§1.1 step 4).

**LoadCar 0x488f70** separates only driver 8 from "not 8" (it compares against 8 as a float, 0x41000000) (M). So a car loaded as 7 can be switched to 6 afterwards.

### 2.2 Per-car structure (car spec, 0x1964 bytes; H for the offsets, M for the meanings)

| Off | Field |
|---|---|
| +0x04 | not in race / disabled. Set by 0x499260, which also moves the car 500+ units away; cleared by 0x4992e0. A car with this set is never sent by the host. |
| +0x08 | per-car physics extension. +0x260 holds a copy of the last net message (0x34 bytes): +0x268 time, +0x26c 4 control bytes, +0x274 sequence number, +0x27c repair time, +0x280 12 damage bytes, +0x290 crush version, +0x292 flags, +0x294 packed body state; +0x49c local sequence number; +0xf0 replay mode. |
| +0x0c | driver: **6** opponent, **7** remote net human, **8** local human (H: LoadCar callers) |
| +0x10 | car actor; +0x50 is the position |
| +0x80 | u16 car ID, set by InitialiseCar 0x413f70 from the racer list: 0 for the local car, 0x100+n for driver 7, 0x200+n for driver 6 (opponents also from InitOpponents). Not used in car messages (M: the net code reads +0x80 only from non-car objects). |
| +0xec | name |
| +0x1ac | car slot |
| +0x1d0 | in the physics list (set by 0x4a7a80) |
| +0x1d4 | knackered |
| +0x1d8 | wasted already reported |
| +0x4e0 | 12 damage units, stride 0x2c |
| +0x1264 | 4 control floats |
| +0x12d0 | key bits |
| +0x18a0 / +0x18a4 | recover/repair times |
| +0x18d4 | crush/deformation data (version at +0x24 / +0x26) |

### 2.3 Limits (H unless tagged)

| Array | Size | Evidence |
|---|---|---|
| gNet_players 0x74bcc0 | 12 × 0xd4 | network.md; JOIN refuses at ≥ 12 (details+0x34 > 0xb) |
| Racer list 0x763084 (race+0xce4) | **12** × 0x10 | the host/join clears at 0x468f26..0x468f35 and 0x4684fc..0x46850b stop at 0x763150 |
| START RACE 0x0c contents | 0xf6 = 6 + **12** × 0x14 | NetGetMessageSize 0x49f440; built by 0x498e70 |
| Car slots 0x763540 | 30 (slot 0 = local; 1..29 found by a search at 0x4890ef..0x489102; fatal error 0x71 when full) | LoadCar |
| gOpponents 0x75d8a0 | about 30 × 0x1a4 before the cops array at 0x7609d8 (M, from the spacing) | |
| Physics car list 0x74c7c0, count 0x74c9ec | about 139 pointers before 0x74c9ec (M); non-cars are appended at 0x416372 | |

- **There is room for cars, but not for racers.** The car-slot and opponent arrays could hold another 10 or more cars. The racer list, the START RACE grid and the player table all stop at 12. Going past 12 means moving the racer list and changing the START RACE size: a larger job with its own risks, not part of the first version.

## 3. In-race messages

Header and batching are described in network.md §3. Sizes come from NetGetMessageSize 0x49f440 and include the 2-byte `[size][type]`; "var" means the caller passes the size. The builders are:
- 0x49f680 builds a guaranteed message;
- 0x4a5bc0 sends it to all **and applies it locally**;
- 0x4a5e20 sends it to one player by ID;
- 0x49f9c0 / 0x49fb70 begin and end a broadcast (to every other player);
- 0x49f6f0 begins a batch to a player (by ID);
- 0x49f860 begins a batch to the host.

Messages 0x10..0x33, except 0x12..0x15, 0x1e and 0x2e, are dropped outside a race (table 0x659d38). Any message of type > 7 refreshes the sender's last-heard time (+0x14).

### 3.1 Table (H for the ids, sizes, senders and handlers; M for the names)

| Id | Size | Sender → receiver | Handler | Meaning (car identity) |
|---|---|---|---|---|
| 0x0a | 4 | host → each player (0x4a5e20) | case 10 → RaceCompleted 0x5030b0 (client, or reason > 7) | race over; reason 8 = winner, 9 = others (0x49a760) |
| 0x0c | 0xf6 | host, guaranteed broadcast (NetStartRace 0x498e70) | 0x414400 etc. | start race: racing flag, racer count, race index, per racer {player index, car index, packed grid transform} |
| 0x0e / 0x0f | 2 / 0x10 | client → host / reply | 0x4a18ad (0x0e) | sync request; the host replies and re-sends START RACE if a race is on (late join) |
| **0x10** | var | client → host (own car, 0x497fc0); host → each other player for every car (0x497900) | 0x4985f0 | **mechanics**: +4 **player ID**, +8 time, +0xc 4 control bytes, +0x10 keys, +0x14 sequence, +0x1c repair time, +0x20 12 damage bytes, +0x2c..0x2e, +0x30 crush version, +0x32 flags, +0x34 packed body (0x49cd00; 0x28 bytes per body, attached bodies linked by +0x224/+0x220), then options (0x10 bytes, crush data) |
| 0x11 | var | host broadcast (0x497f10) | 0x498c00 | non-car object state |
| 0x12 | 8 | host broadcast and client → host (0x497900) | inline | time sync; adjusts the race start time 0x763460 |
| 0x13 | 4 | (no sender found) | inline: player+0x60 = 0 | |
| 0x14 / 0x15 | 8 | (no sender found) | empty | |
| 0x16 | 0x14 | everyone; guaranteed or broadcast (powerup code 0x4d8d40..0x4e0593, 0x4dedf0) | 0x4dece0 | powerup on/off: +4 **player ID**, +8 powerup, +0xc time, +0x10 state |
| 0x17 | 0xc | host broadcast and client → host | 0x498390 | recover/repair time (car+0x18a0) by **player ID** |
| 0x18 | 0x38 | host broadcast (0x49ab00, every 80 ms) | 0x49b7e0 | scores: all players' +0x64 |
| **0x19** | 0xc | **any machine**, guaranteed to all (collision code 0x440660; host rules 0x49abf0) | inline 0x4a24a6 | **wasted**: +4 victim **player ID**, +8 killer ID (−1 none, −2 not a player). Sets car+0x1d4; Car Crusher scoring on the host; HUD text |
| 0x1a | — | none | default (ignored) | C1's pedestrian message, dead in C2 |
| 0x1b | 0x18 | host → player, guaranteed (0x49abf0, 0x49bc20/70/b0; checkpoints 0x5034b0) | 0x49bad0 | gameplay events: checkpoint, lap, finished, pause |
| 0x1c | 0x40 | host → player (0x49b7e0, 0x49b9a0) | 0x498bc0 | non-car position |
| 0x1d | 0x68 | none | `ret` | cop info, dead |
| 0x1e | 0x94 | host, guaranteed (0x49a760) | inline | end-of-game scores |
| 0x1f | 0x14 | guaranteed (0x4a6e50) | 0x4a78e0 | oil spill, by **player ID** |
| **0x20** | 0x30 | **any machine**, broadcast (DoCrushing 0x436170) | 0x43c480 (clients only, for cars not in their physics) | **crush point**, by **player ID** |
| 0x21 / 0x22 / 0x23 | 0x38 / 100 / 0xc | guaranteed (0x4f5110 / 0x4eb8f0 / 0x4de1f0) | 0x4f60b0 / 0x4f6320 / 0x4de5a0 | world/powerup side effects |
| 0x24 / 0x25 / 0x26 / 0x2b / 0x33 | 0x30 / 0x30 / 0x10 / 0xc / 0x1c | guaranteed (0x42dab0, 0x4314b0, 0x4387d0 + 0x43b250, 0x43b2a0, 0x436ad0) | 0x43c940, 0x43cbd0, 0x43cd60, 0x43ce60, 0x43caf0 | car part detach, crush and damage events, all by **player ID** (0x4a6960) |
| 0x27 / 0x29 / 0x28 | var / var / 0x3c | guaranteed, broadcast or to player (0x43d1d0 / 0x43d690) | 0x43d340 / 0x43d810 / 0x43d5f0 | non-car object types 2 and 3 |
| 0x2a | var | client → host (0x497fc0) | 0x4983f0 | crush-version acks for each player's car |
| 0x2c | 8 | client → host (0x42dd50, flap doors) | 0x43d670 | |
| **0x2d** | 0x14 | host → player (0x497c11, via 0x498460) | 0x4984c0 | **far-car state** (≥ 40 units from the receiver): player ID, heading, position, repair time |
| 0x2e / 0x0b | var / 4 | host heartbeat / client status, every 1 s (0x4a4ac0, 0x4a57e0) | inline | statuses of all players. A client refreshes last-heard for every player whose status is not 5, and turns cars on or off (0x499260 / 0x4992e0) |
| 0x2f | 8 | guaranteed (camera code 0x40e430) | 0x497860 | player's spectate target (player+0xd0) |
| 0x30 | 4 | (no sender found) | 0x41ff20 | |
| 0x31 | var | to player and guaranteed (0x4dcfb0 after each send, 0x4ddab0) | 0x4dd210 | packed state of a powerup or non-car body |
| 0x32 | 0x20 | host, guaranteed (respawn in 0x49abf0) | 0x499220 → 0x4fca70, 0x4def00, 0x414400, 0x439510 | respawn or reset car, by player index |

**Car identity is the player ID (or the player index) everywhere** (H).

### 3.2 How a received car state is applied (H for the paths, M for the description)

1. **The send tick** is 0x4978d0, every **80 ms** of 0x74a5ec, called from the physics frame 0x416340 just before NetSendMechanics 0x497900 and 0x4dcfb0.
   - The host loop goes over gNet_players cars with car+4 == 0. For each receiver it chooses:
     - **0x2d** when the car is ≥ 40 units from the receiver's car (or the car it is watching);
     - **0x10** otherwise.
   - A player is sent its own car only when the host has a correction (+0x49c != +0x274).
2. **The receiver** is 0x4985f0.
   - It finds the car by ID and ignores the message if its time (+8) is older than body+0x268.
   - **Host:** ignores the message if it is older than its sequence number (+0x14 < body+0x49c). Otherwise it stores the message (0x498ab0 → body+0x260..) and the keys, controls and powerup bytes.
   - **Client, own car:** applies only a newer host correction.
   - **Client, other car:** stores the message and the keys.
     - If the car is **not in the client's physics** (car+0x1d0 == 0), it unpacks the body state at once (0x49cd90(2)), copies the damage bytes and the crush data, and handles repairs (0x43ace0, or 0x439510 for a jump of 100 s or more).
     - If it **is** in physics, the next physics step does the same through **0x415c40** (called from the per-car loop 0x4157e0 at 0x415993 / 0x415ab6), and the car is then integrated with the received controls.
3. **The physics list 0x4a7a80** contains:
   - the local car;
   - remote cars: **host: all of them; client: only within 10 units** (squared distance < 100) of its own car or camera car;
   - opponents with flag +0xb0 & 8;
   - cops.
4. Driver-7 cars whose last message is more than 1 s old have their keys cleared (0x416340).

**Result:** no interpolation. The receiver snaps to the received state at the message time; near cars are physics-integrated with their last controls. The owner of a car decides its damage: a client reports its own damage in 0x10 and the host relays it. Crush points are broadcast by whichever machine processes the crush, and only machines not simulating that car apply them.

## 4. AI control

### 4.1 Per frame (H for the calls)

The race loop 0x492950 runs:
1. NetReceiveMessages 0x4a4a40;
2. … 0x401170 (frame time, then **MungeOpponents 0x4a9e10**, then 0x4dde20);
3. the physics frame 0x416340: controls for each car 0x43f9f0, physics 0x4b6630, the net send;
4. 0x436170 (crush);
5. host rules and scores 0x49ab00;
6. NetFlushBatches 0x49fcf0;
7. … checkpoints 0x5034b0.

### 4.2 MungeOpponents 0x4a9e10 (H)

- **Returns at once** unless (opponents 0x75d7a0 or cops 0x691744) is non-zero **and** the path counts 0x75d7a8 (nodes) and 0x75d7ac (sections) are both non-zero (0x4a9e13..0x4a9e42).
- **Records the local player's route** (0x75bc3c actor position, ring buffer at 0x75d00c..) for the pursuit code.
- For each opponent whose +0xb0 & 2 is clear, it runs:
  - **0x4a9550:** vector and distance to the **local player car** (+0x40, +0x18). It sets the "seen" time +0x68 when the distance is under the threshold 0x691764.
  - **0x4adae0:** visibility check every 4 s.
  - **ProcessThisOpponent 0x4aa570:**
    - if the player has not been near for **3 s**, the opponent "StartToCheat": physics off through 0x4a79a0, then a rebuild with 0x4a7a80, and it moves along the path;
    - repair 0x4ad910;
    - 0x4ace70;
    - **ProcessCurrentObjective 0x4aa900**, a switch on +4:
      - 1 complete race 0x4aae20 / 0x4af960;
      - 2 pursue 0x4ab190 (target +0x114);
      - 3 run away;
      - 4 get near the player 0x4ac890, which switches to pursue **&0x75bc2c**;
      - 5 0x4ac540;
      - 6 / 7 frozen;
      - 8 0x4abc00;
      - 9 0x4abc40.
- Then the same for cops. At the end it calls 0x4a7a80.

### 4.3 What the AI depends on, and whether it knows about the network

- **Path data:** loaded in network games. The arenas have 2-node stubs (§1.5).
- **The player:** the local car 0x75bc2c / 0x75bc3c is used at 0x4a955c..0x4a9588, 0x4a9f0b..0x4aa17f, 0x4ac94d..0x4ac9b5, 0x4acff0 / 0x4ad1a1 / 0x4ad3eb (target == player), 0x4ad8f2 and 0x4adda8 (H).
- **Network awareness:** none. The opponent code 0x4a7f20..0x4b1400 reads gNet_mode only at 0x4a9a16 (cop starts) and 0x4ade78 (count). It never touches the player table (H, from a scan of the instructions).
- **Other cars:** only through collisions and the generic pursuit target. Nothing in the AI chooses another net player as a target (M).

### 4.4 Running the AI on the host only and showing it on clients

- **As "non-player" cars: not practical.**
  - Every car message routes by player ID, and three senders crash on a car that is not a player's (§Summary).
  - Clients would need new car slots, new message types and new handlers for every car message.
- **As bot players: practical, with the existing messages.**
  - The host's 0x497900 already sends 0x10/0x2d for every car in gNet_players. It packs any car's body, and a driver-6 car works the same way: the packer reads the physics body, not the driver.
  - The client side is unchanged: the cars come from NEWPLAYERLIST through 0x49e450 with driver 7.
  - Ownership of the bots' damage, wasting and respawn sits naturally with the host, because the host owns their physics:
    - its 0x440660 / 0x49abf0 send the 0x19 wasted message;
    - 0x49abf0 respawns players in respawning game types by sending 0x32.
- **What has to change** is listed in §6.

## 5. Race-level effects

- **Race end (H):**
  - **The host decides.** NetGameRules 0x49abf0 runs only when gNet_mode == 2 (from 0x49ab00). Game over is declared by 0x49a760 (winner index): 0x0a to each player (8 = winner, 9 = others), then 0x1e scores.
  - A client ends on 0x0a (RaceCompleted 0x5030b0).
  - Car Crusher's kill target is also checked in the 0x19 handler on the host (killer +0x64 ≥ details+0x60).
- **Game types** (details+0x74; names from NETDEFLT.TXT; H for the branches):

  | Type | Name | How it scores |
  |---|---|---|
  | 0 | Driven to Destruction | last car not wasted; points 0x659c2c by rank |
  | 1 | Car Crusher | kills to a target |
  | 2 | ("Smashy thing, not used") | score threshold 0x68d920 |
  | 3 | Checkpoint Stampede | bit mask in +0x64; host checks checkpoints |
  | 4 | Sudden Death | |
  | 5 | (laps; "Lap me") | laps target details+0x60 |
  | 6 | Curse of the Vampire | |
  | 7 | Fox 'n' Hounds | fox = player index 0x74a734; score = time as fox |

  The menu names for types 2 and 5 are not checked (L).
- **Kills, wasted and points** are per player record (M):

  | Off | Field |
  |---|---|
  | +0x64 | score |
  | +0x70 | wasted flag |
  | +0x18 | respawn time |
  | +0x1c | time wasted |
  | +0x88 | games won |
  | +0x90 | points |

  Credits in network games are only the local player's (0x75bb80, starting value details+0x58). Crash earnings and the other credit HUD code run only when gNet_mode == 0, in 0x440660 (H).
- **Checkpoints (H):** CheckRaceProgress 0x5034b0 runs on the **host only**, for types 3, 4 and 5. It loops over car categories **0..gNet_mode** (0x503990..0x50399c), so on the host that is self, net players **and opponents**. It credits the car's player through 0x4a69b0 **with no null check** (0x503773). In stock code the opponent category is always empty in network games. With AI it would crash (a non-player car) or count twice (a bot that is also in gOpponents).
- **Pedestrians:** none in network races (0x4d2cc0 gate; message 0x1a ignored). AI cars cannot break pedestrian sync, because there is none (H).
- **Would bot players break any of this?** (M)
  - Scores, wasted, respawn and the HUD all key on player records, so no.
  - Fox 'n' Hounds and Vampire can pick a bot as the fox or vampire, and the AI does not know the rules. That is a gameplay oddity, not a fault.
  - Each bot takes one of the 12 slots, and JOIN refuses a car a bot holds ("car taken", −1).

## 6. Feasibility and the smallest plan

**Verdict:** feasible as a **host-side feature with the bots added as players**. Clients only need the shim's "sink" rule (all players run the HD patch anyway). The estimate is a few hundred lines in a new `src/netbots.cpp`, plus about 8 hooks. No new message types.

### 6.1 Hooks and data

1. **Create the bots in the lobby (host).**
   - Trigger: a `[Net] Bots=N` setting (later a NetSync button) when hosting.
   - For each bot, build a player record (0xd4 bytes):
     - **ID** from a reserved range that cannot match a real ID (real IDs are node bytes 2..5, PDNetExtractPlayerID 0x51a130), e.g. `0xB0B0B000 | n`;
     - **address** = sockaddr_ipx with the reserved node `B0:B0:B0:B0:B0:n`;
     - **name** = the opponent's name;
     - **car** +0x4c = an OPPONENT.TXT index whose car is free (mark 0x74b800 + idx×0x14).
   - Easiest route: feed a synthetic **JOIN** (type 2, the 0xd4 record) through ReceivedMessage 0x4a0260, with the sender address set to the bot node. Case 2 then does the whole job: it adds the record, sets the address (0x51a5f0), loads the car (0x49e450) and sends guaranteed NEWPLAYERLISTs to every human.
   - L: the case-2 refusal checks (open game, ≥ 12, car taken).
2. **Sink (all machines, in `netshim.cpp`).**
   - `sendto` to a bot node is dropped.
   - Guaranteed sends to a bot would otherwise be resent for 10 s and fill the 200-entry table 0x68d990. Two fixes:
     - (a) the shim fakes a type-4 ACK (the ACK handler matches the u16 number only: case 4, H); or
     - (b) hook **NetGuaranteedSendToPlayer 0x4a5e20** to return 0 for bot IDs. (b) is simpler; apply it on every machine.
3. **Keep the bots alive (host, every frame, e.g. from the race-loop hook).**
   - For each bot: last-heard +0x14 = now, so the 60 s kick in 0x4a4da0 and the status-5 marking in 0x473610 / 0x4e2c30 never fire.
   - Status +0x48 = the host's own status. The heartbeat 0x2e carries it to clients, which refresh the bots' last-heard and turn their cars on.
4. **Turn bots into AI on the host at race start** (hook the trace wrapper 0x4010b0 after 0x4add50):
   - set the bot cars' driver +0xc = 6;
   - fill gOpponents for the bots. Either call InitOpponents 0x4add50 again with a scratch race-info (+0x90 = bots+1; entry 0 index −1; entries 1.. = {OPPONENT.TXT index, −, −, car spec}) while gNet_mode is temporarily 0, or patch 0x4ade78 and use the scratch struct;
   - restore gNet_mode;
   - set 0x69173c = 1 so that 0x4a7a80 rebuilds with the network branches. (InitOpponents' own rebuild runs while gNet_mode is 0, so it skips the net players.)
   - Do this again for car-choice modes 1 and 2, because DoGame reloads every net car with driver 7 (0x503ea0).
5. **Physics list (host):** at **0x4a7bb5**, in the host branch of 0x4a7a80, skip net-player cars whose driver is 6. Otherwise a bot is added twice, once as a player and once as an opponent with +0xb0 & 8.
6. **Checkpoint double count (host):** cap the category loop of 0x5034b0 at 1 in network games (the bound at 0x503990..0x50399c), since bots are already category 1.
7. **AI "cheat" mode (host, recommended for the first version):** keep the bots physical. For bot opponents, set +0x68 = now every frame, or patch the 3 s test in 0x4aa570. A bot off-screen for the host could otherwise glide along its path with physics off while a client watches it (L whether the packed state even follows the cheat motion).
8. **Optional (later):** make 0x4a9550 and the pursuit target (+0x114, the &0x75bc2c sites in §4.3) use the **nearest human car**, so bots chase whoever is close and not only the host. Restrict bots to tracks with real paths (game types 3..5 use RACES.TXT; NETRACES has CarrierNET and Junknet), or warn.

### 6.2 New data on the wire

None. Bots use the stock 0x02 / 0x03 records, 0x10 / 0x2d state, 0x19 / 0x32 / 0x16 / 0x20 events and 0x2e statuses. Bandwidth (M): each 0x10 is about 0x5c..0x80 bytes. With 11 other cars a client receives about 1.2 KB every 80 ms, roughly 15 KB/s, split into 512-byte batches. The host sends that to each client.

## 7. Risks and open questions (run-time tests)

1. **Synthetic JOIN:** does case 2 accept it cleanly? The address comes from the `edx` sender; the timestamps; the "PLAYER n" name. Does NetSync show the bot, and do clients load its car? Alternative: build the records by hand and call 0x49e450 plus the type-3 sends.
2. **Re-running InitOpponents** with a scratch race-info in a network game: globals it resets (0x691754.., 0x74c6e0 list, dares 0x65a3c8) and anything it reads from race+0x98 (0x762438 +0x180). Check that bots get an objective and drive the route (`Opponents` log strings 0x4a8160 are stubs; use our own log).
3. **Bot wasted and respawn:** does 0x49abf0's reset (0x414510 / 0x439510 / 0x414400) leave the AI state valid? +0xb0 bits 1 and 2 and the objective may need clearing after a respawn. In single player, opponents never respawn.
4. **Driver 6 vs 7 differences on the host** beyond those found: 0x416340 (stale-key clearing for 7 only), 0x415c40 (repair catch-up for driver ≥ 7), 0x413f70 car IDs. Car IDs are local only (M); check that nothing in the crush or collision code sends +0x80.
5. **Client-side look:** bots within 10 units are simulated with the received controls. Do AI control values (+0x1264, written by the AI) give sensible dead reckoning, or do bots jitter between 80 ms snaps?
6. **Collisions** between a client car and a bot: the client computes its own damage and the host computes the bot's. Check that both sides agree well enough, and that the 0x19 wasted message from either side credits the right killer.
7. **12-slot cap:** humans + bots ≤ 12. Should joins be refused or bots dropped when a human joins? Removing a player mid-lobby needs the "leave" path (0x49dc90 / type 8; not studied).
8. **Game types:** Fox 'n' Hounds and Vampire with a bot fox or vampire; Car Crusher scoring for bot kills; Driven to Destruction's "last car standing" with a bot left.
9. **The arena stub paths** (2 nodes): does MungeOpponents misbehave, or is it merely useless? Restrict bots to tracks with real paths if it misbehaves.
10. **A host that alt-tabs or stalls:** bots stop with it (as everything does today, network.md §5).

## Address table

```
0x503c50	DoGame	state 4: 0x48c860, SelectOpponents, LoadRace, NetSync/grid, InitOpponents wrapper, race loop
0x48c860	InitRaceInfoFromList	race list 0x74d660 (x0x244) -> gCurrent_race 0x7623a0
0x5039e0	SelectOpponents	single player only (gNet_mode test at 0x5039e0)
0x503b00	PickRandomOpponent	by rank oppo+0x19c, chosen flag oppo+0x1a4
0x48c1c0	LoadRaces	mode+1 -> table 0x657530: RACES.TXT (SP, types 3-5) / NETRACES.TXT (types 0-2, 6-7)
0x481830	LoadRace	LoadTrack 0x504bf0, then 0x401070
0x401070	trace: LoadOpponentsCars	0x48cda0, 0x4a9600, 0x44fda0
0x48cda0	LoadOpponentsCars	single player only; player driver 8, opponents driver 6, spec 0x1964
0x48cd80	FreeCarSlots	clears 0x763544 x29
0x488f70	LoadCar	ecx name, edx driver (only 8 special), stack spec, owner, name, storage; slot 0x763540
0x4010b0	trace: InitOpponents	0x4add50 then InitDrones 0x44f700
0x4add50	InitOpponents	0x4ade78: count 0x75d7a0 = race+0x90-1, or 0 in network
0x4a9610	LoadInOppoPaths	0x4a9a16 skips cop starts in network; nodes 0x75d7a8, sections 0x75d7ac
0x4a9e10	MungeOpponents	needs count and paths (0x4a9e13..0x4a9e42); no network checks
0x4a9550	OppoDistanceToPlayer	vs 0x75bc3c; seen time +0x68
0x4aa570	ProcessThisOpponent	3 s unseen -> cheat (physics off)
0x4aa900	ProcessCurrentObjective	switch on opp+4 (1..9)
0x4ac890	ProcessGetNearPlayer	-> pursue &0x75bc2c
0x4aad70	NewObjective	(opp, objective, target)
0x4ae790	GetCarCount	categories 0..5
0x4ae7e0	GetCarSpec	categories 0..5
0x4ae880	GetDriverName
0x4a7a60	RequestCarListRebuild	0x69173c = 1
0x4a7a80	RebuildActiveCarList	0x74c7c0 / 0x74c9ec; host branch 0x4a7b66.., client 10-unit test
0x413f70	InitialiseCar	car ID +0x80: 0x200+n (6), 0x100+n (7), 0 (8)
0x416340	PhysicsFrame	controls 0x43f9f0, physics 0x4b6630, send every 80 ms
0x4978d0	NetTimeToSendMechanics	80 ms (0x50) on 0x74a5ec
0x497900	NetSendMechanics	host: 0x12, then 0x10/0x2d per car per receiver; non-cars 0x43d1d0/0x43d690/0x497f10
0x497fc0	NetSendOwnMechanics	client -> host: 0x10, 0x17, 0x2a
0x498460	BuildFarCarMessage	type 0x2d
0x4985f0	ReceivedMechanics	type 0x10
0x498ab0	StoreNetMessageInCar	body+0x260.., packed state +0x294
0x4984c0	ReceivedFarCar	type 0x2d
0x415c40	ApplyNetStateInPhysics	from 0x4157e0; unpack 0x49cd90, repairs 0x43ace0 / 0x439510
0x49cd00	PackBody	recursive over +0x224/+0x220
0x49ce60	PackedBodySize
0x49cd90	UnpackBody
0x4a6960	NetPlayerIDToCar	player.ID -> player+0xc8
0x4a69b0	NetPlayerFromCar	car -> player record (0 if none)
0x49e450	NetPlayersChanged	loads joiners' cars (7, or 8 for self); fills racer list 0x763084
0x498e70	NetStartRace	type 0x0c, 12-entry grid
0x49ab00	NetHostRulesAndScores	host: 0x49abf0, then 0x18 every 80 ms
0x49abf0	NetGameRules	host: respawn 0x32, wasted 0x19, per-type scoring
0x49a760	NetDeclareGameOver	0x0a to each, then 0x1e
0x440660	CarCollisionDamage	network: wasted 0x19; 0x440f4b reads 0x4a69b0 result unchecked
0x436170	DoCrushing	network: crush 0x20; 0x43626d reads 0x4a69b0 result unchecked
0x5034b0	CheckRaceProgress	host types 3-5; category loop 0..gNet_mode (0x503990); 0x503773 unchecked
0x4a57e0	NetSetOwnStatus	host 0x2e / client 0x0b
0x499260	NetCarOut	car+4 = 1, moved 500+ away
0x4992e0	NetCarIn	car+4 = 0
0x4a5bc0	NetGuaranteedSendToAll	applies locally too
0x4a5e20	NetGuaranteedSendToPlayer	(edx msg, stack ID); hook point for bot skip
0x49f680	NetBuildGuaranteedMessage	(type, size decider)
0x49f6f0	NetBeginContentsToPlayer	(type, ID)
0x49f860	NetBeginContentsToHost
0x4d2cc0	SpawnTrackPeds	returns at once in network games
0x498bb0	ReceivedCopInfo	ret
0x7623a0	gCurrent_race	+0x90 racer count (0x762430), +0x98 list entry (0x762438), +0xce4 racers (0x763084, 12 x 0x10)
0x763540	gCar_slots	30 x 4
0x75d7a0	gNumber_of_opponents
0x75d8a0	gOpponents	~30 x 0x1a4: +0 index, +4 objective, +8 car, +0x18 dist, +0x68 seen, +0xb0 flags, +0x114 target
0x691744	gNumber_of_cops	cops at 0x7609d8
0x75d7a8	gNumber_of_path_nodes
0x75d7ac	gNumber_of_path_sections
0x74c7c0	gActive_car_list	physics list, count 0x74c9ec
0x74c6e0	gOpponent_car_list	count 0x74c7a8 (player first)
0x75bc2c	gProgram_state.current_car	+0x10 actor at 0x75bc3c
0x761f00	gNet_car_storage
0x761ca0	gOppo_car_storage
0x657530	gRace_file_by_mode	RACES.TXT / NETRACES.TXT
0x659d38	gRace_only_messages	52 flags
0x74b800	gCar_taken	53 x 0x14
```
