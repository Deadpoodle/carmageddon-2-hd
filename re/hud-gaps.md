# Filling the top-banner gaps in the wide HUD

Scope: the two gaps the wide layout opens in the in-race top banner (src/wide.cpp, re/hud-layout.md §3.1): 270+L..270+C (statbar3 → timer left) and 398+C..398+R (timer right → statbar right). At 854 with L = 0: 270..377 and 505..612. Exe CARMA2_HW.EXE, base 0x400000. Findings rated H (confirmed in the instructions), M (decompiler or a consistent reading), L (inferred).

**Short answer:** statbar3 and statbar right are untextured, flat-colour translucent rectangles. Making them wider fills both gaps exactly. That takes three integer edits in the quad table before Init2DHUD runs, with no new actors and no per-frame hook. Their visibility already follows the F1 HUD detail level.

## 1. How Init2DHUD 0x47e720 builds the panels

- **Allocators (H):** each panel is built from the BRender calls `BrModelAllocate` 0x5217f0 (cdecl: name, 4 vertices, 2 faces; model is 0x5c bytes, vertices and faces are 0x28 bytes each), `BrMaterialAllocate` 0x51f240 (cdecl: name; copies the default material 0x79f49c, 0x9c bytes) and `BrActorAllocate` 0x51e1a0 (cdecl: type 1 = model, 0). Then:
  - actor +0x14 = name, +0x18 = model, +0x1c = material;
  - faces are (0,1,2) and (2,3,0);
  - model +0x20 |= 2 (keep original);
  - `BrModelAdd` 0x51ed00 and `BrMaterialAdd` 0x51ee50 are called.
- **Material (H):** +8 colour, +0x20 flags = 0x2800 (always visible and force front, unlit), +0x40 colour_map, +0x58 extra_prim token list.
- **Geometry (H for the table reads, M for the vertex layout):** the vertices are pixel coordinates with y negated and z = -2.0:
  - v0 = (x, -y), v1 = (x, -y-h), v2 = (x+w, -y-h), v3 = (x+w, -y);
  - the UVs are 0..w/pow2(w) and 0..h/pow2(h). 0x44ba60 and 0x44ba90 round up to a power of two only when 0x74cf60 and 0x74ca20 are both set.
  - The positions are written at the end of 0x47e720, followed by `BrModelUpdate` 0x51f950(model, 1).
  - The table is laid out as w, h, x, y: statbar3 is 0x655eac/0x655eb0/0x655eb4/0x655eb8, and statbar right is 0x655eec/0x655ef0/0x655ef4/0x655ef8. Their only readers are 0x47fc16..0x4810d5 and 0x481367..0x4813cd, all inside 0x47e720 (xrefs, H).
- **Values (H):**

| Quad | w x h at x, y (640) | Material colour | extra_prim | Texture |
|---|---|---|---|---|
| gStatbarHUD3 (actor 0x74ca14, model 0x74cf44, mat 0x74cacc) | 142 x 38 at 128, 10 | 0x040e4a | 0x6569e0 | **none** |
| gStatbarRightHUD (0x74ca58, 0x74cf6c, 0x74ca8c) | 178 x 38 at 398, 10 | 0x040e4a | 0x6569e0 | **none** (and no UVs are set) |
| gTimerLeftHUD (0x74ca40, 0x74cf88, 0x74cf64) | 64 x 64 at 270, 0 | 0x969696 | none | timerlhs (0x74ca80, set in InitHeadups 0x449090) |
| gTimerRightHUD (0x74ca24, 0x74cad8, 0x74cac0) | 64 x 64 at 334, 0 | 0x969696 | none | timerrhs (0x74ca44) |
| gStatbarHUD1 / gStatbarHUD5 | 128 x 128 at 0, 0 / 64 x 128 at 576, 0 | 0x969696 | none | rev1 (0x74ca1c) / a copy of greytop5 (0x74cf00), rebound every frame in 0x44b6a0 |

- **Token lists (H):** 0x6569e0 is {BLEND_B (0x85) = 1, OPACITY_X (0xbe) = 0x700000, which is 112.0 of 255}. 0x6569c8 is the same list with OPACITY_X = 128.0. I identified the token ids from BRender's token-name table: BLEND_B entry at 0x668268, OPACITY_X entry at 0x669918, OPACITY_F (0xbf) entry at 0x6698e8.
- **Translucency of the textured panels (H):** 0x51f630 treats a colour_map of type 0x12 as blended, and the HUD textures are type 0x12 (ARGB4444).
- **HUD root and camera (H):** 0x47e560 creates root 0x74ca00 (`BrActorAllocate` with type 0) and HUD_Camera 0x74cf74. The camera is parallel, with hither 1 and yon 3, and its size equals gBack_screen.
- **Per-frame queue (H):** 0x44b6a0 queues actors with 0x4e5ad0 (fastcall, ecx = actor, no stack arguments). The queue is the array at 0x704e60, its count is 0x703e28, and it holds at most 0x80 entries ("Not enough HUD actor storage").
- **Order in full detail, single player (H):** statbar1, statbar5, timer L, timer R, armour, power, offence, **statbar right (0x44b90b), statbar3 (0x44b916)**, then gHeadup when detail > 2.
- **Render (H):** RenderHUDModels 0x4e5b00 does the following each frame:
  1. Adds every queued actor that is not already linked (actor +4 == 0) under 0x74ca00 with `BrActorAdd` 0x51dfe0.
  2. Zeroes gRender_screen 0x762128 origin and base (+0x30/+0x32/+0x38/+0x3a).
  3. Calls 0x5226d0(root 0x74ca00, camera 0x74cf74, gRender_screen, depth 0x68b8a4).
  4. Restores the screen fields, detaches the actors with `BrActorRemove` 0x51e070 and clears the count.
- **Frame order in 0x4e4e40 (H for order, L for the meaning of the gates):** 0x4496f0 (dim areas) → 0x44a9d0 → DoHeadups 0x449b10 → 0x44aad0 → 0x4dad00 (ticker) → **0x44b6a0 at 0x4e543d** → … → **0x4e5b00 at 0x4e548b**.
  - The gates are 0x75b9a4 (compared at 0x4e53d2 and 0x4e5423) and the replay flags 0x676914/0x6a2358.
  - The glyphs sit at z -1.1, in front of the quads at -2.0, so text stays on top (M).
- **Helper to reuse (H):** there is no single "make a panel" function; 0x47e720 inlines everything. A cloned quad is the allocator sequence above plus `BrModelAdd`, queued each frame with 0x4e5ad0. The simplest clone shares material 0x74cacc. Widening the existing quads (section 4) avoids all of this.

## 2. Textures and band columns

- **Files (H):** the textures are `data\64x48x8\PIXELMAP\PIX16\timerlhs.PIX` and `timerrhs.PIX` (loaded by name timerlhs.tif and timerrhs.tif through 0x48ec00 at 0x449300 and 0x4493cf), `rev1.PIX` and `greytop5.PIX`.
  - They are type 0x12 ARGB4444, big-endian 16-bit, with an empty name; tools/pix.py does not decode this type, so the PNGs come from a small local decoder.
  - Sizes: timers 64x64, rev1 128x128, greytop5 64x128.
- **Statbar3 and statbar right (H):** they have no pixelmap. They are a uniform flat colour RGB (4, 14, 74) at opacity 112/255 (about 0.44), and their ends are square (the quad edges).
- **Band in the textured panels (H):** every texture in the banner carries the same band at rows 10..47, which matches y 10, h 38 of the flat quads.
  - The band pixel is 0x6004: RGB (0, 0, 68) at alpha 6/15 (0.40). Rows 0..9 and 48..63 are 0x0004 (fully transparent).
  - The band is uniform apart from the ghost "88:88" digit outlines (alpha 7..0xb).
- **Timer left (H):** columns 0 and 1 are plain band. Only column 1 is completely clean; column 0 has one stray 0x7004 pixel. The left edge is square, not slanted.
- **Timer right (H):** columns 57, 59, 60, 61 and 62 are plain. Column 63 has 0x7004 in its last 4 band rows. The right edge is square.
- **The curved/rounded ends are elsewhere (H):** they come from rev1 (the rev-counter arc, whose band reaches column 127, square) and from the greytop5 rounded box (band from column 0). So the band's left end is curved and its right end rounded; no panel next to a gap has a slanted end.
- **Translucency source (H):** the textured panels get it from texture alpha (ARGB4444); the flat panels from the material's BLEND_B + OPACITY_X; key colours are not used.
- **Colour match (M):**
  - Flat over black ≈ (2, 6, 33) and texture over black ≈ (0, 0, 27); over white ≈ (145, 151, 176) versus (153, 153, 180).
  - The original 640 layout already shows this slight seam at x 270 and 398.
  - Whether the D3D driver multiplies textures by the 0x969696 material colour is L (not traced in the driver).

## 3. Dim rectangle 0x47cad0 and its caller

- **Calling convention (H):** 0x47cad0 is fastcall. ecx is ignored (the caller passes the pixelmap), edx = left, and there are **four** stack arguments: top, right, bottom and an unused flag; the callee pops 0x10.
  - It writes the gDim model 0x74ca70 vertices, then calls `BrModelUpdate`(model, 1).
  - It sets gDim actor 0x74cf24 render style +0x20 to 4 (faces) and attaches it to the 2D camera 0x74cac4 if it is unlinked.
  - It renders with 0x522f30(0x74cac4, 0x74cac4, gBack_screen 0x74d360, depth 0x68b8a4), then detaches the actor and sets the style back to 1.
- **Caller (H):** 0x47cbd0 (cdecl: pixelmap, left, top, right, bottom, border flag) calls it only when 0x74cf60 is set (the hardware path). Otherwise it remaps 8-bit pixels through a shade table.
  - 0x47cbd0 is reached from 0x4496f0 (car dim areas; none in C2), from 0x4dad00 (the ticker box), from 0x444c00 (network) and through 0x47cce0 (full map, 0x496d6f).
- **Material (H):** gDim material 0x74cad4 has colour 0 (black), flags 0x2800 and extra_prim 0x6569c8 (blend at opacity 128, so 50% black).
- **Changing the colour per call (M):** this is only possible by rewriting material +8 and the OPACITY_X value at 0x6569d4, then re-preparing the material with 0x520e70, and restoring both afterwards.
- **Verdict:** it would not match the panels. It draws black at 50% in a separate immediate scene render, at a different point in the frame from the HUD queue.

## 4. Recommendation

**Use option A, simplified: widen statbar3 and move and widen statbar right.** Both are textureless flat quads, so stretching them cannot distort anything, and the fill uses exactly their colour and opacity. It renders in the same pass, at the same depth and in the same queue slot as the panels, so it sits behind the text like they do.

In `PatchLayout` (it already runs before Init2DHUD's call at 0x47dfd4), with L, C, R as in src/wide.cpp:

| Address | Field | 640 value | New value |
|---|---|---|---|
| 0x655eb4 | statbar3 x | 128 | 128 + L (unchanged from today) |
| **0x655eac** | statbar3 w | 142 | **142 + (C - L)** → right edge 270 + C |
| **0x655ef4** | statbar right x | 398 | **398 + C** (instead of today's + R) |
| **0x655eec** | statbar right w | 178 | **178 + (R - C)** → right edge 576 + R |

- **Result:** the banner is continuous: rev1 0..128, statbar3 128..270+C, timers 270+C..398+C, statbar right 398+C..576+R, statbar5 576+R..640+R. The joins are the same as at 640.
- **Text:** HEADUP slots 2 and 12 (x 416 + R) still sit on the right panel.
- **Detail modes (H):** nothing extra is needed, because the fill is the panels themselves.
  - Full detail (detail 0x655e54 % 3 = 2, single player): both are queued (0x44b90b and 0x44b916).
  - Compact (% 3 = 1): neither is queued. Statbar1 and statbar5 are translated against the timer (0x44b885 and 0x44b89f in today's patch), so there is no gap.
  - Minimal (% 3 = 0): none of the top-banner quads are queued.
  - Network (0x68b918 ≠ 0): the timers, statbar3 and statbar right are never queued.
- **If the panel quads must keep their original size (fallback A):**
  - At init (after 0x47e720 returns): build two actors exactly like statbar3 (model 4/2, the same faces and vertex layout, z -2, model flags |= 2, `BrModelAdd`), sharing material 0x74cacc.
  - Per frame: queue them with 0x4e5ad0 from a wrapper around 0x44b6a0 (its only call is at 0x4e543d), only when 0x68b918 == 0 and 0x655e54 % 3 == 2. That is the same condition as the panels.
  - Make each filler run exactly from the neighbouring panel edge to the next one; overlapping translucent quads double the darkening.
- **Not recommended:** option B (0x47cad0). Its colour and opacity are wrong, it is an immediate separate render, and the material would need patching on every call.

## Images (scratch folder c2hd_hudgap)

- timerlhs_rgba.png, timerrhs_rgba.png, rev1_rgba.png, greytop5_rgba.png: decoded ARGB4444 textures with alpha.
- timerlhs_on_checker.png, timerrhs_on_checker.png, rev1_on_checker.png, greytop5_on_checker.png: the same textures composited over a checkerboard.
- banner_640_original.png, banner_854_gaps.png, banner_854_widened.png: mock-ups of the top banner built from the textures and the flat quads; the last one shows the recommendation.

## names.tsv

```
0x0047e560	InitHUDRootAndCamera	creates HUD root 0x74ca00 and HUD_Camera 0x74cf74
0x0047e720	Init2DHUD	allocates all HUD quad models/materials/actors, sets geometry from 0x655e64 table
0x00449090	InitHeadups	loads HUD pixelmaps (timerlhs/timerrhs etc.) and binds timer/statbar5 textures
0x0044b6a0	QueueHUDPanels	per-frame: binds statbar1/5 textures, translates and queues panel actors by detail level
0x004e5ad0	QueueHUDActor	fastcall ecx=actor; appends to 0x704e60[0x703e28], max 0x80
0x004e5b00	RenderHUDModels	links queued actors under 0x74ca00, renders via HUD_Camera into gRender_screen, unlinks
0x004e4e40	RenderAFrame	frame render; calls 0x44b6a0 at 0x4e543d, 0x4e5b00 at 0x4e548b
0x0047cad0	DimRectangle2D	fastcall edx=left; stack top,right,bottom,flag (ret 0x10); gDim 50% black via 2D camera
0x0047cbd0	DimRectangle	cdecl pm,l,t,r,b,border; HW -> 0x47cad0, else 8-bit shade remap
0x0047cce0	DimRectangleClipped	clips to pixelmap then 0x47cbd0
0x0044ba60	HUDTexDimW	rounds to power of two when 0x74cf60 && 0x74ca20
0x0044ba90	HUDTexDimH	same for height
0x0048ec00	LoadHUDPixelmap	ecx=name; loads a pixelmap by name
0x005217f0	BrModelAllocate	cdecl name,nverts,nfaces
0x0051f240	BrMaterialAllocate	cdecl name
0x0051e1a0	BrActorAllocate	cdecl type,data
0x0051dfe0	BrActorAdd	cdecl parent,child
0x0051e070	BrActorRemove	cdecl actor
0x0051ed00	BrModelAdd	cdecl model
0x0051ee50	BrMaterialAdd	cdecl material
0x0051f950	BrModelUpdate	cdecl model,flags
0x00520e70	BrMaterialUpdate	cdecl material,flags
0x00523160	BrMapUpdate	cdecl pixelmap,flags
0x0051f630	MaterialIsBlended	alpha colour_map types, index_blend or OPACITY tokens
0x005226d0	HUDSceneRender	root,camera,colour,depth (used by 0x4e5b00)
0x00522f30	SceneRender2D	world,camera,colour,depth (used by 0x47cad0)
0x00532a90	BrMatrix34Translate	mat,x,y,z
0x0074ca00	gHUD_root	
0x0074cf74	gHUD_camera	
0x0074cac4	g2d_camera	
0x0074ca70	gDim_model	
0x0074cad4	gDim_material	colour 0, opacity 128
0x0074cf24	gDim_actor	
0x0074cf44	gStatbarHUD3_model	
0x0074cacc	gStatbarHUD3_material	flat 0x040e4a, opacity 112, no texture
0x0074cf6c	gStatbarRightHUD_model	
0x0074ca8c	gStatbarRightHUD_material	flat 0x040e4a, opacity 112, no texture
0x0074cf88	gTimerLeftHUD_model	
0x0074cf64	gTimerLeftHUD_material	
0x0074cad8	gTimerRightHUD_model	
0x0074cac0	gTimerRightHUD_material	
0x0074ca80	gTimerLeft_texture	timerlhs.PIX, ARGB4444 64x64
0x0074ca44	gTimerRight_texture	timerrhs.PIX, ARGB4444 64x64
0x006569c8	gDim_prim_tokens	BLEND_B=1, OPACITY_X=128.0
0x006569e0	gHUDpanel_prim_tokens	BLEND_B=1, OPACITY_X=112.0 (statbar3, statbar right, headup)
0x00655e54	gHUD_detail	F1 cycles; %3: 0 none, 1 compact, 2 full; >2 adds bottom panel
0x00655eac	gStatbarHUD3_w	142
0x00655eec	gStatbarRightHUD_w	178
0x00704e60	gHUD_actor_queue	
0x00703e28	gHUD_actor_queue_count	
```
