# Camera FOV/aspect, HUD and menu layout (CARMA2_HW.EXE, Steam build)

Static analysis, 2026-11-02. Confidence: **H** = read from the instructions (capstone on the real bytes), **M** = decompiler output or a strong inference, **L** = a guess to test in play. Names follow dethrace where the code matches Carmageddon 1. Struct offsets: actor `type_data` +0x5c and transform matrix +0x2c..+0x58 (translation +0x50/54/58); br_camera +4 type, +6 field_of_view (br_angle), +8 hither, +0xc yon, +0x10 aspect, +0x14 width, +0x18 height; br_pixelmap +0x8 pixels, +0x28 row_bytes, +0x30 base_x, +0x32 base_y, +0x34 width, +0x36 height, +0x38 origin_x, +0x3a origin_y.

## 0. The short version

- The game always runs a **640x480 16-bit** device and back buffer. The size is hard-coded twice: in the `BrDevBeginVar` call (push 0x280 / push 0x1e0 at **0x51c34e / 0x51c347**) and in the graf-spec table entry 1 (logical 640x480 at **0x662248/0x66224c**, physical 640x640x480 at **0x662260/0x662264/0x662268**). (H)
- BRender's `field_of_view` is **vertical**: the perspective matrix builder 0x51e450 sets m00 = cot(fov/2)/aspect and m11 = cot(fov/2) (M, clear decompile). The main camera gets fov = `gCamera_angle` (55.55 deg from GENERAL.TXT), so the vertical FOV is fixed and the horizontal FOV comes from `aspect`. The game is already **Hor+ by design**. It only looks stretched because the 4:3 buffer is scaled to a 16:9 display.
- The main camera's `aspect` is written **once**, at camera creation (0x47e40e..0x47e41c, render width/height = 640/480). It is never refreshed in the external view (the refresh at 0x47d9f4 is in a cockpit-only branch that C2 never runs). (H)
- The HUD and menus are a mix of three systems, all in 640x480 pixel units: BRender 2D calls on the back screen, the game's own direct-pixel blitters, and textured quads drawn through **parallel cameras** whose width and height are 640x480 (or the screen size). Positions come from `HEADUP.TXT`, the per-resolution car file, `INTRFACE\*.txt`, `OPTIONS.TXT` (minimap) and constants in the code. Nothing is scaled by screen size, except the minimap move limits and one item at 0x504bf0.

## 1. Cameras

| Camera (global) | Created at | Type, fov | hither / yon | aspect | Notes |
|---|---|---|---|---|---|
| **gCamera** = 0x74d35c (= gCamera_list[1] 0x74c7b4) | 0x47e3b0 loop, once at start-up (from 0x47dd20) | perspective; fov = gCamera_angle 0x74d354 x 182.044 (0x47e3de..0x47e3ef) **H** | hither = 0x74d3e8; yon = gYon 0x761f4c **H** | 0x703e24/0x703e20 (int render w/h, 640/480 at that moment) at 0x47e40e..0x47e41c **H** | Added under the world root 0x74d44c (0x47e441). The game's view. |
| gCamera_list[0] 0x74c7b0 | 0x47e3b0, same values | same | same | same | Never added to the world or rendered. Only SetYon/AssertYons touch it (M). Spare. |
| **gRearview_camera** 0x75b940 | 0x47e461..0x47e4d8 | perspective; fov = gCamera_angle at creation, then mirror angle 0x75c0c0 x 182.044 at 0x47db31 **H** | hither as main; yon = gYon x 0.5 (0x47e4bb) **H** | creation: 640/480; 0x47db4f..0x47db7b: (mirror right-left)/(bottom-top) **H** | m[2][2] = -1 (looks backwards). Rendered only if cockpit_on && cockpit image >= 0 && mirror_on && view==2 (RenderAFrame 0x4e4e40, M). `gCockpit_on` 0x75bbcc is set only from 0x6aaa30 (0x503cc5), which nothing writes (H xrefs), so **the mirror is dead in C2** (M). |
| HUD camera 0x74cf74 ("HUD Camera"), root 0x74ca00 | 0x47e560 | **parallel**, hither 1, yon 3 | | width/height = gBack_screen size (640x480) | Draws "poly font" text and queued 3D HUD items (0x4e5b00, 0x464e40). |
| 2D camera 0x74cac4 ("A 2D camera, possibly even g2d_camera"), root 0x74c9f8 | 0x47e720 | parallel, hither 1, yon 3 | | width/height = gScreen size (640x480) | Draws the HUD quads (gDim, gHeadup, gStatbar*, gTimerLeft/Right, gHUDcurve, gHUDsquare, gMapHUD, gArmour/Power/Offence, gPowerupHUD). Users: 0x47cad0 (dim rectangle), 0x44bac0, 0x43e770, 0x45aa60. |
| Menu backdrop camera 0x686f94, root 0x68650c | 0x46cf10 (menu init 0x46d1c0) | parallel, fov 0x1ffe, hither 1, yon 3 | | width 640.0, height 480.0 (constants) | Draws the INTRFACE "Backdrop" materials (0x46d8e0, 0x472b00). |
| Menu camera 0x688b10 | 0x46f750 (screen handler table 0x5ed6a8) | parallel, fov 0x3ffc | 1 / 3 | 640.0 x 480.0 constants | M |
| Menu 3D preview camera 0x688af0 | 0x46e830 (screen handler table 0x5f8f70) | perspective, fov 0x271a (55.0 deg) **H** | 1 / 100 **H** | **constant 1.6346** (0x3fd13b14, 0x46e89d) **H** | Rendered into sub-pixelmap 0x7635e4 with back-screen origin forced to (320,120) (0x46d8e0, M). |
| Net HUD camera 0x74a730 | 0x49a410 / 0x499a00 | parallel | 1 / 3 | gScreen size | Network scores. |
| tinted_poly_camera 0x6a0430 | 0x4d7040 | parallel | 1 / 3 | gScreen size | Rendered every frame by 0x4d8290. |
| 0x6a20d0 | 0x4e3660 | perspective 70 deg, hither 0.001, yon 1000, aspect 1 | | | Used only for maths by 0x4e36f0/0x4e3bc0/0x4e4370, never rendered (M: line-of-sight/visibility helper). |
| Funk cameras (render-to-texture) | 0x474ac0 (track funk loader), case 2 "camera" and case 3 "mirror" | fov from the track file; hither copied from gCamera; yon = gCamera yon x factor or x 2 | | default 1.0 | Textures (CAMERA.PIX etc.), so screen aspect does not matter. |

BRender's default camera (BrActorAllocate 0x51e1a0, type 3) is perspective, 45 deg, hither 1, yon 10, **aspect 1.0** (M). So every parallel camera above has aspect 1.0. Parallel projection (0x51e520) scales x by 2/(width x aspect) and y by 2/height (M), so **a parallel camera's `aspect` field squeezes or widens its whole 2D layer**.

### When the camera values are written

- **Once at start-up** (0x47dd20): 0x4e4980(0,0) sets gRender_screen and 0x703e24/0x703e20 = back-screen 640/480 (H). Then 0x47e3b0 creates the cameras (fov, hither, yon, aspect) (H). GENERAL.TXT has already been read by 0x486ef0: hither 0.03 x 2 = 0.06 -> 0x74d3e8; cockpit hither 0.01 x 2 -> 0x7634ac; Yon 35 -> 0x761f4c; Camera angle 55.55 -> 0x74d354 (writes at 0x487113/0x48717d/0x487194; values from data\DATA.TWT GENERAL.TXT) (H addresses, M field order).
- **On render-window changes** (0x4e4b40 AdjustRenderScreenSize; called by LoadCar 0x488f70, ToggleMap 0x444610, 0x442f90, view switch 0x44b170, 0x481830): it updates gRender_screen and 0x703e24/0x703e20, then calls 0x47d8d0 ReinitialiseForwardCamera. That function **jumps straight to AssertYons when cockpit_on == 0** (je at 0x47d8e3) (H), so in C2 it only rewrites yon = gYon_multiplier (0x655ddc) x gYon (0x446a10) (H). The cockpit branch (vertical FOV from render height, off-centre shear m[2][1], aspect refresh at 0x47d9f4) never runs (M).
- **Every frame** 0x4940e0 calls PositionExternalCamera 0x40ea30, which writes hither every frame (0x40eda7) and writes fov = gCamera_angle x 182.044 only when the camera type changes (0x40ed92..0x40eda3). Camera types 1 and 2 use 0.7 x that angle, cached in 0x679304 (0x40ee0b..0x40ee34, 0x40ee79..) (H). 0x4940e0 also caches gYon_squared 0x762100 = (multiplier x yon)^2 for object culling (M).
- **Aspect consumers** besides the projection: track-cell culling 0x40d7c0 (frustum footprint = aspect x tan(fov/2) x yon x YonFactor) and the sky/horizon model 0x445e20 (reads fov and aspect). The sky model is rebuilt by 0x445cb0 only when fov (0x67c4ac) or yon (0x67c4b8) change, **not when the aspect changes** (M).
- Hard-coded 4:3 screen maths: 0x413860 (action-replay screen-to-world ray) uses centre (320,240) and 1/240 (M).
- No 1.333 constant is used for aspect. Aspect is always width/height from pixels or a constant (1.6346 menu preview; 1.0 defaults).

## 2. Rear-view mirror and minimap viewports

- **Mirror**: pixel rectangle `Left, top, right, bottom of mirror` from the per-resolution car file (`data\CARS\64x48x8\<car>.txt`, falling back to `CARS\64X48X8\<default>`, M; only eagle3.txt ships: 262,30,349,63). Globals 0x75be38/3c/40/44, shifted by the cockpit offset (car+0x104/+0x108) in LoadCar after 0x489d5e (M). 0x47db30 allocates gRearview_screen 0x68b8a8 = sub-pixelmap of the back screen at that rect and sets the camera aspect to w/h (H). Dead in C2 (see above).
- **Minimap (HUD map)**: pixel rectangle on gBack_screen from OPTIONS.TXT `HeadupMapX/Y/W/H` -> 0x74abac/0x74abb0/0x74aba8/0x74abb4 (writes at 0x48d949..0x48d962, H). The defaults if absent are 228,150,48,48 (0x48d8f0, M). 0x496e30 (InitMiniMap) aligns X/W down to 4, clamps W/H to <= 64, and builds the work pixelmaps. 0x4950b0 (DrawMiniMap, every frame from 0x496d80 when MapMode==1) rotates the map into the scratch pixelmap 0x68d8d8, then blits it with the game's own blitter 0x47cd40 to (HeadupMapX+8, HeadupMapY), WxH, in absolute pixels (H call at 0x495358). Shift+cursor (0x497620) moves float copies 0x74abdc/0x74abd8 at 0.05 px/ms, clamps to the **back-screen width/height** (dynamic, so it would follow a wider buffer), and saves OPTIONS.TXT every step (M).
- **Full-screen map** (MapMode 2, ToggleMap 0x444610): the 3D view shrinks into a small window. `MapRenderX/Y/Width/Height` are read (0x659b2c/30/34/38 floats), but **always overwritten** after the file is read with 80,400,128,80 (0x48e9a7..0x48e9c5, H). 0x4948b0 then forces W=128, H=80 again. **The OPTIONS.TXT MapRender lines are dead.** In map mode 0x4e4b40 uses rect 0x74abc0/0x74abbc/0x74abe8/0x74abc8. The camera aspect stays 4:3 in that 1.6:1 window (a vanilla squash, M). The map itself is drawn full-screen 640x480 (0x496be0, M).

## 3. 2D HUD and menus

### Drawing primitives (all in 640x480 pixel space)

- BRender 1.3 2D front-ends on gBack_screen 0x74d360 or gRender_screen 0x762128: BrPixelmapRectangleCopy 0x538590, RectangleFill 0x538640, Text 0x538a10, Line 0x538990, Pixel 0x538800 (M). These go through the device dispatch, which applies the pixelmap **origin** (M).
- The game's own 16-bit blitters write straight to `pixels + base_x + y*row_bytes`, **ignoring origin**: 0x47ba80 (masked copy), 0x47cbd0, 0x47cd40 (minimap) (M).
- Textured quads through parallel cameras: the 2D camera (0x74cac4) for the HUD quads, and the HUD camera (0x74cf74) for **poly fonts** (fonts listed in data\64x48x8\FONTS\*.TWT: Medium_Headup, BigYellowTimer, TinyFont, NetPosFont, etc.; InitPolyFonts 0x463d40, FONT.TXT 0x4643f0). DrawPolyFontText 0x464e40 places one actor per glyph at (x, -y, -1.1) and renders with the back-screen origin and base zeroed **and its width/height forced to 0x280/0x1e0** for that pass (M). The quad vertices are pixel coordinates with y negated, for example 0x47cad0 (M).
- Bitmap fonts HEADUP.FNT, FONT7.FNT and BIGFONT.FNT are loaded in 0x47dd20 (M).

### Where positions come from

| HUD item | Source | Code |
|---|---|---|
| Credits, ped kill count, ped warning, misc messages, countdown, bonuses, timer text, lap count, cars-out, network cash (15 slots, external and internal views) | `data\64x48x8\HEADUP.TXT`: x, y, font, justification lh/ch/rh/cc, optional clear rect | Loaded per car by 0x48ba60 (called twice from LoadCar 0x488f70 at 0x489d5e) into car+0x728+view*0x370, 0x2c per slot (M). Drawn by DoHeadups 0x449b10 into **gRender_screen** with BrPixelmapText 0x538a10 or poly fonts 0x465a70/0x465aa0, plus shake offsets 0x705188/0x705184 (M). |
| Timer x override | constants: 0x27a (634) or 0x185 (389) by HeadupDetailLevel 0x655e54 | 0x449b10 (M) |
| Damage display, gears, speedo, hands, pratcam, dimmed areas | per-resolution car file (`data\CARS\64x48x8\eagle3.txt`: damage icons x,y; `DAMASKALPHA.PIX` at 0,0) | stored in gProgram_state.current_car around 0x75be58..0x75c318; drawn by 0x44aad0 / 0x4496f0 with 0x47ba80 / 0x47cbd0 (M) |
| Timer frame, HUD curve, stat bars, power/armour/offence icons, powerup HUD, map frame | hard-coded quad positions and actor translations, e.g. 142, -178, -448 px | 0x47e720 (placeholder quads), 0x44b6a0 per frame (M) |
| Opponent bar / opponent status (`Oppostat.txt` font), 0x4dad00 | 0x6a0a54 table, blits to gBack_screen | L |
| Minimap | OPTIONS.TXT HeadupMap* | section 2 |
| HUD detail | F1 cycles HeadupDetailLevel 0x655e54 (write at 0x444f6c; default 5; saved in OPTIONS) | M |
| `ScreenSize` | 0x68be38 shrinks the 3D window by (640/18)*n (0x47dc30), but HW forces 0x74cf8c=1 (0x51c440), so it is **ignored on HW** | H write, M effect |

Only 0x504bf0 scales by screen size: it builds a scale matrix from total_width/640 and total_height/480 when the mode is not 640x480 (M; some 3D overlay, L).

### Menus

- Screens are defined by `data\INTRFACE\*.txt` (e.g. graphics.txt: element xpos,ypos,width,height in 640x480 pixels, fonts, groups) and `Interface.txt` (strings). Loader 0x484fd0, plus MENUMODELS.TXT / MENUIMAGES.TXT in 0x46d1c0 (M).
- Backdrops (INTRFACE\Backdrop, 640x480 art) are three textured quads drawn through the 640x480 parallel camera 0x686f94 onto gBack_screen with origin (0,0) (0x46d8e0 at its render call, M). With a wider back buffer they would **stretch to the full width** (parallel camera = whole viewport). Text and buttons are pixel-placed (M).
- 3D models in menus: preview camera 0x688af0 (aspect 1.6346 constant) rendered to 0x7635e4 with origin (320,120) (M).

## 4. Draw distance

- `Yon` (OPTIONS.TXT, 35) -> SetYon 0x446b70: clamps to >= 5.0 and writes yon into **both** gCamera_list cameras and gYon 0x761f4c (H decompile; call at 0x48dc3d). The effective camera yon is gYon x gYon_multiplier 0x655ddc. The multiplier is per track, read by the track loader (0x506b75, H). Rear camera yon = gYon/2 (once).
- `YonFactor` (OPTIONS.TXT, 1.0; the exe's built-in default at 0x655e60 is **0.25**) -> SetYonFactor 0x40dff0. Its only consumer is the track-cell culling 0x40d7c0 (via 0x506e50): **cells further than yon x YonFactor are not drawn** (H for the reads at 0x40d902/0x40d913/0x40d924).
- Graphics menu (INTRFACE\graphics.txt): elements 8-11 "Scenery Cut-off" distant/far/nearer/nearest -> **Yon 35/30/25/20**; elements 13-16 "Scenery Pop-up" -> **YonFactor 1.0/0.5/0.25/0.125**. Apply handler 0x4b42c0: pushes at 0x4b4313..0x4b4328 and 0x4b434d..0x4b4362 (H).
- The menu's init (0x4b4090) picks the cut-off button as `_ftol((35 - yon) * 0.2)` (0x4b40c4..0x4b40da, H): 0 -> distant, 1 -> far, 2 -> nearer, anything else -> nearest. Apply then **always** calls SetYon with the selected value. So any Yon outside about 20..39 shows as "nearest", and opening and OK-ing the Graphics menu would write **20**, not 35. Yon 35..39.9 snaps to 35 (M; PCGamingWiki says it resets to 35: **L, test in play**). Same pattern for YonFactor: >= 1.0 is button 13, so a YonFactor above 1 snaps to 1.0 (M).
- Gotcha: changing yon changes the sky model's cache key, so the sky is rebuilt (0x445cb0). Pedestrian distances live in PEDS\SETTINGS.TXT (not here).

## 5. Recommendations

### A. Hor+ at any aspect with the current 640x480 buffer (parity with the community FOV ASI)

The wrapper stretches the 4:3 frame to the display. Correct this by setting the main camera's aspect to the display aspect:
1. After 0x47e3b0 returns (or by patching 0x47e40e..0x47e41c), set `gCamera->type_data->aspect` (0x74d35c ->+0x5c ->+0x10) = (640/480) x k, with k = (display W/H)/(4/3). Keep fov. The vertical FOV stays 55.55 deg, so the result is Hor+. Nothing else rewrites the external-view aspect (H), so one write holds for the session. Do the same for gCamera_list[0] for tidiness.
2. Do it **before the first rendered frame**, so the sky model 0x445e20 is built with the new aspect. If it changes later, also force a rebuild by clearing the cached fov 0x67c4ac (M).
3. The track-cell culling 0x40d7c0 and the sky read the aspect from the camera, so they widen with it. **Do not** fake the aspect in the projection matrix or the driver: the culling would then miss the screen edges and scenery would pop in at the sides (M).
4. Optional: make 0x47d8d0 refresh the aspect outside the cockpit branch too, so map mode (128x80 window) gets 1.6 x k (vanilla bug, minor).
5. Risk: the HUD and menus stay stretched, because the wrapper scales the whole frame. Blitted HUD parts cannot be squeezed from inside the game. Only the parallel-camera quads could (aspect field), and squeezing only those would misalign them against the blits. Accept a stretched HUD in this mode, or use the wrapper's own 4:3 pillarbox for menus.

### B. Native widescreen buffer (true fix, HUD not stretched)

Minimal set of patch points:
1. Device size: the immediates at **0x51c348** (480) and **0x51c34f** (640) in PDAllocateScreenAndBack 0x51c300 (H). gBack_screen and the depth buffer are matched from it.
2. gGraf_specs[1] (base 0x66223c, stride 0x34; default index 1 at 0x662200): logical total_width/height **0x662248/0x66224c** (render window, borders, HUD limits, 0x47dc30, 0x4e4e40, 0x504bf0); physical 0x662260/0x662264/0x662268; flag 0x662244. 0x4e4980 already **centres** a logical area inside the physical one on gScreen (base_x = (phys - logical)/2), a C1 leftover. That is a lead for pillarboxing; it acts on the front screen, not the back screen (L).
3. With the back buffer W x H, the camera aspect becomes W/H at creation automatically, so 3D is Hor+ with no FOV change (H for the code path).
4. 2D at 640x480 must be offset by xo = (W x 480/H - 640)/2 (pillarboxed, at 480 lines), or the whole 2D layer moved into a 640x480 sub-view:
   - BRender 2D ops: set gBack_screen/gRender_screen origin_x += xo during the HUD and menu phases (they honour origin, M).
   - The game's direct blitters (0x47ba80, 0x47cbd0, 0x47cd40): offset the `pixels` pointer by xo x 2 bytes for the 2D phase, the same trick the game uses for screen shake on gRender_screen in 0x4e4e40 (M). Lock/pointer behaviour on the 3dfx/D3D device pixelmap is **L**.
   - Parallel cameras (0x46cf10, 0x46f750, 0x47e560, 0x47e720, 0x49a410, 0x4d7040, 0x46e830): set their `aspect` to (W/H)/(4/3) so 640 units cover a 4:3 area, and shift by xo (origin/translation). 0x464e40 forces the back-screen size to 640x480 for poly fonts: change it to the 4:3 sub-area.
   - At higher resolutions (e.g. 1920x1080), 1:1 pixel blits do not scale. Either keep the game at 480 lines (e.g. 854x480) and let the wrapper upscale, or accept small HUD art. The wrapper's internal-resolution override can still raise the 3D resolution (L).
5. Other hard-coded 640/480 users to review: 0x47dd20 and 0x4e5680 (0x4d70c0(640,480,...) full-screen tinted quads), 0x496be0 (full map 640x480), 0x46d8e0 (menu origin 320,120), 0x413860 (picking centre 320,240), 0x43e770 (320x200 Smacker buffer drawn via the 2D camera). MapRender 80,400 is an absolute pixel position (0x48e9a7).
6. Risks: the HUD and menu code mixes origin-aware and origin-blind drawing; 3D overlays may misalign with blits; device pixelmap locking ("lock often/seldom" for Voodoo, 0x51c3c8); the dgVoodoo/nGlide mode list must offer the new size; save-game and screenshot code may assume 640x480 (L).

**Suggested order:** ship A first (a few bytes, low risk, matches the ASI). Then prototype B at 854x480 with only the device and graf-spec changes, look at what breaks, and add the 2D offsets one system at a time.

## names.tsv

```
0x0047dd20	InitialiseGraphics	start-up: screens, cameras, fonts, HUD (strings AFTER LOADING FONTS)
0x0047e3b0	InitialiseCameras	creates gCamera_list[2] and gRearview_camera; fov/hither/yon/aspect
0x0047e320	InitialiseCameraHelpers	allocates helper actors under world root
0x0047e560	InitHUDCamera	parallel "HUD Camera" 0x74cf74, root 0x74ca00
0x0047e720	Init2DHUD	g2d_camera 0x74cac4 and HUD quad models/materials
0x0047d8d0	ReinitialiseForwardCamera	cockpit-only fov/aspect; always AssertYons
0x0047db30	ReinitialiseRearviewCamera	mirror fov/aspect and sub-pixelmap
0x0047dc30	ComputeRenderWindow	render rect from graf spec, ScreenSize, cockpit
0x004e4980	SetBRenderScreenAndBuffers	gRender_screen, render w/h 0x703e24/20, depth buffer
0x004e4b40	AdjustRenderScreenSize	applies render rect / map rect, calls 0x47d8d0
0x004e4e40	RenderAFrame	main frame: 3D, mirror, HUD passes
0x004e5b00	RenderHUDScene	renders HUD world with HUD camera
0x004940e0	UpdateCamerasPerFrame	PositionExternalCamera + camera matrices + yon squared
0x0040ea30	PositionExternalCamera	per-frame hither; fov on camera-type change
0x00446b70	SetYon	clamp >=5, writes camera yons and gYon
0x00446bb0	GetYon	
0x00446a10	AssertYons	yon = gYon_multiplier * gYon
0x0040dff0	SetYonFactor	
0x0040dfe0	GetYonFactor	
0x0040d7c0	RenderTrackCellsInFrustum	track grid culling with fov/aspect/yon*YonFactor
0x00445cb0	DrawSky	rebuilds sky when fov/yon change
0x00445e20	BuildSkyModel	uses camera fov and aspect
0x00486ef0	LoadGeneralParameters	GENERAL.TXT: hithers, yon, camera angle
0x0048d8f0	LoadOptions	OPTIONS.TXT reader; resets MapRender to 80,400,128,80
0x0048d190	SaveOptions	OPTIONS.TXT writer
0x004b4090	GraphicsMenuInit	maps yon/yonfactor to radio buttons
0x004b42c0	GraphicsMenuApply	Scenery Cut-off -> Yon 35/30/25/20; Pop-up -> YonFactor 1/.5/.25/.125
0x0051c300	PDAllocateScreenAndBack	BrDevBeginVar 640x480x16 (imm at 0x51c348/0x51c34f)
0x0051c290	SetDefaultGrafSpec	gGraf_spec_index = 0x662200
0x00488f70	LoadCar	includes HEADUP.TXT and per-res car file
0x0048ba60	LoadHeadupLayout	one view of HEADUP.TXT into car struct
0x00449b10	DoHeadups	draws HEADUP slots into gRender_screen
0x00449090	InitHeadups	SCROLLERS.TXT, timer/hudcurve art
0x00464e40	DrawPolyFontText	glyph quads via HUD camera; forces 640x480
0x00463d40	InitPolyFonts	
0x0047cad0	DimRectangle2D	gDim quad via g2d_camera
0x0047ba80	DRPixelmapRectangleMaskedCopy16	direct-pixel blit, ignores origin
0x0047cd40	DRPixelmapRectangleCopyMap	minimap blit
0x00496e30	InitMiniMap	HeadupMap rect align/clamp, work pixelmaps
0x004950b0	DrawMiniMap	
0x00496d80	DoMap	minimap or full map per MapMode
0x00497620	MoveMiniMap	Shift+cursor, clamps to back screen
0x004948b0	SetMapRenderRect	3D window in map mode, forces 128x80
0x00444610	ToggleMap	
0x0046cf10	InitMenuBackdropCamera	parallel 640x480
0x0046e830	MenuPreview3DEnter	preview camera 0x688af0 aspect 1.6346
0x004e3660	InitLOSCamera	70 deg helper camera, not rendered (name L)
0x00413860	ScreenPointToWorldRay	hard-coded centre 320,240
0x00503c50	InitRaceState	sets mirror_on/cockpit_on from 0x660ce0/0x6aaa30
0x0051e1a0	BrActorAllocate	
0x0051dfe0	BrActorAdd	
0x0051e520	CameraToScreenMatrix	parallel uses width*aspect
0x0051e450	BrMatrix4PerspectiveOffset	m00=cot/aspect, m11=cot
0x00538590	BrPixelmapRectangleCopy	
0x00538640	BrPixelmapRectangleFill	
0x00538a10	BrPixelmapText	
0x005226d0	BrZbSceneRender	(world, camera, colour, depth)
0x00575b80	_ftol	
0x0074d35c	gCamera	
0x0074c7b0	gCamera_list	[0] spare, [1]=gCamera
0x0075b940	gRearview_camera	
0x0074d354	gCamera_angle	vertical fov deg (55.55)
0x0074d3e8	gCamera_hither	0.06
0x007634ac	gCamera_cockpit_hither	0.02
0x00761f4c	gYon	
0x00655ddc	gYon_multiplier	per track
0x00655e60	gYon_factor	YonFactor; exe default 0.25
0x00762100	gYon_squared	
0x00703e24	gRender_width_for_aspect	
0x00703e20	gRender_height_for_aspect	
0x00762128	gRender_screen	
0x0074d360	gBack_screen	
0x0074d3e0	gScreen	
0x0068b8a4	gDepth_buffer	
0x0068b8a8	gRearview_screen	
0x00762324	gGraf_spec_index	
0x00662208	gGraf_specs	stride 0x34; [1] at 0x66223c
0x0075bbcc	gCockpit_on	never set in C2
0x0075bbc4	gMirror_on	
0x0075bbd0	gCockpit_image_index	
0x0075bbd4	gRender_left	+4 top, +8 right, +0xc bottom
0x0075be38	gMirror_left	+4 top, +8 right, +0xc bottom
0x0075c0c0	gMirror_angle	
0x0075c0a8	gDriver_head_offset	x,y,z
0x0068be38	gScreen_size	ScreenSize option
0x0074cf8c	gFull_render_window	HW forces 1
0x0075b9a4	gMap_mode	1 minimap, 2 full map
0x00655e54	gHeadup_detail_level	
0x0074abac	gHeadup_map_x	+4 y at 0x74abb0; w 0x74aba8; h 0x74abb4
0x00659b2c	gMap_render_x	floats x,y,w,h; overwritten after load
0x0074cf74	gHUD_camera	
0x0074cac4	g2d_camera	
0x00686f94	gMenu_backdrop_camera	
0x00688af0	gMenu_preview_camera	
0x0074a730	gNet_HUD_camera	
0x006a0430	gTinted_poly_camera	
0x00679304	gAlt_camera_fov	0.7x angle, camera types 1/2
0x0079efa8	gCamera_type	CameraType option
0x0067c500	gHeadups	stride 0x164
```
