# Smacker cutscenes: playback path and where to swap in an enlarged frame

Scope: `testrun/CARMA2_HW.EXE` (base 0x400000), the movie player `PlaySmackerFile` 0x43e770, and `testrun/d3d.bdd` (= `d3d.bdd.orig`, base 0x10000000; `.data` file offset = VA − 0x10001800). This is static analysis only (capstone on both binaries, plus the Ghidra caches). Nothing was run. Confidence: **H** = read in the instructions, **M** = strong inference, **L** = guess.

## Summary

- **H** The Steam build has two movies, both 320x192, 15 fps, 8-bit palettised, with Smacker audio: `MIX_INTR.SMK` (the intro) and `TOPRANK.SMK` (game complete). `INTERPLAYGO.SMK` is requested but not shipped.
- **H** On HW (16-bit screen), the frame is decoded into a 320x200 8-bit memory buffer. It is cut into **15 INDEX_8 64x64 pixelmaps** (5x3) that share one RGBX palette pixelmap. They are drawn as **15 textured quads of 128x128 screen px** (640x384, rows 48..432) by a model under the 2D camera, through `BrZbSceneRender` at 0x43ef64. The D3D driver turns each 64x64 index texture into RGB565 when it uploads it.
- **H** UVs are normalised (1/128 .. 127/128, a half-texel inset for 64 px). The quads stay the same whatever the texture size, so a bigger texture per tile is the natural swap.
- **H** Driver limits: a stored texture may be at most **256x256**, and the texture cache has **no slot above 128x128**. Only 12 slots are 128x128. A larger texture is **halved with nearest-neighbour sampling** until it fits a slot. So 2x (128 px tiles) works without touching the driver. 4x (256 px tiles) needs a one-entry patch of the driver's slot table.
- **H** dgVoodoo renders D3D at the output resolution (`[DirectX] Resolution = 1920x1080` in the HD config). The 384-row movie area covers about 864 output rows there, which is about **4.5x** the source. So **4x is the useful target**. At a native 854x480 output, 2x is already 1:1.

## Q1. Movie files

| file | size | WxH | frames | rate | length | audio | plays | conf |
|---|---|---|---|---|---|---|---|---|
| `data/CUTSCENE/mix_intr.smk` | 38.4 MB | 320x192 | 2044 | −6666 → 15.0 fps | 136 s | track 0, 11025 Hz, flags 0xd0 (compressed, present, stereo, 8-bit) | intro | H |
| `data/CUTSCENE/toprank.smk` | 15.5 MB | 320x192 | 578 | 15.0 fps | 38.5 s | track 0, 22050 Hz, flags 0xd0 | game complete | H (header), M (when) |

- **H** Both are `SMK2`, header flags 0 (no ring frame, not interlaced or doubled). The palette is 8-bit and can change per frame (Smacker `NewPalette` at handle +0x68, 256×RGB at +0x6c).
- **H** `PlayIntroMovies` 0x43f330 plays `INTERPLAYGO.SMK` (0x58ffc0), then `MIX_INTR.SMK` (0x58ffb0), then `WaitForNoKeys` 0x4833b0. It is called from DoProgram 0x503c50 in state 0 (start-up, 0x503d05) and state 3 (0x503d81).
  - **L** State 3 is set by front-end handlers (0x492950, 0x494540/0x494570, 0x4a0260), probably an attract/replay case.
  - **H** `INTERPLAYGO.SMK` is missing. The player tries the HD path, then the CD path (0x4910d0/0x515950), logs "failed to open" and returns.
- **M** `PlayTopRankMovie` 0x43f390 plays `TOPRANK.SMK` (0x58ffd0), then WaitForNoKeys. Its only caller is the race-result handler 0x4e3440 (call 0x4e34f5, result == 3). It runs when the group pointer passes the last race group, just before `FE_MainLoop` 0x46d8e0. That is the end-of-game "top rank" movie.
- **H** Skip switches: the player returns at once if 0x68b894 (`-NOCUTSCENE(S)`) or 0x68b888 is set. 0x68b888 also gates sound init in 0x455080, and no writer appears in xrefs (L: debug/no-sound flag).

## Q2. The playback loop (0x43e770, fastcall ecx = file name)

Setup (16-bit branch, taken when `gScreen_bits` 0x74ca60 == 16, H):

1. **H** The back-screen type (0x74d360 +0x2c) must be 3, 4 or 5; otherwise it logs "Unexpected backscreen format" and returns.
2. **H** The decode buffer **0x67c3b0** is allocated once with `BrMemAllocate(64000, 0xfd)` (0x43e819). It is never freed, and is 320x200 with pitch 320.
3. **H** Then 0x456910, 0x4b5460 (empty), and `ClearEntireScreen` 0x47b880 (black bars).
4. **H** Smacker set-up: `SmackUseMMX(1)` 0x43e837, then `SmackSoundUseDirectSound(0)` 0x43e83e (Smacker makes its own DirectSound).
5. **H** `SmackOpen(path, 0xfe000 = all tracks, 0x200)` 0x43e88c; on failure it retries with extra-buf −1 (0x43e8a8), then the CD path (0x43e99d/0x43e9b5). The path is `<0x75ba60>\CUTSCENE\<name>`, built by 0x513690.
6. **H** The model actor is `BrActorAllocate(1, 0)` 0x43e9fc → **0x79eca4**, with render_style (+0x20) = 4.
7. **H** Two palette pixelmaps (double-buffered), **0x79eca8** and **0x79ec9c**: `DRPixelmapAllocate` 0x5137d0 (fastcall ecx = 7 RGBX_888, edx = 1; stack 256, 0, 0) → 1x256. `BrMapAdd(0x79eca8)`.
8. **H** `BrModelAllocate("Smack Model", 60 verts, 30 faces)` 0x43ea59.
9. **H** Tiles: 15 materials in **0x79ec60[15]** (end 0x79ec9c). For each:
   - `BrMaterialAllocate("Devious Smack Material")` 0x43eabc.
   - colour_map (+0x40) = `BrPixelmapAllocate(3, 64, 64, 0, 0)` **0x43ead8**, with colour_map->map (+0x10) = 0x79eca8.
   - flags (+0x20) = 0x2800 | 0x20000 | 0x10000. M: ALWAYS_VISIBLE | FORCE_FRONT | map interpolation/antialias.
   - `BrMapAdd(palette)`, `BrMapAdd(tile)`, `BrMaterialAdd`.
10. **H** Mesh (0x43eb49–0x43ed08): row r = 0..2, column c = 0..4.
    - Each quad: x 128c .. 128c+128, y −(176+128r) .. −(48+128r), z −2.0.
    - UVs (u,v): v0 (lo,hi), v1 (hi,hi), v2 (hi,lo), v3 (lo,lo), with lo = 0x3c000000 (1/128) and hi = 0x3f7e0000 (127/128). v is lo at the top edge.
    - Faces (0,3,1) and (1,3,2), material = tile. Vertex and face strides are both 0x28.
    - `BrModelAdd` 0x43ed0f, then model → actor+0x18 and global 0x79ecac.
    - `BrActorAdd(2D camera 0x74cac4, actor)` 0x43ed33.
11. **H** `SmackBufferOpen(hwnd 0x6ad4c8, 0x80, 320, 200, 320, 200)` 0x43ed68. The result is only null-checked: it is never used or closed (leak; the 16-bit path never blits through it).
12. **H** `SmackToBuffer(smk, 0, 0, 320, 200, 0x67c3b0, 0)` 0x43ed98.

Per frame (loop 0x43edb2–0x43efc4; frame counter [esp+0x14] runs from 1 to Frames (smk+0xc), H):

| step | address | what | conf |
|---|---|---|---|
| a | 0x43edb2 | t0 = `PDGetTotalTime` 0x51d410 → [esp+0x30] | H |
| b | 0x43edbf–0x43ee8c | If `smk->NewPalette`: `BrMapRemove`, then swap 0x79eca8/0x79ec9c and write 256 dwords 0x00RRGGBB from smk+0x6c. **Black 0 → 0x040404** (0x43ee0b). Then `BrMapAdd`. Then for every tile: map = new palette, `BrMapUpdate(palette, 0xfff)`, `BrMapUpdate(tile, 0xfff)`, `BrMaterialUpdate(mat, 0x7fff)`. | H |
| c | 0x43ee93 | `SmackDoFrame(smk)` → decodes into 0x67c3b0 | H |
| d | 0x43eea2 | `BrPixelmapFill(depth 0x68b8a4, −1)` (no colour clear) | H |
| e | **0x43eeaa–0x43ef4f** | Copies 64 rows × 64 bytes from buffer + r·0x5000 + c·64 into tile->pixels (+8), then `BrMapUpdate(tile, 0xfff)` 0x43ef04 and `BrMaterialUpdate(mat, 0x7fff)` 0x43ef15, for all 15 tiles | H |
| f | **0x43ef64** | `BrZbSceneRender(0x74cac4, 0x74cac4, gBack_screen 0x74d360, depth 0x68b8a4)`. The HD front-end centring already redirects this call (origin_x += xo). | H |
| g | 0x43ef6e / 0x43ef75 | `PDScreenBufferSwap` 0x51c520 (flip), then 0x51cbe0 (service) | H |
| h | 0x43ef88 | `SmackNextFrame` unless this is the last frame | H |
| i | 0x43ef94–0x43efb4 | Loop: `AnyKeyDown` 0x482d70 (nonzero → abort to 0x43efca), then `SmackWait`. Keep waiting while SmackWait ≠ 0 **and** now < t0 + **0x43 ms** (imm8 at 0x43efb1). | H |

- **H** Cleanup (0x43efca): remove and free the 15 pixelmaps and materials, then the model, actor and palettes. 0x43f211: ClearEntireScreen, then `SmackClose` 0x43f236, then a stats log. The frame/palette counters are only incremented in the 8-bit path, so in HW the log reports 0 frames.
- **H** The non-16-bit branch (never taken in HW) instead does `SmackToBuffer` straight into an 8-bit back screen, or `SmackBlit*` (Open 0x43f06e, SetPalette 0x43f15c, Blit 0x43f1a7 at (0,40), Close 0x43f224).
- **H** smackw32 IAT: NextFrame 0x589274, UseMMX 0x589278, SoundUseDirectSound 0x58927c, Open 0x589280, BufferOpen 0x589284, ToBuffer 0x589288, DoFrame 0x58928c, Wait 0x589290, BlitOpen 0x589294, BlitSetPalette 0x589298, Blit 0x58929c, BlitClose 0x5892a0, Close 0x5892a4.

## Q3. Best substitution point

**Recommended (H for sites, M for the overall result): keep the game's mesh, materials and render; swap the tile pixelmap type/size and the per-frame copy.** Three small changes:

1. **Tile allocation**: redirect `call BrPixelmapAllocate` at **0x43ead8** (`e8 a3 a2 0f 00`; cdecl, 5 args, caller pops 0x14 at 0x43eadf) to a wrapper that returns `BrPixelmapAllocate(5 = RGB_565, 64k, 64k, 0, 0)`, with k = 2 or 4.
   - Setting the colour_map's map to the palette (0x43eaf0, 0x43ee4c) does no harm: the driver ignores the map for type 5 (0x100081e0, case 5).
   - The palette `BrMapAdd/Update` calls stay as they are.
2. **UV inset** to half a texel of the new size. These are the dword immediates; the first six are `c7 44 18 xx imm32` with imm at +4:
   - lo sites: 0x43ebc1, 0x43ec2b, and **0x43ec4b** (`mov edi, imm`, used for both u3 and v3).
   - hi sites: 0x43ebcc, 0x43ebf3, 0x43ebfe, 0x43ec20.
   - Values: k=2 → lo 0x3b800000 (1/256), hi 0x3f7f0000 (255/256). k=4 → lo 0x3b000000 (1/512), hi 0x3f7f8000 (511/512).
   - Leaving them at 1/128 crops k/2 texels per tile edge.
3. **Per-frame upload**: overwrite **0x43eeaa** (`c7 44 24 1c 00 00 00 00 bd 60 ec 79 00`) with `call Movie_UploadFrame` + `jmp 0x43ef50`.
   - **H** No registers are live into 0x43ef50: it reloads from globals, and edi/ebp come back from [esp+0x18]/[esp+0x14] at 0x43ef7a. Both back-edges, 0x43ef34 and 0x43ef4a, lie inside the skipped block.
   - `Movie_UploadFrame(void)` (cdecl, no args) reads:
     - the 8-bit frame: `*(u8**)0x67c3b0`, 320 pitch, 192 rows used;
     - the palette: `*(u32**)(*(int*)0x79eca8 + 8)`, 256 × 0x00RRGGBB, black already 0x040404;
     - the tiles: `mat = ((int*)0x79ec60)[i]`, i = 5r + c, with `pm = *(int*)(mat+0x40)`, `pixels = *(u16**)(pm+8)`, `row_bytes = *(s16*)(pm+0x28)`, `w/h = *(u16*)(pm+0x34/0x36)`.
   - It builds the enlarged image: 8-bit + palette → float RGB, deband/deblock at 320x192, Lanczos-2 to 320k × 192k, CAS, then pack to RGB565. Each tile (c·64k, r·64k) gets its own 64k × 64k block.
   - Then, per tile, it calls `BrMapUpdate(pm, 0xfff)` 0x523160 and `BrMaterialUpdate(mat, 0x7fff)` 0x520e70, both cdecl, 2 args, as the original does.
   - Its palette read happens after `SmackDoFrame`, i.e. the same order the game uses (step b runs before c in the same iteration).

Rules the converter must keep (H from the driver):
- **Never write 0x0000.** The generic upload path sets a source colour key of 0 on the texture (`SetColorKey(DDCKEY_SRCBLT, {0,0})` after Load, 0x10002ba0 tail, when 0x1001bbcc ≠ 0). The game's black → 0x040404 does the same job: in 565 that is 0x0020, so map 0 → 0x0020.
- **Keep each tile's own pixel memory.** The driver keeps the pixmap's pixel pointer (0x100081e0) and reads it when it uploads at draw time (0x10002ba0), not at BrMapUpdate time. Rows are assumed tight: the driver's stride is bytes-per-pixel × width.
- **Pixel format** = the driver's opaque texture format, `*(int*)(drv + 0x3818c)`. It is 4 = RGB565 when the device lists 565 (the enum callback 0x100027f0 keeps the highest of 555 = 2 and 565 = 4); M that dgVoodoo lists 565. If it is 2, pack RGB555 instead. A type-5 texture is copied raw into the surface.

Not recommended: replacing the whole convert/upload path, or building a new mesh. The 15-quad mesh already maps one tile per quad at any k.

## Q4. Texture limits in D3D.BDD

- **H** `bufferStoredNew` 0x100083d0 → 0x100081e0 rejects width or height > 0x100 (error 0xa005). The accepted pixmap types are 3 (INDEX_8, needs a map), 4 (555), 5 (565), 0x12 (4444, alpha) and 0x1f (alpha 1555, M). **There are no 24/32-bit types.**
- **H** INDEX_8 is only converted on the fast path when it is exactly 64x64 (0x10002ba0: LUT at 0x1001b070 built from the palette at each upload). Other index sizes would be misread as 16-bit. So enlarged tiles must be 16-bit.
- **H Slot table `gTexSlotTable` 0x10015898** (stride 24: repeat, remaining, batch, w, h, allocated; zero-batch terminator at 0x10015928). The cache init 0x10004610 (end of `D3D_CreateDisplay`) allocates batch × repeat VRAM textures per size, round-robin, until VRAM fails:
  | VA | file off | size | slots |
  |---|---|---|---|
  | 0x10015898 | 0x14098 | 16x16 | 4×16 = 64 |
  | 0x100158b0 | 0x140b0 | 32x32 | 20×8 = 160 |
  | 0x100158c8 | 0x140c8 | 64x64 | 14×9 = 126 |
  | 0x100158e0 | 0x140e0 | 128x128 | 6×2 = **12** |
  | 0x100158f8 | 0x140f8 | 64x128 | 2 |
  | 0x10015910 | 0x14110 | 128x64 | 2 |
- **H** Lookup `D3D_TextureLoad` 0x10003bd0:
  - It scores each slot: exact size 10, same w and shorter h 9, narrower w and same h 8. With no match it **halves w and h** and retries.
  - The upload then goes through `D3D_ResampleNN` 0x10008570 (nearest neighbour; exact 2x up is a fast path).
  - So a 256x256 texture is silently shrunk to 128x128 today.
  - The global size shift 0x1001bbd0 is never written (0).
- **H** Eviction is LRU by frame stamp. If the slot was already used this frame, the driver first sends `SetRenderState(D3DRENDERSTATE_FLUSHBATCH)` (0x10002ecb), so more tiles than slots stays correct, just with extra uploads.
- **H** Update cost: BrMapUpdate only marks the stored texture dirty (0x10008480 sets +0x68). At draw time each dirty tile is converted or copied into a system-memory temp surface (Lock/Unlock), then `IDirect3DTexture2::Load` copies it into VRAM. Today that is 15 × 64×64 px per frame. At k=4 it is 15 × 256×256 × 2 B ≈ 2 MB copied plus 2 MB Load per frame at 15 fps: negligible.
- Fit:
  - **2x (k=2)**: 15 tiles of 128x128 against 12 slots. It works as is: 3 evictions per frame, which cost nothing since every tile re-uploads each frame anyway (H). Optional tidy-up: entry 0x100158e0 repeat 6 → 8 (file 0x140e0 `06` → `08`) gives 16 slots.
  - **4x (k=4)**: needs 256x256 slots. Patch entry 0x10015910 (file 0x14110) from `01 00 00 00 00 00 00 00 02 00 00 00 80 00 00 00 40 00 00 00` to `08 00 00 00 00 00 00 00 02 00 00 00 00 01 00 00 00 01 00 00`. That gives 16 slots of 256x256 (2 MB VRAM).
    - A 128x64 texture then scores 9 into a 128x128 slot (NN stretch, which looks the same).
    - Patch it in memory at the existing driver-patch point (`PatchDriver` on the DirectDrawCreate resolve, before 0x10004610 runs): image base + 0x15910.
    - M: the driver does not check the device's maximum texture size; dgVoodoo accepts 256.
  - **8x or 512 tiles**: not possible (the 256 stored-texture limit, H).

## Q5. Skip, audio, timing

- **H Skip**: any key or button. `AnyKeyDown` 0x482d70 is polled inside the wait loop (at least once per frame). Abort jumps to cleanup, and the wrapper then calls `WaitForNoKeys` 0x4833b0. In the intro wrapper a key only skips the current file.
- **H Audio**: Smacker's own DirectSound (`SmackSoundUseDirectSound(0)` before SmackOpen, all tracks opened). The game does nothing else with audio. `SmackWait` paces video to Smacker's clock. The loop also caps each frame's wait at 67 ms after t0 (0x43efaf `83 c1 43`).
- **M** There is no frame dropping in this loop. If our per-frame work (step e, plus render) takes longer than about 60 ms, SmackWait returns 0 at once and video runs behind audio until it catches up. Budget roughly < 25 ms for the filter chain on the main thread (1280x768 output at k=4). Do deblock/deband at 320x192, and use separable Lanczos and SIMD (or a worker thread started after SmackDoFrame).
- **H** Keep the step order: palette (b) → DoFrame (c) → our upload (e) → render (f) → swap (g) → NextFrame (h) → wait (i).

## Q6. Recipe, risks, fallback

Recipe (k = 4, or k = 2 without the driver patch):
1. Driver, in memory at `PatchDriver`: the 256x256 slot entry (Q4).
2. Exe: redirect 0x43ead8 (tile allocation: RGB565, 64k), patch the seven UV immediates, and put call + jmp at 0x43eeaa → `Movie_UploadFrame`.
3. `Movie_UploadFrame`:
   - palette → linear RGB;
   - deband (palette-step dither noise, threshold from neighbour deltas);
   - light deblock aligned to the 4x4 Smacker block grid;
   - Lanczos-2 to 1280x768;
   - CAS (sharpness ~0.3);
   - pack to the driver format, with 0 → 0x0020 (565) or 0x0001 (555);
   - write the 15 tiles; BrMapUpdate + BrMaterialUpdate for each.
4. Add an ini switch (e.g. MovieScale=1/2/4); 1 = untouched game code.

Risks:
- **M** At k=4 the new 256 slots also serve game textures of 256 px that are halved today (race and menu textures). They would look sharper but might thrash 16 slots. Mitigation: set the entry's "allocated" field (VA 0x10015924) to 0 after cache init, and restore it only while 0x43e770 runs. The score loop skips entries whose allocated count is 0 (0x10003bd0, `puVar5[2] != 0`).
- **H** If no exact slot exists, the result is silent NN decimation, not a failure. Check the log/driver ("Texture Alloc %d of %dx%d") or read slot sizes at runtime.
- **H** Seams: tiles are separate textures, so there is no filtering across tile edges. With a half-texel inset the edge texel is clamped and nothing bleeds over. At k=4 the step is 1/256 of a tile, i.e. invisible.
- **M** dgVoodoo's forced filtering, anisotropy and autogen mipmaps apply. The tiles are magnified, so LOD 0 and bilinear: harmless.
- **H** The front-end centring hook at 0x43ef64 and the scope thunk at 0x43e770 entry do not overlap the new sites.
- **L** If the movie is not 320x192 (a fan replacement file), rows past 192 are dropped and widths over 320 overflow the 320x200 buffer. That is the same as the stock game.

Fallbacks if texture limits bite:
1. **k=2 with no driver patch** (H that it fits; uses the FLUSHBATCH eviction). That is 1:1 at an 854x480 output, but it is still magnified about 2.25x by dgVoodoo at 1080p.
2. **Own 2D presenter**: replace the call at 0x43ef64 with code that locks `gBack_screen` (`BrPixelmapDirectLock` → back surface) and writes a 640x384 RGB565 image at (xo, 48). Limit: the back buffer is the 854x480 mode, so it can never beat 2x; dgVoodoo upscales it.
3. **Own D3D quad**: before EndScene in the same frame, draw one or two `D3DTLVERTEX` quads with our own large texture (e.g. 2048x1024 RGB565, created through the driver's `gDD` at driver + 0x1bc0c and `gD3DDevice2` at + 0x1bc24). This skips the BRender cache and its limits entirely. Cost: a hook inside the driver's scene control (0x10005b10, case 3), plus our own texture and state management. Only worth it if 4x via slots fails at runtime.

## Runtime checks to do

- Log `*(int*)(drv+0x3818c)` (expect 4) and the allocated column of the slot table after init (+0x14 of each entry).
- With k=4, confirm that 0x10003bd0 picks the 256 entry (score 10) for the movie tiles, and count FLUSHBATCH calls per frame (expect 0 with 16 slots).
- Time `Movie_UploadFrame`, and compare the movie's end-of-play time against 2044/15 s.

## names.tsv

```
address	name	note
0043e770	PlaySmackerFile	fastcall ecx=name; 16-bit: 15 INDEX_8 64x64 tiles, 5x3 quads of 128px under 2D camera
0043f330	PlayIntroMovies	INTERPLAYGO.SMK then MIX_INTR.SMK, then WaitForNoKeys
0043f390	PlayTopRankMovie	TOPRANK.SMK, then WaitForNoKeys; caller 0x4e3440 (game complete, M)
0043ead8	Smack_TileAlloc_call	call BrPixelmapAllocate(3,64,64,0,0): substitution site 1
0043ebc1	Smack_UV_lo_u0	float 1/128 (also 0x43ec2b v2, 0x43ec4b u3/v3)
0043ebcc	Smack_UV_hi_v0	float 127/128 (also 0x43ebf3, 0x43ebfe, 0x43ec20)
0043eeaa	Smack_TileCopy	per-frame 8-bit copy + BrMapUpdate/BrMaterialUpdate, ends 0x43ef50: substitution site 2
0043ef64	Smack_Render_call	BrZbSceneRender(2Dcam,2Dcam,back,depth); front-end centring hook
0043efb1	Smack_FrameCapMs	imm8 0x43 = 67 ms wait cap
004833b0	WaitForNoKeys	
00482d70	AnyKeyDown	movie abort test
005137d0	DRPixelmapAllocate	fastcall ecx=type, edx=w; stack h,pixels,flags (M)
005275c0	BrMemAllocate	(size, class) (M)
00538d80	BrPixelmapAllocate	cdecl type,w,h,pixels,flags
0051efa0	BrMapAdd	(M)
0051efd0	BrMapRemove	(M)
00523160	BrMapUpdate	cdecl pm,flags
00520e70	BrMaterialUpdate	cdecl mat,flags
00537dc0	BrPixelmapFree	(M)
0051ee80	BrMaterialRemove	(M)
0051ed30	BrModelRemove	(M)
005217e0	BrModelFree	(M)
0051e2c0	BrActorFree	(M)
0051cbe0	PDServiceSystem	called after each flip (L name)
0067c3b0	gSmack_buffer	320x200 8-bit decode buffer, pitch 320
0079ec60	gSmack_tile_materials	15 br_material*
0079ec9c	gSmack_palette_b	palette pixelmap 1x256 RGBX (double buffer)
0079eca0	gSmack_palette_prev	
0079eca4	gSmack_actor	
0079eca8	gSmack_palette	current palette pixelmap, pixels +8 = 256 x 0x00RRGGBB, black 0x040404
0079ecac	gSmack_model	
0068b888	gNo_cutscenes_alt	also gates sound init; no writer in xrefs (L)
006ad4c8	gHwnd	SmackBufferOpen window (M)
```

D3D.BDD (Ghidra VAs):

```
address	name	note
10002ba0	D3D_TextureUpload	lock temp surf, 64x64 INDEX_8 LUT path or 16-bit copy/NN resample, Load, colour key 0
10002ecb	D3D_FlushBatchOnReuse	SetRenderState(FLUSHBATCH) when slot used this frame
10003bd0	D3D_TextureLoad	slot scoring; halves size until a slot fits
10004280	D3D_TexCacheAllocSlots	
10004610	D3D_TextureCacheInit	allocates gTexSlotTable entries
100027f0	D3D_EnumTextureFormatsCb	opaque fmt -> 0x1003818c (2=555, 4=565), alpha -> 0x1001dc60
100081e0	D3D_StoredTextureInit	rejects >256; types 3/4/5/0x12/0x1f
100083d0	D3D_BufferStoredNew	"Texture"
10008480	D3D_BufferStoredUpdate	marks dirty
10008570	D3D_ResampleNN	
10015890	gDrvFlags	registry Flags, default 0xffffffff
10015898	gTexSlotTable	{repeat,remaining,batch,w,h,allocated} x6 + terminator
1001b070	gIndexLUT16	256 x u16, built per INDEX_8 upload
1001bbd0	gTexSizeShift	never written (0)
1003818c	gTexFormatOpaque	4 = RGB565
1001dc60	gTexFormatAlpha	
```
