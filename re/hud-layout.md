# In-race 2D layout for a wide back buffer (CARMA2_HW.EXE, Steam build)

Static analysis, 2026-11-02. Tags: **H** = read from the bytes (capstone/pefile on the exe, values dumped from the image), **M** = decompiler output or a strong inference, **L** = to confirm in play. Goal: back buffer W x 480 (W = 854 for 16:9, ~1146 for 21:9, ~1706 for 32:9), keep the 640x480 logical layout, make the 3D view fill W and anchor each HUD element to its nearest edge.

Notation used below: **X = W - 640** (right-anchored shift), **C = X / 2** (centred shift; W even gives an integer). Section 4.4 generalises this with a capped "HUD safe width".

## 0. The short version

- **The render window does not come from the layout entry.** `ComputeRenderWindow` 0x47dc30 sets right/bottom (0x75bbdc/0x75bbe0) from **gGraf_specs[idx].total_width/height** (0x662214/0x662218 + idx*0x34, i.e. 0x662248/0x66224c for idx 1), left/top = 0 because HW forces 0x74cf8c = 1 (H). So the 3D view fills W once **0x662248 = W**. Because 0x47b3e0 matches the layout entry against that same width, also set **gGraf_data[1].width 0x656478 = W**, and **0x662264 (phys width) = W** so 0x4e4980 keeps gScreen base_x at 0 (H).
- **The layout entry holds almost no in-race HUD positions.** Of its 328 ints only 25 functions read anything, and the in-race readers are the width (+0), the action-replay panel, the full-screen map legend, the opponent icons and a few pitches/margins (section 1). Most fields that differ between 320 and 640 are never read (C1 leftovers).
- The in-race HUD lives elsewhere: (a) a **HUD quad table at 0x655e64..0x655f38** just before gGraf_data (w,h,x,y per textured panel), read once by Init2DHUD 0x47e720 and InitHeadups 0x449090; (b) per-frame **actor translations** (float immediates) in 0x44b6a0 for the reduced-detail and network layouts; (c) the **HEADUP.TXT text slots** copied into the player car at 0x75c354; (d) **code constants** (timer x 0x185/0x27a, compact-mode text x 0x14f, poly-font pass 0x280, overlays 0x280, map copy, picking centre 0x140).
- **Gauges and damage icons need no change.** The car file's speedo/gears/damage x,y are drawn **into the HUD panel textures** (0x44aad0 → 0x74ca1c = gStatbarHUD1 texture; 0x44a9d0 → 0x74cf00 = gStatbarHUD5 texture), so they follow their panel (H for the destinations).
- WIKI_LOG's observation "at 1706 the parallel-camera HUD quads are squeezed while blits aren't" is explained: the HUD camera is W wide (0x47e560) but renders into gRender_screen (640 wide today) and the poly-font pass forces the back screen to 640 (0x4651b0). With the render window at W and 0x4651b4 = W both become 1:1 (M, consistent with the observation).

## 1. Layout entry gGraf_data[1] (0x656478 .. 0x656997) — every field the code reads

Read sites found by tracking every load of gCurrent_graf_data 0x74b774 (72 reads, 25 functions, all matched to xrefs.tsv) and the index users 0x74b770/0x74d630. Values are the 640 entry (320 entry in brackets where useful). H for offsets, values and read sites; M for meanings.

| Offset | 640 value | Element | Kind | Read by | Adjust |
|---|---|---|---|---|---|
| +0x000 | 640 | screen width | width | 0x47b3e0 (match), 0x44a600 (headup centred at width/2), 0x43e0a0/0x43e3f0 (mouse crosshair line lengths), 0x4e6280 (replay text centred at width/2), QuickTime capture 0x4e0fe0/0x4e1250/0x4e1400/0x4e1c70/0x4e1da0 | **set W** (must equal 0x662248) |
| +0x004 | 480 | screen height | height | same functions | leave |
| +0x010 | 8 | rolling-letter x pitch | size | 0x483dd0, 0x483e20, 0x484000 | leave |
| +0x024 | 24 | rolling-letter speed/pitch | size | 0x483cf0, 0x484000 | leave |
| +0x104 / +0x108 | 0 / 0 [15/10] | cockpit margin x / y | offset | 0x44b170, LoadCar 0x488f70 (0x489d86..0x489f12), RenderAFrame 0x4e5078/0x4e5090 | leave |
| +0x290 | 8 | opponent-status column x | **x, left** | 0x4dad00 | leave (L) |
| +0x298 / +0x29c | 96 / 43 | column first y / y step | y | 0x4dad00 | leave |
| +0x294 / +0x2a0 | 38 / 10 | text offset x / y inside an opponent icon | rel. | 0x4daff0 | leave |
| +0x2a4 / +0x2a8 | 8 / 350 | opponent row (bottom ticker) start x / y | **x, left** / y | 0x4dad00 | leave (L) |
| +0x2b4 / +0x2ac / +0x2b0 | 4 / 346 / 394 | ticker box left x / top / bottom (right = end of icons, dynamic) | **x, left** / y | 0x4dad00 (0x47cbd0) | leave |
| +0x2b8 | 84 | ticker icon gap | size | 0x4dad00 | leave |
| +0x2f8 / +0x2fc | 10 / 12 | action-replay flashing icon x / y | **x, left** | 0x4e6280 (0x4e6890, 0x47bc40) | leave |
| +0x300 / +0x304 | 430 / 350 | action-replay panel x / y (words) | **x, right** | 0x4e6280 | **+X** |
| +0x308 / +0x30c | 430 / 350 | copy of panel x / y | x | not read | +X (tidy) |
| +0x310..+0x34c | 35..165 | replay button offsets (x,y pairs) relative to the panel | rel. | 0x4e6280 | leave |
| +0x350 / +0x354 | 445 / 461 | replay camera-name text x / y | **x, right** | 0x4e6280 (0x465a70) | **+X** |
| +0x358 / +0x35c | 51 / 57 | "recording" button offset in panel | rel. | 0x4e6280 | leave |
| +0x3e8 / +0x3ec | 6 / 7 | frame margin around the map-mode 3D window | size | 0x442f90, 0x496be0 | leave |
| +0x420 | 12 | font metric divisor (poly fonts) | size | 0x4973b0 | leave |
| +0x4b4 | 200 | network chat entry y | y | 0x444c00 | leave |
| +0x4d8 / +0x4dc | 32 / 438 | full map legend line 1 x / y | **x, centred with map** | 0x494900 | **+C** |
| +0x4e0 / +0x4e4 | 32 / 454 | legend line 2 | x | 0x494900 | **+C** |
| +0x4e8 / +0x4ec | 290 / 454 | legend line 3 (single-player only) | x | 0x494900 | **+C** |
| +0x4f0 / +0x4f4 | 108 / 438 | selected-object name | x | 0x494900 | **+C** |
| +0x4f8 / +0x4fc | 236 / 438 | selected-object icon actor translation | x | 0x494900 (0x532a90) | **+C** |
| +0x500 / +0x504 | 236 / 454 | object value text | x | 0x494900 | **+C** |
| +0x508 / +0x50c | 108 / 458 | object bar | x | 0x494900 | **+C** |
| +0x510 / +0x514 | 352 / 454 | object description | x | 0x494900 | **+C** |
| +0x518 | 24 | minimap scale | size | 0x4950b0 (0x495674) | leave |
| +0x51c | 7 | minimap marker radius | size | 0x4950b0, 0x496e30 | leave |

Everything else (e.g. +0x10c/+0x110 = 704/528, +0x198/+0x19c, +0x2e8 = 636, +0x3b8..+0x468 including 576/636/624/618) is **never read** (H for "no load from 0x74b774 + offset"; M that no code reaches it through another pointer).

Per-resolution arrays outside the entry, indexed by 0x74b770 (H): action-replay progress bar x start **0x65fbe4 = 443**, x end **0x65fbec = 612**, y 0x65fbf4 = 446 (0x4e6280); replay button hit rectangles **0x65fcb0 + i*16, i = 0..8** (l,t,r,b: 519..619 x 372..442) used by the replay mouse handler 0x4e69b0. All right-anchored: **+X** on each x start/end and each rect's l and r.

## 2. Render window and every other 640 assumption in race 2D

### 2.1 Render window (H)

- 0x47dc30 ComputeRenderWindow: cockpit branch dead in C2; otherwise right = spec total_width, bottom = spec total_height; left = top = 0 (0x74cf8c forced 1 on HW). Called by AdjustRenderScreenSize 0x4e4b40, which writes gRender_screen base/size, origin = size/2, 0x703e24/0x703e20, and resizes the two full-window tint overlays 0x655e48/0x655e4c (0x4d8bb0 at 0x4e4c68/0x4e4c93) to the window.
- So the single change is **gGraf_specs[1].total_width 0x662248 = W** (+ 0x656478 = W for the match, + 0x662264 = W for the front-screen base, 0x662260 row = W for tidiness). Order: LoadOptions 0x47de15, CalcGrafDataIndex 0x47de35, then SetBRenderScreenAndBuffers; write the three values before 0x47de35 (or set the spec right after it and also write gCurrent_graf_data->width).
- Side effects of the spec width (all fine): mouse clamp 0x483c10 → W; message screen 0x4a0080 box W/10..9W/10; border fills in 0x4e4e40 (only with gRender_indent); **track load 0x504bf0 rescales the full-map transform** (see 2.4).

### 2.2 Cameras and passes

| Site | What | Change |
|---|---|---|
| HUD_Camera 0x74cf74 (0x47e560) | parallel, width/height = gBack_screen at creation = W x 480 (H) | none; renders quads and glyphs 1:1 once its target is W wide |
| 0x4e5b00 RenderHUDModels | HUD root 0x74ca00 through HUD_Camera into **gRender_screen** with origin/base zeroed (M) | none once the render window is W |
| **0x4651b0** DrawPolyFontText immediate pass | forces gBack_screen width to 0x280 (imm16 at **0x4651b4**), height 0x1e0 (0x4651c0), then BrZbSceneRender with HUD_Camera (H) | **imm16 0x4651b4 = W**. Leaving 640 squeezes glyph x by 640/W (M) |
| 2D camera 0x74cac4 (0x47e720) | width/height = gScreen size = W x 480 (H); used by 0x47cad0 dim rect, 0x44bac0 icon, 0x45aa60 box, onto gBack_screen | none |
| tinted_poly_camera 0x6a0430 (0x4d7040) | gScreen size; overlays rendered onto gBack_screen by 0x4d8290 | none |
| Overlays created 640x480 at (0,0) | 0x47dfec/0x47e005 (imm32 at **0x47dfed**, **0x47e006**) → 0x655e48/0x655e4c, resized by 0x4e4b40 later; **0x4e57a8** (imm32 at **0x4e57a9**) → 0x65fb54, network modes 6/7, never resized (H) | set all three to W (the last one is required) |

### 2.3 Text centring and messages

- 0x44a600 (generic centred headup, many callers) centres at graf width / 2 (H) → follows +0 = W.
- 0x4e6280 replay messages centred at graf width / 2 (H) → follows.
- DoHeadups 0x449b10 case 5 (wrapped text) spans gRender_screen width/10..9/10 (H) → follows the render window.
- HEADUP.TXT slots at x 320 'ch' are absolute → anchored by the slot hook (3.2).

### 2.4 Full-screen map (MapMode 2)

- **0x496be0**: copies the 640x480 map image to gBack_screen at (-origin_x, -origin_y) = (0,0) (origin zeroed first), size 0x280 x 0x1e0 (pushes 0x496cf4/0x496cf9) (H). Keep the size 640. Centre it: replace `mov dx,[ecx+0x38]` at **0x496d13** (66 8B 51 38) with `mov dx, C` (66 BA lo hi) and `neg dx` at **0x496d1b** (66 F7 DA) with three NOPs (H bytes). Then the columns 0..C and C+640..W show stale frame pixels: a **hook must fill them black** (BrPixelmapRectangleFill 0x538640 on gBack_screen) in map mode (M).
- **0x504bf0** (track load): when the spec is not 640x480 it does x' = (x-320)*W/640 + W/2, y' = (y-240)*480/480 + 240 on the map transform at track+0xed4 (H: 0x5060fb..0x5061ef; floats 0x58b1b8 = 320, 0x58b1bc = 240, 0x58b1c0 = 1/480, 0x58b1c4 = 1/640). That would stretch the dots horizontally. Make it a pure +C shift: at **0x506171** change the fild displacement 0x662214 → 0x662218 (byte 0x14 → 0x18) and at **0x506179** change the fmul address 0x58b1c4 → 0x58b1c0 (byte 0xc4 → 0xc0). x scale becomes 480/480 = 1, so x' = x + C (H for bytes, M for the matrix-call argument order).
- Map-mode 3D window: MapRender x is forced to 80.0 after every OPTIONS load (**0x48e9a7**, float imm32 at **0x48e9ad**) → **80 + C** as a float. 0x4948b0 then derives 0x74abc0 (int, &~3) from it. The drag clamp in 0x442f90 uses back-screen width (dynamic) and graf +0x3e8/+0x3ec (leave).
- Legend fields: section 1 (+C).

### 2.5 Minimap (MapMode 1)

- HeadupMapX/Y/W/H 0x74abac/0x74abb0/0x74aba8/0x74abb4 are absolute back-screen pixels, read by LoadOptions 0x48d8f0 (called at 0x47de15) and copied to floats 0x74abdc/0x74abd8 by InitMiniMap 0x496e30 (first called at 0x47dfa8); default OPTIONS has 556,416,56,54 (bottom-right) (H).
- Shift+arrows 0x497620 moves the float copies at 0.05 px/ms, clamps x to [0, backW - w - 8] and y to [0, backH - h] (dynamic, H), then calls InitMiniMap and **SaveOptions 0x48d190 on every step** (H).
- Anchor: after 0x48d8f0, if HeadupMapX + w/2 > 320 add X (right half), else leave. Wrap SaveOptions (all 30+ callers go through 0x48d190): subtract the same offset from 0x74abac for the write and restore it, so OPTIONS.TXT stays in 640 space and does not drift (M). Moving the map into the other half would change its anchor on the next load; acceptable.

### 2.6 Picking and mouse

- **0x413860** ScreenPointToWorldRay (camera type 3, called from the replay mouse handler 0x4e69b0): x centre `add ecx, -0x140` at **0x413932** (imm32 at **0x413934**) → **-(W/2)**. y centre 0xf0 (0x413896) and the 1/240 scale (0x5895a8) stay, since the ray uses the vertical half-FOV for both axes (H code, M maths).
- Mouse clamp 0x483c10 follows the spec width. Mouse coordinates are ScreenToClient pixels; whether dgVoodoo's window maps 1:1 to W x 480 is **L** (re/frontend-centre.md covers the mouse).
- -SCALEMOUSE double-click path (0x51b59b, double 1/640 at 0x58b748) scales by desktop/640: would need 1/W; only with -SCALEMOUSE and not D3D (L, low value).

### 2.7 Remaining `0x140`/`0x280` immediates (full scan of 0x401000..0x527000, H)

| Site | Verdict |
|---|---|
| 0x4651b0, 0x47dfec, 0x47e005, 0x4e57a8, 0x413932, 0x506171/0x506179, 0x496d13 | change (above) |
| 0x496cf9 push 0x280 (map image width) | keep 640 |
| 0x5060fb cmp spec width, 0x280 | keep (the branch becomes a pure shift) |
| 0x43e770 (Smacker 320x200), 0x46cf4c/0x46f7f6 (640.0f menu cameras), 0x46dc74 (menu preview origin 320), 0x46f630 | front end: see re/frontend-centre.md |
| 0x40a730, 0x454f40, 0x46b820 (stack frames), 0x485fe0, 0x4d3520, 0x519a40, 0x4e1277 (320x200 banner check) | not screen x |

## 3. Elements positioned outside the layout entry

### 3.1 HUD quad table 0x655e64..0x655f38 (w, h, x, y ints; H values)

Read only by Init2DHUD 0x47e720 (call 0x47dfd4) and, for the two timer quads, InitHeadups 0x449090 (call 0x47e09a); both run once in InitialiseDeathRace after the spec is set. The y of four quads come from BSS 0x68b8f0..0x68b904 (zero; never written, H). Patch the x values in memory **before 0x47dfd4** (any time after W is known). Actors and which ones are drawn come from 0x44b6a0 (H).

| x address | Quad (actor) | x .. x+w (640) | y | Drawn | Anchor |
|---|---|---|---|---|---|
| 0x68b8f0 (=0) | gStatbarHUD1 (0x74ca7c), texture 0x74ca1c: speedo/gears/APO art | 0..128 | 0 | detail%3 ≥ 1 | L (leave) |
| 0x655eb4 | gStatbarHUD3 (0x74ca14) | 128..270 | 10 | detail%3 = 2 | L (leave) |
| 0x655edc | gTimerLeftHUD (0x74ca40) | 270..334 | 0 | detail%3 ≥ 1 | **+C** |
| 0x655ee8 | gTimerRightHUD (0x74ca24) | 334..398 | 0 | detail%3 ≥ 1 | **+C** |
| 0x655ef4 | gStatbarRightHUD (0x74ca58) | 398..576 | 10 | detail%3 = 2 | **+X** |
| 0x655ed0 | gStatbarHUD5 (0x74cf18), texture 0x74cf00: damage icons | 576..640 | 0 | detail%3 ≥ 1 | **+X** |
| 0x655f14 | gArmour (0x74cf80) | 149..277 | 49 | detail%3 = 2, net | L (leave) |
| 0x655f24 | gPower (0x74ca98) | 298..426 | 49 | same | **+C** |
| 0x655f34 | gOffence (0x74ca84) | 448..576 | 49 | same | **+X** |
| 0x655e6c | gHeadup (0x74cef4) = INTRFACE\BOTHUP.DAT model, vertices x ±263.7, y ±20.5; translated to x = 0x655e6c+10+0x655e64 = 270, y = -454 | 6..534 | 434..475 | detail ≥ 3 (default 5) | **+C** (M) |
| 0x655ec4 | gHUDsquare (0x74cef0) | 384..512 | 0 | not queued by 0x44b6a0; only material code 0x445340/0x4e9c40 | +X (L) |
| 0x655e7c, 0x655e8c | gHeadup2, gPowerupHUD | 292.., 10.. | | never queued (dead) | leave |
| 0x655f04/0x655f08 | gMapHUD x,y | written by 0x496e30 from HeadupMapX | | actor never queued | follows 2.5 |

### 3.2 Per-frame translations in 0x44b6a0 (float imm32 of `push`, H)

These are offsets from the quad's base x, so they must absorb the difference between anchors:

| imm32 at | 640 value | Meaning | New value |
|---|---|---|---|
| 0x44b885 | +142.0 | statbar1 joins the timer (compact, detail%3 = 1) | 142 + C |
| 0x44b89f | -178.0 | statbar5 joins the timer (compact) | -178 - X + C = -178 - C |
| 0x44b754 | -448.0 | statbar5 next to statbar1 (network) | -448 - X |
| 0x44b79a | +48.0 (y 35 at 0x44b792) | armour stacked (network) | leave |
| 0x44b7b7 | -102.0 (y 5) | power stacked (network) | -102 - C |
| 0x44b7d5 | -252.0 (y -25) | offence stacked (network) | -252 - X |
| 0x44b95b | `mov edx, 0x14f` (int 335) | compact-mode centred poly text x | 335 + C |

Full-detail mode resets statbar1/5 translations to 0 (0x44b921..0x44b948), so the table offsets alone apply there.

### 3.3 HEADUP.TXT text slots (data\64x48x8\HEADUP.TXT)

- Loader 0x48ba60 (fastcall: edx = view, stack = car) is called twice by LoadCar (0x489d5e, 0x489d6b). Slot i of view v sits at car + 0x728 + v*0x370 + i*0x2c; for the player car (0x75bc2c) that is **0x75c354 + (v*20 + i)*0x2c** (H). Fields: +0 x, +4 y, +8 font, +0xc vertical-centre flag, +0x10 has-clear-rect, **+0x14 clear left**, +0x18 top, **+0x1c clear right**, +0x20 bottom, +0x24 justification (0 l, 1 r, 2 c), +0x28 first column (H for the parse).
- Slot creation (0x44a3a0, 0x449fd0) copies x into gHeadups 0x67c500 (stride 0x164: +4 drawn x after justification, +8 y, +0xc anchor x) every time a message appears (H). So adjusting the car layout once per load covers every message.
- Hook: wrap 0x48ba60 and, after it returns, add the anchor shift to x, clear-left and clear-right of slots 0..count-1 of that view. C2 only uses view 0 (cockpit_on never set), adjust both.

| Slot | Content | x (ext / int) | Anchor |
|---|---|---|---|
| 0 | development info | 20 | L |
| 1 | credits | 130 | L |
| 2 | pedestrian kill count (on gStatbarRight) | 416 | **R** |
| 3 | pedestrian warning (cc) | 520 | R |
| 4 | misc messages | 320 | C |
| 5 | countdown | 320 | C |
| 6 | fancy bonuses / race over | 320 | C |
| 7 | timer (rh) | 389 / 377 | C (overridden, below) |
| 8 | lap count | 130 | L |
| 9, 10, 11 | end bonus, time bonus, time awarded | 320 | C |
| 12 | cars out | 416 | **R** |
| 13 | network cash | 430 / 632 | R |
| 14 | network cash | 430 / 320 | R / C |

A pure "nearest third" rule would put slots 2 and 12 (x 416) in the centre; they sit on the right panel, so use the table (M).

- Timer override in DoHeadups 0x449b10 every frame for slot 0x79eaec (H): detail 0/3 → x 0x27a (634) y 2 (imm32 at **0x449c19** and **0x449c28**) → **634 + X**; other details → x 0x185 (389) y 13 (imm32 at **0x449bd6** and **0x449be5**) → **389 + C**.

### 3.4 Car file (data\CARS\64x48x8\eagle3.txt)

- Speedo, tacho, gears, hands, damage icons, DAMASKALPHA: drawn into HUD textures (0x44aad0 → 0x74ca1c; 0x44a9d0 → 0x74cf00 via 0x47ba80 with ecx = texture, H), so texture-relative: **leave**.
- Dimmed areas (count 0) via 0x4496f0, mirror and cockpit rects: cockpit-only or empty in C2: leave.
- Pratcam 0,0,52,46: "relative to screen" per the file comment; not traced (L, probably a texture too).

### 3.5 Other in-race drawers

- Opponent icons 0x4dad00/0x4daff0: left column and bottom-left ticker (section 1): leave.
- 0x45aa60 "Acc Poly" box: callers pass x = 8 (0x45a711 push 8.0), left: leave (M).
- Network chat 0x444c00: box from 15% to 85% of back width (follows W), text at a fixed x (push 0x64 at 0x444cb7): on very wide buffers the text may start left of the box (L; low priority).
- 0x47cad0 dim rectangle, 0x44bac0 icon: callers' coordinates, not traced (L).

## 4. Recipe

### 4.1 Once, before InitialiseDeathRace builds the HUD (e.g. at process start, W known)

| Address | Size | Value |
|---|---|---|
| 0x662248 (spec[1] total width) | int | W |
| 0x662264 (spec[1] phys width), 0x662260 (row) | int | W |
| 0x656478 (gGraf_data[1] width) | int | W |
| 0x656778 (+0x300 replay panel x, word read) | int | 430 + X |
| 0x656780 (+0x308) | int | 430 + X |
| 0x6567c8 (+0x350 replay text x) | int | 445 + X |
| 0x656950, 0x656958, 0x656960, 0x656968, 0x656970, 0x656978, 0x656980, 0x656988 (+0x4d8, +0x4e0, +0x4e8, +0x4f0, +0x4f8, +0x500, +0x508, +0x510) | int | + C each |
| 0x65fbe4, 0x65fbec (replay bar x start/end, res 1) | int | + X |
| 0x65fcb0 + i*16 (+0 and +8), i = 0..8 (replay hit rects) | int | + X |
| 0x655edc, 0x655ee8 (timer quads), 0x655f24 (power), 0x655e6c (BOTHUP) | int | + C |
| 0x655ef4 (statbar right), 0x655ed0 (statbar5), 0x655f34 (offence), 0x655ec4 (HUD square) | int | + X |
| 0x4651b4 | imm16 | W |
| 0x47dfed, 0x47e006, 0x4e57a9 | imm32 | W |
| 0x449bd6, 0x449be5 | imm32 | 389 + C |
| 0x449c19, 0x449c28 | imm32 | 634 + X |
| 0x44b885 / 0x44b89f / 0x44b754 / 0x44b7b7 / 0x44b7d5 | float imm32 | 142+C / -178-C / -448-X / -102-C / -252-X |
| 0x44b95b | imm32 | 335 + C |
| 0x413934 | imm32 | -(W/2) |
| 0x48e9ad | float imm32 | 80 + C |
| 0x506171 | byte | 0x18 (was 0x14) |
| 0x506179 | byte | 0xc0 (was 0xc4) |
| 0x496d13 | 4 bytes | 66 BA (C as imm16) |
| 0x496d1b | 3 bytes | 90 90 90 |

The layout-entry rows equal "gCurrent_graf_data + offset" after 0x47b3e0; writing them statically is the same thing because the index is constant 1 (H). Code bytes are in .text: VirtualProtect first.

### 4.2 Hooks (data not available statically)

1. **HEADUP slots**: wrap 0x48ba60; after it returns, shift slots per the 3.3 table (x, clear-left, clear-right).
2. **Minimap**: after LoadOptions 0x48d8f0 (call 0x47de15) shift HeadupMapX 0x74abac by X if its centre is right of 320; wrap SaveOptions 0x48d190 to write the 640-space value.
3. **Map mode side bars**: in 0x496be0 (MapMode 2) fill [0, C) and [C+640, W) of gBack_screen with black before the map copy.
4. Optional: nothing re-reads the static tables later, so no per-race re-application is needed except hook 1 (each LoadCar).

### 4.3 Must stay 640 / unchanged (H for the reads)

- gGraf_data[0] (320 entry), gGraf_specs[0], every y and height, graf +0x10/+0x24/+0x104/+0x108/+0x294/+0x2a0/+0x2b8/+0x3e8/+0x3ec/+0x420/+0x518/+0x51c and the replay button offsets +0x310..+0x35c.
- The map image copy size (0x496cf9) and the y half of the map transform.
- Car-file positions and the HUD textures (they are texture space).
- Arrays indexed by x: none found in race code. Per-resolution arrays (0x65fbe0.., 0x65fc20..) are indexed by the graf index, not by x.

### 4.4 Ultrawide: one formula for any W

Use a configurable HUD safe width **A = min(W, HudMaxWidth)** (A ≥ 640) and its left edge **xo = (W - A)/2**:
- left-anchored: **+xo**; right-anchored: **+xo + (A - 640)**; centred: **+(W - 640)/2** (= xo + (A-640)/2).
- Relative translations in 3.2 become: statbar1 compact 142 + (C - L), statbar5 compact -178 - (R - C), statbar5 net -448 - (R - L), power net -102 - (C - L), offence net -252 - (R - L), where L, C, R are the three shifts above.
- With A = W this is the X / C recipe. Check at 854: top banner becomes [0..270] [377..505] [612..854] with 107-px gaps and no overlap; compact mode [249..505] contiguous; network stack x 196 unchanged (M arithmetic).

**Better centred on very wide screens (32:9, W ≈ 1706)**, i.e. set A to about 854..1146 there:
- the top banner (statbars, timer, APO bars, credits/peds/laps/cars-out text): edge anchoring spreads it over 1706 px with 533-px gaps;
- the action-replay panel and bar (right edge would be far from the centred replay text);
- the bottom opponent ticker (left at x 4..) and the bottom BOTHUP bar;
- the minimap if the player wants it near the action.
Keep at the true edges: nothing is required to; the opponent column and powerup column could stay at xo.

### 4.5 What may break as W grows (risks)

- **Track culling 0x40d7c0** uses aspect x tan(fov/2) x yon x YonFactor for the cell footprint (camera-hud.md); with aspect W/480 the footprint widens, so more cells render (cost grows roughly with aspect). Whether its wedge test stays correct for aspect > ~2.4 (horizontal FOV > ~103 deg) is **L**: look for pop-in at the screen sides at 21:9 and 32:9.
- **Hor+ at 32:9**: horizontal FOV = 2 atan(tan(27.8 deg) x 3.55) ≈ 124 deg (16:9 ≈ 86, 21:9 ≈ 103). Expect edge stretching; the sky model 0x445e20 (built from fov and aspect, rebuilt only on fov/yon change) may not cover the sides (L). Consider an option to cap horizontal FOV (lower vertical FOV) beyond 21:9.
- **Poly-font pass** if 0x4651b4 is not patched: glyph x scaled by 640/W (the squeeze already seen at 1706).
- **Overlay 0x65fb54** (network modes 6/7) stays 640 wide unless 0x4e57a9 is patched.
- **Map mode**: stale side bars without hook 3; dots stretched without the 0x506171/0x506179 patch.
- **Minimap drift** if SaveOptions writes the shifted x.
- **Mouse**: action-replay buttons and picking depend on client coordinates being W x 480 (L, dgVoodoo scaling).
- **Message screen 0x4a0080** and **network chat** boxes widen with W (percent of width); text placement may look odd at 32:9 (L, cosmetic).
- **QuickTime capture** (0x4e0fe0 family, debug only) allocates (W/2)x240x2 and records W/2-wide frames (L, harmless).
- **Front-end/pause menus** (FE_MainLoop 0x46d8e0, in-race pause = mode 1 Options) are separate: 640.0 parallel cameras stretch backdrops to W. See re/frontend-centre.md.
- HUD actor list cap 0x80 (0x4e5ad0) and glyph cap 0x100 (0x464e40) do not depend on W.

## names.tsv

```
0x0047dc30	ComputeRenderWindow	render rect from gGraf_specs total w/h; HW forces full window
0x0047b3e0	CalcGrafDataIndex	matches gGraf_data[i].width/height to spec total
0x0047e720	Init2DHUD	HUD quad models from table 0x655e64; g2d_camera
0x00449090	InitHeadups	SCROLLERS.TXT, re-places timer quads from 0x655ed4..
0x0044b6a0	QueueHUDQuads	per-frame panel translations by detail level / net mode
0x004e5ad0	QueueHUDActor	adds actor to 0x704e60 list (max 0x80)
0x004e5b00	RenderHUDModels	HUD root via HUD_Camera into gRender_screen
0x00464e40	DrawPolyFontText	glyph actors; immediate pass forces back width 0x280 at 0x4651b0
0x00465a70	PolyTextLeft	fastcall(pm, x, y, font, text)
0x00465f10	PolyTextCentred	fastcall(pm, xcentre, y, font, text)
0x0048ba60	LoadHeadupLayout	HEADUP.TXT view into car+0x728+view*0x370, slot 0x2c
0x0044a3a0	NewHeadupSlotFromLayout	copies layout slot into gHeadups
0x00449fd0	NewTextHeadupSlot	text headup from layout slot
0x0044a380	NewTextHeadupSlotDefault	0x449fd0(..., 1)
0x0044a600	NewCentredHeadup	centred at graf width/2
0x00449b10	DoHeadups	timer x override 0x185/0x27a
0x0044aad0	DrawGaugesToTexture	speedo/tacho/gears into 0x74ca1c
0x0044a9d0	DrawDamageIcons	damage icons into 0x74cf00
0x00449830	DrawAPOBars	armour/power/offence into textures
0x004496f0	DrawDimmedAreas	car-file dim rects (none in C2)
0x004dad00	DrawOpponentStatus	column x graf+0x290, ticker x graf+0x2a4
0x004daff0	DrawOpponentIcon	
0x004e6280	DrawActionReplayHUD	panel graf+0x300, bar 0x65fbe0, text graf+0x350
0x004e69b0	ActionReplayInput	button hit rects 0x65fc20+idx*0x90, picking
0x00413860	ScreenPointToWorldRay	x centre imm -0x140 at 0x413932
0x00494900	DrawFullMapLegend	graf+0x4d8..0x514
0x00496be0	DrawFullMap	640x480 map copy at origin, imm 0x496cf4/0x496cf9
0x004948b0	SetMapRenderRect	forces 128x80
0x00442f90	MoveMapRenderWindow	clamps with graf+0x3e8/0x3ec and back width
0x00504bf0	LoadTrack	map transform rescale when spec != 640x480 (0x5060fb)
0x00444c00	DrawNetChatEntry	box 15..85% of width, y graf+0x4b4
0x004d70c0	NewScreenOverlay	fastcall(x, y, w, h, type, ...)
0x004d8bb0	SetOverlayRect	(idx, x, y, w, h)
0x004d8290	RenderOverlays	tinted_poly_camera onto back screen
0x0047cad0	DimRectangle2D	gDim quad via g2d_camera
0x0044bac0	Draw2DIcon	icon actor 0x74ab84 via g2d_camera
0x0045aa60	AddAccPolyBox	box via g2d_camera
0x0047bc40	BlitMaskedAt	fastcall(x, y, pixelmap) -> 0x47ba80
0x004e0fe0	QTCaptureBegin	QuickTime screen capture (debug)
0x004e1da0	QTCaptureUpdate	state machine, called from RenderAFrame
0x00483cf0	AddRollingLetter	uses graf+0x24
0x00655e64	gHUD_quad_layout	w,h,x,y per HUD quad (640 space)
0x0068b8f0	gHUD_quad_dyn_xy	BSS x/y for statbar1, y for HUDsquare/statbar5/timers
0x00655e48	gOverlay_window_1	resized to render window by 0x4e4b40
0x00655e4c	gOverlay_window_2	
0x0065fb54	gOverlay_netmode	640x480 at 0x4e57a3, never resized
0x0065fbe0	gReplay_bar_x_start	[2] per res; +8 x end, +0x10 y
0x0065fc20	gReplay_button_rects	[2][9] l,t,r,b
0x0075c354	gPlayer_headup_layout	[2 views][20 slots] stride 0x2c
0x0067c500	gHeadups	stride 0x164; +4 x, +8 y, +0xc anchor x
0x0079eaec	gTimer_headup_slot	
0x0074ca7c	gStatbarHUD1_actor	
0x0074ca14	gStatbarHUD3_actor	
0x0074ca40	gTimerLeftHUD_actor	
0x0074ca24	gTimerRightHUD_actor	
0x0074ca58	gStatbarRightHUD_actor	
0x0074cf18	gStatbarHUD5_actor	
0x0074cf80	gArmour_actor	
0x0074ca98	gPower_actor	
0x0074ca84	gOffence_actor	
0x0074cef4	gHeadup_actor	BOTHUP.DAT bottom bar
0x0074ca1c	gStatbarHUD1_texture	gauges drawn here
0x0074cf00	gStatbarHUD5_texture	damage icons drawn here
0x0074abac	gHeadup_map_x	+4 y, 0x74aba8 w, 0x74abb4 h
0x0074abdc	gHeadup_map_x_f	float copy moved by Shift+arrows
0x00659b2c	gMap_render_x	float; forced 80.0 at 0x48e9a7
```
