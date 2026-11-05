# Pop-in of pedestrians, drones (traffic) and opponents (CARMA2_HW.EXE, Steam build)

Static analysis of `testrun\CARMA2_HW.EXE` (base 0x400000), 2026-11-03. Nothing was run. Confidence: **H** = read from the instructions (capstone on the real bytes) or the data files, **M** = decompiler output or a strong inference, **L** = a guess to test in play. Fastcall throughout. Positions: camera-to-world matrix 0x74d460 (built every frame by 0x4940e0), camera z axis 0x74d478..0x74d480, **camera position 0x74d484/0x74d488/0x74d48c**; actor +0x20 = render_style (1 = none, 4 = faces, 0 = default), actor +0x50..0x58 = translation. World unit: the code converts with 6.9 and 1/6.9 = 0.14492753 (0x4a87a0, 0x41e660), so **1 unit is about 6.9 m** (M; the drone files also say "1 BRU/s = 15 mph").

## 0. Summary

- **Nothing that decides when pedestrians or drones appear reads the yon, the YonFactor, the FOV or the aspect.** They use fixed, world-axis-aligned boxes around the camera: peds 12 units (processed) / 9 (pop-up guard), drone cars 15 (drawn) / 18 (processed). The patch's DrawDistance=max takes scenery to 100 units, so these actors now appear in plain view, 9-20 units away (H).
- **Peds** (section 1): a ped becomes active only in the ring between the 9 and 12 boxes (|dx|,|dz| from SETTINGS.TXT, |dy| = 0.6 x that), and is drawn only while active. That ring is the pop-in. Caps: 30 drawn per ped form, 100 AI records, 30 simple / 10 boned physics per form. Loaded once at start-up into 0x694118 / 0x69bc1c / 0x6944bc / 0x694128 (H).
- **Drones** (section 2): processed (moving, physical) inside an 18 x 13 x 18 box, **drawn only inside a 15 x 12 x 15 box** whose centre is 5 units ahead of the camera. Planes and choppers 43/40, trains 43/50. At most **12** "distance" drones processed and **10** drones drawn at once. All hard-coded floats and immediates (H). There is no separate cop or traffic type: C2's traffic is the drone system; cops are opponents.
- **Opponents** (section 3): hidden only by `PointOutOfSight(pos, gYon_squared)` every frame (0x41e97c..0x41e99a): beyond yon or behind the camera plane. So they **already follow the draw distance** (H). What can still "pop": the **LOD swap** from `simple_<car>.act` to the full model at **20 units** (CarSimplificationLevel 0; `min_dist_squared 8` in every car file, divided by a factor of 50) (H), and opponents rematerialising, which happens beyond gYon+10 from the player car and so out of sight (H).
- **Side effect the patch already has** (section 3.3): InitOpponents sets the opponents' "in view" distance to **gYon + 10 (minimum 45)** (0x4ade44..0x4ade7c, H). At Yon 100 opponents stay fully simulated within 110 units of the player, instead of 45, before they switch to path-following "cheat" mode. That is an AI and physics behaviour change, not only a visual one (M for the gameplay effect).
- **Fog makes it worse** (section 4): the depth-cue fog on every material runs from gYon x 10^(-start/10) to gYon x 10^(end/10) (0x4451a0, H). On New City 1 ("4,0") that is 14-35 units at Yon 35 but 40-100 at Yon 100. In vanilla, drones appeared partly fogged; with the patch, nothing is fogged inside 40 units.
- **Recommendation** (section 5): scale the drone boxes and caps with DrawDistance (cheap: few drones per track, at most 12/10 active). Raise ped distances moderately (24/18 or 30/22.5), with a separate cap check. Push the car LOD distance out by scaling the factor table. Leave opponent culling alone, but make a deliberate choice about the in-view distance. Confirm everything with the counters in section 6.

## 1. Pedestrians

### 1.1 Settings and their globals (H)

`PEDS\SETTINGS.TXT` (loose file in `data\PEDS`, not in DATA.TWT): line 1 `12 // Max distance from camera that peds are processed within`, line 2 `9 // Min distance from camera that peds can suddenly pop up at`.

- Loader 0x4cadc0, called **once at start-up** from InitialiseGraphics 0x47dd20 (call at **0x47e28f**). It reads two floats (0x48fb80):
  - max -> 0x694118 (0x4cae4e), and 0.6 x max (double 0.6 at 0x58a990) -> 0x69bc1c (0x4cae5c);
  - min -> 0x6944bc (0x4cae67), and 0.6 x min -> 0x694128 (0x4cae75).
  So 12/9 gives a 12 x 7.2 x 12 box and a 9 x 5.4 x 9 box.
- Only reader: the per-frame ped update 0x4d3740 (from the main loop at 0x492da7; also from action replay 0x4e6971/0x4e699c).

### 1.2 Activation test (0x4d3811..0x4d3916, H)

For each ped record (array 0x744808, count 0x7447d4, stride 0x54) not disabled (flag 0x80), against the **camera position** 0x74d484:
1. **Inside the outer box**, i.e. |dx| < 0x694118, |dy| < 0x69bc1c and |dz| < 0x694118, or threatened in the last 7500 ms (proc+0x8c, `cmp ebp,0x1d4c` at 0x4d3885): the ped may be active.
2. Already active (flag 1): it stays active.
3. Not active: it becomes active only if 0x694490 is set (camera jumped more than 20 units in one frame: `400 <` test in 0x4940e0, setter 0x4d6390), **or if it is outside the inner box** (|dx| > 9 or |dy| > 5.4 or |dz| > 9; tests at 0x4d38a2/0x4d38ba/0x4d38d2).
4. Otherwise flag 1 is cleared (0x4d3913). Inactive peds release their model slot (0x408400) and their actors get render_style 1 (0x4d3d80). They are not drawn and do not move (peds.md section 4).

So **a ped appears when it enters the 9-12 ring** (H). A ped that is still inactive when you get within 9 units (you drove in faster than one frame across the ring, or it was beyond the cap) stays invisible until you leave and come back (H for the logic, M that it is ever visible to the player). The boxes are **axis-aligned in world space**: depending on heading, peds pop at 12 units ahead or at up to 12 x sqrt(2) = 17 units on a diagonal (H).

### 1.3 Limits on active and drawn peds

- **Drawn**: per form, "Max rendering at once" (`PEDS\FORMS\BIPED1.TXT`/`BIPEDF.TXT` line 3: **30**). 0x4083b0 scans the form's slot array (form+0x30, count form+0x2a) and returns 1 when it is full. The ped then has no model slot and is not drawn, although it is active (M). An enlarged box can therefore produce a second kind of pop-in, peds appearing when a slot frees up.
- **AI records**: pool 0x6944c0, 100 x 0x12c, stolen least-recently-used when full (peds.md section 0; M). Beyond about 100 active peds, peds lose their AI state.
- **Physics**: "Max simple physicing at once 30", "Max boned physicing at once 10" per form (data, M for the consumer).
- Ped count per track is set at load by `Peds per 100 square metres` per material in the race file (e.g. `RACES\newcity1.TXT` "Ped specs"). **The activation box does not change how many peds exist** (M).
- No distance-based animation or detail cut-off was found for active peds. Every active ped is animated every frame (peds.md section 2) and drawn with its one model (M; no ped LOD table was seen).

### 1.4 What makes them pop in

The 9-12 activation ring is fixed in world units and independent of yon (H). At Yon 35 it sat at a third of the draw distance; at Yon 100 it sits at an eighth, with no fog in front of it (section 4). The PCGamingWiki 24/18 tweak moves the ring to 18-24. That is 4x the processed area.

## 2. Drones (traffic, planes, cable cars)

### 2.1 Data (H)

`DRONES\Drone.txt` lists the types; each `DRONES\<name>.TWT` has a `<name>.txt` with type `CAR`/`PLANE`/`TRAIN`/`CHOPPER` (type table 0x594770: 0 CAR, 1 PLANE, 2 TRAIN, 3 CHOPPER) and `Processing - 'always' or 'distance'`. Road traffic (SEDAN01, HATCH01, ESTATE01, ...) is `distance`. 747 and CABLECAR are `always`. The loader 0x44ed10 stores "always" as flag 4 in spec+0x2c (0x44f4c6). Paths and instances come from the race file `START OF DRONE PATHS` (0x450bf0, up to **200** drones, 0x6820d0, records 0x684504 stride 0x5d8). The Graphics "Traffic" option is 0x684518 (setter 0x44ecf0).

### 2.2 Per-frame test (0x4512f0, from 0x40123b; H)

1. 0x451bd0 sets the test centre 0x6820c0..c8 = camera position + **5.0** (0x589e60) x the camera's forward axis (normalised -z of 0x74d460).
2. Near box (drawn), table 0x594760 by type, |d| < x/y/z:

   | Type | Near (draw) box | Far (process) box, table 0x594750 |
   |---|---|---|
   | CAR | 15 / 12 / 15 (0x44ca90; floats 0x589d98, 0x589d9c) | 18 / 13 / 18 (0x44cb80; 0x589db0, 0x589db4) |
   | PLANE, CHOPPER | 40 / 30 / 40 (0x44cae0; 0x589da0, 0x589da4) | 43 / 31 / 43 (0x44cbd0; 0x589db8, 0x589dbc) |
   | TRAIN | 50 / 20 / 50 (0x44cb30; 0x589da8, 0x589dac) | 43 / 31 / 43 (0x44cc20; 0x589db8, 0x589dbc) |

   The result is stored in drone+0x45. Both boxes are world-axis-aligned.
3. Outside the far box (and not "always"): **PROCESSING OFF** (0x451710). The drone leaves the physics world and its actor is set to render_style 1. It is frozen in place until it is processed again.
4. Turning processing **on** needs: outside the near box (`+0x45 == 0`, or the debug flag 0x58f038), not already on, not paused (0x68452c), **fewer than 12 "distance" drones already processed** (`cmp [0x681fb4],0xc` at **0x451528**, also at **0x44f7e8** in the race-start pass 0x44f700), at most one try per 1000 ms (0x4518e0), and a free physics slot (0x451650). So, like peds, a drone only switches on in the ring between the near and far boxes.
5. Drawing: inside the near box, 0x451810 sets render_style 4 ("START RENDERING") only if the drone is processed and **fewer than 10 drones are drawn** (`cmp [0x684500],0xa` at **0x451841**). Outside the near box, 0x4516d0 sets render_style 1 ("STOP RENDERING").

So a traffic car **appears when it enters the 15 x 12 x 15 box centred 5 units ahead of the camera**: at most 20 units ahead, 10 behind, 15 to the side, up to 21 on a diagonal. Planes appear at 40-45 (H).

### 2.3 Other non-opponent cars

There is no separate traffic or cop system. The race files' `Number of cop start points` feeds the opponent code (cop cars are opponents with car id 0x3xx, list 0x7609d8, count 0x691744; section 3). Non-car objects (`NONCARS`) were not examined (L: they are track actors and follow the track-cell culling).

## 3. Opponents (including cops)

### 3.1 Render culling: follows yon (H)

Every frame (main loop 0x4930bd -> 0x41e660, loop 0x41e942..0x41e9a7) for the player (0x75bc2c) and each opponent (count 0x4ae790(1), spec 0x4ae7e0): unless car+0xc == 8 (the local player, M), call **PointOutOfSight 0x4e5ce0** (ecx = car+0x4c position, arg = **gYon_squared 0x762100**). The car is hidden (render_style 1) when it is at or beyond that distance from the camera or **behind the camera plane** (dot with the camera z axis >= 0); otherwise it is shown (render_style 0).

- gYon_squared is computed every frame in 0x4940e0 as mult^2 x camera_yon^2, while camera_yon is already mult x gYon (AssertYons 0x446a10). Opponents are therefore cut at **mult^2 x gYon**, and scenery at mult x gYon (x YonFactor for track cells). With a per-track gYon_multiplier 0x655ddc (read by the race loader at 0x506b75) other than 1, the two differ (H for the maths, value per track unknown: **log it**).
- No FOV or aspect test: a wider Hor+ view shows no culled cars at the edges (H).
- Opponents are not hidden by YonFactor. With YonFactor < 1 they could be drawn floating beyond the drawn track cells (M).

### 3.2 Model detail switch (H)

0x41f110, every frame (0x493a58), for each car in the active list 0x74c7c0 (count 0x74c9ec, car+0xc > 5, not hidden): d = (camera distance)^2 / factor, with factor from the table **0x58f6b0[gGraf_spec_index 0x762324 x 5 + CarSimplificationLevel 0x6793d8]**:

| | Level 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|
| graf spec 0 (0x58f6b0) | 50 | 3 | 1.5 | 0.75 | 0 (always simple) |
| graf spec 1, used on HW (0x58f6c4) | **50** | 5 | 2.5 | 1.5 | 0 |

It picks the highest LOD whose threshold car+0xe1c[i] is below d. Every shipped car file has `1 // Number of extra levels of detail` with `8 // min_dist_squared` (EAGLE3, COPCAR, BUGGY, BUGTOO5, VLAD3, SKYLINE checked). The `simple_<car>` model is therefore shown beyond sqrt(8 x 50) = **20 units** at level 0, 6.3 / 4.5 / 3.5 units at levels 1-3. The swap is done through 0x5146f0 on the car's model tree (car+0xe0c). The Graphics menu "Car complexity" (0x448f20/0x448f10) sets the level 0..3; OPTIONS.TXT `CarSimplificationLevel` can also give 4. It does not depend on yon. **At 20 units the full model replaces the simple one in plain view: this is the likely "opponent pop" (M).** Fog is applied to every LOD's materials (0x445340 loops car+0xe14 LODs), so the swap is not hidden by a material difference.

### 3.3 Cheat mode (dematerialise and rematerialise)

- InitOpponents 0x4add50 (race start, from 0x401131): **gIn_view_distance 0x691764 = gYon + 10, minimum 45** (0x4ade44..0x4ade7c, H; identical to dethrace).
- 0x4a9550, every frame from DoOpponents 0x4a9e10: opponent+0x18 = distance from the opponent to the **player car** (0x75bc3c), and the time stamp opponent+0x68 is refreshed while that distance is < gIn_view_distance (H).
- 0x4aa570: 3000 ms after the last refresh, the opponent starts to cheat (flag 0x20 in +0xb0). On a follow-path objective, ProcessFollowPath 0x4af960 then "Dematerialises" it: TurnOpponentPhysicsOff 0x4a79a0 (render_style 1, off the active car list), and it travels along its path without physics. Back within range, 0x4a7f20 / 0x4a8170 -> RematerialiseOpponent 0x4a87a0 puts it back on the path with physics on. Cop cars (id >> 8 == 3) have their physics turned off directly in 0x4aa570 ("StartToCheat - Turning physics off", call at 0x4aa793) and are sent back to their start point when far (0x4abc40) (H for the calls, M for the objective names).
- Because gIn_view_distance (gYon + 10) exceeds the cull distance (gYon), rematerialising should always happen out of sight (H when mult = 1).
- **The patch already changes this**: gYon is 200 at Max when InitOpponents runs (ApplyDrawDistance runs at start-up, hdtab.cpp), so the in-view distance is 210 instead of 45 (Far 80, Further 110). Opponents within that distance of the player keep real physics and AI; vanilla gave up at 45. Cost: more cars in the physics and collision step. Effect: opponents ahead or behind you race "for real" instead of being moved along the path (M). The value is fixed per race, so changing Draw distance mid-race affects only the next race.
- **Real physics at range means they can leave the road where the player can't see.** In vanilla a car over 45 units away soon runs along its path with no physics and can't fall in the water; at 210 most cars keep physics all race, and on the smaller tracks all of them do (H for the rule, M for how often it happens).
- **Don't lower it to dodge that.** A cheating car isn't drawn, so the distance has to stay above the cull distance (section 5.3).

### 3.4 Out of the world: the teleport, and the freeze it caused

- 0x4ace70, per opponent every 500 ms (+0x4c): HasCarFallenOffWorld 0x442130, then Teleport 0x4a9180 and objective 4. 0x442130 asks for the ground under the car (0x4e4600, below -100 means none; with nudges of 0.05) or a ground material whose name starts with `^` (H). New City has none of those materials, so there it is no ground (M). The other caller of 0x4a9180 is 0x4a914d.
- 0x4a9180 turns physics off, then walks a random route (TopUp 0x4a93c0, nodes in 0x761a40, sections in 0x761a44): each step moves the car to the next node and measures it with 0x4a9550. **The loop 0x4a9293..0x4a93a5 had one exit: distance > gIn_view_distance (0x4a933f)** (H).
- At Max that is 210. Node spans from the race files: Carrier 128, Quarry 154, Funfair 158, Ski Track 175, so no node can ever qualify there; Timber 218 (89% of nodes have none in range), New City 266 (54%), Junkyard 288 to 296, Silo 280, Airport 358, Desert 379. From New City 3's start grid the furthest node is 172. At 45 no racing track has the problem; the 2-node network arenas would, but they have no opponents.
- Seen in a real race: the hang log's stack, all samples under 0x4acebe, EIP in 0x4a9428..0x4a95c8 (0x4a93c0 and 0x4a9550). Reproduced hidden with the stock loop, once by chance and every time with `C2HD_FORCE_FALL`.
- The guard (fixes.cpp) replaces the test at 0x4a933c. The first fill of the route (call at 0x4a9289) resets it; the distance wanted drops 10% every 64 steps, with a hard stop at 4096. Measured on New City 3 over 15 forced falls: cars land 138 to 193 units away after 66 to 258 steps.
- A car put back within the view distance is safe: 0x4b1153 sends any car with neither physics (8) nor cheating (0x20) to 0x4a8170, which turns physics back on. That doesn't depend on distance, and stock reaches the same state (H). In the test all cars came back with physics on.

### 3.6 The primitive heap: missing water and bare trees

- BrZbBegin 0x525d20 takes the primitive heap, called at 0x4e4b00 with 0x6baa40 and 300,000 bytes. The heap struct is 0x79f4b0 (base, size, current); 0x522ddc sets current = base each frame and token 0x103 hands it to the renderer, with the order table at 0x79f4a4 (token 0x102) (H). The [0x79f4b8] field did not track use in a test (always 140184), so don't measure with it.
- Translucent faces wait there for the end of the frame, and what doesn't fit is dropped (M for the mechanism, H for the effect). Tested on New City 3 by flying the camera along the path sections with a temporary hook on the call at 0x4940f7 (it runs just before the view matrix is taken at 0x49410f), one shot per pose:
  - At pose (-47.8, 2.0, 48.5) the sea is present at Yon 35, 70 and 100 and missing at 200. At Yon 200 it comes back with YonFactor 0.5 and is gone at 0.75: the amount drawn decides, not the yon.
  - With the stock heap, trees are bare trunks at the same frames where the larger heap draws them whole.
  - 400 KB still loses faces in all four test views; 800 KB, 1600 KB and 3200 KB match 16 MB exactly. Frame rate didn't change.
- The same buffer and size also go to 0x4c6510 from 0x4b5ca0, so the game shared it with another user. That user keeps it.
- **Not a bug:** the sea is a ring about one original Yon wide, with nothing beyond it in the level file. At (60, 65) there's no geometry to the east at all, and that view is sky at every draw distance, including Original at 4:3.
- Rejected on the way: track-cell culling 0x40d7c0 walks the whole footprint with no cap, and 0x58f490 (default 1) is a developer toggle between cell slots +0 and +4.

### 2.4 Wrecks and the pile-ups

- A drone is 0x5d8 bytes (array [0x684504], count 0x6820d0). +4 type (+0x2c flags: 2 can reset, 4 always processed), +0x30 time it took its state, +0x38 state, +0x3c/+0x40 previous states, +0x44 processed, +0x45 in the near box, +0xec actor, +0xf8 physics body (H for the offsets, M for the flag names).
- State routines are at 0x594738 + state x 4, called with ecx = drone and edx = command (0 start, 1 run, 2 processing off, 4 stop): 1 Reset 0x44cc70, 2 ControlledMovement 0x44d1d0, 3 PhysicsActive 0x44eb70, 4 StationaryPassive 0x44ec50 (H). 5 is a drone below the world (0x451415).
- A hit runs MyDroneHathCollideth 0x452230 (state 3); coming to rest runs MyDroneHathHalteth 0x452360 (state 4). Both states stay until +0x44 is 0, unless the type has flag 4 and has left the near box (H). There is no timer and no test of the camera.
- PROCESSING OFF 0x451710 (ecx = drone, edx = force) clears +0x44. **Without force it skips a drone in state 3**, so a car still being knocked about never stops (H).
- Reset then runs SemiInitDroneSpec (0x44ce3b onwards): back to its start, with physics off. Processing starts again only outside the near box (section 2.2), so neither end is seen (H).
- Measured on New City 3 at Max, player parked: stopped and piled cars after 30 s, 45 s, 60 s: 28, 38, 39 of 77 as stock. actors.cpp wraps states 3 and 4: after 4 s with +0x44 set, flag 2 and not flag 4, a car not in the near box, or 15+ units off and outside the camera's field of view, gets 0x451710 with force. Then 9, 6, 5, and 3 by 72 s. "Moving" drops from about 40 to about 21 while parked, because cars at home inside the near box can't start until the player moves.

### 2.5 Traffic at twice the original, and the fade

- Following k (86 / 103 at Max, caps 64) was too much traffic: wreck clearing alone left pile-ups in view. The boxes are now fixed at twice the original at every setting but Original (near 30 / 24 / 30, far 36 / 26 / 36) with caps of 20 drawn and 24 moving. PCGamingWiki has no traffic tweak; 2x is the rule of its two tweaks, `Yon` 35 to 70 and pedestrians 12 / 9 to 24 / 18.
- Flying the camera along the race paths at driving pace on New City 3: 24 moving, at worst 7 stopped and 5 piled, then none. **A parked player sees no traffic at this size**, so parked tests measure nothing.
- trafficfade.cpp: opacity follows the car's margin inside the near box, measured from the test centre 0x6820c0 on each axis, so there are no timers and it works both ways. Five levels over the last 4 units. At 8 units a third of the drawn cars were see-through at any moment, right beside solid ones (the zone is 46% of the box); at 4 it is 11 to 13% of them, none nearer than 21 units. Opacity belongs to a material and cars of a type share theirs, so each level is a copy of the model (BrModelAllocate) with faces on copies of the materials (BrMaterialAllocate), extra_prim = {BLEND_B 1, OPACITY_X n}; a level is chosen by pointers in the actor.
- Layout (H, read from live data): a car's top actor (drone+0xec) has no model; the parts are its children, and **its `next` is the rest of the world**, so walk children only. Vertices and faces are 40 bytes, and **a face's material is at +8**. A null face material means the actor's (+0x1c): all of a wheel's faces and some of the body's, so the actor's material needs a level too. Models are shared by the drones of a type.
- A car that is hit takes a model from a pool for its type: the faces are shared, the vertices its own to dent, the slot marked at type+0x80, and that model has no faded copies. So cars in states 3 and 4 stay solid; otherwise the body stays solid while the wheels fade. Its materials are the originals, so a hit never leaves a car see-through (checked on live models). Reset puts the body back on its base model (0x44cf33, table 0x76236c, index type+0x78) whatever level it was at, so the level is set every frame.
- Built after InitDrones (call at 0x401144), freed by a detour of 0x44fc10, which nothing calls directly; models go back to the originals first. Five races in a row, on and off: same memory at the end (207.0 and 207.7 MB), same frame rate. New City has 25 models and 55 materials to copy; Quarry and Timber have no road cars.

### 3.5 What else the scaled values reach

Checked every read of gYon, gIn_view_distance, gYon_squared, the four pedestrian distances and the two drone counters, for a loop that depends on them to end (H):

- gIn_view_distance: only the loop above. 0x4a95cd and 0x4abcc1 are single comparisons.
- 0x4abcc1 sends a far cop back to its start only if that start is over gIn_view_distance from the player, so at 210 this rarely or never happens on the smaller tracks (behaviour, not a fault).
- Pedestrian distances: comparisons only, in UpdatePeds' fixed loop 0x4d380d..0x4d3925. More awake pedestrians can fill the AI pool, but 0x4d3aff takes the least recently used record when all 100 are in use, so it can't overrun.
- Drone counters: compared with the caps and counted up and down, and never used as indexes.
- gYon: the rest is cameras, fog and the menu.

## 4. Relation to yon, view width and fog

| Distance | Source | Scales with yon? | Uses FOV/aspect? |
|---|---|---|---|
| Ped activation 12 / 9 (x 0.6 vertical) | SETTINGS.TXT -> 0x694118 etc. | no (H) | no; world-axis box around the camera (H) |
| Ped draw cap 30 per form | FORMS\*.TXT | no | no |
| Drone draw 15/12/15, process 18/13/18 (cars) | constants 0x589d98.. | no (H) | no; box 5 units ahead of the camera (H) |
| Drone caps 12 processed / 10 drawn | immediates 0x44f7ee, 0x45152e, 0x451847 | no (H) | no |
| Opponent cull | gYon_squared (mult^2 x gYon) | **yes** (H) | no; half-space behind the camera only (H) |
| Car LOD 20 units | car file 8 / factor 50 | no (H) | no |
| Opponent in-view (cheat) distance | gYon + 10, min 45, at race start | **yes** (H) | no |
| Fog start / end | gYon x 10^(-start/10) / gYon x 10^(end/10) | **yes** (H) | no |

**Fog** (H for the maths, M for the race-file field mapping): race file `Depth cue mode` and `Degree of fog/darkness` (e.g. newcity1 `colour`, `4,0`) -> 0x75d760 (mode), 0x75d764 (start), 0x75d768 (end). 0x4451a0 writes material fog_min (+0x5c) = 10^(-0.1 x start) x gYon and fog_max (+0x60) = 10^(0.1 x end) x gYon (`fld 10.0` 0x589ca8, `fmul 0.1` 0x589cb0, pow 0x5769a0, `fmul [0x761f4c]` at 0x44520a/0x445230), sets material flag 0x80000 and the fog colour. 0x445340 applies this to the track, all car LODs, gibs and smears. It uses **gYon at the time it runs**, not the multiplied camera yon. With start 4: fog begins at 0.40 x gYon and is full at gYon. So the patch moved the fog out with the scenery. The fixed actor distances are now unfogged:
- vanilla Yon 35: fog 13.9-35. Drones popping at 15-20 units were 5-29% fogged; peds at 9-12 were not fogged.
- Yon 100: fog 39.8-100, so everything inside 40 units is clear.

**Hor+** (the patch widens the view): no actor test uses the camera's fov or aspect. A wider view only shows more of the same boxes (H). With world-aligned boxes, the side edges are as close as the front edge (12 for peds, 15 for drones), so pop-in at the screen edges becomes more visible with a wider view (M).

## 5. Recommendations

Make the distances one function of our DrawDistance setting, with Original = stock values. Let k = Yon/35: 1.43 for Far (50), 2.0 for Further (70), 2.86 for Max (100).

### 5.1 Drones (cheapest, biggest visible win)

Done, then taken back: scaling the boxes by k caused pile-ups, so traffic is now fixed at 2x with a fade (section 2.5). What follows is the original plan.

- Scale the CAR boxes by k: near 15/12/15 and far 18/13/18 (keep the 3-unit hysteresis, or scale it too). Planes and trains need little change, they are already 40-50.
- Do it by **redirecting the fcomp operands** to our own floats, not by writing the .rdata constants:
  - car near: 0x44ca9c/0x44cacc (x/z) and 0x44cab4 (y);
  - car far: 0x44cb8c/0x44cbbc and 0x44cba4.
  0x589d98..0x589db4 are only used there (H, byte scan), but **0x589da8 (50.0) is shared** with the "fell below the world" test at 0x451422. The operand form is cleaner and per-type.
- Scale the forward offset 5.0 (0x589e60, used only at 0x451c4d/55/5d) by k, or leave it.
- Raise the caps: processed 12 -> 24..32 (imm8 at **0x44f7ee** and **0x45152e**), drawn 10 -> 24..32 (imm8 at **0x451847**). Without this, a bigger box just hits the caps, and drones show up late (stay invisible) instead of early.
- Cost: each processed drone is a physics body with collision (0x4b63b0) plus path following. Tracks have tens of drones, not hundreds (limit 200). Expect a small CPU cost (L until measured).
- Gameplay: drones outside the far box are frozen. A bigger box means traffic moves over a wider area and you meet it in different places. More traffic in range also means more to crash into, including opponents (M).

### 5.2 Pedestrians

- Override the four globals after the loader instead of editing SETTINGS.TXT. Redirect the call at **0x47e28f** (like `options.cpp` does for 0x47de15) to a wrapper that calls 0x4cadc0, then writes 0x694118 = 12 x kp, 0x69bc1c = 0.6 x that, 0x6944bc = 9 x kp, 0x694128 = 0.6 x that. Keep the 0.75 ratio (min/max), which keeps the hysteresis band proportional.
- Suggested kp: Far 1.5 (18/13.5), Further 2 (24/18, the PCGamingWiki value), Max 2.5 (30/22.5). Full yon-proportional (34/26 at Max) is 8x the area; only try it after measuring.
- Cost: processed area ~ kp^2 (x4 at 24, x6.25 at 30). Each active ped runs AI (car-threat loop over cars within 5 units), animation, ground finding (0x4d1d70) and sound every frame. The 100-record AI pool and the **30-per-form draw cap** become the limit. If the counters (section 6) show the 30 cap being hit, raising it needs a form-loader change (the slot array is sized from the FORMS value; L) or a data value change.
- Gameplay: the number of peds on a track does not change (it is set by density at load), and kill and mission totals stay the same. More of them are awake: they walk, flee and panic-spread over a wider area, and opponents can hit them further from you (M). Peds are activated around the camera, not the car, so the effect follows the view.
- The "camera jump" path (0x694490) still activates every ped in the box at once after a recovery or teleport. That is fine.

### 5.3 Opponents

- Render culling already follows yon. Leave it, but check gYon_multiplier per track (section 6). If mult != 1, the squared-multiplier quirk cuts opponents at mult^2 x gYon. A fix would be to recompute 0x762100 as camera_yon^2 (patch the second `fmul [0x655ddc]` at 0x4941a5) (M).
- LOD: scale the HW row of 0x58f6b0 by k^2 (write 0x58f6c4..0x58f6d0 = 50, 5, 2.5, 1.5 x k^2) so the swap distance scales with yon: 20 -> 57 units at Max for level 0. Cost: full car models drawn further away. There are at most a handful of cars, so this is small (L until measured). Optionally also scale the spec-0 row (software).
- In-view distance: decide whether the 110-unit simulation radius is wanted:
  - keep it (more "real" racing, higher physics cost);
  - or clamp 0x691764 after InitOpponents to max(45, stock Yon + 10) while keeping it above the opponent cull distance. This needs a post-0x4add50 write. **Don't set it below the cull distance**, or dematerialised cars would vanish in view and rematerialised ones pop up in view (M).

### 5.4 Fog

Leave the fog as it is (it is what the track designer set, relative to yon). If pop-in stays visible after 5.1/5.2, an optional "close fog" could compute fog with stock Yon 35 in 0x4451a0, hiding the boxes as vanilla did. That trades back most of the draw-distance gain, so it is not recommended by default.

## 6. Run-time checks (hidden race, log once per second)

Add a temporary logger, e.g. in the main loop after 0x4930bd:
- **Peds:** number with flag 1 (`[0x744808]+i*0x54+8`), number with a model slot (`(signed char)[[P+0]+4] >= 0`), the largest |dx|,|dz| from 0x74d484 among active peds, the count per form against 30, and the AI pool usage.
- **Drones:** 0x6820d0 (total), 0x681fb4 (processed distance drones, cap 12), 0x684500 (drawn, cap 10), and per drone +0x44 (processed) / +0x45 (in near box) with the distance to 0x6820c0. Watch for frames where the cap blocks a drone that is in the box.
- **Opponents:** gYon 0x761f4c, gYon_multiplier 0x655ddc, gYon_squared 0x762100, gIn_view_distance 0x691764; per opponent (0x75d8a0 + i x 0x1a4): +0x18 distance to the player, +0xb0 flags (8 physics on, 0x20 cheating), actor render_style, LOD index car+0xe18, car+0xe1c thresholds.
- **Fog:** fog_min/fog_max of one track material, after load and after a Draw distance change.
- **Frame time:** before and after each change, on a busy track (New City, Downtown) with all opponents.
- To match pop-in to the code, log transitions with distances: ped flag 1 0->1, drone "START RENDERING" (0x4518ce) and LOD index changes. Each event should line up with one of the distances above.

## 7. Addresses

| Address | Name | Notes | Conf. |
|---|---|---|---|
| 0x47e28f | call LoadPedSettings | in InitialiseGraphics 0x47dd20, once at start-up | H |
| 0x4cadc0 | LoadPedSettings | SETTINGS.TXT; distance writes 0x4cae4e..0x4cae75 | H |
| 0x694118 / 0x69bc1c | gPed_max_dist / _y | 12 / 7.2 (0.6 x, double 0x58a990) | H |
| 0x6944bc / 0x694128 | gPed_min_pop_dist / _y | 9 / 5.4 | H |
| 0x694490 | gPed_camera_jumped | allows activation inside the min box; set by 0x4d6390 | H |
| 0x4d3740 | UpdatePeds | activation test 0x4d3811..0x4d3916; hides inactive 0x4d3d80 | H |
| 0x4083b0 / 0x408200 | AllocPedModelSlot | per-form "Max rendering at once" | M |
| 0x6944c0 | gPed_proc_pool | 100 x 0x12c | M |
| 0x44ed10 | LoadDroneTypes | DRONE.TXT + per-type txt; "always" = spec+0x2c bit 4 | H |
| 0x450bf0 | LoadInDronePaths | limit 200 drones | H |
| 0x44f700 | InitDrones (processing pass) | cap 12 at 0x44f7e8 | H |
| 0x4512f0 | ProcessDrones | per frame, from 0x40123b | H |
| 0x451bd0 | SetDroneTestCentre | camera pos + 5 x forward -> 0x6820c0 | H |
| 0x594760 / 0x594750 | drone near / far box tables | by type CAR, PLANE, TRAIN, CHOPPER | H |
| 0x44ca90 / 0x44cb80 | car near / far box | 15,12,15 / 18,13,18 | H |
| 0x44cae0 / 0x44cbd0 | plane, chopper boxes | 40,30,40 / 43,31,43 | H |
| 0x44cb30 / 0x44cc20 | train boxes | 50,20,50 / 43,31,43 | H |
| 0x589d98.. 0x589dbc | box floats | 0x589da8 also used at 0x451422 | H |
| 0x589e60 | drone centre offset 5.0 | | H |
| 0x681fb4 | gDrones_processed | cap 12 (imm 0x44f7ee, 0x45152e) | H |
| 0x684500 | gDrones_rendered | cap 10 (imm 0x451847) | H |
| 0x451710 / 0x4516d0 / 0x451810 | processing off / stop / start rendering | | H |
| 0x684518 | gDrones_off | Graphics "Traffic" (0x44ecf0 / 0x44ed00) | H |
| 0x41e660 | MungeCarGraphics | opponent cull loop 0x41e942..0x41e9a7 | H |
| 0x4e5ce0 | PointOutOfSight | (pos in ecx, dist^2): beyond or behind camera | H |
| 0x762100 | gYon_squared | (mult x camera yon)^2, 0x4940e0 | H |
| 0x41f110 | SwitchCarLODs | per frame from 0x493a58 | H |
| 0x58f6b0 | gCar_simplification_factor | [spec][level], spec 1 at 0x58f6c4 | H |
| 0x6793d8 | gCar_simplification_level | 0x448f20 / 0x448f10 | H |
| 0x4add50 | InitOpponents | in-view = gYon + 10, min 45 | H |
| 0x691764 | gIn_view_distance | | H |
| 0x4a9550 | CalcOpponentDistance | +0x18 to player, +0x68 time stamp | H |
| 0x4a9e10 | DoOpponents | from 0x401346 | H |
| 0x4aa570 | ProcessThisOpponent (cheat start/stop) | 3000 ms rule | H |
| 0x4af960 | ProcessFollowPath | dematerialise / rematerialise | M |
| 0x4a7960 / 0x4a79a0 | TurnOpponentPhysicsOn / Off | render 0 / 1 | H |
| 0x4a87a0 | RematerialiseOpponent | | M |
| 0x4a7a80 | RebuildActiveCarList | opponent flag 8 = physics on | M |
| 0x4451a0 | SetMaterialDepthCue | fog_min/max = gYon x 10^(-/+0.1 x degree) | H |
| 0x445340 | ApplyDepthEffect | track, car LODs, gibs | M |
| 0x75d760 / 0x75d764 / 0x75d768 | depth-cue mode / start / end | from race file | M |
| 0x655ddc | gYon_multiplier | per track, 0x506b75 | H |
