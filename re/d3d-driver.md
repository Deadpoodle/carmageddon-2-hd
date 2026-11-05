# D3D.BDD: why widths other than 640 fail, and how to patch it

Scope: `testrun/d3d.bdd.orig` (= `testrun/d3d.bdd`, 120 320 bytes, "Jims Brender driver for D3D version 0.0.1"), image base 0x10000000, no relocations on any patch site below. Exe: `testrun/CARMA2_HW.EXE`. Static analysis only (Ghidra cache `re/ghidra-export-d3dbdd/` + capstone on the binaries). Confidence: H = read in the instructions, M = strong inference, L = guess.

## Summary

- **H** The driver **does not read WIDTH/HEIGHT tokens**. BRender's own `BrDevBeginTV` (0x528e10) picks an *output facility* whose WIDTH/HEIGHT/PIXEL_BITS/PIXEL_TYPE match the tokens exactly. The driver builds exactly 8 facilities from a fixed table: 512x384, 640x400, 640x480 and 800x600, each at 15 and 16 bit. No 854/856/864/1024 facility exists, so the find fails and `BrDevBeginVar` returns an error. The driver's pixelmap-create code (and `SetDisplayMode`) is never reached.
- **H** The display mode is hard-coded **640x480x16** in the driver's EnumDisplayModes callback, whatever facility was chosen. That is why 800x600 gives `SetDisplayMode(640,480,16)`.
- **H/M** The "heap corruption" crash is a **secondary crash in the exe's fatal-error path**: the BRender failure handler removes all devices and crashes freeing the exe's built-in renderer device. It happens before the "Unable to allocate Main Front Screen" message box can appear. Nothing is written out of bounds by the driver at 854.
- **H** Fix = 3 small data/immediate patches in D3D.BDD (add an 854x480 facility, make the mode picker look for 854x480, change the default mode size), plus the existing exe immediate. It also needs dgVoodoo to enumerate an 854x480x16 mode. Two optional hardening patches are listed.

## Q1. Tokens → output facility → display mode

1. **H** `BrDrv1Begin` 0x10001320 → `D3D_DriverInit` 0x10005080 (subclasses the WndProc to 0x10004f30, `LoadLibraryA("DDRAW")`, `DirectDrawEnumerateA` twice with callback 0x10002130) → `D3D_DeviceAllocate` 0x10001200 → renderer/primitive facility 0x100068f0 → **`D3D_OutputFacilitiesCreate` 0x100066c0**.
2. **H** 0x100066c0 loops i = 0..7 over the table **`gModeTable` 0x10017098** (stride 12: mode-number, width, height; i/2 picks the row). Even i = 15-bit (`PIXEL_BITS`=15, `PIXEL_TYPE`=4 RGB555), odd i = 16-bit (16, type 5 RGB565). Each facility is named "F:%dx%dx%d" (0x10017144). Table contents:
   | VA | mode# | width | height |
   |---|---|---|---|
   | 0x10017098 | 3 | 512 (0x200) | 384 (0x180) |
   | 0x100170a4 | 6 | 640 | 400 (0x190) |
   | 0x100170b0 | 7 | 640 | 480 |
   | 0x100170bc | 8 | 800 (0x320) | 600 (0x258) |
   The mode# field (facility +0x3c) is never read (M).
3. **H** The facility token template 0x10016f28 (14 entries) exposes WIDTH_I32/WIDTH_MIN/WIDTH_MAX all at facility +0xc, HEIGHT_* at +0x10, PIXEL_BITS_I32 at +0x14, PIXEL_TYPE_U8 at +0x1c, INDEXED_B +0x24, WINDOW_FULLSCREEN_B +0x30 (=1), PIXELMAP_MAX_I32=1, CLUT_MAX_I32=0, PRIMITIVE_LIBRARY_O +0x38. WINDOW_HANDLE_H is not in it (ignored in matching).
4. **H** Exe side: `BrDevBeginTV` 0x528e10 calls `device->find(&of, OUTPUT_FACILITY(0x27), NULL, tv)` = `_M_br_object_container_find` 0x52d2b0. Each facility is checked with tokensMatchBegin/tokensMatch/End (0x52d560/0x52d5e0/0x52d670): `queryMany` into a copy of the token list, then `BrTokenValueCompare` 0x52ee40, an **exact** equality test (no closest match, no default). If nothing matches, find returns 0x1002 and `BrDevBeginTV` returns that error before calling `of->pixelmapNew` (vtable +0x70). (BRCORE1 exports mapped from the exe's export table at 0x665e70: names 0x665710, ordinals 0x6659f8, functions 0x665b70.)
5. **H** On a match, `of->pixelmapNew` 0x10006890 → **`D3D_ScreenPixelmapNew` 0x100013a0**. It first calls **`D3D_CreateDisplay` 0x10005200 with no arguments** (so the facility size is not passed). Then it accepts only pixel type 5 or 4 and reads only WINDOW_HANDLE_H from the tokens (template 0x10015440). It `MoveWindow`s the window to SM_CXSCREEN x SM_CYSCREEN (0x100014af/0x100014c2) and creates the screen pixelmap with **width/height = facility width/height** (pm +0x34/+0x36). The pixel type is set from the real back-buffer format, not from the request.
6. **H Mode choice** (`D3D_EnumModesCallback` 0x10002460, run twice from 0x10005200 via `IDirectDraw2::EnumDisplayModes`, vtable +0x20):
   - Modes with width < 320 (0x10002477), height < 240 (0x10002489) or bpp ≠ 16 (0x100024ad) are skipped and not counted.
   - Pass 0: picks the 16-bit mode with **width == 0x280 (imm at 0x100025ae) and height == 0x1e0 (imm at 0x100025b5)**. Among several, the RGB565 one is taken only if nothing was chosen yet; a non-565 one always overwrites. The index goes to `gChosenModeIndex` 0x1003890c (initially -1).
   - Pass 1: at that index, it stores w/h in **`gModeWidth` 0x10015888 / `gModeHeight` 0x1001588c** (copies at 0x10037cc0/c4, 0x10038004/08) and calls `IDirectDraw2::SetDisplayMode(w,h,16,0,0)` at 0x10002518. If that call fails, the chosen index moves on to the *next* enumerated 16-bit mode (0x1000251f–0x10002534).
   - If no 640x480x16 mode is enumerated, no SetDisplayMode happens and gModeWidth/Height keep their static defaults of 640/480.
7. **H Why 800x600 → 640x480:** an "F:800x600x16" facility exists, so the find succeeds and the pixelmap is 800x600. But the mode picker only ever looks for 640x480, so the surfaces are 640x480. The pixelmap is then larger than its surfaces (see Q3 risks).

## Q2. What is written out of bounds?

1. **H For 800/854/856/864/1024 wide (and 1024x768), the driver is never entered after `BrDrv1Begin`.** Facility match fails (Q1.4), so `gScreen` 0x74d3e0 stays NULL. `PDAllocateScreenAndBack` calls `BrFailure("Unable to allocate Main Front Screen")` 0x527c80 at 0x51c387. This fits the observation that the crash comes before any SetDisplayMode. The crash also shows up exactly for the sizes missing from gModeTable, and not for 800x600.
2. **H The crash chain is the failure handler's teardown.** 0x51c6c0 installs the game's diag handler (failure = 0x51c620). 0x51c620 formats the message, then calls 0x513400 (`if begun: BrDevFind(&d,NULL); BrDevRemove(d)` loop, 0x528860/0x528800), and only *afterwards* calls PDShutdownSystem 0x51c110 (MessageBox, ExitProcess). `BrDevRemove` calls `device->free` (vtable +0x10). For the exe's built-in renderer device (class vtable 0x58bcb0, created by 0x540810, "RECALC_BITS_U32"/"PRIMITIVE_STATE_O") that is **0x540880**. It calls `BrObjectContainerFree` 0x52d690, which frees its renderer facility (vtable 0x58bc20, created by 0x5405e0 "V1Buckets/Primitives/Lighting/V1Model") via free **0x540670**. Then come `BrResFreeNoCallback` 0x5278b0 → res free 0x5277f0 → `BrMemFree` 0x5275f0 → game allocator free 0x44c990 (`HeapFree(GetProcessHeap())`). That is exactly the reported chain.
   - **L** Why the heap is already bad at that point is not established. The width-dependent work before it (token match) runs the same code as at 640. Likely a teardown-order or double-free bug in this rarely used error path (the D3D device is half-built: no screen pixelmap, image dereferenced by `BrDevRemove`). Whether a normal quit (0x491f70 → 0x513400) also crashes is not known. This does not block the fix: once a facility matches, this path is not taken.
3. **H Hard-coded 640x480 in the driver** (what would break *after* the match is fixed):
   - `gModeWidth/Height` defaults 0x10015888/0x1001588c = 640/480 (used by D3D viewport 0x10002ac0, Z-buffer size 0x100054c6/0x100054cb, clear rect 0x10005db3/0x10005dc1).
   - Mode-pick immediates 0x100025ae / 0x100025b5 (above).
   - **Shadow 2D buffer**, active only if `gNo2DDuring3D` 0x1001bb9c = 1. That flag is set at 0x1000220f when *any* enumerated DirectDraw device reports `DDCAPS2_NO2DDURING3DSCENE` (dwCaps2 bit 0x2; checked before the device-index test, so not only the selected device).
     - `gShadowBuffer` 0x10038a80 is a static **0x96000 = 640x480x2** array in .data (the next globals start at 0x100cea80).
     - DirectLock 0x10001d70 hands it out with **row_bytes 0x500** (0x10001db4).
     - DoubleBuffer/flip 0x10005b10 case 4 copies **480 rows x 640 px** of it into the locked back buffer (0x1e0/0x280/0x140/0x50 loop counts, from 0x10005c6c).
     - RectangleCopyTo 0x10001b00 goes through CIWT 0x10004780 (10x6 tiles of 64 plus a row of 32-px tiles = 640x480).
     - In this mode a pixelmap wider than 640 would write past the static buffer (heap/.data corruption). **M**: probably inactive under dgVoodoo2, but not verified.
   - PixelSet 0x10001910 drops pixels with x > 0x27f (imm 0x1000196b) or y > 0x1df (imm 0x10001977). This is clipping, not corruption.
   - Dirty-rect reset to 639/479 at 0x10005d8c–0x10005da6 (0x100156ec..f8). Written but only re-read by PixelSet itself, so harmless.
4. **M The exe's BRender code is size-agnostic.** A linear capstone sweep of 0x527000–0x546000 finds no 0x280/0x1e0/0x500/0x96000/0x4b000/0x12c000/640.0f/480.0f immediates. The memory pixelmap helpers the driver imports (`_M_br_device_pixelmap_mem_*`, `gen_*`) work from pixelmap width/height/row_bytes. The other 0x1e0 in driver data (0x10019e6c) is the CRT small-block-heap threshold, not a screen size.

## Q3. Back buffer and depth buffer

1. **H** `D3D_CreateDisplay` 0x10005200 (order matches its debug strings):
   - EnumDisplayModes x2 (Q1.6), then QI IDirect3D2 into 0x1001bc18 and EnumDevices x2 (0x10002630).
   - Primary surface (0x1000537d): DDSD flags 0x21 (CAPS|BACKBUFFERCOUNT), caps **0x2218** (PRIMARYSURFACE|FLIP|COMPLEX|3DDEVICE), 1 back buffer, stored in 0x1001bc34.
   - **Back buffer = `GetAttachedSurface(DDSCAPS_BACKBUFFER)`** (0x100053d2), stored in 0x1001bc3c. Its pixel format goes to 0x10038520 (555 vs 565 test from 0x10005438).
   - **Z buffer**: CreateSurface flags 0x47 (CAPS|HEIGHT|WIDTH|ZBUFFERBITDEPTH), size **gModeWidth x gModeHeight** (0x100054c6/0x100054cb), caps 0x24000 (video memory) when 0x10037cc8 == 1, else 0x20800 (system memory). Depth is 32/24/16/8 from the device's zbuffer caps 0x100389c0 ("Unsupported Z-buffer depth" otherwise). Attached with AddAttachedSurface (0x100055a8).
   - IDirect3DDevice2 created on the back buffer (0x1001bc24). Viewport2 0x1001bc48 set by `D3D_SetViewport` 0x10002ac0 to **gModeWidth x gModeHeight**. Background material, then the texture cache 0x10004610.
2. **H** BRender pixelmaps (`D3D_PixelmapMatch` 0x10001660):
   - OFFSCREEN (0x65): a "Offscreen" pixelmap, type 1, with the *screen pixelmap's* width/height. It has no own memory and locks the real back buffer (DirectLock 0x10001d70 → `IDirectDrawSurface::Lock`, row_bytes = lPitch).
   - DEPTH (0x54): a "Depth" pixelmap, type 2, pixel type 0x0b, same size.
   - NO_RENDER (0x17b), or OFFSCREEN on a non-screen pixelmap: a plain memory pixelmap of the requested size (0x10001ec0).
   - Projection/viewport scale comes from the pixelmap origin and size (0x10006490); clear and D3D clip come from the mode size.
3. **H The driver cannot have a back buffer of a different size than the display mode.** The back buffer is the flip chain's attached surface (always mode size). The Z buffer and D3D viewport use the mode size, and there is no offscreen render target or Blt/stretch present path. Present is `IDirectDrawSurface::Flip(NULL, DDFLIP_WAIT)` on the primary at 0x10005d40 (vtable +0x2c). The pixelmap size (facility) and the surface size (mode) are set independently, and nothing checks that they agree:
   - pixelmap smaller than the mode: the image sits in the top-left, the rest is unused.
   - pixelmap larger than the mode (the current 800x600 case): 3D is clipped by the D3D viewport, but 2D writes through DirectLock use pixelmap width/height with the surface pitch, and **run past the end of the back-buffer surface (rows ≥ mode height)**. That is a real OOB risk inside the DirectDraw implementation (**M**).
4. **M** So with dgVoodoo2 the practical route is to make the *mode itself* 854x480 (dgVoodoo then scales that mode to the window/output). A "640x480 mode + 854x480 back buffer" would need new code: an offscreen 3D surface plus a Blt-based present replacing Flip. That is a hook/code-cave job, not a byte patch.

## Q4. Minimal patch for 854x480 (W = 854 = 0x356)

D3D.BDD file offsets (`.text` raw = VA − 0x10000c00, `.data` raw = VA − 0x10001800 for these sites). None of these sites are relocated.

| # | VA | file off | orig | new | purpose | conf |
|---|---|---|---|---|---|---|
| 1 | 0x1001709c | 0x1589c | `00 02 00 00` | `56 03 00 00` | gModeTable row 0 width 512 → 854 | H |
| 2 | 0x100170a0 | 0x158a0 | `80 01 00 00` | `e0 01 00 00` | row 0 height 384 → 480 (adds "F:854x480x15/16", keeps 640x480) | H |
| 3 | 0x100025ae | 0x19ae | `80 02 00 00` | `56 03 00 00` | mode picker looks for 854 wide (height imm 0x100025b5 / 0x19b5 stays 0x1e0) | H |
| 4 | 0x10015888 | 0x14088 | `80 02 00 00` | `56 03 00 00` | default gModeWidth (viewport/Z/clear if no mode is set) | M |
| 5 (opt.) | 0x1000196b | 0xd6b | `7f 02 00 00` | `55 03 00 00` | PixelSet x clip 639 → 853 | M |
| 6 (opt.) | 0x1000220f | 0x160f | `89 1d 9c bb 01 10` | `90 90 90 90 90 90` | never enable the 640x480 shadow-buffer path | M |

Plus the exe: `push 0x280` imm at **0x51c34f** → `56 03 00 00` (already known; height 0x51c348 unchanged). Plus the game-side graf/HUD work already listed in `re/display.md`.

Requirements and risks:
- **H** dgVoodoo must **enumerate an 854x480 16-bit mode** to `IDirectDraw2::EnumDisplayModes` (exact match, bpp 16). If it does not, no SetDisplayMode happens. The primary/back buffer then take the current mode's size while the Z buffer is 854x480 (with patch 4), and AddAttachedSurface/CreateDevice probably fails → `pixelmapNew` returns 0xa002 → the same fatal path. Without patch 4 it is worse: the Z buffer and viewport stay 640x480 under an 854 pixelmap (the OOB case of Q3.3).
- **M** If SetDisplayMode(854,…) fails, the driver falls through to the *next* enumerated 16-bit mode (Q1.6), again leaving pixelmap ≠ surface. Make sure dgVoodoo accepts the mode it lists.
- **M** Patch 6 matters only if a DirectDraw device reports DDCAPS2_NO2DDURING3DSCENE. Leaving that path on at W > 640 corrupts .data (Q2.3). Disabling it means the driver always locks the real back buffer for 2D, as on a normal card. Cheap insurance.
- **M** Patch 1/2 replaces 512x384, which the game never requests. If 640x400 is preferred instead, use row 1 (0x100170a8/0x100170ac, file 0x158a8/0x158ac).
- **L** For a height other than 480, also patch 0x100025b5 (file 0x19b5), 0x1001588c (file 0x1408c), 0x10001977 (file 0xd77) and the exe imm 0x51c348.
- **L** `hardware_d3d.bdd` (exported as 3dfx_win.bdd, a different code base) was not analysed. It fails the same way at runtime, so it presumably also has a fixed facility list.
- Cheapest alternative if dgVoodoo cannot expose a custom 16-bit mode: none inside this driver without new code. The next cheapest is a DDraw-side shim that reports and accepts an 854x480x16 mode (dgVoodoo config or a wrapper), keeping patches 1–6.

### Runtime checks to confirm
- With the stock driver and the exe at 854: a hook/breakpoint on 0x527c80 should show "Unable to allocate Main Front Screen". The device find (0x52d2b0) should return 0x1002.
- With patches 1–4: expect `SetDisplayMode(854,480,16)` from 0x10002518 (relative to where BRender loaded the image), and a Z-buffer CreateSurface with dwWidth 854.
- Read the driver byte at image+0x1bb9c after init: 0 means the shadow-buffer path is off even without patch 6.

## names.tsv (D3D.BDD, Ghidra VAs)

```
address	name	note
10001000	D3D_ClutAllocate	"Pseudo-CLUT" object, 256 grey entries
10001080	D3D_ClutFree	
10001200	D3D_DeviceAllocate	device object 0x100150c8, vtable 0x100151e8
10001280	D3D_DeviceFree	ObjectContainerFree + BrResFree(device res)
10001320	BrDrv1Begin	export; init, device, renderer facility, output facilities
100013a0	D3D_ScreenPixelmapNew	calls D3D_CreateDisplay; pm size = facility size; one screen pm per device
100015c0	D3D_PixelmapFree	
10001660	D3D_PixelmapMatch	OFFSCREEN/DEPTH/NO_RENDER
100018a0	D3D_PixelmapRectangleFill	stub: clip only
100018d0	D3D_PixelmapDoubleBuffer	-> D3D_SceneControl(4) flip
10001910	D3D_PixelmapPixelSet	hard clip x<=0x27f y<=0x1df
10001b00	D3D_PixelmapRectangleCopyTo	lock+copy, or CIWT in shadow mode
10001c90	D3D_PixelmapAllocateSub	
10001d70	D3D_PixelmapDirectLock	Lock back buffer; shadow mode: static buf, row_bytes 0x500
10001e40	D3D_PixelmapDirectUnlock	
10001ec0	D3D_MemPixelmapAllocate	memory pixelmap of requested size
10001f90	D3D_ReadRegistryGuids	HKLM\Software\SCI\Carmageddon2 DrvGuid/DevGuid/Flags
10002130	D3D_DDEnumCallback	DirectDrawEnumerate cb; sets gNo2DDuring3D; QI, SetCooperativeLevel 0x11
10002460	D3D_EnumModesCallback	picks 640x480x16 (imm 0x100025ae/0x100025b5); SetDisplayMode 0x10002518
10002630	D3D_EnumDevicesCallback	label (not a Ghidra function)
10002ac0	D3D_SetViewport	viewport2 = gModeWidth x gModeHeight
10003bd0	D3D_TextureLoad	
10004610	D3D_TextureCacheInit	
10004780	D3D_CopyImageWithTextures	"CIWT": 640x480 via 64/32-px texture tiles
10004f30	D3D_WndProc	subclassed WndProc (WM_ACTIVATEAPP)
10005000	D3D_FlushRenderStates	
10005080	D3D_DriverInit	LoadLibrary DDRAW, DirectDrawEnumerateA x2
10005200	D3D_CreateDisplay	modes, primary 0x2218, back buffer, Z, D3D device, viewport
10005a50	D3D_FatalShutdown	release all, ExitProcess(0)
10005b10	D3D_SceneControl	2 BeginScene, 3 EndScene, 4 flip (+shadow composite), 5 clear
10005e20	D3D_DebugLog	no-op (ret)
10006490	D3D_SetupViewportScale	from pixelmap origin/size
100066c0	D3D_OutputFacilitiesCreate	8 facilities from gModeTable
10006800	D3D_OutputFacilityFree	
10006890	D3D_OutputFacilityPixelmapNew	vtable +0x70
100068f0	D3D_RendererFacilityInit	static object 0x10015110
10006970	D3D_RendererFacilityFree	
10015888	gModeWidth	default 640
1001588c	gModeHeight	default 480
10016f28	gOutputFacilityTemplate	14 token entries
10017098	gModeTable	{mode#,w,h} x4: 512x384 640x400 640x480 800x600
100170c8	gOutputFacilityVtbl	
10015478	gPixelmapVtbl	
1001bb9c	gNo2DDuring3D	1 = shadow-buffer 2D path
1001bbac	gShadowBufferPtr	= 0x10038a80
1001bc0c	gDD	IDirectDraw
1001bc10	gDD2	IDirectDraw2
1001bc18	gD3D2	IDirect3D2
1001bc24	gD3DDevice2	
1001bc34	gPrimarySurface	
1001bc3c	gBackSurface	
1001bc44	gZSurface	
1001bc48	gViewport2	
1003890c	gChosenModeIndex	-1 until a 640x480x16 mode is found
10038a6c	gModeEnumIndex	
10038a80	gShadowBuffer	static 0x96000 = 640x480x16
```

## Lock failures (the Alt+Tab crash)

The lock count 0x1001bbd8 goes up before Lock and down after Unlock, in the rectangle copy 0x10001b00 and in DirectLock 0x10001d70 / DirectUnlock 0x10001e40 (H). Both leave it up when Lock fails (0x10001bc4, and 0x10001e2c for DirectLock), and nothing resets it. With the count stuck, the next copy sees 2, skips Lock at 0x10001b85, and uses the unset local [ebp-0x68] as its destination: `rep movs` at 0x10001c2c. A failed DirectLock also leaves pixelmap +8 unset, and the game's own 2D code writes through it (0x47d21f) (H, both reproduced with `LOCKFAIL`).

The fix is in capture.cpp, not here: the back buffer's Lock (slot 25, compared with the driver's surface at 0x1001bc3c) can't fail. A failure returns a private buffer laid out like the last real lock, and the matching Unlock (slot 32) does nothing. Patching the driver alone wasn't enough, because of 0x47d21f.
