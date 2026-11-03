# Pedestrian moves and animation vs frame rate (CARMA2_HW.EXE, Steam build)

Static analysis of `testrun\CARMA2_HW.EXE` (base 0x400000), 2026-11-03. Confidence: **H** = read from the instructions (capstone) or the data files, **M** = decompiler output or a strong inference, **L** = a guess to check in play. Fastcall throughout (ecx, edx, then stack).

## Short answer

- A ped's move is picked by game-time rules: timers in ms, car proximity, and random rolls. Animation advances by elapsed game time with a carried fraction. **Nothing in move choice or animation advance divides by the frame period or counts frames** (H). The frame period 0x74d3e4 has only three readers in ped code (§3), and none of them can switch a ped between standing and running.
- The ped code never reads camera FOV, aspect or yon (H, xrefs). Activation uses a fixed box around the camera position, with hysteresis (§4).
- One code path *can* switch a ped between a standing pose and running on consecutive frames: the **unlatched reaction re-roll at 0x4cf7e7** (§1.3). It fires once per frame, so it fires more often at 100 fps, but it fires at 25 fps too. Confidence that this is what the player sees: **L-M**. Confirm with the run-time log in §5 before patching.

## 0. Data structures (M unless marked)

Ped record: array 0x744808, count 0x7447d4, stride 0x54.

| Off | Meaning |
|---|---|
| +0x00 | BONER character ("char") |
| +0x04 | health byte; 100 at spawn (0x4d314f), <= 0 means dead |
| +0x08 | ushort flags: 1 = active, 0x20 = first active frame done, 0x40 = later frames, 0x80 = disabled |
| +0x0c | process data ("proc"), pool 0x6944c0, 100 x 0x12c, LRU-stolen by +0x74 time when full |
| +0x10 | state: 0 calm, 1 running, 2 scared reaction, 3 falling/physics, 4 getting up, 5 flying, 6 dead (H for the writers listed below) |
| +0x14 | movement spec from SETTINGS.TXT (table 0x69bcf0, 0x14 bytes). +0/+4 = min/max random-turn time in ms. +0x10 = max angle. +8/+0xc = walk rate as ms per anim frame = 1000/(30 x mult), so 47 and 19 for 0.7 and 1.7 (H, loader 0x4caf80..0x4cb015) |
| +0x1c..0x24 | position last frame (used by the activation test) |
| +0x28 | time of the last threat (or of the last re-roll) |
| +0x2c | time of the **first** scare. Written only at 0x4cf7dc (calm -> scared) and zeroed at reset 0x4ce560 (H) |

Char fields:

| Off | Meaning |
|---|---|
| +0x07 | current move index |
| +0x08 | next-move index |
| +0x09 | bit 2 = call end-of-move callback |
| +0x14 | bit 4 = physics on |
| +0x18 | float frame fraction |
| +0x1c | short current frame |
| +0x1e | short **ms per anim frame** |
| +0x24 | time of the last advance |
| +0xbc | blend slot |
| +0xd8..+0xe0 | root velocity in units/s |

Proc fields:

| Off | Meaning |
|---|---|
| +0x18 | avoidance mode |
| +0x6c / +0x70 | turn start / turn end time |
| +0x7c | next random-turn time |
| +0x80 | run-ramp rate |
| +0xb8 | ramp slope |

Moves (FORMS\BIPED1.TXT, all "Default frame rate 30") (H): 0 stand_still, 1 look_around, 2 stand_still_scared, 3 jump_out_of_skin, 4 silly_scared, 10 plead, 11 on_knees_mercy, 13 out_of_breathe, 30 walk_normal_male, 31 pissed_person, 40 run_slow, 41 run_fast, 70-74 FALL, 80-85 get_up, 90-97 hit/dead, 110 drowning, 111 silly_dance, 112 panicking, 113 squashed. Biped moves 0-13 have looping flag "None" and 30-42 have "Y".

## 1. Where the move is chosen

### 1.1 SetPedMove 0x4cbcd0 (H)

The signature is ecx = ped, edx = move, then stack: rate (int ms/frame, -1 = file default), blend ms, end-callback flag, time, new state (-1 = keep).

- It remaps the move through the table 0x65d7f4. Each row is `[move*7 + col]`. The columns are:
  - 0: fallback when the form lacks the move;
  - 1 / 2: missing leg (ped type flags 0x58f2e4 / 0x58f2ec);
  - 3 / 4 / 5: power-up modes 0x7447a8 (dance 111), 0x7447b8 (pissed 31), 0x7447f8 (panic 112).
- It clears the run ramp (proc+0x80 = 0) unless the move is 31, 40, 41, 42 or 112.
- It then calls SetMove 0x40a050.

### 1.2 Who calls it

| Where | Trigger | Move | State | H/M |
|---|---|---|---|---|
| Spawn 0x4d3188 | track ped polygons | 30 at rand(19..47) ms/frame, or 0 if spec+0xc == 0 | - | H |
| AI 0x4cef80, calm path 0x4cf766 | car threat while state 0 | 0x65e468[rand 0..7] = {2,2,2,2,2,2,3,4} | 2 | H |
| AI re-roll 0x4cf93e | car threat, state != 0, now - ped+0x2c >= 10000, proc+0x98 <= 0 | 0x65e488[rand 0..8] = {2,2,2,10,11,13,-41,-41,-41} (negative = 41) | 1 if run, else 2 | H |
| AI calm-down 0x4cf3b8 | state 1, no threat this frame, now - ped+0x28 >= 10000, boned, proc+0x18 == 0 | 30 at rand walk rate | 0 | H |
| End-of-move callback 0x4cbf20 (slot 3 of table 0x65d778; called from 0x409e93 when a move with the end flag hits its last frame) | moves 2-6, 10, 11 -> turn away + 41 with ramp; 13 -> 40 with ramp; 80-85 -> 41 with ramp via 0x4cc660 (walk if 0x7447d0 or 0x6a041c); 90-92 -> 97; 96/97 -> 93/94; 113 -> 0x4cc6e0 | | 1 / 5 | H (jump table 0x4cc224/0x4cc244) |
| Panic spread 0x4d544b | every 300 ms (0x6a042c): calm peds within 3 units of a running, dead or state-5 ped | scared reaction | 2 | M |
| Fall check 0x4d41da | ground face more than offset+0.435 below, or slope over 25 deg; only on frames where the face changed | 72 (FALL), physics on | 3 | H |
| Physics-end callback 0x4d1020 -> 0x4d1030 (slot 1, from 0x4094f7) | after a ragdoll | 81 get-up (state 4), 41, 93/94 | | H |
| Per-frame mode check 0x4d4d52..0x4d4de2 | power-up mode on and move != mapped move; or no mode and move in {31, 111, 112} -> 30 | | | H; the columns are idempotent for the mapped moves (table dump), so this is stable |

### 1.3 The AI (0x4cef80), called each frame from 0x4d4739

- **Gate (H, 0x4d3e8a..0x4d3e96):** only peds in **state 0 or 1** with physics off. Peds in state 2 (playing a scared or plead move) are not re-evaluated until the end-of-move callback sets state 1.
- **Inputs (H):**
  - every car (list 0x4b5fd0 / 0x4b5ff0) inside a 5-unit box around the ped;
  - relative velocity = car velocity (+0x68, +0x70) minus the ped's animation root velocity (char +0xd8, +0xe0);
  - threat if the closest-approach distance squared is less than 2 x car[+0x140] **and** the time to closest approach t is in [0, 7k);
  - k = rand(0.8..1.2), fixed per ped by `srand(ped)` at 0x4cefac;
  - car speed squared must be above 1.0 (0x4cf6d2), otherwise the ped returns without reacting.
- **RNG (H):** after the car loop, `srand(time ^ ped)` (0x4cf4b6 / 0x4cf4c5; MSVC LCG at 0x576970).
  - The first rand after it (the flee-angle jitter, `rand(+-60 deg)` at 0x4cf667) changes by about 3.3 per ms of game time. It is a slow function of game time, not of frame count (M).
  - The re-roll at 0x4cf8af is a later call, so it is effectively a fresh roll every frame (M).
- **The re-roll is not latched (H):**
  - the 10 s test at 0x4cf7e7 reads ped+0x2c;
  - the re-roll path writes only ped+0x28 (0x4cfa05);
  - ped+0x2c is set only on the calm -> scared path (0x4cf7dc).

  So once a running ped's ped+0x2c is 10 s old, *every frame* with a threat re-rolls. That includes peds that never set it: started running after getting up, panic spread or a zap, where ped+0x2c is still 0.

  Each roll has a 2/3 chance of a standing-type move (2, 10, 11, 13 in state 2) with an instant turn away. When that move ends, the callback restarts run 41 at 100 ms per anim frame in state 1. On the next threatened frame the ped re-rolls again. The ped therefore stands, pleads or kneels, shows run frame 0 for about 1.5 frames, and stands again (M).

## 2. How the animation advances

- **AdvanceMove 0x409ca0**, once per frame per active ped (0x4d4df4), with game time 0x514c30 (H):
  - steps = floor(frac + (now - last) / (ms per frame)), clamped to +-10;
  - the fraction is kept in char+0x18, and last = now every frame;
  - each step applies one key frame through 0x407b30 (the root translation from the frame data moves the ped, so locomotion is root motion; intermediate steps skip the bone rotations);
  - there is **no interpolation**: on frames with 0 steps, pose and position are held.

  This is exact in game time at any frame period. At 10 ms, a 33 ms/frame move steps on one game frame in three (H).
- **Root velocity (H):** char+0xd8..0xe0 = root delta / (steps x rate x 0.001). It is updated only on frames with steps and kept otherwise. It depends on the animation, not on the frame time.
- **SetMove 0x40a050 (H):**
  - Same move without force bit 0x40000000: it updates the rate only and returns 1, so there is no restart. The per-frame rate pushes 0x4d1c60 (edx = -1) take this path too.
  - New move: frame 0, fraction 0, last = 0. The next advance only stamps the time.
  - A blend slot (pool 0x677730, 20 x 0x15c) is used only if the blend argument is non-zero. Blend progress is driven by elapsed ms (0x4097b0). Every ped call above passes 0, so **move changes snap** (no blend, no per-frame reset).
- **Run ramp 0x4d3ebf..0x4d3ff8 (H):**
  - runs started by the callback, 0x4cc660 or 0x4d1986 begin at proc+0x80 = 100 ms per anim frame, slope proc+0xb8 = 0.09;
  - each frame: proc+0x80 = **trunc**(proc+0x80 - period x 0.09) (0x575b80 is `_ftol`, chop mode);
  - at <= 30 it fixes 30 and stops;
  - the rate is set through 0x4d1c60 with no restart.
- **Orientation 0x40a730 (H):** re-applies the *current* frame (0x407b30), not a rest pose. Turns therefore never show an idle pose between steps.

## 3. Frame-period-dependent code (complete for the ped code)

The readers of 0x74d3e4 are 0x4d376c, 0x4d3ee1 / 0x4d3f42 and 0x4ce7d0 (H, xrefs).

1. **Run-ramp truncation (H):** the per-frame decrement becomes ceil(period x 0.09), so 1 per frame at 10 ms. The ramp lasts 700 ms at 10 ms, about 770 ms at 33 ms and 580 ms at 16.7 ms (designed: 780 ms). The effect is small and monotonic and cannot flip a move.
2. **Scare-suppression timer 0x6a041c (H):** decremented by the period and clamped at 0. It is set to 5000 by 0x4d6dc0. Exact.
3. **0x4ce7d0 (M):** five 7-sprite chains attached to zapped peds. They move by period x 0.002 and retarget every > 50 ms. No ped moves are involved.
4. **Per-frame decisions whose count scales with fps (H):**
   - the threat test;
   - the re-roll (§1.3);
   - the obstacle-avoidance direction choice after 0x4d4739 (random bool and rand(0.7..1.0), both from the `time ^ ped` seed; it changes facing, not the move).
5. **Everything else is ms of game time (H):** 10 s calm-down and re-roll, 300 ms panic spread, 125 ms sound sequence (0x4d4f05), 7.5 s proc keep-alive, random turns of 2-30 s, 30 s drowning.
6. **Not present:** "every N frames" logic, speed from position delta divided by the frame period, or integer rounding of distance moved (ped positions are floats, moved by root motion per key frame) (H for everything read).
7. 0x69bce0 = "game time unchanged since last frame" only gates the dead-body bone jitter 0x4d6c10. It never fires at 10 ms frames.

## 4. Camera, FOV, yon

- **Activation (H)**, top of 0x4d3740, against the camera position 0x74d484 (the translation of camera-to-world 0x74d460, built every frame by 0x4940e0 through 0x51e7d0):
  - **Deactivate** when |dx| or |dz| >= 12 (0x694118) or |dy| >= 7.2 (0x69bc1c), **and** proc+0x8c is older than 7499 ms or unset.
  - **Stay inactive** inside 9 / 5.4 (0x6944bc / 0x694128) unless 0x694490 is set. 0x4d6390 sets it when the camera jumps more than 20 units in one frame (0x4940e0).
  - The values are "Max distance ... processed" 12 and "Min distance ... pop up" 9 from PEDS\SETTINGS.TXT.
  - Inactive peds release their model slot (0x408400) and are not drawn.
- **No reads (H):** no ped function reads gCamera_angle 0x74d354, the aspect, gYon 0x761f4c, gYon_squared 0x762100 or the yon multiplier 0x655ddc.
  - The box is omnidirectional, so a wider Hor+ view only shows peds that were already active.
  - Peds beyond 12 units are inactive, so a higher yon does not bring in more ped work.
  - The hysteresis rules out per-frame toggling.
- **Frame-rate side effect (L):** a ped can only activate while it is in the 9-12 ring. At 100 fps a fast camera spends 3-5x more frames in the ring, so more peds pop in rather than staying hidden. That affects visibility, not idle vs run.
- **Model-slot cap (M):** "Max rendering at once 30" per form (FORMS) caps visible peds by count (0x4083b0). It does not depend on frame rate.

## 5a. Stand/run flicker (re-roll): explanation and fix

- **Code-backed candidate (L-M):** the unlatched re-roll in §1.3.
  - It needs a ped in state 1 (running), a car closing on it, and ped+0x2c at least 10 s old (or never set).
  - The ped then alternates every 1-2 frames between a standing-type move with a snap turn and run frame 0. That is exactly "flipping between idle and run".
  - It is driven by frames, so 100 fps gives 2-5x more flips per second than 20-50 fps. But it exists at any rate, so it does not fully explain "never saw it in 1998".
- **Perceptual alternative (L):** at 100 fps the world moves every 10 ms, but ped poses *and positions* change only on key-frame steps:
  - every 19-47 ms when walking;
  - every 100 ms falling to 30 ms during the first 0.7 s of a run.

  Held poses followed by a lurch look like stop/go next to a smooth 100 fps camera. At 25 fps every rendered frame is also a step. Fixing this would mean pose interpolation in 0x409ca0 / 0x407b30, not a byte patch.
- **Confirm first (per "read the code, don't guess"):** hook 0x4cbcd0 entry and log, per call:
  - game time;
  - ped (ecx), move (edx), state ([esp+0x14]);
  - the return address ([esp]).

  Re-roll flips show up as alternating 2/10/11/13 and 41 calls returning to 0x4cf943, with the 0x4cbf20 callback (returns to 0x4cc1f0) in between.
- **Smallest fix if the log shows the re-roll (H for the bytes):** latch ped+0x2c when re-rolling, so a running ped re-rolls at most once per 10 s. Rewrite 35 bytes at **0x4cf7e7** with the same flags, the same branch targets and eax = proc on exit. The code after it only uses cl, and reloads edx before using it.
  - Old: `8b 56 2c 8b c5 2b c2 3d 10 27 00 00 0f 82 87 01 00 00 8b 46 0c 8b 88 98 00 00 00 85 c9 0f 8f 76 01 00 00`
  - New: `8b c5 2b 46 2c 3d 10 27 00 00 0f 82 89 01 00 00 8b 46 0c 83 b8 98 00 00 00 00 0f 8f 79 01 00 00 89 6e 2c`

    (`mov eax,ebp / sub eax,[esi+0x2c] / cmp eax,10000 / jb 0x4cf980 / mov eax,[esi+0xc] / cmp dword [eax+0x98],0 / jg 0x4cf980 / mov [esi+0x2c],ebp`)
  - ebp holds the game time throughout 0x4cef80 up to 0x4cf84a (H). No branch targets the inside of the block (H, scan).
  - Behaviour change: a chased ped keeps running between re-rolls, instead of re-rolling every threatened frame.
- **Not worth patching for this symptom:** the ramp truncation (§3.1). If wanted, replace the `_ftol` result with a float accumulator, but it cannot cause a flip.

## 5b. Torso jitter (follow-up: "torso jitters between current and previous position, running in place")

This section covers only the animated path, which runs while char+0x14 bit 4 (physics) is clear. 0x409ca0, 0x407b30, 0x40a730 and 0x40b0a0 all return early when physics is on (H), which fits the clue that ragdolled peds don't jitter.

### 5b.1 Where the drawn transforms live (H unless marked)

- **Pointers:** ped `P = [0x744808] + i*0x54`; char `C = [P+0]`; form `F = [[C]+0x28]`; model slot `s = (signed char)[C+4]`. When s < 0 the char has no model and is not drawn.
- **Actor list:** `L = [[F+0x30] + s*8 + 4]`. Bone i's actor is `[L + i*4]`. Its br_matrix34 is at **actor+0x2c**, translation at **actor+0x50 / +0x54 / +0x58** (= matrix +0x24/+0x28/+0x2c). Helpers: 0x407aa0(ecx=C) returns the bone-0 matrix, 0x407ad0(ecx=C, edx=i) returns bone i's. If `[C+0xe8]` is non-zero it replaces the list, with matrices at `[C+0xe8] + i*0x30`.
- **Bone 0 is the torso.** In FORMS\BIPED1.TXT, bone 0 is "Bip01 Spine" with parent NONE; head (1), legs (2-5) and arms (6-9) hang off it. 0x407b30 treats the parent<0 bone as the root and writes `[L+0]+0x2c` (0x407cea..0x407cfb).
- **Limbs are world-space siblings, not BRender children (M).** 0x407e70 builds each bone matrix from the key-frame rotation bytes (key frame +0x30, 3 bytes per bone), the base rotation C+0x8c, the pivots (bone def +0x20/+0x2c) and the parent bone's world matrix, translation included. A limb is therefore only placed relative to the torso when 0x407e70 runs, i.e. inside 0x407b30.
- **Char fields:**

| Off | Meaning |
|---|---|
| +0x8c..+0xbb | base rotation; translation +0xb0..+0xb8 is forced to 0 wherever the base is rebuilt (0x40a6aa..0x40a6b6, 0x409400, 0x4096f0, 0x4097b0) |
| +0x5c..+0x8b | movement frame; root-motion deltas are rotated by it |
| +0xc0..+0xc8 | facing direction |
| +0xcc..+0xd4 | up vector |
| +0x07 | move index |
| +0x1c | short key frame |
| +0x18 | float fraction |
| +0x1e | ms per key frame |
| +0x24 | time of the last advance |
| +0xd8..+0xe0 | root velocity |

- **Ped and proc fields:** P+0x1c..+0x24 is the torso position copied at the end of each frame. Proc `Q = [P+0xc]`:

| Off | Meaning |
|---|---|
| +0x18 | avoidance mode byte (1/2/8 = obstacle, set by 0x4cfab0, which clears them first with `& 0xf4`; 4 = car threat, set by 0x4cef80, which clears it each frame) |
| +0x3c..+0x44 | obstacle or car point |
| +0xbc | obstacle distance squared |
| +0xa4..+0xac | escape direction |
| +0x0c..+0x14 | desired direction |
| +0x48..+0x50 | last turn target |
| +0x54..+0x5c | turn target |
| +0x60..+0x68 | turn start direction |
| +0x6c / +0x70 | turn start / end time |
| +0x7c | next random turn |
| +0x90 | car-avoid recheck time |

### 5b.2 Every writer of the torso matrix on the animated path

| Writer | When | What it does to the torso | Limbs re-posed? |
|---|---|---|---|
| 0x407b30 root branch (0x407cbf..0x407e19), from AdvanceMove 0x409ca0 | **only on frames with >= 1 key-frame step** | rotation = key frame x base (skipped for intermediate steps, edx=2). Translation = saved + key-frame delta x move scale (F+0x30+move*0xc+8), rotated by C+0x5c (0x407d4a..0x407e02). With stack arg 0 it restores the saved translation and adds no delta (0x407e07) | yes, in the same call |
| 0x40a730 SetCharFacing, from 0x4cc2c0 (instant turn, amount 0) and the turn lerp at 0x4d43f7 / 0x4d4597 | every frame while a turn runs, and every avoidance frame within 0.5 units | rotation only; translation is saved and added back (base.t = 0). **No root-motion delta**: it calls 0x407b30 with **edx=3, arg 0** (0x40aeab..0x40aeb4), which skips the root branch and re-poses the limbs from the *current* key frame | yes |
| 0x40b0a0 ground snap, from 0x4d4220 (mode 1) | only on frames where 0x4d1d70 had to re-find the ground face (the ped left its triangle, or came within 0.217 of the cached box) | translation = ground point + grounding offset (DEF "GROUND 0.154", F+0x30+move*0xc+4) + summed key-frame deltas 1..frame, on the axes named in the move's loop-reset flags (move+0x2b: X=1, Y=2, Z=4). For walk/run ("Y") only the vertical bob is kept, so x,z = the ground point | not inside 0x40b0a0, but the same branch then calls 0x4cc2c0(..., 0) at 0x4d4268, which reaches 0x40a730 and re-poses (M; needs the face cache Q+0xb0) |
| 0x40a050 SetMove | on move change | y = clearance + ground, then 0x407b30 | yes |

Root motion is **delta-only** (M, loader 0x404f60 at about 0x406340..0x406440):
- key frame n's translation = pos[n] - pos[n-1];
- frame 0 holds -sum for the loop-reset axes, otherwise frame 1's delta;
- move+0x2a = 1 for in-place moves (no net motion, or X reset), and the AI and the ground-snap sums are skipped for those.

So re-applying a key frame through 0x40a730 never adds the delta again, and nothing snaps the torso back to the start of a frame. I found **no path** that writes the torso differently on 0-step frames than on step frames, apart from turns. Turns rotate the whole body and keep its position (H per writer).

### 5b.3 Avoidance turn (0x4d4733..0x4d4b5b) (H)

- **When it runs:** every frame while Q+0x18 != 0 and the ped is within 4 units (0x694300 = 4^2) of Q+0x3c. Further away, it clears Q+0x0c..+0x14 (0x4d4b4a).
- **Turn amount:** `amount = min(0.5, (sqrt(d2) - 0.5) * rand(0.7..1.0))` (rand at 0x4d47c7), or 0 within 0.5 units.
- **Side A:** taken from 0x403f90 cross products of the facing and the escape direction Q+0xa4. When |facing . escape| > 0.985 (0x58ab70, within about 10 deg), a **random bool** picks the axis sign instead: 0x513550(50) at 0x4d496b, held only in the local [esp+0x34] and never kept in a field.
- **Sequence:**
  1. 0x4cc2c0(A) (0x4d4a3d), then probe f1 = 0x4cfab0.
  2. Flip the axis; 0x4cc2c0(B) (0x4d4aef), then probe f2.
  3. If f2 <= f1: restore Q (0x4b dwords) and call 0x4cc2c0(A) again (0x4d4b3b), then clear Q+0x0c..+0x14.

  **Equal probes (open ground) keep A.**
- **How it moves the body:** with an amount > 0, 0x4cc2c0 *restarts* the timed turn (Q+0x54 = target, Q+0x6c = now, Q+0x60 = current facing, end <= now + 500 ms; 0x4cc47d..0x4cc4cf) whenever it is not turning or the target differs from Q+0x48 by more than about 18 deg (dot <= 0.95). With amount 0 it calls 0x40a730 at once (instant snap).
- **Seed:** `srand(time ^ ped)` is set in 0x4cef80 earlier in the same frame. The random bool is the 1st rand after it when d2 <= 0.25, otherwise the 2nd (after rand(0.7..1.0)).
  - As the 2nd call it is effectively random every frame (the MSVC LCG multiplier squared spreads a seed change of 10).
  - As the 1st call it follows game time at about 3.3 LCG units per ms, i.e. a 50/50 bucket about 100 ms long. The XOR with the ped pointer adds jitter at bucket edges, so with 10 ms steps it can flip on consecutive frames there (M).
- **Result:** while the facing is within about 10 deg of the escape axis and both probes are equal, the target can swap left/right every frame. Each swap restarts the turn from the current facing, so the **whole body yaws back and forth** until the facing gets past about 10 deg. This is possible at any frame rate; the number of swaps per second scales with fps (M). It is a rotation wobble of the whole body, not "torso at the previous position".

### 5b.4 Best explanation (M)

The torso and limbs move only on key-frame steps of 0x409ca0. Ped locomotion is pure root motion, and there is no interpolation between key frames (H).
- **Step rates:** walk 19-47 ms per key frame (21-53 Hz); run 30 ms, and 100 ms easing down to 30 ms for the first 0.7 s of each run.
- **At 100 fps:** 1-4 of every 2-5 rendered frames show the ped frozen at its last key frame while the camera moves at 100 Hz, then the whole body jumps by one delta. The spacing is irregular (e.g. 2,3,2,3 frames for a 25 ms walk). The held body reads as dropping back to its previous position and catching up, with the legs cycling on the same coarse steps ("running in place").
- **At 20-33 fps:** almost every rendered frame contains a step, so the motion looks continuous.

This is frame-rate dependent with no code bug behind it. Mechanism H, match to the player's description M.

Secondary candidates:
- the avoidance yaw wobble (5b.3, L-M);
- a torso/limb split on a ground-snap frame if 0x4cc2c0 does not reach 0x40a730 (L).

**Log per frame, for one ped near the camera:** game time; `C+0x1c`, `C+0x24`, `C+0x1e`; torso `[L+0]+0x50..0x58`; left leg `[L+8]+0x50..0x58` (bone 2); facing `C+0xc0..0xc8`; `Q+0x18`; `Q+0x54..0x5c`; `Q+0x6c`. Read it as follows:
- Quantisation: the torso moves only on frames where C+0x24 jumps and C+0x1c advances, and the torso-to-leg offset is constant between steps.
- Wobble: the facing swings back and forth while Q+0x18 != 0.
- Real desync: the torso-to-leg offset changes on a frame where C+0x1c did not.

**Quick play test:** FrameRate=30. Quantisation should vanish; wobble and desync would remain.

**Fix if quantisation:** a render-time lerp of each ped's root translation and pose by the fraction C+0x18 toward the next key frame. That needs a hook (after 0x409ca0, before render), not a byte patch.

## 5c. Physics sync snap-back (run-time confirmed cause of the torso jitter)

The run-time log shows the torso x being written back by 0x532f60 (called at 0x4c2637, return 0x4c263c) inside 0x4c2600, from 0x4b66a2 in the physics step 0x4b6630. This section explains why, from the code.

### How an animated ped has a body in the physics list (H)

- **The list:** 0x74a5f0 is the single global physics-body list, linked by body+0x220 (next) with child chains at body+0x224. Each body also has a slot in 0x692458 (100 x 0x18), pointed to by body+0x240, with the slot type at slot+8.
  - Bodies are added by 0x4b5d40, which sets slot+8 = 1 and body+0x19c |= 0x20.
  - 0x4b6090 sets slot+8 = 2 ("simulated"). Nothing sets it back to 1.
  - Bodies are removed by 0x4b5ea0.
- **Who is in it:** cars and other physics objects (added through 0x4b6210 / 0x4b62e0, which also call 0x4b6090, so type 2), and **ped bodies**.
- **Ped bodies:** the BONER allocator 0x4085f0 binds body+0 = the char's bone actor and copies its matrix to body+0x8c. It sets body+0x238 = mode + 2 (3 simple, 4 boned; 0x4cef80 skips types 2..4 when looking for threatening cars) and body+0x23c = back-pointer. With param_8 it adds the body through 0x4b62b0 -> 0x4b5d40 (slot type 1).
  - This happens when 0x408c30 gives the char a physics mode (char+0x14 = 1 simple / 2 boned).
  - The body is removed only by 0x409090 (from 0x408400 when the model slot is released, and from 0x409570 with mode 0).
- **Animated = asleep, not removed:** physics-off 0x409400 clears char+0x14 bit 4 and sets **body+0xec = 1 (asleep)**; physics-on 0x409570 sets +0xec = 0. So an animated ped with a "PASSIVE SIMPLE / PASSIVE BONED" body stays in the list asleep.
  - 0x4d3740 also writes the anim root velocity char+0xd8..e0 into the simple body's +0x68..+0x70 every frame (around 0x4d4cd8).

### What 0x4c2600 reads and who writes it (H)

- **0x4c2600 / child version 0x4c25a0**, for every body in the list and its children, **whether asleep or not**:
  - body+0xf0 == 1: copy 0x5325d0(actor+0x2c, body+0xbc);
  - otherwise: 0x532f60(actor+0x2c, body+0x8c), an orthonormalising copy that keeps the translation.
- **body+0x8c (actor -> body) is written:**
  - **once per physics tick** for every body in 0x4baa00 (called from the tick step 0x4ba6b0 at 0x4b69f5). Asleep bodies jump straight to the copy: 0x4baa81 `cmp byte [esi+0xec],0` / `jne 0x4badeb`, then 0x4badfe `0x5325d0(body+0x8c, actor+0x2c)`;
  - at allocation (0x4085f0).
  - The ground snap 0x40b0a0 also copies the actor into the simple body's +0x8c when char+0x14 bit 1 is set.
- **body+0xf0 / +0xbc:** reset in 0x4baa00 (0x4baa4b). They belong to the simulated path, not to peds.

### Why it shows only at high fps (H)

- **No-tick frames:** 0x4b6630 runs 40 ms ticks (0x74a5ec = next tick time, at most 5 per frame). When **no tick is due this frame** (`cmp edi,eax / ja 0x4b6749` not taken), it takes the interpolation path 0x4b669c..0x4b6741:
  1. 0x4c2600 resets every actor to its body+0x8c from the last tick;
  2. bodies with slot type 2 are rewound by -(next tick - now) x velocity (0x4c2830, loop at 0x4b6717).
- **Effect on peds:** for an animated ped, body+0x8c is the actor pose captured at the last tick. Any key-frame step in between is undone on the next no-tick frame, until the next tick re-copies the actor.
- **At ~25 fps** every frame contains a tick, so 0x4c2600 never runs. **At 100 fps** 3 frames in 4 have no tick.

### Fix (a): never sync sleeping bodies on no-tick frames (smallest, H for the bytes)

- **Why it is safe:** for an asleep body, actor == body right after each tick (the copy above), so skipping the reset changes nothing unless code moved the actor since the tick. That only happens with animation, or with game code that repositions an asleep object, which the next tick picks up anyway. Asleep cars had their velocities zeroed when they settled (0x4baa00, the `uVar10 = 1` branch), so they are unaffected (M).
- **Patch:** skip the whole body, children included, when body+0xec != 0. It uses the NOP padding at 0x4c25f2 (14 bytes) and 0x4c2668 (8 bytes); no xrefs point into either.

| Address | Old | New |
|---|---|---|
| 0x4c260e | `39 9f f0 00 00 00` (cmp [edi+0xf0],ebx) | `e9 df ff ff ff 90` (jmp 0x4c25f2; nop) |
| 0x4c25f2 | 11 x `90` | `80 bf ec 00 00 00 00` (cmp byte [edi+0xec],0) `75 5f` (jne 0x4c265a = next body) `eb 6b` (jmp 0x4c2668) |
| 0x4c2668 | 8 x `90` | `39 9f f0 00 00 00` (cmp [edi+0xf0],ebx) `eb a4` (jmp 0x4c2614, the original jne) |

- **Caveat (M):** a ped that was ragdolled and is animated again keeps slot type 2 (nothing resets it), so the rewind loop 0x4b6717 still moves it by -dt x the anim velocity that 0x4d3740 writes into body+0x68. With fix (a) that rewind is no longer cancelled by a reset and would add up over consecutive no-tick frames.
  - If such peds exist, also skip asleep bodies there. The bytes don't fit in place, so it needs a cave: after `mov eax,[esi+0x240]` at 0x4b6717, add `cmp byte [esi+0xec],0 / jne 0x4b6737`.
  - Alternative: limit fix (a) to slot type 1 by also checking `[ [edi+0x240] + 8 ] == 1`.
- **Fix (b), not needed:** refresh body+0x8c from the actor after 0x409ca0. It needs the char -> body lookup (simple body `[[F+0x38] + 4 + (signed char)[C+5]*8]`, boned through 0x409040), and so a larger hook.
- **Fix (c):** I found no existing kinematic flag that suppresses the write. body+0xf0 only selects the source matrix, and +0x19c bit 0x20 ("added, not yet simulated") is cleared by 0x4b6090 for cars but stays set for peds; it is not checked in 0x4c2600.
- **Other users of the list (keep working):** 0x4cef80, 0x4a7a80, 0x4d6880 and 0x4f9790 iterate it via 0x4b5fd0 / 0x4b5ff0 (cars, opponents, other objects). The tick loop already ignores asleep bodies (0x4b6798).

## Address table

```
0x4b6630	PhysicsStep	40 ms ticks; no-tick frames take the sync path at 0x4b669c
0x4c2600	SyncBodiesToActors	actor+0x2c := body+0x8c (or +0xbc if +0xf0==1); ignores +0xec (patch site 0x4c260e)
0x4c25a0	SyncChildBodies	same for child chains (+0x224)
0x4c2830	RewindBody	actor t += vel * -dt for slot type 2
0x4baa00	TickBodies	per tick: integrate awake bodies; all bodies body+0x8c := actor (0x4badfe)
0x74a5f0	gPhysBodies	body list, next +0x220, children +0x224, slot +0x240 (type at slot+8)
0x4085f0	BonerAllocBody	ped body: +0 actor, +0x238 type 3/4, +0x23c back-pointer, add via 0x4b62b0
0x409400	BonerPhysicsOff	body+0xec = 1 (asleep), char+0x14 &= ~4
0x407aa0	GetCharRootMatrix	bone 0 (Bip01 Spine = torso) matrix: [[[[C]+0x28]+0x30 + slot*8 + 4]] + 0x2c, or [C+0xe8]
0x407ad0	GetCharBoneMatrix	bone i matrix ([L+i*4]+0x2c)
0x407e70	PoseBone	world-space bone from rotation bytes, base C+0x8c, parent bone matrix
0x40b0a0	SnapCharToGround	torso t = ground + offset + summed deltas (axes per move+0x2b)
0x4cc2c0	PedTurnTo	timed turn (Q+0x54/0x6c/0x70) or instant 0x40a730 when amount 0
0x4d4733	PedAvoidTurn	per frame while Q+0x18 != 0; random side at 0x4d496b within 10 deg
0x404f60	LoadBonerForm	key-frame t stored as deltas; loop-reset flags -> move+0x2b, +0x2a
0x4d3740	PedsUpdate	per frame (from 0x492da7); activation, proc, ramp, AI, AdvanceMove
0x4cef80	PedAI	car-threat test, scare / re-roll / calm-down; states 0,1 only (gate 0x4d3e8a)
0x4cf7e7	PedAIRerollGate	10 s test on ped+0x2c, not latched (patch site)
0x4cf766	PedAIScare	calm -> scared, table 0x65e468, sets ped+0x28/+0x2c at 0x4cf7d9
0x4cf8a8	PedAIReroll	table 0x65e488 {2,2,2,10,11,13,-41,-41,-41}
0x4cf34a	PedAICalmDown	state 1 & 10 s no threat -> walk 30
0x4cbcd0	SetPedMove	ecx ped, edx move, rate, blend, endflag, time, state; remap 0x65d7f4
0x65d7f4	gPedMoveRemap	[move*7+col]: fallback, leg L/R, dance/pissed/panic
0x4cbf20	PedMoveEnded	callback slot 3; scared/plead -> run ramp; getup -> run
0x4cc660	PedStartRun	41/40 with ramp 100 ms, slope 0.09, state 1
0x4d1030	PedPhysicsEnded	callback slot 1 via 0x4d1020; getup/run/dead
0x65d778	gPedBonerCallbacks	slots: 0 impact 0x4cdc00, 1 0x4d1020, 3 0x4cbf20
0x40a050	SetMove	BONER; same move = rate only; new = frame 0, last 0; blend optional
0x409ca0	AdvanceMove	BONER; steps by game time / ms-per-frame, fraction kept, no interp
0x407b30	ApplyKeyFrame	BONER; pose + root motion
0x4097b0	BlendStep	BONER; time-based blend
0x40a730	SetCharFacing	BONER; re-applies current frame
0x4d1c60	SetPedMoveRate	edx=-1 path, no restart
0x4d3ebf	PedRunRamp	proc+0x80 -= trunc(period*0.09), floor 30
0x4d40c4	PedFallCheck	-> state 3, move 72
0x4d4d52	PedModeMoveCheck	power-up mode remap per frame
0x4d544b	PedPanicSpread	every 300 ms, radius 3
0x4d1d70	PedGroundFace	cached face; re-query on leaving it
0x4cfab0	PedObstacleProbe	ray vs polygons, returns distance
0x4ce7d0	PedZapSprites	frame-period motion, 50 ms retarget
0x4d6c10	DeadBodyJitter	gated by 0x69bce0
0x4cadc0	LoadPedSettings	12/9 distances, slope, obstacle, movement specs
0x4d3188	PedSpawnMove	walk 30 at rand(19..47) or stand 0
0x514c30	GetTotalTime	game time
0x576960	srand	MSVC
0x576970	rand	MSVC LCG
0x575b80	_ftol	chop
0x74d3e4	gFrame_period	readers in ped code: 0x4d376c, 0x4d3ee1, 0x4d3f42, 0x4ce7d0
0x74d484	gCameraPos	translation of 0x74d460 (camera-to-world)
0x694118	gPedMaxDist	12 (x,z); 0x69bc1c = 7.2 (y)
0x6944bc	gPedPopUpDist	9 (x,z); 0x694128 = 5.4 (y)
0x694490	gPedAllowPopUp	set by 0x4d6390 on camera jump > 20
0x6a041c	gPedScareSuppress	ms, -= frame period
0x744808	gPeds	stride 0x54, count 0x7447d4
0x6944c0	gPedProcPool	100 x 0x12c
```
