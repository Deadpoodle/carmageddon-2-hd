# Pedestrians in network races (CARMA2_HW.EXE, Steam build)

Static analysis only: the Ghidra export, capstone/pefile on `testrun/CARMA2_HW.EXE`, and the data files (`data/DATA.TWT` holds NETRACES.TXT, RACES.TXT and POWERUP.TXT; the `data/RACES/*.TWT` archives hold the track text files, extracted with `tools/twt.py`). The game was not run. Tags: **H** = read from the instructions or the data files; **M** = from the decompiler, or an inference from it; **L** = a guess to check at run time.

Read `re/network-race.md` (car messages, player IDs, host relay), `re/peds.md` (ped record, update, physics bodies) and `re/network.md` (transport) first. Fastcall is the norm: `ecx` holds the first argument and `edx` the second.

## Summary

- **Only one instruction keeps pedestrians out of network races: the spawner gate at 0x4d2cd7** in SpawnTrackPeds 0x4d2cc0. A full sweep of every `[0x68b918]` (gNet_mode) operand in .text finds 401 references; **the only one in the whole ped module 0x4ca000..0x4d8000 is that gate** (H).
  - Track ped specs are still loaded in network games (0x4ca9f0, from LoadTrack), and InitPeds 0x4d5bd0 still runs. The update 0x4d3740 and the per-frame pass 0x4d5970 are called unconditionally from the race loop. Ped sounds and rendering have no network test (H).
- **Removing the gate gives working local, unsynchronised pedestrians on each machine, with one thing that must also be bypassed** (H for the code, M for the outcome):
  - **The "every ped killed" race win.** LoadRaces keeps only race type 0 ("Carma1") entries for network lists (0x48c1c0), so every network race is type 0. In type 0, PedDeathCount 0x4cd160 calls **RaceCompleted(1) at 0x4cd1c6** once the local kill count reaches the ped count.
    - On the host, RaceCompleted sends type **0x0a** with reason 1 to every player (0x4a5710). That ends the race for everyone, with no winner and no 0x1e scores.
    - On a client it ends only that client's race (L for what follows).
  - Everything else either already behaves in a network game or is already turned off there:
    - **time bonuses** are gated (0x44b490 needs gNet_mode == 0);
    - **the HUD ped counter** is skipped (the race loop's network branch at 0x493430);
    - **ped powerups** are excluded by their POWERUP.TXT flag 2, "net_inappropriate";
    - **no network game type scores ped kills**.
- **What changes for players** (H unless tagged):
  - **Credits.** A kill by your own car pays credits (0x4cd260 → 0x44b300, no network test), and network credits pay for your own repairs. Only kills by the local car (driver 8) pay, and nothing goes on the wire.
  - **Different crowds.** Every machine spawns its own crowd from its own `srand(time)` (0x47dd8b), so players see different peds in different places.
  - **Remote cars kill your local peds**, with no credit to anyone.
- **No leftover synchronisation.** Message 0x1a still has a size entry with three sub-sizes (0x18 / 0x24 / 0x3c, 0x49f49e), but its slot in the ReceivedMessage jump table is the default (0x4a44c2), and no code builds a 0x1a (H).
  - Leftovers do show that peds were planned for network play:
    - most ped powerups carry flag 1 ("net_global");
    - the map draws peds in network games when "Show pickups on map" is on (0x495e85).
  - A host-authoritative sync would be a multi-week job (§3), so it is not recommended.
- **Recommendation: `[Network] Pedestrians=1`**, which turns on two patches:
  1. NOP the 6-byte `jne` at **0x4d2cd7**;
  2. divert **0x4cd1a3** to a stub that skips the "all peds killed" RaceCompleted while gNet_mode != 0.

  Optional extras:
  - **a spawn seed** shared by all machines (`srand(seed)` around the 0x40cc50 call at 0x505a2d), so everyone starts with the same crowd;
  - **turning ped-kill credits off** in network games.

  The setting is per machine. If machines disagree, nothing breaks, because no ped state is ever sent.

## 1. Every place network mode touches pedestrians

### 1.1 Method (H)

- A linear sweep of .text with capstone (resyncing on bad bytes) for operands containing `0x68b918` finds 401 instructions in about 190 functions.
- The ped module spans 0x4ca000..0x4d8000:
  - the BONER character code (0x404000..0x40b600) is shared;
  - the powerups (0x4d8a00..0x4e0e00) are listed separately.
- Inside the ped module the sweep finds **one** reference: 0x4d2cc6 in 0x4d2cc0.
- The ped code calls three outside helpers that read gNet_mode:
  - **GetTotalTime 0x514c30.** In network games it leaves out the pause accumulator 0x6abef4, because network games cannot pause. The time stays monotonic, so it is harmless.
  - **AwardTime 0x44b490.**
  - **RaceCompleted 0x5030b0.**
- 0x502d70, called by 0x4cbc62, is a one-line flag clear; Ghidra wrongly folds a neighbouring gNet_mode read into it.

### 1.2 Table

| Area | Where | Network behaviour | Tag |
|---|---|---|---|
| **Spawner** | SpawnTrackPeds 0x4d2cc0. Per track polygon, from ExtractColumns 0x40cc50 → 0x40d530 (call at 0x40d566), called by LoadTrack 0x504bf0 at 0x505a2d | `mov eax,[0x68b918] / test / jne 0x4d334e` at 0x4d2cc6..0x4d2cd7. Bytes at 0x4d2cd7: `0f 85 71 06 00 00`. **The only network gate in ped code.** | H |
| Ped settings, forms, moves, sounds | LoadPedSettings 0x4cadc0, once from InitialiseGame 0x47e28f | no network test | H |
| Track ped spawn sets | 0x4ca9f0, from LoadTrack at 0x505a1c | Loaded in network games too. Reads "Ped specs" from the track file into 0x694138 (count 0x69bce8, 0x20 bytes each), sets byte 7 of each spawn material's name to `?`, allocates the 2000 × 0x54 ped array 0x744808 | H |
| Spawn shrink | 0x4d3520, from LoadTrack at 0x505a37 | reallocates 0x744808 to count × 0x54 (0 in network games today) | H |
| Ped reset | InitPeds 0x4d5bd0, from LoadRace at 0x48188f (unconditional) | count 0x7447d4, kills 0x7447cc and the powerup mode flags cleared | H |
| Update | PedsUpdate 0x4d3740, from the race loop at 0x492da7 (unconditional) | no network test | H |
| Per-frame pass, render prep | 0x4d5970, from the race loop at 0x493a53 (unconditional) | none | H |
| Physics bodies | ped bodies sit in the physics list 0x74a5f0 (peds.md §5c) | No network test. Network non-car traffic (0x11 / 0x27 / 0x29) comes from the NONCARS array 0x79eda0 (count 0x6793dc, loop at 0x497e73), not from the body list, so ped bodies are never sent | H for the loop, M that peds are never in 0x79eda0 |
| **Kill scoring** | PedKilled 0x4cd260, from 0x4cce70 (0x4ccfdf) and 0x4d3740 (0x4d55fe, 0x4d568d) | Returns unless the killing car (proc+0xc0) has driver +0xc == **8**, the local human (0x4cd2be). Credits go through 0x44b440 → **0x44b300: no network test**, adds to 0x75bb80. Time goes through **0x44b490: needs gNet_mode == 0**, so there is no time bonus | H |
| Bonus credits | 0x4d18c1 / 0x4d190b (in 0x4d1030) and 0x4d24b6 / 0x4d258d (in 0x4d2320) | only when proc+0xc0 == &0x75bc2c (the local car). Credits only | H |
| **Race objective** | PedDeathCount 0x4cd160, from 0x4ccfd6, 0x4d55dd, 0x4d56f0 | Race type 0: 0x7447cc++, and when it reaches 0x7447d4 → headup text 0x1a, then **RaceCompleted(1)** at 0x4cd1c6. Type 2 → RaceCompleted(1); type 5 → RaceCompleted(3). No network test. See §2.2 | H |
| HUD ped counter | race loop 0x4934a5..0x493500 (text 0x12, "%d/%d" 0x658980) | skipped in network games: 0x493430 `jne` → 0x499360 (network HUD), then jmp 0x4939ea | H |
| Map | 0x495e10 | Draws peds when 0x74d09c (the map powerup) is set, or the race type is 2, **or (network and details+0x4c = "Show pickups on map")** (0x495e6d..0x495e9b; option order from 0x48d110). Today 0 peds, so nothing is drawn | H |
| Ped camera | camera mode 5 is skipped while 0x7447d4 == 0 (0x40eaa7, 0x40eb38) | becomes selectable once peds exist | H |
| **Powerups** | GotPowerup 0x4d8d40 and 0x4d9150 | Flag 0x40 with 0 peds → substituted (0x4d8d9c / 0x4d91b0). In network games, **flag 2 → substituted** (0x4d8db3 / 0x4d91bf). Every ped powerup has flag 2 (§2.4) | H |
| Ped sounds | 0x4cd880 → 0x455710 (2D, local car) / 0x455f80 (3D) | no network test | H |

**Would removing just the spawner check work?** Yes for spawn, AI, animation, physics, gibs, sound, credits and the map (H for the absence of other gates; M that it plays correctly). The one functional problem is the type-0 "all peds killed" race end (§2.2). Two things stay off without any change: time bonuses, which have no meaning in network games, and the HUD counter.

## 2. Unsynchronised local pedestrians: what goes wrong

### 2.1 Different crowds on every machine (H)

- **Spawning is random.**
  - The game calls `srand(time(NULL))` once, at 0x47dd8b in InitialiseGame (time 0x577dc0).
  - The spawner draws from the global MSVC LCG (0x576970) through 0x513520 (int range), 0x513620 and 0x513650 (float ranges):
    - the ped type within its group;
    - two barycentric coordinates;
    - the walk rate.
  - The count per polygon is area × density (spec +0xc, "Peds per 100 square metres").
- **The AI reseeds every frame**, with `srand(ped)` (0x4cefac) and `srand(time ^ ped)` (0x4cf4b6 / 0x4cf4c5). `ped` is a heap address and `time` is each machine's own game time.
- **Activation is local.** Only peds within 12 units of the local camera are processed (peds.md §4), and the threat test only sees cars in the local physics list.
  - On a client that means the remote cars within 10 units of its car or camera car (network-race.md §3.2).
- **Result:**
  - Even with a shared spawn seed (§4.2), crowds diverge within seconds.
  - Without one, they differ from the start.
  - A remote car on your screen will splatter **your** peds, or drive through the place where its owner saw one.

### 2.2 The "every ped killed" race end (H, must be bypassed)

- **Why it applies:** LoadRaces 0x48c1c0 keeps an entry in a network list only when its race type (entry+0x210, "Race type (0 = Carma1, ...)") is 0. The test is `param_3 < 0 || type == 0`. NETRACES.TXT lists type 0 for all 11 arenas. So `[0x762438]+0x210 == 0` in every network race, including the RACES.TXT tracks used by network types 3, 4 and 5.
- **PedDeathCount 0x4cd160** counts **every** ped death on the machine, whatever killed it: the own car, a remote car, a non-car, drowning or a powerup. When the count reaches the ped count, it calls RaceCompleted(1).
- **RaceCompleted 0x5030b0:**
  - **Host** (gNet_mode 2, reason < 8): **0x4a5710** builds message 0x0a with the reason and sends it guaranteed to every other player (0x4a5e20). The clients' case-10 handler calls RaceCompleted too. So everyone's race ends with reason 1 ("all peds killed"), bypassing NetDeclareGameOver 0x49a760, which means no winner, no 0x1e scores and no game-type result (H for the sends; L for what the summary screens show).
  - **Client:** reason 1 runs the local end-of-race path and sets the countdown 0x74d62c = 8000 ms. The host is not told; that client leaves the race on its own (L).
- **When it can happen:**
  - Arenas with a small crowd: silonet1 has 4 spawn sets and Junknet has 5 (§2.6).
  - Lucky ped-dense runs.
  - Not by powerup, since ped powerups are excluded (§2.4).
- **Bypass:** in network games, skip the RaceCompleted at 0x4cd1a3 (§4.1).

### 2.3 Scores, credits, time and messages (H)

- **Network game types never read ped data.**
  - NetGameRules 0x49abf0, the score broadcast 0x49ab00 (0x18) and the wasted handler (0x19) never touch the ped globals 0x744808 / 0x7447cc / 0x7447d4. The only references to those outside ped code are the race-loop HUD, the map (0x495e10), the camera (0x40e7f0 / 0x40ea30) and the mouse-pick debug 0x413860.
  - No type scores ped kills: Driven to Destruction, Car Crusher, Checkpoint Stampede, Sudden Death, Lap me, Vampire and Fox 'n' Hounds all score cars, checkpoints, laps or time.
- **A kill sends nothing.** The ped module's outside callees include no message builder (0x49f680, 0x49f6f0, 0x49f860, 0x49f9c0, 0x4a5bc0, 0x4a5e20). The only network-area callee is 0x4977f0, from the spawner at 0x4d3291, which adds a local map marker for type-5 mission peds.
- **Credits.**
  - A kill by your own car adds credits to 0x75bb80 (0x44b300) and shows the "credits" headup.
  - In network games, credits start at details+0x58. They are reset to that value on respawn (0x49af38) and on the 0x4a442f path, and are compared with a per-type value 0x7634cc in the network HUD (0x499423).
  - In effect, peds buy repairs and recovery. This is local, and every player can do the same, but it is a balance change, especially in Driven to Destruction and Car Crusher. §4.1 offers a switch.
- **Time:** none (0x44b490 gate). Network races have no countdown anyway.
- **Kills by remote cars:** 0x4cd260 returns at the driver test, so there are no credits and no headup. The death still counts in 0x4cd160 and still plays its sound (M).

### 2.4 Ped powerups (H from POWERUP.TXT and the flag tests)

- **Flags in POWERUP.TXT:** 1 = net_global, 2 = net_inappropriate, 4 = off_before_snapweld. 0x40 is tested as "needs peds" (0x4d8d9c); 0x80 starts pratcam sequence 0x34 / 0x35 (0x4c8da0 / 0x4c8dc0).
- **Ped-dependent powerups.** Each one has flag 2, so network games already substitute it:

  | Flags | Powerups |
  |---|---|
  | 0xc3 | 2, 3, 6, 43, 44, 47, 48, 58, 59, 60, 62, 63, 64 |
  | 0x43 | 4, 33, 36, 37, 61 |
  | 0x42 | 24, 25, 55, 65, 91, 92 |
  | 0xc0 | 90, "DrUnk Driving": no flag 2 and not a ped effect |

  **Nothing changes there when peds are enabled** (H).
- **If they were unlocked later** by clearing bit 2 in the loaded table (entry+0x38, table 0x6a0a54 + i × 0xac):
  - **net_global** ones (flag 1) are broadcast as 0x16 by 0x4d9150. Each receiver's 0x4dece0 then applies them through 0x4d9150 to its own peds. That suits local peds (M).
  - **Non-global** ones (24 map, 25 electro-bastard ray, 55 annihilator, 65 repulse, 91 napalm, 92 valium) call the action for the sender's car on each receiver (0x4dece0). What a ray or annihilator then does to the receiver's local peds is untested (L).

  Leave all of them excluded in the first version.

### 2.5 Physics side effects (M / L)

- **Ped bodies** (simple or boned, body+0x238 = 3/4) are local physics bodies.
  - A ped or ragdoll that hits the **local** car changes the local car's physics. That car's state is then relayed as usual, so all machines stay consistent (owner-authoritative).
  - A ped hitting a **remote** car that is in the client's physics gives a local impulse that the next 0x10 or 0x2d message overwrites, at most 80 ms later. Expect small jitter at worst (L).
- **Crush and damage events** (0x20, 0x24..0x26, 0x2b, 0x33) are sent by whichever machine processes the collision (network-race.md §3).
  - If a ped impact can deform or damage a car, a client could broadcast damage to a remote car from a ped only that client has.
  - Whether ped bodies reach DoCrushing 0x436170 or the car damage code was not traced. **Check at run time** (§5).
- **The global LCG reseed** by the ped AI (§2.1) also affects host-only random choices, such as respawn picks in 0x49abf0 and the powerup rolls. They become functions of game time. This is already true in single player, and the host alone decides them (M: harmless).

### 2.6 Which network tracks have peds (H, track files)

- **The "Ped specs" count in each NETRACES arena's track file:**

  | Spawn sets | Arenas |
  |---|---|
  | 0 (stay empty) | Newsumo, arena1, Silonet03, CarrierNET, funfairnet |
  | 4 | silonet1 |
  | 5 | Junknet |
  | 6 | skitracknet |
  | 7 | silonet2 |
  | 12 | timbernet |
  | 13 | quarrynet |

- **RACES.TXT tracks** (network types 3..5) have full single-player sets; newcity1 has 9.
- Whether the listed materials actually sit on polygons decides whether anyone spawns (L, but likely, since the sets name track materials).

### 2.7 CPU and memory (M)

- **Per machine, the same budget as single player.**
  - Spawning is capped at 2000 (0x4d2d28) and done at load.
  - Only peds within the 12-unit camera box run AI and animation.
  - The process pool holds 100 × 0x12c entries, with LRU stealing, and the forms cap visible peds ("Max rendering at once 30").
- **The extra network cost is small:** the threat test scans cars within 5 units of each active ped, and there are at most 12 cars.
- **Bandwidth is unchanged** (nothing is sent).
- **Load time** goes up by the spawn pass on ped tracks, as in single player.

## 3. Leftover synchronisation and the cost of a real sync

- **Message 0x1a:**
  - NetGetMessageSize 0x49f440 still sizes it by a decider: 0 → 0x18, 1 → 0x24, 2 → 0x3c (0x49f49e..0x49f4bc).
  - The ReceivedMessage jump table 0x4a4584 maps 0x1a to 0x4a44c2, the default (out-of-range) target.
  - A scan for `mov cl/ecx, 0x1a` before calls to the builders 0x49f440 / 0x49f680 / 0x49f6f0 / 0x49f860 / 0x49f9c0 / 0x49fdb0 finds nothing.
  - So the id and its sizes are free to reuse, with no sender and no handler (H).
- **Nothing else carries ped data.**
  - The non-car messages carry NONCARS objects by their +0x80 ID.
  - 0x16 carries powerups.
  - 0x31 carries powerup or non-car bodies.

  None of them reads the ped array (H for the list sources).
- **A host-authoritative sync**, roughly (M):
  1. Shared spawn: the seed (§4.2). Small.
  2. The host activates peds around **every** player's car, not only its camera. That means changing the activation box at the top of 0x4d3740 (against 0x74d484), with 12 × ~30 active peds on the host.
  3. A state stream per receiver for nearby peds: index, position, facing, move and frame, state, health, about 0x18 bytes each, plus packed bodies for ragdolls (0x49cd00). This could reuse id 0x1a. At 30 peds × 0x18 bytes × 12.5 Hz it adds about 9 KB/s per client to the ~15 KB/s already used (network-race.md §6.2).
  4. Clients stop local AI, damage and death (0x4cef80, 0x4cd640, 0x4d1030 / 0x4d2320) and apply snapshots through SetPedMove 0x4cbcd0 and the BONER pose code.
  5. Kill credit needs the killer's player ID and a new "credit" message, because credits are local.

  That adds up to several new hooks deep in the BONER and physics code and a new message pair, and late join and respawns must be handled too: **weeks of work, with a high risk of visual popping**. It is not worth it for a cosmetic crowd. **Local peds are the recommendation.**

## 4. Recommendation: local peds with `[Network] Pedestrians=1`

### 4.1 Required patches (H for the bytes, M for safety)

Both patches go in `src/peds.cpp` (which already patches 0x4c260e and 0x4b6721), driven by a new `[Network] Pedestrians` key in `settings.cpp`:

1. **Spawner gate.** At **0x4d2cd7**, replace `0f 85 71 06 00 00` with six `90`s.
   - In single player the `jne` is never taken, so the patch changes nothing there.
   - Apply it at startup, before any track loads.
2. **No race end from ped kills in network games.** At **0x4cd1a3** (`b9 1a 00 00 00`, `mov ecx,0x1a`, the first instruction after the count test `jl` at 0x4cd1a1, bytes `7c 2a`), place a 5-byte `jmp` to a stub:

   ```
   cmp dword [0x68b918], 0
   jne 0x4cd1cb          ; pop esi / ret: count kept, no headup, no RaceCompleted
   mov ecx, 0x1a
   jmp 0x4cd1a8
   ```

   - Only the type-0 branch matters, since network races are always type 0. The type-2 and type-5 branches (0x4cd1fc, 0x4cd24f) never run in network games.
   - Leave the 0x100 "counted" flag and the counter alone. The HUD doesn't show the counter in network games.

Optional:

3. **`[Network] PedCredits=0`** to turn kill credits off in network games. Hook 0x44b300 (the credit award); its callers are ped code (0x4cd614, 0x4d18c1, 0x4d190b, 0x4d24b6, 0x4d258d) and others through 0x44b440. For each ped call site, return early while gNet_mode != 0. The simplest way is to check the return address for 0x4cd619 / 0x4d18c6 / 0x4d1910 / 0x4d24bb / 0x4d2592. The default should be 1 (credits on); that is a gameplay call for the player.

### 4.2 Optional shared spawn seed (M)

- **Goal:** every machine starts with the same crowd.
- **How:** hook the call to ExtractColumns at **0x505a2d** in LoadTrack. Before it, `srand(seed)`; after 0x4d3520 at 0x505a37, `srand(time(NULL))` again.
- **The seed** must be something every machine has at LoadTrack, for example the race index (0x75bbf8) ^ the host's player ID (gNet_players[0].ID at 0x74bce4, stride 0xd4; M that index 0 is the host).
- **Conditions:**
  - The same track data.
  - The same ped options on every machine. The spawner skips ped types in an exclusion list when 0x65d7c8 == 0 (the 0x515870 loop), and a skip changes the rand stream.
  - No other rand consumer inside 0x40cc50 that differs per machine (L).
- **What it buys:** crowds match only at the start. They diverge once the AI runs (§2.1).
- **Order:** worth doing after the basic version is proven.

### 4.3 Risks (summary)

| Risk | Tag | Mitigation |
|---|---|---|
| All peds killed ends the race (host: for everyone) | H | patch 2 |
| Ped impacts send crush or damage for remote cars | L | run-time check below; if seen, ignore ped bodies in that sender |
| Kill credits change the balance | H | optional PedCredits=0 |
| Players see different crowds; a remote car "hits nothing" | H | accept, or the seed (partial) |
| Frame time on ped-dense RACES.TXT tracks with 12 cars | M | measure |
| A mismatched setting between machines | H | harmless: one machine just has no peds |

## 5. Run-time checks

All of these use two hidden copies on loopback with the netshim (one UDP port per instance), host plus client, with the setting on.

1. **Spawn.**
   - Log 0x7447d4 after 0x4d3520 on both copies. The game's own "%d pedestrians (%d bytes each...)" (0x65e69c, through 0x44c590) works if the debug log is on.
   - Use Junknet, quarrynet and timbernet, and one RACES.TXT track in a type-3 game.
   - Expect a non-zero count, and peds visible on both copies.
2. **Race end.** On silonet1 (the smallest crowd), kill every ped on the host, then on the client. The netshim log must show no 0x0a, the race must go on, and the stub must be hit (log it).
3. **The wire.** Count packets by type during ped kills. There must be no new types and no 0x1a, and the 0x19 / 0x20 traffic must match a run with peds off.
4. **Damage from peds.**
   - Drive the client's car into a ragdoll, and drive a remote car into the observer's peds.
   - Log sends of 0x20, 0x24..0x26, 0x2b and 0x33 whose player ID is not the sender's own car. Expect none.
5. **Credits.** An own kill adds credits once (headup). A remote kill adds nothing, and no copy crashes, including at 0x4cd2be with a driver-7 car.
6. **Powerups.** Ped powerups still come out substituted in network games (log the GotPowerup index at 0x4d8d40 against the result).
7. **Map and camera.** "Show pickups on map" draws local peds, and ped camera mode 5 is selectable without upsetting the spectate message 0x2f.
8. **Performance.** Frame time with peds on and off on newcity1 in a type-3 game, as host with 2+ cars.
9. **Seed (if built).** Dump the first 20 ped positions after 0x4d3520 on both copies; they must match.

## Address table

```
0x68b918	gNet_mode	0 single, 2 host, 3 client
0x4d2cc0	SpawnTrackPeds	per polygon; gate 0x4d2cc6..0x4d2cd7 (jne 0f 85 71 06 00 00 -> 0x4d334e); cap 2000 at 0x4d2d28
0x40cc50	ExtractColumns	LoadTrack call at 0x505a2d; -> 0x40d530 -> spawner at 0x40d566
0x504bf0	LoadTrack	0x4ca9f0 at 0x505a1c, 0x40cc50 at 0x505a2d, 0x4d3520 at 0x505a37
0x4ca9f0	LoadTrackPedSpecs	"Ped specs": 0x694138 (x0x20), count 0x69bce8; allocs 0x744808 (2000 x 0x54)
0x4d3520	ShrinkPedArray	0x744808 -> count x 0x54
0x4d5bd0	InitPeds	from LoadRace 0x48188f; clears 0x7447d4, 0x7447cc, mode flags
0x4cadc0	LoadPedSettings	once, InitialiseGame 0x47e28f
0x4d3740	PedsUpdate	race loop 0x492da7, unconditional
0x4d5970	PedsFramePass	race loop 0x493a53, unconditional
0x4cd160	PedDeathCount	type 0: 0x7447cc vs 0x7447d4 -> RaceCompleted(1) at 0x4cd1c6; patch site 0x4cd1a3 (b9 1a 00 00 00)
0x4cd260	PedKilled	driver test 0x4cd2be (== 8); credits 0x4cd614, time 0x4cd631
0x44b440	AwardCreditsWrap	edx = 0x676920, jmp 0x44b300
0x44b300	AwardCredits	no gNet_mode test; 0x75bb80 += n; headup
0x44b490	AwardTime	gNet_mode == 0 only
0x4d18c1	PedBonusCredits	also 0x4d190b (0x4d1030), 0x4d24b6 / 0x4d258d (0x4d2320); proc+0xc0 == 0x75bc2c
0x5030b0	RaceCompleted	host and reason < 8 -> 0x4a5710; race-over countdown 0x74d62c
0x4a5710	NetSendRaceOver	type 0x0a (reason) guaranteed to each other player
0x48c1c0	LoadRaces	network lists keep race type (entry+0x210) == 0 only
0x762438	gRace_info	+0x210 race type (0 Carma1, 1 cars, 2 peds, 3 checkpoints, 4 smash, 5)
0x7447d4	gPed_count
0x7447cc	gPeds_killed
0x744808	gPeds	stride 0x54
0x493430	RaceLoopNetHUD	network -> 0x499360, skips the ped counter 0x4934a5..0x493500
0x495e10	DrawMap	peds if 0x74d09c, type 2, or network and details+0x4c ("show pickups"); test at 0x495e85
0x48d110	ParseNetOptions	order -> details+0x44.. ([2] = +0x4c show pickups, [5] = +0x58 credits, [7] = +0x60 target)
0x40ea30	CameraModeSelect	mode 5 (ped cam) needs 0x7447d4 > 0 (0x40eaa7, 0x40eb38)
0x4d8d40	GotPowerup	flag 0x40 + no peds (0x4d8d9c), network flag 2 (0x4d8db3) -> substitute
0x4d9150	ApplyPowerup	same tests 0x4d91b0 / 0x4d91bf; network send 0x16
0x4dece0	ReceivedPowerup	flag 1 (net_global) -> local 0x4d9150; else action on sender's car
0x6a0a54	gPowerups	x0xac; +0x38 flags (1 global, 2 net_inappropriate, 4, 0x40 needs peds, 0x80 pratcam)
0x49f440	NetGetMessageSize	0x1a: 0x18 / 0x24 / 0x3c (0x49f49e)
0x4a4584	ReceivedMessageTable	0x1a -> 0x4a44c2 (default)
0x497e73	NetSendNonCars	loops NONCARS 0x79eda0 (count 0x6793dc), not peds
0x47dd8b	InitialSrand	srand(time(NULL))
0x576960	srand	MSVC; ped AI reseeds at 0x4cefac, 0x4cf4b6, 0x4cf4c5
0x576970	rand	MSVC LCG; wrappers 0x513520 (int), 0x513620 / 0x513650 (float)
0x514c30	GetTotalTime	network: no pause accumulator 0x6abef4
0x4977f0	AddMapMarker	type-5 mission peds (spawner 0x4d3291); local
```
