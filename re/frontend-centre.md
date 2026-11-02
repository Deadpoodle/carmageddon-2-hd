# Centring the front end in a wider back buffer (CARMA2_HW.EXE + D3D.BDD, Steam build)

Static analysis only (Ghidra caches plus capstone/pefile on `testrun/CARMA2_HW.EXE` and `testrun/d3d.bdd`). Tags: **H** = read from the instructions, **M** = decompiler output or a strong inference, **L** = a guess to test in play. Addresses are VAs (exe base 0x400000, driver 0x10000000). Pixelmap fields: +0x08 pixels, +0x28 row_bytes, +0x30 base_x, +0x32 base_y, +0x34 width, +0x36 height, +0x38 origin_x, +0x3a origin_y. Camera (actor+0x5c): +0x10 aspect, +0x14 width, +0x18 height. W = back-buffer width, xo = (W − 640)/2.

## 0. Summary

- Every front-end element ends up in one of three primitives, and each one places things by the **back screen's origin** (H):
  - **3D through a parallel camera**: 0x522f30 → 0x522c80 and 0x5226d0. Both compute the NDC x offset from `(origin_x − width/2)/(width/2)` (0x522ca3..0x522cce). The driver puts the centre at `base_x + width/2` (0x10006490). So pixel x = base_x + origin_x + x·width/(camera_width·aspect).
  - **2D rectangle copies**: 0x5191b0 → 0x538590 → 0x538430 → driver RectangleCopyTo 0x10001b00. The driver clips with BRender's `PixelmapRectangleClipTwo` 0x53dc80, which **adds the destination's origin_x** (0x53dc90).
  - **Sub-pixelmaps**: AllocateSub 0x10001c90 calls `PixelmapRectangleClip` 0x53dbb0, which also adds the parent's origin_x.
- So xo goes in as "**origin_x + xo while drawing onto gBack_screen**", plus the four front-end cameras made 1 unit = 1 pixel. Nothing else needs moving.
- Why it looks like it does today at W=854:
  - The backdrop is a 1:1 blit (left 640). Poly-font text is 1:1 because the HUD camera width = back width.
  - The panels, glows and main-menu bars go through 640.0-wide camera constants, so they are **stretched by W/640**. Past x=640 the dark translucent panels are invisible over black (M; in `testrun/shots/wide.png` the panels run to the 640 edge at 1.33× their 640 positions).
  - The version label reads the back width (H).
- The bars stay black on their own. After every Flip the driver clears colour and Z over the whole mode rectangle (0x10005b10 case 4 → case 5, `IDirect3DViewport2::Clear(0,0,gModeWidth,gModeHeight, TARGET|ZBUFFER)`) (H). BRender fills cannot do it: Fill (+0x60 → 0x100018c0 `xor eax,eax; ret`) and RectangleFill (+0x9c → 0x100018a0, clip only) are no-ops on D3D device pixelmaps (H).
- The mouse is raw client pixels, clamped to 640×480 (0x483c10). One detour on 0x51c900 (its only caller is 0x483c10), plus the double-click reader 0x51db00, maps client → frame → menu space (H for the plumbing; **L** for what dgVoodoo hands back).

## 1. Drawing paths and the point to add xo

The front-end frame (FE_MainLoop 0x46d8e0) runs in this order (H):
1. back origin := 0 (0x46d969/0x46d973)
2. backdrop blit (0x46d996)
3. FE_UpdateScreen (0x46d9c1); Main's update also draws the version label
4. depth "fill" (0x46d9db, a no-op on D3D)
5. back origin := 0 again (0x46d9e8/0x46d9f2)
6. panel morph if a transition is running
7. scene render (0x46dbc3)
8. Wrecks preview (0x46dca0)
9. main-menu bars (0x46de9f/b2/c5)
10. Credits (0x46deda)
11. FE_DrawItems (0x46dee5)
12. crosshair (0x46df0a)
13. Flip (0x46df23)

The Controls key-capture loop 0x472b00 is a copy of this loop (blit 0x472bdd, scene 0x472c50, items, crosshair, Flip).

| Element | How it is placed now | Best point for xo | Conf |
|---|---|---|---|
| **Backdrop** (640×480 art, g_backdrop 0x686f8c) | `0x5191b0` fastcall (ecx=dst gBack_screen, edx=dx 0, then dy, src, sx, sy, w, h) at **0x46d996**. The same call is at 0x472bdd (Controls). | Add xo to dx at those call sites (redirect the `call`), or add xo to dst origin_x for the call | H |
| **Panels A/B/C** (A-8.dat etc.), **morph** (0x46f5b0), **panel fades** (material opacity 0x686f5c/2c/44), **glow/button quads and image buttons** (FE_CreateButton 0x46c5c0 actors in world 0x68650c) | Parallel camera 0x686f94, width 640.0 at **imm 0x46cf4f** (instr. 0x46cf4c), height 480.0 at 0x46cf56. Rendered by 0x522f30 at **0x46dbc3** (and 0x472c50) → 0x522c80 (called at **0x522f55**). Today stretched by W/640. | Camera width imm → (float)W; origin_x += xo around 0x522c80 | H (camera, origin maths), M (stretch seen in shot) |
| **Item text**: normal, greyed, focused pulse, selected (centred mode 2), titles, sliders' and level bars' labels, LoadGame text, Credits (x = 0x140 constant, 0x46f6a4) | Glyph actors at (x, −y, −1.1) under 0x74cf10, rendered by 0x465380 at **0x46575e** (0x5226d0) through the HUD camera 0x74cf74. That camera's width is the back width at creation (0x47e5c9), so text is 1:1. Origin and base are zeroed around the render (0x465380 tail). | origin_x += xo inside the 0x5226d0 call at 0x46575e | H |
| **Highlight / glow** | Button quad material colour (0x46e020); same actors as the panels | covered by the panel row | H |
| **Sliders, scrollers, spinners** (0x686820, 0x688ab0, 0x686508) | Logic lists only (0x470c20 / 0x471ba0). Their visuals are items (button quads plus text) in 640 space. | covered; only the mouse needs mapping | M |
| **Main-menu bar graphs** (three 30-cell bars) | 0x4708a0(x=95/146/195, y=348) at 0x46de9f/0x46deb2/0x46dec5 (also 0x4705ae.., LoadGame). Quad under camera 0x688b10, width 640.0 at **imm 0x469d5f** (Main enter 0x469a40) and **imm 0x46f7f9** (LoadGame enter 0x46f750). Rendered by 0x5226d0 at **0x470988**. Stretched today. | Both width imms → W; origin += xo at 0x470988 | H |
| **Crosshair cursor** (mse_line ×4 + mse_cros 16×16; tinted-poly slots 0x58fdc8..0x58fdd8, built by 0x43e3f0 with 640/480 lengths from graf data) | 0x43e0a0 moves the quads to the mouse (0x4d8bb0, vertices at (x, −y)) and renders through the tinted camera 0x6a0430 (width = gScreen width, 0x4d7093 → 1:1) via 0x4d8290 → 0x522f30 at 0x4d82a4 → 0x522c80. Drawn while 0x58fddc == 1 (static 1, never written). | origin += xo around 0x522c80 (same hook as the panels) | H |
| Software cursor save/restore (0x43e010, 0x43e0a0 copy at 0x43e22c) | Gated by 0x74ca28 == 0. 0x51c452 sets it to 1 unconditionally (edi=1), so it is dead on HW. | none | H |
| **Wrecks 3D preview** (camera 0x688af0, perspective, aspect 1.6346) | Rendered into sub-pixelmap 0x7635e4 = `BrPixelmapAllocateSub(gBack_screen, 65, 55, 510, 312)` at **0x46e928** (cdecl, `push 0x41` at 0x46e925), origin (255,120). Viewport centre = sub base_x + 255. The loop's temporary rewrite of gBack_screen around the 0x46dca0 render (base 0, origin 320/120, then restore) does not affect the sub. | x += xo at the AllocateSub call. Alternatively, having back origin_x = xo at that moment works too, since the clip adds it. | H |
| **NetworkSummary player icons** | Update 0x474400 → 0x44bac0(x=88, y=112+24i) via the 2D camera 0x74cac4 (width = gScreen W) → 0x522f30 at 0x44bb5d | origin += xo around 0x522c80 | H |
| **Version label** "v1.02a" (font 11) | Drawn every frame by Main's update 0x46a0e0 (struct field 0x5a8200): x0 = back.w − textwidth − 4 (**0x46a1a0**), x1 = back.w (**0x46a166**), y from back.h | Make both reads 640, then the text origin shift puts it at xo + 640 − tw − 4 | H |
| **Smacker movies** (0x43e770) | With 16-bit screen bits (0x74ca60 == 16, always on HW) the textured path is used: the 320×200 frame is cut into 64×64 textures on 128-px quads, x 0..640, under the 2D camera 0x74cac4 (W wide, 1:1). Rendered by 0x5226d0 at **0x43ef64**. The SmackBlit direct-pixel path at (0,40) only runs at other bit depths. | origin += xo at 0x43ef64 (when in a centring scope) | H |
| **Loading screen** | 0x47b990 (from DoProgram via 0x43f5c0/0x503dcc): loads a pixelmap, blits it at (0,0) with 0x5191b0 at **0x47ba42**, then Flip | dx += xo at 0x47ba42 | H |
| **Message screen** 0x4a0080 (net/race messages) | ClearEntireScreen, then text box x 64..576 from graf spec 640 via 0x466000 → 0x4638b0 → **0x464e40**. That function forces back width 640 (imm16 at **0x4651b4**, instr. 0x4651b0) while the HUD camera is W wide, so at W>640 text is squeezed by 640/W. | 0x4651b4 → W, plus origin += xo at 0x4651dc | H (mechanism), M (squeeze; matches the race HUD text in the 854 shots) |
| **Fades** | No screen-space fade quad in the front end. 0x4b5470/0x4b55f0/0x4b5770 only touch the 8-bit palette (0x51c840); 0x4b5460 is empty. | none | M |
| **Transitions** | 16-step panel morph plus opacity in the FE world. No text during the morph. | covered by the panel row | H |
| **In-race pause menu** | MainGameLoop calls ClearEntireScreen (0x493be3), then 0x494570 → FE_MainLoop(1) (0x4945a6). The same loop with the Options backdrop; the race is not drawn behind it. | same as the front end | H |
| **Black bars** | Driver clear after every Flip (§0) | nothing | H |

Dead or irrelevant: 0x463130/0x463270 (front-end flic blits, no callers of 0x463270); 0x44ba10 (no callers); the 0x538640 fills at 0x46dd70 and 0x47046f.. write into bar-graph *textures*, not the screen (H).

## 2. Mouse

- **Read**: PDGetMousePosition 0x51c900 (fastcall &x, &y):
  - When app state 0x6621e0 == 2: GetCursorPos + ScreenToClient(hwnd 0x6ad4c8), stored in 0x6ad458/0x6ad45c.
  - Otherwise it returns those last values.
  - The -SCALEMOUSE branch is dead (`test eax,eax; jmp` at 0x51c93b).
  - **Its only caller is 0x483c10** (H).
- **Clamp**: GetMousePosition 0x483c10 clamps x to [0, graf_spec.total_width] and y to [0, total_height] (0x662214/0x662218 indexed by 0x762324), i.e. **0..640 and 0..480 inclusive, not 639/479**, and stores 0x6571f8/0x6571fc (H). The graf spec stays 640×480, so the clamp keeps menu space at any W (helps).
- **Users** (20 call sites):
  - front-end handlers: 0x46c164 generic update, 0x470f1b/0x470fa9 options handler (sliders), 0x46a3b7 Main, 0x46baf5 ChangeCar, 0x46ebbe Wrecks, 0x47009f LoadGame, 0x467389, 0x469426;
  - the crosshair 0x43e0ce;
  - input helpers 0x4824d9 (button), 0x482e88, 0x4834cb/0x483543;
  - in race, the replay panel 0x4e6abc (0x4e69b0), whose hit rects and picking 0x413860 are in 640 space, left-anchored.
- Hit-testing: FE_HitTestItem 0x4677d0 / 0x471ba0 compare against item rects in 640×480 space (menus.md).
- **Double-click**:
  - WndProc 0x51b52c.. stores the raw client lParam in 0x6621d4/0x6621d8 (scaled only with -SCALEMOUSE on a non-D3D device), with flag 0x6ad454.
  - 0x51db00 hands it out.
  - GetMouseButton 0x4824c0 returns 2 (double-click) **only if that raw position equals the clamped 0x483c10 position** (H).
  - Any remap must therefore also remap 0x51db00's output, or double-clicks are lost (today they work only inside the client's top-left 640×480).
- **Cursor sprite**: the tinted crosshair (§1), drawn at the 640-space mouse position. The hardware cursor is hidden (ShowCursor(0) in 0x51b9f0).
- **Mapping to add** (detour 0x51c900; call the original, then transform both outputs):
  - Client rect `cw×ch` (GetClientRect each call). The window is moved to SM_CX×SM_CY by the driver (0x100014af/c2) and by WM_ACTIVATEAPP (0x51b2d3), so this is the desktop size (H for the calls).
  - dgVoodoo ScalingMode `stretched_ar` (as written by the patch) → `s = min(cw/W, ch/480)`, `ox = (cw − W·s)/2`, `oy = (ch − 480·s)/2`.
  - `fx = (cx − ox)/s`, `fy = (cy − oy)/s`.
  - Inside a centring scope (§4): `fx −= xo`.
  - Let 0x483c10 clamp to 0..640 / 0..480.
  - Apply the identical transform plus the same clamp to 0x51db00's outputs (detour; when it returns 1).
  - **L**: whether dgVoodoo already rescales GetCursorPos or resizes the client. Log the GetClientRect size and raw points at the client corners once. If the client is W×480, s = 1 and the formula degenerates correctly.

## 3. Code that reads the back-screen (or screen) width

| Address | What | Effect at W>640 |
|---|---|---|
| 0x46a166 / 0x46a1a0 (Main update 0x46a0e0) | version label x1 / x0 from back.w | Puts it at the real right edge today. **Hurts** once text is shifted by xo (it would land off-screen), so patch to 640 (H) |
| 0x47e5c9 (0x47e560) | HUD camera width = back.w | **Helps**: poly-font text stays 1:1 (H) |
| 0x47ee2e (0x47e720), 0x4d7093 (0x4d7040), 0x499f04 / 0x49a6b5 / 0x4998fa | 2D, tinted and net-HUD camera widths = gScreen.w | **Help**: Smacker, crosshair, icons 1:1 (H) |
| 0x4651b0 (0x464e40) | forces back.w = 640 around the HUD-camera render | **Hurts**: text through 0x464e40 is squeezed by 640/W. Front-end use: only the message screen 0x4a0080. Race: all HEADUP text (DoHeadups 0x449b10 etc.), which in the 854 shots sits at ~0.75× its 640 x while its 2D art does not (M) |
| 0x46cf4f, 0x469d5f, 0x46f7f9 (code constants, not reads) | front-end parallel cameras 640.0 wide | **Hurt**: panels, glows and bars stretched by W/640 (H) |
| 0x483c34 / 0x483c6b (0x483c10) | mouse clamp from the graf spec (640/480), not the back | **Helps** (H) |
| 0x4a0080 | saves/restores back pixels (height×row_bytes); text box from the graf spec | neutral; text needs the 0x464e40 fix (H) |
| 0x43e770 | reads back type, row_bytes, height | neutral (textured path) (H) |
| 0x10005b10 case 5 (driver) | clear rect = gModeWidth × gModeHeight | **Helps**: bars cleared every frame (H) |
| 0x4e4980 / 0x4e499d | gRender_screen = back size; gScreen base_x = (phys − logical)/2 from the graf spec | race 3D full width (helps). The graf-spec centring acts on gScreen, and phys stays 640, so it does nothing (H) |
| race only: 0x444c00 (box at back.w·15/100…85/100, text at 20/100..80/100), 0x494900 (litopstat), 0x442f90 (map rect), 0x497620 (minimap clamp), 0x4e4e40 (border fills, shake), 0x4e5680, 0x4e6280 / 0x4e72e0 (replay UI), 0x504300 (height test) | scale or clamp to W | out of front-end scope; list for the HUD work (M) |

## 4. Recipe (any W; identical at W = 640 because xo = 0 and W = 640 make every step a no-op)

Let `back = *(pm**)0x74d360`, `W = back->width`, and `xo = (W − 640)/2`, computed when used. Keep a **centring scope** counter: increment/decrement it, nesting-safe.

1. **Scopes** (detours at entry; all prologues are relocatable):
   - FE_MainLoop **0x46d8e0** (`a1 28 23 76 00`; covers the in-race pause and Controls capture);
   - Smacker **0x43e770**;
   - loading screen **0x47b990**;
   - message screen **0x4a0080**.
   - Outside these (race frames, replay UI) nothing changes.
2. **Camera widths** (install time, verified PatchU32 `0x44200000` → `(float)W`):
   - **0x46cf4f** (FE backdrop/panel camera 0x686f94);
   - **0x469d5f** and **0x46f7f9** (bar camera 0x688b10, Main and LoadGame).
   - Leave 0x46e8eb alone (perspective preview, width unused).
   - The heights (480.0) stay.
3. **3D shift** (redirect the `call` sites; cdecl `(world, camera, pm, depth)`). The wrapper is: if in scope and pm == back, then `back->origin_x += xo`, call the original, `back->origin_x −= xo`. Sites:
   - **0x522f55** (the single call 0x522f30 → 0x522c80; covers the scene 0x46dbc3/0x472c50, crosshair 0x4d82a4, icons 0x44bb5d);
   - **0x46575e** (text);
   - **0x470988** (bars);
   - **0x4651dc** (0x464e40 text);
   - **0x43ef64** (Smacker).
   - Equivalent: detour 0x522c80 (`8b44240c 56`) and 0x5226d0 (`8b15ecef7900`) with the same body.
   - The preview render to 0x7635e4 is skipped by the pm test.
4. **2D shift**: redirect `call 0x5191b0` at **0x46d996**, **0x472bdd** and **0x47ba42** to a fastcall wrapper `(dst, dx, dy, src, sx, sy, w, h)` that adds xo to dx when in scope.
5. **Wrecks preview**: redirect `call 0x537d60` at **0x46e928** to a cdecl wrapper `(pm, x, y, w, h)` that passes x + xo.
6. **Version label** (byte patches, H):
   - **0x46a166** `66 8b 50 34` → `66 ba 80 02` (mov dx, 640);
   - **0x46a1a0** `66 8b 4a 34` → `66 b9 80 02` (mov cx, 640).
7. **0x464e40 forced width**: imm16 **0x4651b4** `80 02` → W, so its text is 1:1 like the rest. This also un-squeezes race HUD text; coordinate with the HUD plan (L for the race look).
8. **Mouse**: detours on **0x51c900** (`a1 e0 21 66 00`) and **0x51db00** as in §2. Subtract xo only inside a scope.
9. Nothing to do for the bars (driver clear), fades, sliders, scrollers or the cursor save path.

**Risks**
- Nothing clips at the 4:3 edges any more, so anything that used to spill past x<0 or x>640 now shows in the bars. Nothing is known to do this: panels morph in place, credits are centred at 320 (L).
- The crosshair arms are 640/480 long and start at the mouse, so they stay inside the 4:3 area (H).
- Any new front-end code that writes back->origin_x and then draws inside a hooked call must expect the +xo. The game itself writes 0 at 0x46d969/0x46d9e8, 0x472b00 and 0x465380/0x464e40, then restores; the wrappers add and remove around each call, so these stay correct (H).
- The Wrecks wrapper must not double-add if back origin_x is ever non-zero at Wrecks enter, because the clip adds origin too (0x53dbba). In vanilla it is 0 there (M).
- Mouse: the dgVoodoo behaviour is **L**. With the scale step, mouse behaviour at W = 640 also changes on a desktop-sized client (it becomes correct). Gate the scale step by a setting if exact stock behaviour is wanted.
- Heights other than 480: the same trick works with origin_y (0x522cd8 reads +0x3a): yo = (H − 480)/2, plus the 480.0 camera heights (0x46cf56, 0x469d66, 0x46f800), imm16 0x4651c0 (instr. 0x4651bc) and the mouse y (M).

**Runtime checks**
- At W=854 the backdrop left edge sits at x=107 and the panels hug it at the same 1:1 size as at 640.
- Hovering an item lights that item, at the crosshair's centre.
- The version label is at about x 701..743.
- Wrecks preview: 172..682.
- Intro Smacker: 107..747.
- Pause menu from a race: the same.
- Double-click in LoadGame still works.
- Repeat at W=640 and diff against stock shots.

## names.tsv

```
address	name	note
0046d8e0	FE_MainLoop	front-end frame loop; centring scope entry
00472b00	FE_ControlsCaptureLoop	key-capture copy of the FE loop (blit 0x472bdd, scene 0x472c50)
0046a0e0	Main_Update	screen+0x110 of Main; draws version label (x from back.w at 0x46a166/0x46a1a0)
00469a40	Main_Enter	creates bar camera 0x688b10 (640.0 imm 0x469d5f)
00469df0	Main_Exit
0046f750	LoadGame_Enter	creates bar camera 0x688b10 (640.0 imm 0x46f7f9)
00470020	LoadGame_Update
0046e830	Wrecks_Enter	preview camera 0x688af0; sub-pixelmap 0x7635e4 at (65,55,510,312) via 0x46e928
0046eb80	Wrecks_Update
00474400	NetworkSummary_Update	player icons via 0x44bac0
004708a0	FE_DrawBarGraph	quad at (x,-y) under camera 0x688b10, renders via 0x5226d0 at 0x470988
0046f630	Credits_Draw	centred text at x=320
0043e0a0	FE_DrawCursorAndThrottle	crosshair tinted quads at mouse; 21 ms cap
0043e3f0	FE_InitCrosshair	mse_corn/mse_cros/mse_line; tinted slots 0x58fdc8..0x58fdd8
0043e6c0	FE_CursorReset
0043e010	FE_RestoreUnderCursor	dead on HW (0x74ca28=1)
004d7040	InitTintedPolys	tinted camera 0x6a0430 width = gScreen.w
004d70c0	NewTintedPoly	(w,h,type,...) -> slot
004d8bb0	SetTintedPolyRect	fastcall slot,x,y,w,h (vertices at x,-y)
004d8290	RenderTintedPolys	0x522f30 with gBack_screen
004d8220	ShowTintedPoly
004d8250	HideTintedPoly
00464df0	PolyFont_DrawLineAlpha1	-> 0x465380 with alpha 1.0
00465380	PolyFont_DrawLine	glyph actors; render at 0x46575e with origin/base zeroed
00464e40	PolyFont_DrawLineForced640	forces back 640x480 (imm16 0x4651b4/0x4651c0); render 0x4651dc
00466000	PolyFont_DrawWrapped	-> 0x4638b0 -> 0x464e40
004a0080	ShowMessageScreen	clear, wrapped text 64..576, flip, 3 s wait
0047b990	ShowLoadingPixelmap	blit at 0,0 via 0x47ba42, flip
0043f5c0	ShowRaceLoadingScreen	-> 0x47b990
0043e770	PlaySmackerFile	16-bit: 64x64 tiles on 128-px quads, 2D camera, 0x5226d0 at 0x43ef64
0044bac0	Draw2DIcon	2D camera quad at (x,-y), 0x522f30 at 0x44bb5d
005191b0	DRPixelmapRectangleCopy	fastcall dst,dx,dy,src,sx,sy,w,h -> 0x538590
00538590	BrPixelmapRectangleCopy
00538430	BrPixelmapRectangleCopyDispatch	device +0x84/+0x88/+0x8c
005382f0	BrPixelmapFill	device +0x60 (no-op on D3D)
00538640	BrPixelmapRectangleFill	device +0x9c (clip-only stub on D3D)
00537d60	BrPixelmapAllocateSub	cdecl pm,x,y,w,h
0053dbb0	PixelmapRectangleClip	adds origin_x/y (BRPMAP1 export)
0053dc80	PixelmapRectangleClipTwo	adds dst and src origin (BRPMAP1 export)
00522f30	BrZbSceneRender2	calls 0x522c80 at 0x522f55
00522c80	BrZbSceneRenderBegin	NDC offset from origin at 0x522ca3
005226d0	BrZbSceneRender	same origin maths
0051e520	CameraToScreenMatrix	parallel: scale 2/(w*aspect), post-translate by 0x79f4c4
0079f4c4	gVp_ndc_offset_x	(origin_x - w/2)/(w/2)
0051c900	PDGetMousePosition	GetCursorPos+ScreenToClient; only caller 0x483c10
00483c10	GetMousePosition	clamps to graf spec 0..640 / 0..480 inclusive
0051db00	PDGetDoubleClickPos	raw client pos from WndProc
004824c0	GetMouseButton	2 = double-click only if raw dbl pos == mapped pos
006571f8	gMouse_x	+4 y
00494570	RaceOptionsMenu	FE_MainLoop(1) from MainGameLoop 0x4945a6
00686f94	gFE_camera	parallel, 640.0 at 0x46cf4f
00688b10	gFE_bar_camera	parallel, 640.0 at 0x469d5f / 0x46f7f9
00688af0	gFE_preview_camera	perspective, aspect 1.6346
007635e4	gFE_preview_pm	sub of back at 65,55 510x312
00763940	gFE_bar_models	8 quads
0074cf10	gPolyFont_string_root
0058fddc	gCrosshair_on	static 1
0058fde0	gCrosshair_thickness	5
0074ca28	gNo_cursor_save	1 on HW
0065574c	sVersion	"v1.02a"
10005b10	D3D_SceneControl	case 4 Flip then case 5 Clear(full mode, target+Z)
100018c0	D3D_PixelmapFillStub	xor eax,eax; ret
100018a0	D3D_PixelmapRectangleFill	clip only
10001c90	D3D_PixelmapAllocateSub	base += clipped (x+origin)
```
