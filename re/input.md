# Carmageddon II (Steam CARMA2_HW.EXE): keyboard, joystick and key mapping

Static analysis, 2026-11-02. Confidence: **H** = read from the instructions (capstone), **M** = decompiler or inference from structure/dethrace, **L** = guess to test in play. Addresses are VAs (image base 0x400000). The code is a close relative of Carmageddon 1's input.c / controls.c as decompiled by dethrace; most names below follow dethrace.

## 1. Input path

### DirectInput keyboard (PDInitDIKeyboard 0x51cbf0, called from 0x51b9f0)
- 0x51b9f0: `ShowCursor(0)`, builds the scan-code tables (0x51ba10), then creates DirectInput (H).
- `DirectInputCreateA(hInstance 0x6acea4, 0x500, &gDI 0x6acc78, 0)`: DirectInput **version 5** (H).
- `IDirectInput::CreateDevice` (vtbl+0x0c) with GUID_SysKeyboard built on the stack (6F1D2B61-D5A0-11CF-BFC7-444553540000) → `gDI_keyboard` 0x6acea0 (H).
- `SetDataFormat` (+0x2c) with the format object at 0x54e330 (statically linked c_dfDIKeyboard, M); `SetCooperativeLevel` (+0x34) on hwnd 0x6ad4c8 with flags **6 = DISCL_NONEXCLUSIVE|DISCL_FOREGROUND**; `Acquire` (+0x1c); failures log "Can't aquire keyboard; HRESULT %x" (H).
- It then initialises the DirectInput joystick (0x458860) and, if one is found, loads FFB/JOYSTICK settings (0x45c410, 0x45c0b0) (H).
- **Immediate mode only.** No `GetDeviceData`/`SetProperty(BUFFERSIZE)` on the keyboard; the only read is `GetDeviceState(256 bytes)` (+0x24) (H).
- WndProc 0x51b0c0 handles WM_ACTIVATEAPP: Acquire on activate, Unacquire (+0x20) on deactivate; gApp_active 0x6621e0 = 2 when active (H).

### Per-frame read: PDSetKeyArray 0x51cef0 (fastcall: key array, mark)
- Pumps messages (Win32ServiceMessages 0x51cad0), returns immediately unless active (0x6621e0==2), and ignores input for 1000 ms after re-activation (0x6ad49c) (H).
- Calls `GetDeviceState(0x100, buf)` at 0x51cfba; on failure it re-Acquires and retries once (0x51cff2), else clears the array ("Zeroing pKeys") (H).
- Loops key codes 0..150 (0x97): `scan = gScan_code[code]` (0x6b34a0); if `buf[scan] & 0x80`, `keys[code] = mark` (the poll counter), else clears it if it still holds this mark. Code 0/1/2-3 (scan 0xff) are the "any Shift/Alt/Ctrl" pseudo-keys built from the L+R scan codes of codes 8/5, 9/6, 10/7 (H).
- Each newly pressed key also updates gKeys_pressed 0x74d350 = `prev*256 + code+1` (feeds the cheat hash) (H).
- Callers: PollKeys 0x481eb0 (`gKey_poll_counter` 0x68bed4 ++), and the stale-poll paths of KeyIsDown 0x483040, PDKeyDown2 0x482590, PDAnyKeyDown 0x482a00, AnyKeyDown 0x482d70, WaitForNoKeys 0x4833b0, CyclePollKeys 0x4821c0 (all re-poll if more than 500 ms since the last poll) (H).
- Target array: **gKey_array 0x68bee0, int[151]**, indexed by the game's own key code (H).

### Scan codes → game key codes (PDInitScanCodes 0x51ba10)
- gScan_code[151] at 0x6b34a0 holds a DIK code per game code; also an ASCII table 0x6ad1f8 and shifted ASCII 0x6ac588 (H).
- Game key code = KEYNAMES.TXT line index − 2. KEYNAMES line 0 "[UNDEFINED]" is code −2 (the "unassigned" marker), line 1 "uh?" is −1. Proven by the Controls list drawing `gKey_names[gKey_mapping[x] + 2]` (0x472144–0x47214d) (H).
- Codes: 0 Shift(any), 1 Alt(any), 2 Ctrl(any), 3 "Command" = also Ctrl(any) (Mac leftover), 4 Caps, 5–10 R/L Shift/Alt/Ctrl, 11–20 digits 0–9, 21–46 a–z, 47 \`, 48 -, 49 =, 50 backspace, 51 return, 52 KP enter, 53 tab, 54 /, 55 \\, 56 ;, 57 ', 58 ., 59 ,, 60 [, 61 ], 62 #, 63 esc, 64 ins, 65 del, 66 home, 67 end, 68 pgup, 69 pgdn, 70 left, 71 right, 72 up, 73 down, 74 numlock, 75–80 KP / * - + . =, 81–90 KP 0–9, 91–102 F1–F12, 103 prtsc, 104 scrlk, 105 pause, 106 space, 107–138 Joy1 B1–B32, 139–142 Joy2 B1–B4, 143–146 Joy1 L/R/U/D, 147–150 Joy2 L/R/U/D (H).
- **Never pressable:** 62 "#" and 80 "KP =" (scan 0), 105 "pause" (scan 0xff, not handled by the 0–3 special cases) (H). These make safe "unbound" values in a KEYMAP file. Caps (4) is skipped by PDAnyKeyDown (H).

### Other key-reading helpers (input module 0x481e90–0x483e20)
| Address | Name | What |
|---|---|---|
| 0x483040 | KeyIsDown(map_index) | `gKey_array[gKey_mapping[i]]`; −1 = any of gGo_ahead_keys 0x657200 {51 return, 52 KP enter, 106 space}; −2 = true (H) |
| 0x4833a0 | KeyIsDownNoPoll | same without the stale-poll check (H) |
| 0x482550 | PDKeyDown(raw code) | edge/auto-repeat via PDKeyDown2 when gEdge_trigger_mode 0x68c1c4 is set; codes < 11 (modifiers) are level-triggered (H) |
| 0x482590 | PDKeyDown2(raw code, reset) | 0 up, 1 first press, 2 held, 3 repeat (300 ms) (H) |
| 0x482a00 | PDAnyKeyDown | scans codes 142..0 (skips 4); returns the first held code or −1 (H) |
| 0x482d70 | AnyKeyDown | the same plus mouse buttons (M) |
| 0x4833b0 | WaitForNoKeys | spins until no key/mouse is down (M) |
| 0x482f10 | KevKeyService | cheat-code hash over gKeys_pressed; emits a code pair after 1 s idle; callers 0x443c50/0x443c90 (M, matches dethrace) |

### Text entry and Windows messages
- WndProc 0x51b0c0 handles **WM_CHAR** (0x102) only: printable chars (0x20–0x7e) go into a 20-byte buffer 0x6ace88 (count 0x6ad4b0) and log "KEY PRESSED, BUFFER NOW IS: '%s'" (H). **No WM_KEYDOWN/WM_SYSKEYDOWN handling** (H).
- Consumers: PDGetChar 0x51b040 ("KEY RETURNED %d") from the in-race message editor 0x444910 and interface text entry 0x46e470; PDClearCharBuffer 0x51b0a0 (H for the calls, M for the roles).
- Cheats do **not** use WM_CHAR. They are typed as raw DirectInput key codes and hashed by KevKeyService (M).
- Mouse: WM_xBUTTONDOWN/UP flags plus GetCursorPos/ScreenToClient (H).

## 2. Key-mapping tables

### Files and globals
- **gKey_mapping 0x74b5e0, int[77]** (0x74b5e0..0x74b714). Each line of a KEYMAP file is one slot: a game key code, so KEYNAMES line = value + 2 (H).
- **gKey_map_index 0x68b88c** = OPTIONS.TXT `KeyMapIndex`, read by LoadOptions 0x48d8f0 (write at 0x48e1c4) and written by SaveOptions 0x48d190 ("KeyMapIndex %d") (H). The file name is made by overwriting the X of "KEYMAP_X.TXT" with `'0'+index`, with no range check (H). An index above 9 gives a non-digit name (L).
- **LoadKeyMapping 0x487e10** (startup, from 0x47dd20): reads 77 `%d` from `data\KEYMAP_<idx>.TXT`; FatalError(9) "Couldn't open Key Map file" if it is missing (H).
- **Controls screen** (spec at 0x604940 "Controls", tick InterfaceScreenTick 0x470c20):
  - Start 0x4725f0 loads KEYNAMES.TXT into gKey_names 0x688458 (153 strings). It reads all four KEYMAP_0..3 into 0x6869e0 (4×77) and copies the current set back into gKey_mapping (H).
  - Element 0x27..0x2a (buttons A–D) → 0x472440: **saves** the current set to KEYMAP_<old>.TXT (`%d\r\n`), sets index = element − 0x27, loads KEYMAP_<new> (H).
  - Elements 0x2b..0x3e (10 rows × name/key columns) → Controls_GetNewKey 0x472b00. Elements 0x3f/0x40 scroll (0x4725a0/0x4725c0). OK = element 0x43 → Controls_Ok 0x472d80. End/save 0x472a30 writes KEYMAP_<idx>.TXT (H).
  - Element 0x24/0x25 → 0x472400 sets the joystick config flag +0xe0 to 1/0 (H; meaning in §4).
- **DKEYMAP0..3.TXT** (factory defaults) are read only by 0x4b363b. That is case 2 of a legacy Carmageddon 1-style options screen: 0x4b39f0 "BEFORE ENTERING OPTIONS", spec 0x65ca90 run through the old DoInterfaceScreen 0x4846e0, 18-row table 0x65bd30. No caller or pointer to 0x4b39f0 exists, so DKEYMAPs are effectively unused (M). The legacy row table is off by one against the current layout (e.g. 48,49,46,47 vs 49,50,47,48), which is more evidence that it is dead (M). `INTRFACE\keyboardcontrols.txt` (18 items: Left, Right, Accel, Brake, Handbrake, Repair, Recover, Horn, ...) belongs to that dead screen. The live screen is `controls.txt` (M).
- The four shipped DKEYMAPs equal the shipped KEYMAPs, except that testrun's Keymap_0 has Recover = r and Wheelspin = z (user edits) (H).

### Line index → action → default key
Labels for the 29 editable rows come from TEXT.TXT lines 140+row, through the row table gControls_rows 0x604888 = {49,50,47,48,54,45,60,58,56,46,57,67,68,69,71,61,62,63,64,74,59,70,72,73,75,76,35,65,66} (H). Hidden-line meanings come from the toggle table 0x5900a0 and the KeyIsDown call sites. Default = DKEYMAP0 (keymap A); keymap B (index 1) differs where shown.

| Line | Action | Where used | Default (A) | B |
|---|---|---|---|---|
| 0 | Escape: in-race menu / exit replay / close map | 0x493ba0, 0x4032ac, 0x448f32 (H) | esc | |
| 1 | Ctrl+Q quit (with line 7) | CheckQuit 0x5134b0 (H) | q | |
| 2 | (Carmageddon 1 Ctrl+S save) unused, no reference found | (M) | s | |
| 3 | (Carmageddon 1 Ctrl+L load) unused | (M) | l | |
| 4 | Ctrl+A abort race (sets 0x74d1a0) | toggle #7 → 0x441490 (H) | a | |
| 5 | Shift+D screen dump "DUMP" | toggle #9 → 0x518780 (H/M) | d | |
| 6 | unused (Carmageddon 1 help?) | (M) | / | |
| 7 | Ctrl modifier | (H) | Command(=Ctrl) | |
| 8 | Shift modifier (Shift+Tab; reverse camera cycle) | 0x40e7f6, 0x40eac2, toggle #3 (H) | Shift | |
| 9 | Alt modifier (Alt+Tab map variant) | toggle #5 (H) | Alt | |
| 10 | unused | (M) | KP0 | |
| 11 | Ctrl+KP1 toggle flying ("We have lift off!!"), cheat-gated (0x67c46c), single-player only | toggle #29 → 0x4443c0 (H) | KP1 | |
| 12 | fly down/up while flying (control bit 0x400000) | 0x444212 (H) | KP3 | |
| 13 | car control bit 0x800000 (unknown) | 0x443f40 (H; meaning L) | KP5 | |
| 14 | fly up/down while flying (bit 0x200000) | 0x4441fe (H) | KP9 | |
| 15–17, 38–44 | edit-mode function keys 0,8,9,1–7 (with Shift/Alt/Ctrl variants); active only with the dev cheat (0x68b8e0 == 0xa11ee75d, or 0x564e78b9 in net) | toggles #19–28 → table 0x5904f0 (H gating, M meaning) | 0,8,9,1..7 | |
| 18 | F4: cycle edit mode (same cheat gate) | toggle #11 → 0x4414b0 (H) | f4 | |
| 19–25 | F5,F6,F7,F8,F10,F11,F12 edit-mode functions (cheat) | toggles #12–18 (H) | f5..f12 | |
| 26, 27 | unused | (M) | r, i | |
| 28–30 | unused (but block the key on the Controls screen) | (M) | f1, f2, f3 | |
| 31–34 | Up/Down/Left/Right: camera look (bits in 0x79efa4); with Shift move the head-up minimap (saved to OPTIONS.TXT); scroll the full map when MapMode 0x75b9a4 = 2 | 0x444270, 0x497620, 0x442f90 (H) | arrows | |
| 35 | Mini-map toggle | toggle #0 → 0x4420e0 (H) | m | |
| 36 | unused | (M) | p | |
| 37 | Sound on/off (plain S, works in menus too: in_game_only = 0) | toggle #8 → 0x455a50 (H; "sound" M) | s | |
| 45 | Repair (hold) | 0x4433e3 (H) | backspace | / |
| 46 | Cockpit toggle | toggle #43 → 0x40e900 (H) | c | |
| 47 | Turn left | 0x443ecd (H) | KP4 | , |
| 48 | Turn right | 0x443ee0 (H) | KP6 | . |
| 49 | Accelerate | 0x44404c (H) | KP8 | a |
| 50 | Brake/reverse | 0x4440b1 (H) | KP2 | Alt |
| 51–53 | unused (but block the key) | (M) | q, w, e | d, f, g |
| 54 | Handbrake | 0x443f54 (H) | space | z |
| 55 | unused | (M) | KP- | |
| 56 | Wheelspin | 0x444180 (H) | x | x |
| 57 | Map toggle (Tab; Shift/Ctrl/Alt+Tab variants) | toggles #2–5 (H) | tab | |
| 58 | Action replay | 0x4e69f5, 0x4e7002 (H) | KP enter | return |
| 59 | Horn | 0x444226 (H) | h | |
| 60 | Recover | toggle #6 → 0x442300, 0x4429db (H) | insert | Shift |
| 61 | Activate powerup | 0x4db515 (H) | Alt | |
| 62 | Inventory | toggle #39 (H) | ; | |
| 63 | Inventory left | toggle #40 (H) | [ | |
| 64 | Inventory right | toggle #41 (H) | ] | |
| 65 | Screen smaller | toggle #30 (H) | - | |
| 66 | Screen bigger | toggle #31 (H) | = | |
| 67 | Buy armour | toggle #32 (H) | delete | |
| 68 | Buy power | toggle #33 (H) | end | q |
| 69 | Buy offence | toggle #34 (H) | pg-down | w |
| 70 | View net players | toggle #35 (H) | space | e |
| 71 | Send message (chat) | toggle #36, 0x444a9c (H) | \` | |
| 72 | Target lock | toggle #37 (H) | t | |
| 73 | Cycle target | toggle #38 (H) | y | |
| 74 | Flap doors | toggle #1 → 0x42dd50 (H) | return | |
| 75 | Change camera | toggle #42 → 0x444ed0 (H) | KP * | |
| 76 | Cycle head-ups (the readme's F1) | toggle #10 → 0x444f40 (H) | f1 | |

### Toggle table gToggle_array 0x5900a0 (44 × 24 bytes, CheckToggles 0x442e90)
- Entry layout `{key1 map line, key2 map line or −2, in_game_only, exact_modifiers, last_state, action}` (H).
- An action fires on the rising edge of `KeyIsDown(key1) && (key2==−2 ? (!exact || no Shift/Alt/Ctrl held) : KeyIsDown(key2))` (H).
- Entries with key2 = −2 are skipped while 0x68b858 or 0x67c474 is set. Those are probably the "typing a message" states (M).
- CheckToggles runs from the race loop 0x492950 and from 0x492050/0x492180 (the interface idle hook). So the in_game_only = 0 toggles (S sound, Shift+D dump, F4) also fire in menus (H/M).

### What the Controls screen edits (H)
- Only lines 28..76 are checked for conflicts, and only the 29 lines in gControls_rows are shown.
- Controls_GetNewKey 0x472b00 waits on PDAnyKeyDown. Esc (63) cancels.
- If the new key already sits in a **shown** line, that line becomes −2 ([UNDEFINED]).
- If it sits in a **hidden** line within 28..76 (28–34, 36–44, 51–53, 55), the key is **refused** (sound 0xc1c, key dropped). Loop at 0x472ca2..0x472cec.
- Joystick axis codes 143–150 can't be captured (PDAnyKeyDown scans from 142 down).
- Controls_Ok 0x472d80 refuses to leave while any line 28..76 is −2. With the joystick disabled, it also refuses while a line uses a joystick-button code 107–138. It forces a re-capture of that line.

## 3. Hard-coded keys (raw codes, not in any keymap)

### Menus (all through PDKeyDown 0x482550; 219 call sites)
| Keys | Function | Sites |
|---|---|---|
| Up or KP8 / Down or KP2 | previous / next item | InterfaceScreenTick 0x47124d/0x47125b, 0x4715dc/0x4715ee; list widgets 0x4673cf.., 0x46946a.., 0x46a4da.., 0x46b00e.., 0x46c1aa.., 0x473942.. (H) |
| Left or KP4 / Right or KP6 | move within a hotspot group | 0x471600/0x471612, 0x471624/0x471632 (H) |
| Left / Right (no keypad) | slider adjust | 0x4710c3, 0x471108 (H) |
| Return or KP Enter | activate / OK | 0x4718b3/0x4718c1 and in each list widget (0x4675e7, 0x4696e3, 0x46a7c4, 0x46b219, 0x46bb5a, 0x46c3c8, 0x46f2e3, 0x47062b, 0x473ab0) (H) |
| Esc | back (on Controls it runs Controls_Ok first) | 0x4718a1, 0x467541, 0x469619, 0x46a699, 0x46b182, 0x46c31c (H) |
| any key / mouse | attract and timeout reset | PDAnyKeyDown in 0x470c20 (H) |
| arrows, KP 2/4/6/8, Esc | legacy DoInterfaceScreen 0x4846e0 (Carmageddon 1 screens still in use, e.g. 0x4b39f0 family) | 0x484877..0x484be7 (H) |

There is no remap table: every site is `mov ecx, imm8; call 0x482550` (H). So WASD can't drive the menus.

### In race (outside the keymap)
| Key | Effect | Site |
|---|---|---|
| Shift / Alt (raw 0/1) | while held, the arrow keys (lines 31–34) stop moving the camera; Shift+arrows move the minimap | 0x444271 (H) |
| Ctrl (raw 2) | camera bit 0 (0x79efa4) | 0x44431c (H) |
| Shift/Alt/Ctrl (raw 0–3) | `exact_modifiers` gate for toggles; edit-mode function variants | 0x442f01.., 0x441629.. (H) |
| Shift/Alt/Ctrl/Caps (raw 0,1,2,4) | ×10/×20/×40/×80 multipliers in dev give-powerup functions | 0x4dbdb0..0x4dc14c (H, dev) |
| Return / KP Enter | skip the end-of-race countdown (0x74d62c) | 0x493eca/0x493ed8 (H/M) |
| Return | confirm "can't afford to recover" (TEXT 96) | 0x4437b9 (H/M) |
| Insert/Delete/Home/End/PgUp/PgDn | debug list browser, only when 0x68b8e8 (edit/debug display) is on | 0x45ae3b..0x45b645 (H gate) |
| l / o (raw 32/35) | dev function in 0x4d84a0/0x4d8a20 | (H sites, L meaning) |
| WM_CHAR text | chat (line 71) and name entry | 0x51b0c0 (H) |

The readme's keys: F1 cycles HUD = keymap line 76. Shift+cursor moves the minimap = line 8 semantics but raw Shift, plus lines 31–34. Up raises the bumper camera = line 31 → camera bit (M).

### WASD collisions
- **Menus:** no WASD at all (hard-coded arrows/keypad) (H).
- **Controls screen:**
  - With keymap A loaded, binding **W** (line 52), **S** (line 37), Q (51) or E (53) is refused. So are arrows, F1–F3, P, 1–7 and KP- (H).
  - **A** and **D** can be bound (lines 4/5 are below 28), but Ctrl+A then aborts the race and Shift+D dumps the screen (H).
  - With keymap B, D/F/G (lines 51–53) are refused instead, and q/w/e are already Buy power/offence/View net players (H).
- **In race, if W/S are forced in by editing KEYMAP_n.TXT:**
  - Plain **S** also fires line 37 = sound off/on (exact_modifiers, any time) (H/M).
  - **W**/Q/E hit only unused lines (M).
  - The arrow keys stay camera/minimap/full-map scroll (H).

## 4. Joystick

- **DirectInput joystick (primary).**
  - 0x458860 calls `DirectInputCreateA(GetModuleHandle(0), 0x500, &gDI_joy 0x686158, 0)`, then `EnumDevices(DIDEVTYPE_JOYSTICK=4, cb 0x459c70, .., DIEDFL_ATTACHEDONLY)` (H).
  - The callback stores the name (0x79d9cc+i*0x68) and `CreateDevice`. It calls `SetDataFormat(0x54d310` = c_dfDIJoystick, M`)`, then `QueryInterface(IID 0x58b398` → IDirectInputDevice2 for `Poll`, vtbl+0x64`)` into gDI_joy_dev[16] 0x79e060 (H).
  - `SetCooperativeLevel(hwnd, 9 = EXCLUSIVE|BACKGROUND)`, `SetProperty(DIPROP_RANGE, 0..0xffff)` on X and Y (H).
  - Every device is Acquired ("Joystick #%d acquired"), and gJoystick_index 0x595f88 = 0 (−1 = none/disabled) (H). Force feedback (IFORCE2, FFB.TXT) is set up by 0x457760/0x457ff0/0x458520 (M).
- **Reads:**
  - DIGetJoyX 0x459050 / DIGetJoyY 0x459370: Poll + GetDeviceState(DIJOYSTATE 0x50), scaled and clamped 0..0xffff, reacquire on DIERR_INPUTLOST (H).
  - Buttons: 0x459690 → SetJoyButtonKeys 0x45b360 writes the 32-bit mask into gKey_array[107..138] (Joy 1 B1–B32) (H).
- **Into the game:**
  - The polling functions (e.g. 0x481eb0) turn axes into gJoy_array 0x74b5c0[8]: [0]/[1] left/right amount, [2]/[3] up/down, [4..7] joystick 2 (H).
  - Threshold crossings (<0x4000, >0xc000) set the key codes 143–150 (Joy L/R/U/D) (H).
  - PollCarControls 0x443e80 uses digital keys when left/right are mapped to codes < 0x8f, otherwise analog X steering. Accelerate/brake come from the keys, else from Y up/down (H). Config flag +0xe0 = 1 turns the axes into digital thresholds (>10) instead of analog (H; this is the Controls element 0x24/0x25 radio, L which label).
- **WinMM `joyGetPosEx` (vestigial):**
  - PDReadJoysticks 0x51d1d0 calls joyGetPosEx(0/1) only when no DI joystick is active and the "present" flags 0x6ad4b8/0x6ad4bc are set. Those flags are only ever set to 1 on the DI path, which then returns before the call (H). So the WinMM path effectively never runs (M).
  - The platform getters 0x51d230..0x51d3d0 map DI X/Y for joystick 1. Joy2 X/Y = JOYINFOEX Rpos (rudder, if JOYCAPS_HASR) or joystick #2. Joy2 B1–B4 = joy0 buttons 5–8 (H).
- **JOYSTICK.TXT** (LoadJoystickConfig 0x45c0b0, SaveJoystickConfig 0x45bd90; per-device record writer 0x45b920, reader 0x45bbc0; config struct 0xec bytes, current via 0x459f80) (H):
  ```
  &Device_Name_with_underscores
  <n>                  count of button entries (32)
  <n lines>            +0x54 int[32]: button table (M: button → Joy B index)
  <int>                +0xd4  (L: active/axis option)
  <f>, <f>             +0xd8, +0xdc: two sliders on Controls (M: X/Y sensitivity; PCGW "minus inverts Y" fits, L)
  <int>                +0xe0: digital/analog mode (H usage)
  <int>                +0xe4: slider in % (L: FFB strength or dead zone)
  <int>                +0xe8: joystick enabled (1) / disabled (0) → EnableJoystick 0x45a070 / DisableJoystick 0x45a0c0
  ```
  The Steam file is empty (0 bytes), so defaults apply until the Controls screen saves one (H).

## 5. Recommended patch points (we proxy dinput.dll)

1. **One choke point for all keyboard input: the keyboard's `GetDeviceState`.**
   - Every key test in the game (mapped actions, hard-coded menu keys, modifiers, edit mode, cheats, Controls-screen capture) reads gKey_array, which is filled only by PDSetKeyArray 0x51cef0 from one `GetDeviceState(256)` (H).
   - Wrap `IDirectInputA::CreateDevice(GUID_SysKeyboard)` in the proxy, and return a device whose `GetDeviceState` post-processes the 256-byte DIK buffer. Uses: remap DIK→DIK, and OR in synthetic keys from XInput.
   - No exe patch needed, and it survives the 1-s reactivation guard and the reacquire logic. Text entry is untouched (WM_CHAR).
   - The catch: a single DIK map applies everywhere. Menu and race remaps need a context flag. Candidates: the race loop 0x492950 is active, or `CheckToggles(racing)`; a clean global still needs identifying (L).
2. **Menu keys, configurable:** hook **PDKeyDown 0x482550** (5-byte detour at entry) with a raw-code translation table. Every hard-coded menu test goes through it with an immediate code (51/52/63/70–73/83/85/87/89), while in-race actions go through KeyIsDown/gKey_mapping. So remapping menu codes there leaves driving alone. Keep codes 0–4 (modifiers) identity.
3. **In-race actions:** keep the game's own gKey_mapping/KEYMAP files. To let WASD (and Q/E/P/F2/F3/KP-) be bound on the Controls screen, use either fix:
   - (a) Data-only: ship KEYMAP files whose unused hidden lines 2, 3, 6, 10, 26–30, 36, 51–53, 55 hold 80 (KP =, never pressed). Move line 37 (sound toggle) to 80 or another key too, so S is free.
   - (b) Code: narrow the conflict loop in Controls_GetNewKey 0x472ca2..0x472cec, so hidden lines never refuse a key. Do the same in Controls_Ok 0x472d80 (start index 0x1c).
   - Warn about Ctrl+A (abort) and Shift+D (dump) when A/D are bound.
4. **XInput:**
   - Best fidelity: present a **virtual DirectInput joystick** from the proxy. Return one device in `EnumDevices(DIDEVTYPE_JOYSTICK)` on the second DirectInput object (0x458860). Implement Device2 `Poll`/`GetDeviceState(DIJOYSTATE)`, `SetProperty(DIPROP_RANGE)`, coop level 9 and Acquire.
     - Map left stick X → lX.
     - Map the triggers → lY (RT = up = accelerate, LT = down = brake, matching gJoy_array[2]/[3]).
     - Map up to 32 buttons → rgbButtons. They then become "Joy 1 B1..B32", bindable on the Controls screen, and the game's analog steering/throttle code works unchanged.
   - Add **keyboard injection** for menus: D-pad → DIK arrows, A → Return, B → Esc, Start → Esc in race. Menus never read the joystick (only raw keys and the mouse) (H).
   - JOYSTICK.TXT keys its settings by device name, so give the virtual pad a fixed name.
   - Cheaper fallback: inject everything as DIK keys (digital steering only).

## names.tsv
```
0051b0c0	MainWndProc	WM_CHAR → char buffer, WM_ACTIVATEAPP acquire/unacquire, mouse buttons; no WM_KEYDOWN
0051b040	PDGetChar	pops one char from the WM_CHAR buffer ("KEY RETURNED %d")
0051b0a0	PDClearCharBuffer	"KEYBOARD BUFFER CLEARED"
0051b9f0	PDInitInput	ShowCursor(0), scan tables, DirectInput keyboard
0051ba10	PDInitScanCodes	fills gScan_code and the ASCII tables
0051cbf0	PDInitDIKeyboard	DirectInputCreateA v5, SysKeyboard, NONEXCLUSIVE|FOREGROUND, Acquire
0051cad0	Win32ServiceMessages	message pump
0051cef0	PDSetKeyArray	GetDeviceState(256) → keys[code]=mark via gScan_code
0051d1d0	PDReadJoysticks	WinMM joyGetPosEx path (vestigial)
0051d230	PDGetJoy1X	DI X or -1
0051d250	PDGetJoy1Y	DI Y or -1
0051d270	PDGetJoy2X	WinMM Rpos/joy#2 X
0051d2a0	PDGetJoy2Y	WinMM joy#2 Y
0051d2c0	PDGetJoy1Button1	(..0x51d320 buttons 2-4)
0051d340	PDGetJoy2Button1	(..0x51d3d0 buttons 2-4)
00481eb0	PollKeys	poll counter++, PDSetKeyArray, joystick → key array
00482160	ResetPollKeys	clears stale keys, counter=0
004821c0	CyclePollKeys	re-poll if >500 ms stale
00482550	PDKeyDown	raw-code key test (edge mode aware); all hard-coded keys
00482590	PDKeyDown2	0 up / 1 press / 2 held / 3 repeat
00482770	SetJoystickKeys	joystick buttons/axes into the key array
00482a00	PDAnyKeyDown	first held code 142..0 or -1
00482d70	AnyKeyDown	key or mouse
00482f10	KevKeyService	cheat-code hash from gKeys_pressed
00483040	KeyIsDown	gKey_array[gKey_mapping[i]], -1 go-ahead keys, -2 true
004833a0	KeyIsDownNoPoll	same without stale check
004833b0	WaitForNoKeys	spin until nothing held
004824c0	EitherMouseButtonDown	M
00483c10	GetMousePosition	M
00487e10	LoadKeyMapping	reads KEYMAP_<idx>.TXT into gKey_mapping
00442e90	CheckToggles	walks gToggle_array; param racing
00442f90	CheckOtherRacingKeys	full-map scroll, repair hold, recover/race-over logic
00443e80	PollCarControls	steering/accel/brake/handbrake/wheelspin/horn bits → 0x75cefc
00444270	PollCameraKeys	arrow keys → camera bits 0x79efa4; Shift/Alt → minimap move
00497620	MoveHeadupMapByKeys	Shift+arrows move minimap, SaveOptions
005134b0	CheckQuitKeys	Ctrl+Q
004420e0	ToggleMiniMap	line 35
00444610	ToggleMap	line 57
0040e7f0	CycleCamera	line 75 (Shift = backwards)
00441490	AbortRaceKey	Ctrl+A sets 0x74d1a0
004414b0	CycleEditMode	F4, dev cheat only
004443c0	ToggleFlying	Ctrl+KP1, cheat-gated
00455a50	ToggleSound	plain S (line 37)
00518780	DumpScreen	Shift+D
00470c20	InterfaceScreenTick	new front-end loop; hard-coded menu keys
004846e0	DoInterfaceScreen	Carma1-style menu loop; hard-coded menu keys
004b39f0	LegacyOptionsScreen	"BEFORE ENTERING OPTIONS"; no caller found (dead)
004b3220	LegacyControlsHandler	only user of DKEYMAPx (case 2 at 0x4b363b)
004720e0	Controls_DrawList	name = gKey_names[mapping+2]
00472210	Controls_SetJoyItems	greys joystick items, slider values
00472400	Controls_SetJoyMode	elements 0x24/0x25 → config +0xe0
00472440	Controls_SelectKeymap	A-D buttons: save current, load KEYMAP_<n>
004725a0	Controls_ScrollUp
004725c0	Controls_ScrollDown
004725f0	Controls_Start	loads KEYNAMES, all KEYMAPs
00472a30	Controls_End	saves KEYMAP_<idx>.TXT
00472b00	Controls_GetNewKey	capture + conflict rules (hidden lines refuse)
00472d80	Controls_Ok	forces re-assign of unassigned lines
0048d8f0	LoadOptions	OPTIONS.TXT incl. KeyMapIndex
0048d190	SaveOptions	writes "KeyMapIndex %d"
00458860	DIJoystickInit	second DirectInput, EnumDevices(joystick)
00459c70	DIEnumJoystickCb	CreateDevice, c_dfDIJoystick, Device2, coop 9, range 0..ffff
00459050	DIGetJoyX
00459370	DIGetJoyY
00459690	DIGetJoyButtons	M
0045b360	SetJoyButtonKeys	32 buttons → gKey_array[107..138]
00459f80	GetJoyConfig	current device config (0xec bytes)
0045a070	EnableJoystick
0045a0c0	DisableJoystick
0045a0f0	JoystickActive
0045c550	GetJoyDigitalMode	config +0xe0
0045c570	SetJoyDigitalMode
0045c8d0	GetJoySlider1	config +0xd8 float
0045c8f0	GetJoySlider2	config +0xdc float
0045c7f0	GetJoySlider3	config +0xe4 int %
0045c0b0	LoadJoystickConfig	JOYSTICK.TXT
0045bd90	SaveJoystickConfig	JOYSTICK.TXT
0045b920	WriteJoystickRecord	"&%s", ints, "%.3f, %.3f"
0045bbc0	ReadJoystickRecord
006acc78	gDI	IDirectInputA (keyboard)
006acea0	gDI_keyboard	IDirectInputDeviceA
00686158	gDI_joy	IDirectInputA (joysticks)
0079e060	gDI_joy_dev	IDirectInputDevice2A*[16]
00595f88	gJoystick_index	-1 = none/disabled
006621e0	gApp_active_state	2 = active
006b34a0	gScan_code	int[151] DIK per game key code
006ad1f8	gASCII_table	int[151]
006ac588	gASCII_shift_table	int[151]
0068bee0	gKey_array	int[151] per game key code
0068bed4	gKey_poll_counter
0074b5c0	gJoy_array	int[8] axis amounts
0074b5e0	gKey_mapping	int[77] (KEYMAP lines)
0068b88c	gKey_map_index	OPTIONS KeyMapIndex
00688458	gKey_names	char*[153] KEYNAMES.TXT
00604888	gControls_rows	int[29] row → keymap line
00657200	gGo_ahead_keys	{51,52,106}
005900a0	gToggle_array	44 x {key1,key2,in_game_only,exact_mods,state,action}
005904f0	gEdit_funcs	edit-mode dispatch [mode][key][mods]
0067c468	gEdit_mode
0068b8e0	gCheat_state	0xa11ee75d enables edit mode
0068b910	gFlying	M
0067c46c	gFlying_allowed	M
0074d350	gKeys_pressed	cheat hash input
0068c1c4	gEdge_trigger_mode	M
006ace88	gChar_buffer	20 bytes WM_CHAR
006ad4b0	gChar_count
0075cefc	gCar_control_bits	M
0079efa4	gCamera_key_bits	M
0075b9a4	gMap_mode	OPTIONS MapMode
