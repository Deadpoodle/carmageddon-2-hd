# Sharper menu/HUD textures: where to substitute enlarged pixelmaps (CARMA2_HW.EXE + D3D.BDD, Steam build)

Static analysis only: the Ghidra caches in `re/ghidra-export/` and `re/ghidra-export-d3dbdd/`, checked against the instructions with capstone/pefile on `testrun/CARMA2_HW.EXE` and `testrun/d3d.bdd` (identical to `d3d.bdd.orig`). The font and HUD data was decoded from `testrun/data`. Tags: **H** = read in the instructions or the data; **M** = decompiler output or a strong inference; **L** = a guess to check in play. Addresses are VAs (exe base 0x400000, driver base 0x10000000).

## 0. Summary

- **Do not enlarge pixelmaps in place.** The engine keeps using their pixel sizes for UVs (image buttons), for layout (armour bars, car-icon packing) and for CPU blits. Keep every original `br_pixelmap` P exactly as it is. Give each texture a **twin** T (P enlarged k×), and hand T to the renderer only, inside **BrMapUpdate 0x523160**. The driver's texture object then holds T's pixels and size, while every material still points at P (H for each step; §3).
- **UVs never change.** Poly-font pages use a fixed /64 (0x58a040 = 0.015625). HUD quads use quad-table w/pow2(w). Buttons use image w/pow2(image w), read from P. D3D tu/tv are copied straight from BRender's normalised U/V (0x100076e9). The driver stretches any texture to fill its cache slot (H).
- **Size cap.** The driver refuses textures wider or taller than **256** (0x100081fb/0x10008205). Its texture cache only has slots up to **128×128**, and there are just 12 of those. Anything bigger is halved until it fits (0x10003bd0). To get past 2× you have to patch the slot table in d3d.bdd (§4, H).
- **Poly fonts.** Every font's glyphs are packed into shared **64×64 ARGB4444 pages** (the pool at 0x76c960). Car icons share the same pool. Glyph advance widths come from the source glyph pixelmaps, never from the page (H). So the fonts become sharper if the page twins are filled glyph by glyph through **0x515690**.
- **Backdrops** (640×480 RGB565) are **CPU blits into the 640×480 back buffer** (0x46d996 → 0x5191b0), not quads. An enlarged backdrop would not show. It would first need a textured-quad path made of 256² tiles (H for the blit; §6).
- **Filtering.** The HUD and font materials have no 0x20000 (map interpolation) flag, so the driver samples them **point-filtered** (M). A 2–4× xBRZ twin should therefore be a clear improvement.

## 1. Poly fonts (InitPolyFonts 0x463d40, loader 0x4643f0)

- **Loader signature** (H, call sites 0x4642e2..0x46439d): `void __fastcall PolyFont_LoadFontFile(const char* name, int cell, int index)`, with ecx=name, edx=cell size and index on the stack.
  - The wrapper `0x464090(ecx=index, edx=name, push float, push cell)` is used for SERP38 (index 9) and HAND15lo (0x1a).
- **Fonts and cell sizes** (H):

  | Index | Font | Cell |
  |---|---|---|
  | 0, 2 | HAND15U | 16 |
  | 1, 3, 0x1a | HAND15 (0x1a: HAND15lo) | 16 |
  | 10, 11 | HAND10U, HAND10 | 16 |
  | 4 | SERP15 | 16 |
  | 5, 7 | SERP30 | 32 |
  | 6 | SERP30U | 32 |
  | 8 / 9 | SERP38U / SERP38 | 64 |
  | 0xc–0xf | TINYFONT | 8 |
  | 0x10 | BIGYELLOWTIMER | 32 |
  | 0x11–0x14 | MEDIUM_HEADUP | 16 |
  | 0x15–0x18 | BIG_ITALIC | 32 |
  | 0x19 | NETPOSFONT | 16 |

  - The front-end set is loaded lazily by 0x4642d0. The in-race set is loaded inside InitPolyFonts.
- **Files** (H):
  - The path is `data/<graf dir>/FONTS/<name>`, where the graf dir comes from 0x662224[0x762324·13], i.e. `64X48X8`. The `<name>.TWT` is opened through 0x4b4df0.
  - `FONT.TXT` holds five ints: count, inter-character spacing, first char, height, blank width (HAND15: 135/0/31/16/10; SERP30: 133/0/33/30/20).
  - The glyphs are read from **PIXIES.P16 only**: the name is hard-coded in 0x464a70, and PIXIES.P08 is never read.
  - All glyphs are loaded with BrPixelmapLoadMany 0x538ef0 into 0x764f00 (≤1000) and looked up by the name `"%d"` (the char code; "Blank" for BLANK.PIX).
  - Each glyph is **type 0x12 ARGB4444, cell×cell** (16×16, 32×32, 64×64 or 8×8), with alpha in the top nibble. Transparent texels are **0x0FFF** (white at α 0), not 0.
- **Pages** (H, 0x46467d..0x4646da):
  - A page is `BrPixelmapAllocate(type of BLANK, 64, 64, 0, 0)` (0x538d80), followed once by **BrMapAdd 0x51efa0** (call at 0x4646d5).
  - The type must equal the glyph type, or the loader fails with "BLOODY FONTS :(".
  - Glyphs are packed left→right, top→bottom in cell steps. A new page starts when y > 63. The page index is the global 0x68648c (≤0x3ff), and the page table is 0x76c960[1024].
  - Each glyph goes in through `DRPixelmapRectangleCopy 0x515690` (fastcall ecx=page, edx=x; stack y, src, 0, 0, cell, cell; `ret 0x18`), which calls BrPixelmapRectangleCopy 0x538590 (call at 0x464742).
  - **BrMapUpdate(page, 0xfff)** runs after every glyph (0x46475a).
  - The same pool also holds the **car-icon pages**: 0x4973b0 (called from InitPolyFonts before the in-race fonts) allocates at 0x497474, calls BrMapAdd at 0x4974ad, copies at 0x4974d7 through 0x515690, and updates at 0x4974ef.
  - Front-end fonts restart at page index 0x765ea0 on every load and **reuse** the page pixelmaps that already exist; only their glyph cells are rewritten (0x4642d0, 0x4640f0).
- **Per-glyph record** (H): 0x76d9c4 + font·0x1c68 + ch·0x1c holds +0 String Model, +4 cached material, +8 present, +0xc page index, +0x10 advance width, +0x14 u0 = x·(1/64), +0x18 v0 = y·(1/64).
  - Font header (at 0x76d960 + font·0x1c68): +0x44 count, +0x48 spacing, +0x4c first char, +0x50 height, +0x54 blank width, +0x58 cell, +0x60 loaded. 0x76f5c4 (header + 0x1c64) is the head of the font's String Model list.
- **Width** (H, 0x4647a7..0x464800): the loader scans the **source glyph pixelmap** (pixels +8, row_bytes +0x28, w +0x34, h +0x36) from the rightmost column leftwards. The width is the first column holding any non-zero u16, plus 1; an empty glyph gets width 1.
  - The transparent 0x0FFF margin counts as non-zero, so widths include the 1-px spacing column. With the font data: 'H' = 8, 'A' = 9, 'W' = 14; an alpha-only test would give one less.
  - Text measuring (0x465ca0) and drawing (0x465380) use only these stored widths, plus spacing (0x76d9a8) and the blank width (0x76d9b4). **The page is never read back** (xrefs of 0x76c960 and 0x76d9d8/dc, H).
- **Quads and UVs** (H):
  - Each font has one "String Model" per cell size: 4 vertices at (0,0), (cell,0), (0,−cell), (cell,−cell), z −1.2, map coordinates 0..1 (0x464841..).
  - Each character actor (0x74cae0[n], 256 actors under gString_root_actor 0x74cf10) sits at (x, −y, −1.1).
  - The material is taken from a ring of 80 "Character Material"s at 0x686340, made in InitPolyFonts with flags 0x2806 (imm at 0x463d5d) and in 0x464cf0 (imm at 0x464d14). The pick is done in 0x465250:
    - colour_map = page;
    - map_transform = BrMatrix23Scale(cell/64, cell/64) (0x534c50);
    - translation (+0x34/+0x38) = (u0, v0);
    - then BrMaterialUpdate 0x520e70(…, 0x7fff).
  - So **UVs are fractions of a 64×64 page**, scaled by a constant and not by the page's real size. A page twin scaled k× (glyph cell at k·x, k·y, k·cell) maps 1:1.
- **Texture path** (H): BrMapAdd = registry add (0x79f46c) + BrMapUpdate(pm, 0xfff). BrMapUpdate 0x523160 then does one of two things:
  - if pm+0x40 (`stored`) ≠ 0, it calls `stored->vtbl[0x44]` = driver **0x10008480**. This only marks the texture dirty (+0x68 = 1) and does not re-read the size or pixels;
  - otherwise it calls `renderer->vtbl[0x78](renderer, &pm->stored, 0xa5, pm, tokens)`, which ends in driver **0x100083d0 → 0x100081e0**. That captures the pixel pointer (+0x8c/+0x78), w (+0x18), h (+0x1c), row = bpp·w (+0x14) and the format (types 3, 4, 5, 0x12, 0x1f; 0x12 → format 0xc, alpha).
  - BrMaterialUpdate passes **colour_map->stored** as BRT_COLOUR_MAP_O, at 0x520e70 line `+0x40 of colour_map` (H). So a material draws whatever buffer sits in P->stored.

## 2. HUD art (InitHeadups 0x449090, QueueHUDPanels 0x44b6a0)

All of these are type 0x12 ARGB4444, loaded through `LoadPixelmap 0x48ec00` by "*.tif" name (H, call list in 0x449090). Every texture-side CPU write uses **texture-local pixel coordinates**, never 640×480 coordinates. The only 640×480 pixel drawing goes to gBack_screen (last row of the table).

| Pixelmap (global) | Size | Bound as texture | Written at run time? | Class |
|---|---|---|---|---|
| timerlhs 0x74ca80, timerrhs 0x74ca44 | 64² | timer L/R materials (0x449316 / 0x4493e5) | no | **static**, k=4 |
| hudcurve1/2/3 (0x67c4f8, 0x67fcfc, 0x67f87c) | 64², 32×64, 32² | BrMapAdd only; the globals are never read again (maybe found by name, L) | no | static |
| rev1..rev13 (0x67fd00..0x67fd30) | 128² | statbar1 texture 0x74ca1c, rebound to rev_n by rpm (0x44ac2f). Only rev1 gets BrMapAdd (0x449240); rev2..13 get their `stored` lazily from **BrMapUpdate in 0x44b6b6** | **yes**: the gear digit (0x75bdd4) is masked-copied into the current rev frame (0x44ac9b), plus 0x75bd84 art (0x44ad44). Custom dials copy a rev frame into it (0x44abda) | **dynamic**, k=2 |
| greytop5 0x67fcf4 | 64×128 | not bound; only the copy source | no | source only |
| statbar5 copy 0x74cf00 (BrPixelmapAllocate 0x12, greytop5 size; BrMapAdd 0x4492c1) | 64×128 | statbar5 material 0x74c9fc | **yes, every call**: BrPixelmapCopy 0x517d90 ← greytop5 (0x44aa37), then damage sprites (*DMB.PIX, 10 frames each) masked-copied at local x,y (0x44aab2) | **dynamic**, k=2 |
| Armour area 0x74cf84 (alloc'd to powerbar size; BrMapAdd 0x4491ee); powerbar copies 0x74cab4 / 0x74cf5c (BrMapAdd 0x449206 / 0x44921e) | 128×16 | armour/power/offence materials (0x449830) | **yes, when the value changes**: copy ← powerbar (0x44995a), icon at (2,0), blocks at x = 2 + icon.w + i·block.w + 2, y = 0/5/10. The layout uses the **source widths** (+0x34) | **dynamic**, k=2 |
| icons, blocks, damage sprites, GEARS, SPEEDO0 … | small | never bound | no | blit sources only: no twin needed |
| cockpit instruments (0x44b050), rev needle (0x47c740 from 0x44aff4), dim rects, backdrop/loading blits | — | **gBack_screen 0x74d360** | 640×480 CPU drawing | cannot be enlarged by texture substitution |

- **Update cadence** (H): 0x44b6a0 alternates frames. One frame runs BrMapUpdate(0x74ca1c) and rebinds 0x74cf14; the next runs BrMapUpdate(0x74cf00) and rebinds 0x74c9fc. The bars update only when their value changes (0x449830).
- **The masked blitter** 0x47ba80 (H) is a fastcall: ecx=dst, edx=dx, then stack dy, src, sx, sy, w, h. It copies u16 values ≠ 0 and clips to dst w/h, with no blending.
- **HUD UVs** (H, 0x47f1cd..): u = table w / HUDTexDimW(table w), and the same for v (0x44ba60 / 0x44ba90 round up to pow2 when 0x74cf60 && 0x74ca20). They come from the quad table at 0x655e64.., **never from the texture**.

## 3. Common load path and the substitution point

- **LoadPixelmap 0x48ec00** (H): `br_pixelmap* __fastcall LoadPixelmap(const char* name)`, with ecx = name. It returns P in eax and preserves esi. It has 62 call sites: HUD, front end, car HUD, CARICONS, …
  - The body is 0x48ec20. It returns 0 for "none". It builds `data/<graf>/PIXELMAP/` and either:
    - if 0x68c724 ≠ 0 (a TWT is open, or General.txt's first value is non-zero), loads `PIX16/<name>.P16` through 0x48ea40 → **BrPixelmapLoadMany 0x538ef0**;
    - otherwise (the normal case: General.txt in DATA.TWT says 0) goes through 0x4856c0 → 0x485750. That path picks between `PIX16/*.PIX` (via **BrPixelmapLoad 0x539320**, through the Zlib filesystem) and **converting `TIFFRGB/*.tif`** (0x485fe0 / 0x4864a0), then saves the PIX (0x5393a0).
  - The PIX16 files in testrun are newer than the TIFFs, so the game converted them on its first run (M). It retries in `data/PIXELMAP/`.
  - It then calls the **empty stub 0x5193f0** (`ret` + nop padding; ecx=pm, edx=0) at 0x48ec0c. 0x46abf0 (backdrops) and 0x5024f0 call the same stub. It is a free post-load hook slot (H).
  - **Hook LoadPixelmap, not BrPixelmapLoad**: only LoadPixelmap sees both the PIX and the TIFF paths.
- **Fonts and backdrops do not use it.** They call BrPixelmapLoadMany 0x538ef0 directly (0x464ac3, 0x46ac43). For fonts, only the pages matter anyway.
- **br_pixelmap** (BRender 1.3 layout; size 0x44 from BrResAllocate in 0x539620, H):

  | Offset | Field |
  |---|---|
  | +0 | dispatch (0x58baf0, memory pixelmap) |
  | +4 | identifier |
  | +8 | pixels |
  | +0x10 | map (palette) |
  | +0x28 | row_bytes (s16) |
  | +0x2a | mip_offset |
  | +0x2c | type (u8) |
  | +0x2d | flags |
  | +0x30 / +0x32 | base_x / base_y |
  | +0x34 / +0x36 | width / height (u16) |
  | +0x38 / +0x3a | origin |
  | +0x3c | user |
  | +0x40 | **stored** (the renderer buffer) |

- **Allocator** (H): `BrPixelmapAllocate 0x538d80` (cdecl: type, w, h, pixels=NULL, flags=0) → 0x539620.
  - The struct is a BrResAllocate child of 0x79f950 (class 2); the pixels are a BrResAllocate child of the pixelmap (class 3).
  - For 16-bit pow2 widths, row_bytes = w·2 (tight).
  - Free with `BrPixelmapFree 0x537dc0` (cdecl). Parent the twin with `BrResAdd 0x5278e0(parent P, child T)` (BRCORE1 export table at 0x665e70), so freeing P frees T (M: standard BRender resource semantics).
  - The driver copies source rows using **bpp·w as the stride** and ignores row_bytes (0x100081e0, H), so twins must be tight.
- **The substitution (H for each fact it relies on).** Detour BrMapUpdate (cdecl `void(br_pixelmap*, br_uint_16)`; prologue `83 ec 18 56 8b 35 ec ef 79 00`):
  1. If P is not marked, call the original.
  2. Otherwise, make sure T exists (BrPixelmapAllocate(P.type, k·w, k·h); BrResAdd(P, T); zero-fill).
  3. For *regen* textures, rescale P → T.
  4. Then run `T->stored = P->stored; orig(T, flags); P->stored = T->stored; T->stored = 0`. The original uses its argument only as the `stored` slot and as the pixelmap to wrap, so:
     - a first call builds the driver texture from T's pixels and size;
     - a later call only marks it dirty, and the driver re-reads T's pixels at bind.
  - Materials keep colour_map = P and pick up P->stored. BrMapRemove 0x51efd0 → 0x523280 frees P->stored as before.
  - **Keep T's pixel buffer at the same address for P's whole life**, because the driver keeps the pointer.

## 4. Driver texture limits (D3D.BDD)

- **Hard cap 256** (H): 0x100081e0 compares pm width and height against `mov eax,0x100` (imm at 0x100081ec, file 0x75ec) and returns 0xa005 if larger (0x100081fb / 0x10008205).
  - 0x100083d0 ignores that error and still returns a stored object with no pixel pointer. The bind (0x10003bd0) then returns early (+0x68 == 0), so the quad draws **untextured** (M).
  - **A 512 or 1024 texture therefore fails.** No size or pow2 check was found in the exe. 0x74cf48 = 256 is written at 0x51c416 and never read (M).
- **Cache slots** (H): table at **0x10015898** (file 0x14098), 24-byte entries {batches, work, slots_per_batch, w, h, allocated}, ending at the first entry with slots_per_batch = 0:

  | VA (file) | Size | Batches × per batch | Total slots |
  |---|---|---|---|
  | 0x10015898 (0x14098) | 16² | 4 × 16 | 64 |
  | 0x100158b0 (0x140b0) | 32² | 20 × 8 | 160 |
  | 0x100158c8 (0x140c8) | 64² | 14 × 9 | 126 |
  | 0x100158e0 (0x140e0) | 128² | 2 × 6 | **12** |
  | 0x100158f8 (0x140f8) | 64×128 | 1 × 2 | 2 |
  | 0x10015910 (0x14110) | 128×64 | 1 × 2 | 2 |

  - **Building the slots** (H): 0x10004610 builds them round-robin. Each surface is a DDSCAPS texture made by 0x10004280, plus system-memory staging surfaces per size. The slot array 0x1001dcc0 (stride 0x34) has room for 2048 slots before 0x10037cc0, and there is no count check.
  - **Square-only devices** (caps bit 0x20): non-square classes are disabled and the 16² batch is doubled.
  - **The first CreateSurface failure stops all further allocation** (`bVar2`).
  - There is no free space after the table: strings start at 0x10015940.
- **Fitting a texture to a slot** (H, 0x10003bd0):
  - w and h are each raised to at least 16 (the shift 0x1001bbd0 is a static 0).
  - Score 10 = exact size; 9 = same w with a taller slot; 8 = same h with a wider slot. If nothing scores, w and h are **halved** and the search repeats.
  - The upload (0x10002ba0) copies rows 1:1 when the sizes match, otherwise **nearest-resamples** into the slot (0x10008570).
  - So any texture >128 is stored at ≤128 today. Never remove the 16² class: a 16² texture with no matching class would loop forever.
- **Eviction** (H): LRU per class. If the slot being reused was already used this frame, the driver first sends **D3DRENDERSTATE_FLUSHBATCH** (`SetRenderState(0x32,0)`, vtable +0x5c, at 0x10002ba0). So running short of slots costs speed, not correctness.
- **Formats** (H/M):
  - 16-bit types are copied raw into a slot whose format was chosen at EnumTextureFormats (+0x24, callback 0x100027f0): an alpha format for 0x12/0x1f, a non-alpha one for 3/4/5.
  - If the device's alpha format is not 4444 (0x1001dc60 ≠ 0x10), 0x1001bbcc = 1 and 4444 is converted in place (to 1555 with dithered alpha, M).
  - 8-bit indexed is only handled for 64×64 (palette LUT 0x1001b070).
- **Filtering** (H for the code, M for the mapping):
  - The driver sets TEXTUREMAG/MIN to LINEAR only if Flags bit 0 is set (static default 0xffffffff at 0x10015890; the registry `Flags` value can override) **and** state +0x24 ∉ {1, 0x92} (0x10005f62).
  - +0x24 is token 0x91 (template at 0x10017480). BrMaterialUpdate sets it to 0x93 only when material flag **0x20000** is set (0x520e70).
  - HUD (0x2800; imms at 0x47ef77…0x480e0b and 0x46c741) and font (0x2806) materials lack it, so they are **point-sampled**.
- **Recommended table patch** (H for the offsets; values are a suggestion):
  - 128² class: batches 2 → 0x10. The byte at file 0x140e0 becomes 0x10, giving 96 slots.
  - 64×128 entry → **256×256** (96 slots, ~12 MB): file 0x140f8 = 0x10 (batches), 0x14100 = 6 (per batch), 0x14104 = 0x100 (w), 0x14108 = 0x100 (h). Leave 128×64 alone.
  - Fallbacks still work: 64×128 → 128² (score 8); 128×256 and 256×32 → 256² (scores 8/9).
  - dgVoodoo must accept 256² textures and enough VRAM (L, but small).
  - Beyond 256, also raise the imm at file 0x75ec and add a class (M).

## 5. Read-backs that rule out enlarging in place

- **Glyph width scan**: reads the source glyph pixelmap, not the page (H). Safe either way.
- **Image buttons** (FE_CreateButton 0x46c5c0) copy the menu image twice (BrPixelmapAllocate + BrPixelmapCopy 0x538880), BrMapAdd the copy (**0x46c6cc**), and compute **u = w / pow2(image.w)** at 0x46c884/0x46c897 (H). Enlarging the image in place would show only its top-left 1/k.
- **Armour/power/offence composite** (0x449830): the block positions come from the source widths plus the constants 2 and 5 (H), so enlarged sources would misplace the blocks.
- **Car icons** (0x4973b0): page packing uses CARICONS +0x34/+0x36 and the icon height (H).
- **Masked/rect/whole copies** (0x47ba80, 0x538590, 0x517d90): texture-local pixel coordinates (H).
- **Driver shadow-buffer path** (gNo2DDuring3D 0x1001bb9c = 1 only): reads stored pixels in texel units using the stored w/h (0x10007140). It stays consistent with a twin, and patch 6 of `re/d3d-driver.md` disables the path (M).
- No pixel-alpha hit tests were found: menu hit tests use item rects (`re/hd-tab.md`, H). Collision and other alpha reads were not swept exhaustively (M).
- **Rule:** all CPU work stays on P at the original size. Only the uploaded T is bigger.

## 6. Recipe

1. **Mark** (decide k = 4 if 4·w and 4·h ≤ cap, else 2; enlarge only pow2 types 0x12/5; skip ≤8 px):
   - `LoadPixelmap 0x48ec00` detour or call-site redirects: names rev1..13, timerlhs/rhs, hudcurve1..3, powerbar, the CarImage tiles (64² ARGB4444, `INTRFACE/CarImage/*CI.TWT`) and the MENUIMAGES names → mark P (a mark only takes effect when the pixelmap reaches BrMapUpdate).
   - Redirect `call 0x51efa0` at **0x4646d5** (font page) and **0x4974ad** (car-icon page) → mark P *incremental* and create T before BrMapAdd.
   - Redirect `call 0x51efa0` at **0x46c6cc** (button copy), **0x4491ee** (Armour area) and **0x4492c1** (statbar5 copy) → mark P *regen*.
2. **Incremental pages:** detour `0x515690` (2 callers, 0x464742 and 0x4974d7). After the original copy, if dst is marked, scale only the source rect (clamped edges, so neighbouring cells never bleed) and write it at (k·dx, k·dy) in T.
3. **BrMapUpdate 0x523160 detour** with the stored swap (§3). Regen textures rescale P → T first:
   - rev + statbar5: one 128²/64×128 → 2× per frame;
   - bars: on change only.
   - Optional: skip the rescale when a checksum of P is unchanged.
4. **BrPixelmapFree 0x537dc0 detour:** drop the registry entry. T goes with P as a resource child. Check w/h/type/pixels on lookup, in case an address is reused.
5. **d3d.bdd:** apply the slot-table patch from §4. Without it, 2× pages (128²) share 12 slots and thrash. 4× (256²) has no class at all and falls back to 128².
6. **Optional:** OR 0x20000 into the HUD/font material flag immediates (§4) for bilinear filtering (L for the look; with D3D WRAP addressing, edges may bleed into the opposite side). Confirmed in play for the power-up icons once dgVoodoo forces filtering: lines down the quad edges; fixed by a half-texel UV inset (src/icons.cpp, 0x4d9a53).
7. **Scaler:** xBRZ (ARGB mode) or ScaleFX for this pixel art; Lanczos only for painted art.
   - Work in straight alpha, keep the RGB of α-0 texels (fonts 0x0FFF white, HUD 0x0004 blue) so nothing fringes, and requantise to 4444 with rounding.
   - xBRZ is GPL-3: check that the patch's licence is compatible (L).
   - Offline pre-scaled PNGs looked up by name can replace runtime scaling for the static set. Pages and regen textures still need a runtime scaler.
- **Budget** (M): ≈460 font pages (in-race ≈136 + front end ≈327; SERP38/38U take 98 one-glyph pages each) plus car-icon pages. That is ≈15 MB of twins at 2× and ≈60 MB at 4×, in a 32-bit process.
- **Backdrops** (H): 640×480 RGB565 (`INTRFACE/Backdrop/*.TWT` PIXIES.P16, e.g. Graph). They are blitted by 0x5191b0 at 0x46d996 (also 0x472bdd for Controls, and 0x47ba42 for LOADSCRN) into gBack_screen, which is app-sized and scaled by dgVoodoo. A Lanczos 2× backdrop **would not show** this way, and 640 > 256 cannot be a texture.
  - Making it show needs a new quad path: 1280×960 cut into 5×4 tiles of 256², drawn with the 2D camera 0x74cac4 via 0x5226d0 (as Smacker playback does at 0x43ef64), before the existing depth clear. Each tile needs a 256² slot.
  - The gain over dgVoodoo's own filtered upscale of the 640×480 buffer would be small, because both interpolate the same 640×480 painting (L). Only worth doing if the backdrop moves to quads anyway, e.g. for widescreen.
- **Risks:**
  - slot thrash without the table patch (correct but slower, H);
  - CreateSurface failure halts slot building (H);
  - non-pow2 k·w lands in small slots (H);
  - per-frame regen cost (M);
  - twin memory at 4× (M);
  - cockpit instruments, the needle, dim rects and backdrops stay at 640×480 (H);
  - dgVoodoo texture caps and filtering overrides (L).

## names.tsv

```
0x004643f0	PolyFont_LoadFontFile	fastcall ecx=name edx=cell; stack index; glyphs from PIXIES.P16 into 64x64 pages
0x00464090	PolyFont_LoadFontFileEx	fastcall ecx=index edx=name; stack float, cell (SERP38, HAND15lo)
0x004642d0	PolyFont_LoadFrontEndFonts	lazy; page index restarts at 0x765ea0
0x00463d40	InitPolyFonts	80 Character Materials 0x686340, char actors 0x74cae0, FontCol, car icons 0x4973b0, in-race fonts 0xc..0x19
0x00464a70	PolyFont_FindGlyphPixelmap	PIXIES.P16 LoadMany into 0x764f00 (load-all flag), find by name
0x00465250	PolyFont_GetGlyphMaterial	ring of 80 materials; map_transform scale cell/64, translate u0,v0
0x00464b80	BuildPageQuadModel	4-vertex model with UVs /64 (car icons)
0x00464cf0	PolyFont_NewCharMaterial	flags 0x2806, colour_map page
0x004640f0	PolyFont_UnloadFrontEndFonts	clears glyph present flags
0x004973b0	LoadCarIconPages	CARICONS strips into 64x64 pages via 0x515690
0x00515690	DRPixelmapRectangleCopy	fastcall dst,dx; stack dy,src,sx,sy,w,h (ret 0x18) -> 0x538590; page writer
0x005191b0	DRPixelmapRectangleCopy2	fastcall dst,dx; stack dy,src,sx,sy,w,h -> 0x538590 (backdrop blit)
0x0047ba80	DRPixelmapMaskedCopy16	fastcall dst,dx; stack dy,src,sx,sy,w,h; copies u16 != 0
0x00517d90	DRPixelmapCopy	fastcall dst,src; same type -> 0x538880, 8->16 via palette
0x0048ec00	LoadPixelmap	fastcall ecx=name -> pm; then stub 0x5193f0
0x0048ec20	LoadPixelmapInner	PIX16 (0x48ea40) or TIFF/PIX check (0x4856c0)
0x0048ea40	LoadPixelmapsPIX	LoadMany on <dir>.P16/.P08 then PIX16/PIX8 <name>.PIX
0x004856c0	LoadPixelmapTIFFOrPIX	-> 0x485750; sets identifier
0x00485750	ConvertOrLoadPixelmap	PIX if newer than TIF, else convert TIFFRGB/TIFFX and save PIX
0x0048eb80	LoadPixelmapByPath	used by 0x513870 / material loaders
0x005193f0	PixelmapPostLoadStub	empty ret; ecx=pm; free hook slot
0x00538d80	BrPixelmapAllocate	cdecl type,w,h,pixels,flags
0x00539620	BrPixelmapAllocateMem	struct 0x44 bytes res class 2, pixels child class 3
0x00537dc0	BrPixelmapFree	cdecl pm
0x00538590	BrPixelmapRectangleCopy	cdecl dst,dx,dy,src,sx,sy,w,h
0x00538880	BrPixelmapCopy	cdecl dst,src
0x00538ef0	BrPixelmapLoadMany	cdecl name,pms,max
0x00539320	BrPixelmapLoad	cdecl name
0x005393a0	BrPixelmapSave	cdecl name,pm
0x0051efa0	BrMapAdd	registry 0x79f46c + BrMapUpdate(pm,0xfff)
0x0051efd0	BrMapRemove	frees pm->stored, registry remove
0x00523160	BrMapUpdate	cdecl pm,flags; stored update (dirty) or renderer bufferStoredNew(&pm->stored)
0x00523280	BrBufferStoredFree	pm->stored->free, clear
0x005278e0	BrResAdd	cdecl parent,child
0x00449830	HUD_DrawBar	fastcall value,type 'A'/'P'/'O'; composites into 0x74cf84/0x74cab4/0x74cf5c
0x0044a9d0	HUD_DrawDamageDiagram	copy greytop5 -> 0x74cf00, masked damage sprites
0x0044aad0	HUD_DrawRevCounter	rebinds 0x74ca1c to rev_n; gear digit into it; needle 0x47c740 to back screen
0x0044b050	HUD_DrawCockpitInstruments	masked blits to gBack_screen (cockpit view)
0x0046c5c0	FE_CreateButton	copies menu image, BrMapAdd copy at 0x46c6cc, u = w/pow2(img.w)
0x0046abf0	FE_LoadBackdropPixelmap	LoadMany PIXIES.P16, 640x480 RGB565
0x00686340	g_char_materials	80 x material*
0x0076c960	g_texture_pages	1024 x 64x64 pages (fonts + car icons)
0x0068648c	g_texture_page_next	
0x00765ea0	g_texture_page_fe_base	first page for front-end fonts
0x0076d960	g_poly_fonts	stride 0x1c68; glyphs at +0x64 stride 0x1c
0x0058a040	k_one_over_64	0.015625 page UV scale
0x0067fd00	g_rev_pixelmaps	rev1..rev13 (128x128 ARGB4444)
0x0074ca1c	g_statbar1_texture	current rev frame
0x0074cf00	g_statbar5_texture	runtime copy of greytop5 + damage sprites
0x0067fcf4	g_greytop5	64x128 source
0x0074cf84	g_armour_area_texture	BrPixelmapAllocate(0x12, powerbar size)
0x0068c724	g_load_pix_directly	General.txt first value / TWT open; 0 = TIFF/PIX check path
0x10015898	gTexCacheClasses	{batches,work,per_batch,w,h,allocated} x6
0x100081e0	D3D_BufferStoredSet	rejects w/h > 256 (imm 0x100081ec)
0x100083d0	D3D_BufferStoredNew	token 0xa4/0xa5
0x10008480	D3D_BufferStoredUpdate	marks dirty only
0x10003bd0	D3D_TextureCacheBind	class score 10/9/8, halve until a class fits, LRU
0x10002ba0	D3D_TextureUpload	FLUSHBATCH if slot used this frame; raw copy or nearest resample
0x10008570	D3D_ResampleNearest16	
0x10004280	D3D_TextureCacheAllocClass	creates per_batch slot surfaces (+ staging)
0x10017480	D3D_StateToken91	token 0x91 -> state +0x24 (filter: 1/0x92 nearest)
```
