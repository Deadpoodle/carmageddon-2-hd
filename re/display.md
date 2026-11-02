# Display, window and renderer (CARMA2_HW.EXE, Steam build 9b896c2c...)

Static analysis only. Tags: **H** read from the instructions (capstone on the mapped image), **M** decompiler output or inference from dethrace naming, **L** a guess to test in play. Addresses are VAs (image base 0x400000).

## Corrections to WIKI_LOG

- **H** The switch parser (WinMain 0x51aaa0) upper-cases the command line (0x587ac0, `_strupr`), splits on " \t\n" (0x575d00, `strtok`) and compares each token:
  - `-NOCUTSCENE` / `-NOCUTSCENES` set 0x68b894 (read only by the Smacker player 0x43e770). **Not** -D3D.
  - `-D3D` sets the pointer 0x6621dc from "3DFX_WIN" (0x6622b8) to "D3D" (0x662518).
  - `-ZOMBIE` calls 0x4d6f50, `-BLOOD` 0x4d6f30, `-ALIEN` 0x4d6f70.
  - `-AFE` sets 0x74d3dc (cleared first at 0x51aab1). **Not** -SCALEMOUSE. 0x74d3dc is read in 0x4e5680, 0x4f9790, 0x4fe360 and 0x4fe570 (network game modes 6 and 7; not display).
  - `-SCALEMOUSE` sets 0x6ad468.
- **H** The exe never names `hardware_*.bdd`. It asks BRender for the device "3DFX_WIN" or "D3D", which loads `3DFX_WIN.BDD` or `D3D.BDD` (see Q1). `3dfx_win.bdd` is byte-identical to `hardware_3dfx.bdd` (re/hashes.txt). `d3d.bdd` (120 KB, "Jims Brender driver for D3D version 0.0.1", export name `d3d.bdd`) is **not** `hardware_d3d.bdd`.
  - `hardware_d3d.bdd` (236 KB) is a D3D driver whose export name is `3dfx_win.bdd`, built from `C:\dev\brd_bill\...\3dfx_win.pdb`. It is a drop-in replacement for 3dfx_win.bdd. Something (installer or launcher, not checked) must copy it over; the launcher `carma2.exe` has no "hardware_" string (L).

## 1. Start-up order up to the first frame

1. **H** CRT `entry` 0x5788a0 calls WinMain 0x51aaa0.
2. **H** WinMain:
   1. Switch parse (above).
   2. Single-instance check: `FindWindowA("Carma2MainWndClass", NULL)` at 0x51ad11. If the class exists: `ExitProcess(0x2bf)`.
   3. RegisterClassA (style 0xb, WndProc 0x51b0c0, IDC_ARROW, BLACK_BRUSH).
   4. `CreateWindowExA(0, class, "Carmageddon II", 0x90000000 WS_POPUP|WS_VISIBLE, 0,0,4,4)` at 0x51adb1. The handle goes to 0x6ad4c8.
   5. UpdateWindow (0x51adbd), then SetFocus (0x51adc9).
   - Bug, harmless: hInstance comes from 0x6acea4, which is stored only after window creation (0x51ae6e), so class and window get hInstance 0.
3. **H** Japanese locale check: the "AUDIQUATTRO" string checks plus LANGID 0x11 lead to MessageBoxA and ExitProcess(0x2c0).
4. **H** `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` goes to 0x6ad460/0x6ad464: the desktop size before any mode change. The current directory goes to 0x6b3380. Then GameMain 0x4924a0 is called (ecx=1, edx=argv).
5. **M** GameMain 0x4924a0 (dethrace order) runs InitialiseDeathRace 0x47dd20, then DoProgram 0x503c50, then QuitGame. Display-relevant steps inside InitialiseDeathRace, all **H** call order:
   1. 0x51b9f0 `PDInitialiseSystem`: ShowCursor(FALSE), 0x51ba10, then 0x51cbf0 (DirectInput keyboard, `SetCooperativeLevel(hwnd, 6 = NONEXCLUSIVE|FOREGROUND)`, Acquire).
   2. 0x525f50 `BrV1dbBeginWrapper` (M: 0x52cf20 BrBegin + 0x525860 BrV1dbBegin).
   3. 0x51d7c0 timer init (QueryPerformanceFrequency).
   4. 0x48d8f0 options load (M).
   5. 0x51c290 `PDInitScreenVars`: `gGraf_spec_index` 0x762324 = 0x74d630 = 0x662200 (constant 1). If it returns 0, fatal error 0.
   6. 0x47b3e0 `CalcGrafDataIndex`.
   7. 0x51c270 and 0x51c2b0: empty PD stubs (`PDInitScreen` etc.).
   8. 0x4e4980 `SetBRenderScreenAndBuffers(0,0,0,0)`, which calls 0x51c300 `PDAllocateScreenAndBack` (below).
   9. Then the actors/cameras (0x51e1a0), 0x47e560 HUD camera, fonts, 0x4d70c0 overlays (640x480), etc.
6. **H** `PDAllocateScreenAndBack` 0x51c300:
   1. `SSDXStart` 0x500500 (hwnd, 0, 1, 2). It only calls `DirectSoundCreate` (flag 2); the DirectDraw part of SSDX is dead in this build (Q4).
   2. `BrDevBeginVar(&gScreen 0x74d3e0, *0x6621dc, tokens..., 0)` at 0x51c371 (0x528d70). The tokens are in the Q2 list. On failure: fatal "Unable to allocate Main Front Screen".
   3. If `gScreen->identifier` (+4) is not "Voodoo Graphics", 0x74cadc = 1 ("lock often"). A block of capability flags is set to 1: 0x74ca0c, 0x74cf38, 0x74cf20, 0x74ca20, 0x74cab8, 0x74ca10, 0x74cf8c, 0x74cf68, 0x74ca54, 0x74ca28, 0x74cf60. Also 0x74c9f4 = "VOODOO".
   4. `gBack_screen` 0x74d360 = `BrPixelmapMatch(gScreen, 0)` (0x537e20; 0 = USE_T OFFSCREEN).
   5. 0x6ad47c = `BrPixelmapMatch(gBack_screen, 0)`. It is never read again (a dead buffer).
   6. Clears the back buffer through `->pixels`, then `BrPixelmapDoubleBuffer(gScreen, gBack_screen)` 0x5389e0: the first flip, of a black screen.
7. **H** Device to driver: `BrDevBeginVar` → 0x528e10 (BrDevBeginTV), which splits "name,args" at the comma. If the name is not loaded yet, it goes to 0x5285c0 (driver find/load), which first loads the BRENDER_DRIVERS list (0x5286d0), then calls `BrImageReference(name)` 0x52ffa0. For a name with no dot, the load order is:
   1. BRender's own PE loader 0x530e70 on `NAME.BDD` (the .bdd is **not** LoadLibrary'd: no DllMain, not in the module list).
   2. `LoadLibraryA(name)` via 0x53fb50.
   3. The PE loader on `NAME.DLL`.
   - It then looks up `BrDrv1Begin` (0x5301e0) and calls it.
   - The .bdd's own imports (USER32, glide2x, KERNEL32 LoadLibraryA/GetProcAddress, ...) are resolved by the same BrImageReference. A system DLL becomes `LoadLibraryA(name)` (0x53fb50 → IAT 0x5891a8), and each symbol becomes `GetProcAddress` (0x53fb70 → IAT 0x5891a4).
   - **So every driver import passes through the exe's LoadLibraryA/GetProcAddress IAT slots.**
   - glide2x.dll (nGlide) is LoadLibrary'd that way. d3d.bdd itself does `LoadLibraryA("DDRAW")` + GetProcAddress(`DirectDrawCreate`, `DirectDrawEnumerateA`) at run time.
   - BRender config (0x530460, M): `SOFTWARE\Argonaut\BRender\1.3.0` registry key, `BRender.ini` [BRender], or the env vars BRENDER_PATH, BRENDER_DRIVERS / BR_DRIVERS, BRENDER_DEVICES, BRENDER_DOS_GFX (the default device when the name is NULL).
8. **H** Inside the drivers (the window is resized here, not in the exe):
   - 3dfx_win.bdd: `GetSystemMetrics(0/1)`, `SetForegroundWindow(hwnd)`, `MoveWindow(hwnd,0,0,cx,cy,TRUE)` (0x10002183–0x100021a6), then `grSstWinOpen(hwnd, res, GR_REFRESH_60Hz(0), ARGB(0), UPPER_LEFT(0), 2 colour buffers, 1 aux)` at 0x100021bf. Swap: `grBufferSwap(1)` after a `grBufferNumPending` wait (~0x10002932).
   - d3d.bdd: the same SetForegroundWindow + MoveWindow (0x100014af/0x100014c2), `SetWindowLongA` (subclasses the WndProc; "D3D.BDD: WM_ACTIVATEAPP=TRUE/FALSE"), `IDirectDraw::SetCooperativeLevel(hwnd, 0x11 FULLSCREEN|EXCLUSIVE)` at 0x100022c4, `IDirectDraw2::SetDisplayMode(w,h,bpp,0,0)` at 0x10002518. It enumerates modes and favours 640x480 RGB565 (0x100025ad, M).
9. **M** The first real frame: DoProgram 0x503c50 runs the intro Smacker 0x43e770 (`SmackBufferOpen(hwnd,…,320,200,320,200)`, drawn as a textured model), then the front end 0x46d8e0. Each path ends in `PDScreenBufferSwap` 0x51c520.

## 2. Resolution: where 640x480 / 320x200 are fixed

- **H `gGraf_specs[2]`** at 0x662208, stride 0x34, dethrace `tGraf_spec` layout:

  | idx | depth | dbl | total w×h | black | dir | init string | row | phys w×h |
  |---|---|---|---|---|---|---|---|---|
  | 0 @0x662208 | 8/1 | 0 | 320×200 | 0 | "32X20X8" | "320x200 init string" | 320 | 320×200 |
  | 1 @0x66223c | 8/1 | 0 | 640×480 | 0 | "64X48X8" | "640x480 init string" | 640 | 640×480 |

  - Code reads only these fields: doubled (+8), total_width (+0xc), total_height (+0x10), black (+0x14), data dir (+0x1c), phys w/h (+0x28/+0x2c).
  - **The init strings (+0x20) are never read.** They are placeholders, and nothing is passed to the driver from them.
  - Depth 8 is stale: the hardware path is 16-bit.
- **H Index choice:** a single constant. 0x662200 = 1 is copied by `PDInitScreenVars` 0x51c290 into `gGraf_spec_index` 0x762324 and into 0x74d630. Nothing else writes 0x762324. 320x200 is never selected in the HW exe.
- **H `gGraf_data[2]`** at 0x655f58, stride 0x520 (328 ints each: width, height, then HUD and layout coordinates).
  - 110 fields differ between the 320 and 640 entries, so all HUD positions are absolute pixel values per resolution.
  - `CalcGrafDataIndex` 0x47b3e0 scans for the entry whose width/height equals the graf spec's total width/height. It sets 0x74b770, 0x74d630 and `gCurrent_graf_data` 0x74b774 (72 reads in 26 functions). With no match: fatal 0x18 ("Screen width/height doesn't match graf data").
- **H The real mode is hard-coded in `PDAllocateScreenAndBack`, not taken from the table.** Token list pushed at 0x51c33d–0x51c35c (names decoded from the token table at 0x667f50, stride 0x18):
  - `WINDOW_FULLSCREEN_B`(0x196)=1
  - `WINDOW_HANDLE_H`(0x195)=hwnd
  - `WIDTH_I32`(0x45)=**0x280 (push at 0x51c34e)**
  - `HEIGHT_I32`(0x48)=**0x1e0 (push at 0x51c347)**
  - `PIXEL_BITS_I32`(0x4c)=0x10
  - `PIXEL_TYPE_U8`(0x4b)=5 (BR_PMT_RGB_565)
  - Also 0x74ca60 = 16 (screen bits; 0x74ca18 = bits==16 ? 5 : 4).
  - The device string can carry ",TOKEN=val" args (0x52e9f0), but explicit tokens override them (0x528e10 merge). **So the `push 0x280`/`push 0x1e0` immediates (or the BrDevBeginVar call) are the single "set mode" hook.**
- **H Buffers:**
  - `gBack_screen` = Match(gScreen, OFFSCREEN).
  - `SetBRenderScreenAndBuffers` 0x4e4980 sets `gRender_screen` 0x762128 = sub-pixelmap of back (0x513820), plus gX/gY offset 0x705060/0x6baa2c and gWidth/gHeight 0x703e24/0x703e20 (= back dims when 0).
  - gScreen base_x/y = (phys − total)/2 (doubled is never set).
  - `gDepth_buffer` 0x68b8a4 = Match(back, 1 = DEPTH, PIXEL_BITS 16).
  - Then 0x525d20 (BrZbBegin-like, M) on the render and depth types.
- **H Other places where 640/480 appear in code** (immediates scan of all functions):
  - 0x51c347/0x51c34e: device size (above).
  - 0x47dfe7–0x47e005 (InitialiseDeathRace) and 0x4e57a3 (0x4e5680): `0x4d70c0(640,480,…)` full-screen tint/overlay quads (2D overlay space, M).
  - 0x4651b0/0x4651bc (0x464e40, 3D text renderer): temporarily forces `gBack_screen->width/height = 640/480` (origin 0) around `BrZbSceneRender` 0x5226d0, then restores.
  - 0x46cf4c/0x46cf53 (0x46cf10) and 0x46f7f6/0x46f7fd (0x46f750): front-end parallel cameras with width 640.0f, height 480.0f constants.
  - 0x496cf4 (0x496be0): full-screen map copy (`0x5191b0(…,640,480)`) in map mode (0x75b9a4==2, graf-data index ≠0).
  - 0x5060fb/0x506107 (track load 0x504bf0): if the spec ≠ 640×480, the map transform is rescaled by W/640, H/480 about (W/2,H/2) (floats 0x58b1b8–0x58b1c4). A generic scaling path exists.
  - 0x48c8a9, 0x499595/0x4995a5: `mov reg,0x1e0` (not examined, L).
  - 0x5763e4/0x5769c9/0x576c54 `cmp …,0x27f` are CRT FPU control words, not screen.
- **H Other readers of the graf spec dims** (index 0x762324):
  - 0x47dc30 `ReinitialiseRenderStuff` (render window from ScreenSize / gRender_indent 0x68be38; full-screen when 0x74cf8c).
  - 0x483c10 mouse clamp.
  - 0x4a0080 message screen.
  - 0x4e4e40 border fills.
  - 0x47b880 `ClearEntireScreen` (black value).
  - Data-dir users: 0x4643f0, 0x465850, 0x466050 (fonts), 0x488f70 (car res-dependent file), 0x48ec20 (pixelmaps), 0x4d96c0, 0x513a30.
  - 0x41f110 uses the index into a float table at 0x58f6b0.
- **Hook verdict (M):** there is no table to extend for the device size; the device size is the two immediates. Extending gGraf_specs/gGraf_data would mean a third 0x520-byte HUD layout and relocating ~45 indexed displacement sites.
  - The cheaper route: keep the logical 640×480 (graf spec and graf data unchanged) and change only the device/back/depth size.
  - The HUD camera 0x47e560 takes its width/height from `gBack_screen` (0x47e5c3…). Pin it to 640×480 (or 480×aspect) so the HUD scales.
  - The front-end cameras already use 640.0/480.0 constants.
  - The forced 640×480 in 0x464e40 must follow the real back size.
  - Direct pixel 2D (fonts 0x466000→0x4638b0→0x464e40 are models; 0x538640 RectangleFill, 0x5191b0/0x515e70/0x518690 copies, the 3D-camera aspect = gWidth/gHeight in 0x47d8d0/0x47e3b0) works in back-buffer pixels and needs checking per site (L).

## 3. The frame

- **H** The race loop is `MainGameLoop` 0x492950 (called from DoProgram 0x503c50).
  - Per frame: input (0x51cef0 via callees), `UpdateFramePeriod` 0x492680, simulation, then `RenderAFrame` 0x4e4e40 (call at 0x493aea).
  - An FPS ×10 average is computed after the render (0x68c804 / 0x761ee0, 31 samples, M).
  - `Sleep(1500)` once before the loop (0x492b5d).
- **H `RenderAFrame` 0x4e4e40:**
  1. 0x496be0 (map backdrop).
  2. Screen-shake offsets: the render-screen pixels pointer is moved by `row_bytes*dy+dx`.
  3. Border fills with `BrPixelmapRectangleFill` 0x538640 using the graf spec total width/height (when gRender_indent).
  4. **The 3D world**: 0x4e54f0(main camera 0x74d35c, gRender_screen, gDepth_buffer), which calls 0x4e5680. That clears colour/depth with `BrPixelmapFill` 0x5382f0, then runs `BrZbSceneRenderBegin` 0x522c80 / `Add` 0x522ea0 / `End` 0x522eb0 (M names).
  5. Optional rear-view: 0x4e54f0(0x75b940, 0x68b8a8, 0x75b93c).
  6. **HUD**: 0x4d8290, 0x4496f0, 0x44a9d0, 0x449b10 (text head-ups), 0x44aad0, 0x4dad00, 0x44b6a0, 0x449800, 0x496d80, 0x444c00 (net).
     - Text and gauges are mostly quads/models added to the 2D root 0x74ca00 via 0x464e40.
     - Then `0x4e5b00` renders that 2D world with the parallel "HUD_Camera" 0x74cf74 (made in 0x47e560: hither 1, yon 3, width/height = back dims) into gRender_screen through `BrZbSceneRender` 0x5226d0 (origin zeroed around the call).
     - A few direct pixelmap ops remain (0x538590, 0x538a10, 0x517d90, 0x47ba80/0x47cbd0, M).
  7. **Present**: `PDScreenBufferSwap` 0x51c520 = `BrPixelmapDoubleBuffer(gScreen, gBack_screen)`, i.e. the driver's pixelmap vtable +0x64. The swap is skipped while 0x67c478 is set and less than 500 ms old.
- **H Other presents:**
  - Front end 0x46d8e0: parallel camera scene via 0x522f30, swap at 0x46df23.
  - Smacker 0x43e770, `ClearEntireScreen` 0x47b880 (fills gScreen+back+depth, then swaps).
  - 0x44ba10, 0x462c12, 0x462dc0, 0x472b00, 0x47b990, 0x494400, 0x4a0080, 0x4e6900.
  - `PDSetPaletteEntries` 0x51c850 (SetPaletteEntries + DoubleBuffer, legacy 8-bit).
- **H Timer:** `PDGetTotalTime` 0x51d410 = (QPC − start)/(QPF/1000) ms. 0x51d900 is a duplicate; 0x51d990 gives µs. 119 callers.
  - Init 0x51d7c0. If QPF fails, every time read returns 0 (no timeGetTime fallback).
  - Game time: 0x514c30. Real-time since race start: 0x514c70.
- **H Caps:** **no frame limiter in the exe.**
  - `UpdateFramePeriod` 0x492680 clamps `gFrame_period` 0x74d3e4 to [10, 1000] ms (0x4927d5, 0x4927e6). The simulation assumes ≤100 fps; above that, game time runs faster than real time (L, test).
  - The only Sleep calls are the 1500 ms pre-race one and 0x51b930 (replay buffer allocation).
  - Busy-wait helpers: 0x466ec0 (200 ms, front end), 0x4a0080 (3 s message), 0x46f590 (unreferenced).
  - **M:** the Glide cap is presentation: 3dfx_win.bdd opens at `GR_REFRESH_60Hz` with `grBufferSwap(1)` (vsync); the "63 fps" is nGlide's own pacing (L).
  - **L:** the 48 fps menu cap was not found in the exe (the front-end loop 0x46d8e0 has no wait). Probably driver/vsync or front-end animation cost; measure in play.

## 4. Window procedure and focus

- **H WndProc 0x51b0c0:**
  - WM_PAINT: Begin/EndPaint only.
  - WM_DESTROY: logged, nothing else.
  - WM_MOVE/WM_SIZE while iconic: DeActivateApp.
  - WM_CHAR: a 20-character type-ahead buffer at 0x6ace88.
  - WM_SYSCOMMAND SC_CLOSE: DestroyWindow + `PDShutdownSystem` 0x51c110.
  - Mouse buttons: 0x201/0x204 set button flags 0x6ad488/0x6ad48c (only when active) + SetCapture; 0x202/0x205 clear them + ReleaseCapture.
  - 0x203/0x206 (double-click): store the click position in 0x6621d4/0x6621d8 and set the flag 0x6ad454.
- **H App state 0x6621e0** (initially 2): 2 = active, 1 = background (Glide), 0 = background (D3D).
- **H WM_ACTIVATEAPP (0x1c):**
  - If the foreground window is ours and not iconic ("Activating app"):
    - DirectInput keyboard Acquire (0x6acea0 vtable +0x1c).
    - `SetWindowPos(hwnd,0,0,0,SM_CX,SM_CY,0x414)` at 0x51b2d3.
    - Activation time → 0x6ad49c; state = 2.
  - Else, if `gBack_screen->pixels` is non-NULL (back buffer locked), the message is swallowed (return 1, no DefWindowProc). Otherwise DeActivateApp:
    - Unacquire (+0x20).
    - `SetWindowPos(hwnd,0,0,0,0,0,0x414)` (0x51b413): the window shrinks to 0×0.
    - state = (device != "D3D") ? 1 : 0.
- **H `Win32ServiceMessages` 0x51cad0** (pumped from the key poller 0x51cef0, 0x51b040, 0x51cbe0, shutdown):
  - **When state==1 it calls `SetForegroundWindow(hwnd)` on every pump (0x51cb0b): the Glide build actively steals focus back.**
  - When state==0 (D3D) it uses blocking `GetMessageA`, so the game freezes until reactivated. Otherwise PeekMessage.
  - WM_QUIT: QuitGame (0x491f70, only if active), then PDShutdownSystem.
- **H "Task Switched" message:** `MainGameLoop` 0x492c40 calls 0x51afd0 (state==1) and, once a second, `NewTextHeadupSlot`-like 0x44a380(4,0,500,−4, `GetMiscString(0x8b)` 0x514d70). TEXT.TXT line 139 is "TASK SWITCHED - RUNNING IN BACKGROUND - USE ALT-TAB". Glide only; D3D blocks instead.
- **H Mouse:** `PDGetMousePosition` 0x51c900: if state≠2, return the last position (0x6ad458/0x6ad45c). Else GetCursorPos → ScreenToClient(hwnd). `GetMousePosition` 0x483c10 clamps to the graf spec total w/h (640×480). ClientToScreen is imported but called only from dead SSDX code (0x500d0b).
- **H -SCALEMOUSE (0x6ad468):** when set and the device is not "D3D", maps desktop cursor pixels to 640×480 as `x / (SM_CXSCREEN * (1/640))` = `x*640/SM_CXSCREEN`, `y*480/SM_CYSCREEN` (doubles 0x58b748 = 1/640, 0x58b750 = 1/480).
  - In the WndProc double-click path (0x51b550–0x51b5c9) it works.
  - **In `PDGetMousePosition` the branch is dead:** `test eax,eax; jmp 0x51c9ad` at 0x51c93b (bytes `85 C0 EB 6E`). It looks like a one-byte `je`→`jmp` patch, so normal mouse reads never scale (M, the origin of the patch is unknown).
  - Purpose: with Glide the window stays at desktop size (MoveWindow to SM_CX×SM_CY), so client coordinates are desktop pixels.
- **H Windowed/debug leftovers:** the SSDX DirectDraw code (`SSDXDirectDrawSetup`, `LocalWindowedDDSetup`, `SSDXWindowPosOrSizeChanged`, lock/unlock) at 0x500070–0x5002cf and 0x500750–0x500d4f has no references from outside itself (raw rel32/abs scan). It is dead code from the software build.
  - `SSDXStop` 0x500590 only "restores the display mode" if the DDraw object 0x6aaa18 exists, and it never does in HW.
  - `PDEnterDebugger` 0x51a7b0 shows the cursor.
  - No windowed switch exists.
- **H Shutdown `PDShutdownSystem` 0x51c110:**
  1. SSDXStop/Release.
  2. DirectInput release.
  3. ShowCursor(TRUE).
  4. `SetWindowPos(hwnd,0,-100,-100,64,64,0x414)`.
  5. Pumps messages.
  6. **`SendMessageA(HWND_BROADCAST, WM_SHOWWINDOW, TRUE, 0)` (0x51c1e4).**
  7. MessageBoxA on a fatal error (0x51c20c).
  8. DestroyWindow, ExitProcess.

## 5. What a hidden test mode must intercept

All exe-side items are **H** (call sites listed). Driver-side items are **H** for the calls, **M** for the route.

1. **Single instance (both directions):** FindWindowA 0x51ad11 exits if the player's game is running. A hidden test window with the stock class would also make the player's launch exit. Rename the class: point 0x6621d0 at a test class name, or hook FindWindowA/RegisterClassA/CreateWindowExA.
2. **CreateWindowExA** 0x51adb1: drop WS_VISIBLE, add WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE, move off-screen. Also SetFocus 0x51adc9 and UpdateWindow.
3. **SetWindowPos** in WndProc (0x51b177, 0x51b2d3, 0x51b413) and shutdown (0x51c1b5): they resize the window to the full desktop on activation. Neutralise them, or never deliver WM_ACTIVATEAPP.
4. **SetForegroundWindow** 0x51cb0b (Glide background state, every pump). Also force 0x6621e0 to 2 so the game never enters the background states (no GetMessage block, no task-switch text).
5. **SendMessageA(HWND_BROADCAST, WM_SHOWWINDOW)** 0x51c1e4, and **MessageBoxA** 0x51c20c / 0x51ae38: block them and log.
6. **ShowCursor(FALSE)** 0x51b9fd and SetCapture 0x51b67c: harmless but cosmetic; skip in test.
7. **Drivers** (the display-mode change happens only here). Their USER32/glide2x/DDRAW imports are resolved through the exe's IAT `LoadLibraryA` 0x5891a8 and `GetProcAddress` 0x5891a4 (via 0x53fb50/0x53fb70), so one hook on those two slots can substitute:
   - `SetForegroundWindow`, `MoveWindow`, `SetWindowLongA`, `GetActiveWindow`;
   - for D3D: `DirectDrawCreate` (wrap SetCooperativeLevel→DDSCL_NORMAL, no SetDisplayMode, no Flip to primary);
   - for Glide: `glide2x` (nGlide opens its own fullscreen) by a stub or a windowed config.
   - Safest (L): run the test with `-d3d` plus a DDRAW wrapper that never touches the mode, or replace the `BrDevBeginVar` call at 0x51c371.
   - The game reads `gScreen->identifier` (+4) and dereferences it, and uses `gBack_screen->pixels`, so a fake device must supply both.
   - Note that 0x51c300 also performs `DirectSoundCreate` (SSDXStart), so replacing the whole function skips sound init.
8. **DirectInput** keyboard is NONEXCLUSIVE|FOREGROUND: no focus effect. In a hidden window it never acquires, so test input must be injected elsewhere (L).
9. DirectSound uses the hwnd at 0x459da7 (cooperative level, M); no focus effect expected (L).

## names.tsv

```
0051aaa0	WinMain	H; switch parse, FindWindowA, CreateWindowExA 4x4 WS_POPUP|WS_VISIBLE
0051b0c0	Win32WindowProc	H; WM_ACTIVATEAPP, mouse, SC_CLOSE
004924a0	GameMain	M; dethrace name
0047dd20	InitialiseDeathRace	M; string "AFTER INITIALISING BRENDER"
00503c50	DoProgram	M; calls MainGameLoop
00492950	MainGameLoop	M; race loop, Sleep(1500), task-switch headup
00492680	UpdateFramePeriod	M; clamps gFrame_period to [10,1000] ms
004e4e40	RenderAFrame	M; world, HUD, PDScreenBufferSwap
004e54f0	RenderView	M; per-camera render wrapper
004e5680	RenderSceneWithCamera	M; BrPixelmapFill + BrZbSceneRenderBegin/Add/End
004e5b00	RenderHUDModels	M; 2D root 0x74ca00 via HUD camera
0047e560	InitHUDCamera	H; parallel camera width/height = gBack_screen dims
0046cf10	InitFrontEndCamera	H; parallel camera 640.0 x 480.0 constants
00464e40	RenderTextModels	M; forces back 640x480 around BrZbSceneRender
004e4980	SetBRenderScreenAndBuffers	M; dethrace name
004e4b40	AdjustRenderScreenSize	M; dethrace name
0047dc30	ReinitialiseRenderStuff	M; dethrace name
0047b3e0	CalcGrafDataIndex	H; matches gGraf_data to graf spec size
0047b880	ClearEntireScreen	M; fill screen/back/depth, swap
0051c290	PDInitScreenVars	H; gGraf_spec_index = 0x662200
0051c270	PDInitScreen	M; empty stub
0051c300	PDAllocateScreenAndBack	H; BrDevBeginVar 640x480x16 RGB565
0051c520	PDScreenBufferSwap	H; BrPixelmapDoubleBuffer(gScreen,gBack_screen)
0051c840	PDSetPalette	M
0051c850	PDSetPaletteEntries	M; SetPaletteEntries + DoubleBuffer
0051c900	PDGetMousePosition	H; GetCursorPos/ScreenToClient, scale branch dead
00483c10	GetMousePosition	H; clamps to graf spec size
0051afd0	PDIsTaskSwitchedBackground	H; returns state==1
0051b9f0	PDInitialiseSystem	M; ShowCursor(0), DirectInput
0051cbf0	Win32InitInputDevice	M; DirectInput keyboard NONEXCLUSIVE|FOREGROUND
0051cef0	PDReadKeyboard	M; pumps messages, reads DI state
0051cad0	Win32ServiceMessages	H; string; SetForegroundWindow when state==1
0051c110	PDShutdownSystem	H; string
0051a7b0	PDEnterDebugger	H; string
0051d7c0	Win32InitTimer	H; QueryPerformanceFrequency
0051d410	PDGetTotalTime	H; ms since start (QPC)
0051d990	PDGetTotalTimeMicro	H; microseconds
00514c30	GetTotalTime	M; game time
00514c70	GetRaceRealTime	M; real ms since race start
00514d70	GetMiscString	M; TEXT.TXT index -> string
0044a380	NewTextHeadupSlot	M; (4,0,500,-4,text)
00500500	SSDXStart	H; string; only DirectSoundCreate in HW build
00500590	SSDXStop	H; string
005006d0	SSDXRelease	M; releases DSound/DDraw
004d70c0	NewScreenOverlay	L; 640x480 tint quad
00525f50	BrV1dbBeginWrapper	M
0052cf20	BrBegin	M
00525860	BrV1dbBegin	M
00528d70	BrDevBeginVar	H; varargs token list
00528e10	BrDevBeginTV	M; "name,args" parse and token merge
005285c0	BrDevLoadDriver	M; loads BRENDER_DRIVERS, then image
005286d0	BrDrvLoadList	M; "name:args;..."
0052ffa0	BrImageReference	H; NAME.BDD own loader, LoadLibraryA, NAME.DLL
00530e70	BrImageLoad	H; in-process PE loader for .bdd
005301e0	BrImageLookupName	H; GetProcAddress for host images
0053fb50	HostImageLoad	H; LoadLibraryA
0053fb70	HostImageLookupName	H; GetProcAddress
00530460	BrSystemConfigBegin	M; registry/BRender.ini/env
00537e20	BrPixelmapMatch	H; 0=OFFSCREEN, 1=DEPTH 16
00537dc0	BrPixelmapFree	M
005382f0	BrPixelmapFill	M
00538640	BrPixelmapRectangleFill	M
005389e0	BrPixelmapDoubleBuffer	H; vtable +0x64
005226d0	BrZbSceneRender	M
00522c80	BrZbSceneRenderBegin	M
00522ea0	BrZbSceneRenderAdd	M
00522eb0	BrZbSceneRenderEnd	M
006621d0	gWindow_class_name	H; -> "Carma2MainWndClass"
006621d4	gMouse_click_x	H; double-click pos
006621d8	gMouse_click_y	H
006621dc	gRenderer_device_name	H; "3DFX_WIN" or "D3D" (-d3d)
006621e0	gApp_state	H; 2 active, 1 bg Glide, 0 bg D3D
00662200	gDefault_graf_spec_index	H; =1 (640x480)
00662208	gGraf_specs	H; [2], stride 0x34
00655f58	gGraf_data	H; [2], stride 0x520
00762324	gGraf_spec_index	H
0074d630	gReal_graf_data_index	M
0074b770	gGraf_data_index	M
0074b774	gCurrent_graf_data	H
0074d3e0	gScreen	H
0074d360	gBack_screen	H
00762128	gRender_screen	H
0068b8a4	gDepth_buffer	H
006ad47c	gUnused_back_copy	H; written once, never read
00705060	gX_offset	M
006baa2c	gY_offset	M
00703e24	gWidth	M
00703e20	gHeight	M
0074ca00	gHUD_root	M
0074cf74	gHUD_camera	H; "HUD_Camera"
0074d35c	gCamera	M; main camera actor
0075b940	gRearview_camera	M
0068b8a8	gRearview_screen	M
0075b93c	gRearview_depth_buffer	M
0074ca60	gScreen_bits	H; 16
0074cadc	gLock_often	H; set unless identifier "Voodoo Graphics"
0068be38	gRender_indent	M
006ad4c8	gWin32_hwnd	H
006acea4	gWin32_hinst	H
006ad460	gDesktop_cx	H; GetSystemMetrics(0) at start
006ad464	gDesktop_cy	H
006ad468	gScale_mouse	H; -SCALEMOUSE
0074d3dc	gAFE_flag	H; -AFE
0068b894	gNo_cutscenes	H; -NOCUTSCENE(S)
006ad458	gLast_mouse_x	H
006ad45c	gLast_mouse_y	H
006ad49c	gActivation_time	H; ms at WM_ACTIVATEAPP
006acea0	gDI_keyboard	H; IDirectInputDevice
006acc78	gDirectInput	H
006acc70	gQPC_available	H
0074d3e4	gFrame_period	H; ms, clamped 10..1000
006b5f40	gMisc_strings	H; TEXT.TXT lines
```
