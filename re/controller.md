# Carmageddon II (Steam CARMA2_HW.EXE): joystick path and an XInput recipe

Static analysis, 2026-11-02. Confidence: **H** = read from the instructions (capstone), **M** = decompiler, structure or DirectInput behaviour not tested here, **L** = guess to test in play. Addresses are VAs (image base 0x400000). This builds on re/input.md §4–§5 and corrects it in places (marked "correction").

## Headline findings

- **The joystick is disabled by default, and nothing in the retail UI turns it on.** With no matching JOYSTICK.TXT record, the default config has enable = 0 (0x45c3f6), so LoadJoystickConfig 0x45c0b0 calls DisableJoystick 0x45a0c0 and gJoystick_index becomes −1 (H). The only other calls to Enable/Disable are a dev edit-mode function (0x5112d0, "Joystick Enabled/Disabled", table entry 0x5906e4), which the dev cheat gates (M). The Controls screen has no enable switch (H). **A virtual pad has to force-enable itself** (§5).
- **LoadJoystickConfig deletes JOYSTICK.TXT** when the file is shorter than 0x20 bytes or holds no record for the current device. The code is fclose, then DeleteFileA through 0x5776c0 at 0x45c123–0x45c131 (H).
- **Poll must return exactly DI_OK (0).** For any other non-error value, the X/Y readers log "Can't poll, unknown error" and return −1 (0x459090, 0x4593b0) (H). DirectInput returns DI_NOEFFECT (1) from Poll on devices that don't need polling. Most modern HID pads are like that, so stock analog probably never works with a real modern pad (M).
- **Steering is quadratic; throttle is linear; brake is effectively not analog** (§3).
- **No force-feedback stubbing is needed** if the virtual device leaves DIDC_FORCEFEEDBACK out of its caps. Every FFB/IFORCE2 path is gated on that bit (H).

## 1. The joystick path

### Init (once, from PDInitDIKeyboard: call 0x51cec6)
- **DIJoystickInit 0x458860** (H):
  - `DirectInputCreateA(GetModuleHandle(0), 0x500, &gDI_joy 0x686158, 0)`. This is a second IDirectInputA. It has the same class and vtable as the keyboard's, so our CreateDevice slot swap already covers it (M: shared vtable).
  - `EnumDevices(DIDEVTYPE_JOYSTICK=4, cb 0x459c70, pvRef=gDI_joy, DIEDFL_ATTACHEDONLY=1)` (vtbl+0x10, call at 0x458911).
- **Callback DIEnumJoystickCb 0x459c70** (stdcall, `ret 8`) runs for **every** attached device; there is no first-only rule and no 16-slot bound. For slot n = gJoy_count 0x686160 (H):
  1. strncpy the **product name** (lpddi+0x12c, max 0x50) to the record name 0x79d9cc+n·0x68, terminated at +0x4f.
  2. Device type from the subtype byte lpddi+0x25: 4 (gamepad) → 4, 6 (wheel) → 2, else 1. Stored at record+8 and used only to choose FFB effects (0x457cee).
  3. `CreateDevice(pvRef, &lpddi->guidInstance, &dev, NULL)` (+0x0c). On failure it logs "Could not create dinput object" and returns 1.
  4. `SetDataFormat(dev, 0x54d310)` (+0x2c). 0x54d310 = {0x18, 0x10, DIDF_ABSAXIS, dwDataSize **0x50**, **44** objects, rgodf 0x54d050}, which is **c_dfDIJoystick / DIJOYSTATE** (H). On failure: Release, return 1.
  5. `QueryInterface(dev, IID 0x58b398 = {5944E682-C92E-11CF-BFC7-444553540000} = IID_IDirectInputDevice2A, &gDI_joy_dev[n] 0x79e060)` (+0x00). On failure: Release, return 1.
  6. `SetCooperativeLevel(dev2, hwnd 0x6ad4c8, 9 = DISCL_EXCLUSIVE|DISCL_BACKGROUND)` (+0x34). **On failure it returns 0 (DIENUM_STOP) without counting the device.**
  7. Calls on the *base* `dev`:
     - `SetProperty(DIPROP_RANGE=4, {0x18,0x10,dwObj, DIPH_BYOFFSET}, 0..0xffff)` (+0x18), first for dwObj 0 (X), then 4 (Y).
     - `SetProperty(DIPROP_DEADZONE=5, DIPROPDWORD 1000 = 10%)` on X, then Y.
     - On any failure it logs and calls `Release(dev)`, then calls Release again at 0x459eaa. That is a **double release** (H).
  8. `Release(dev)`. The Device2 reference is kept. gJoy_count++, return 1.
- Back in 0x458860, for each of 16 slots (H):
  - **DIJoyQueryCaps 0x4589d0**: `Acquire` (+0x1c), then `GetCapabilities` (+0x0c, DIDEVCAPS dwSize **0x2c** = DX5). dwFlags & 0x100 (DIDC_FORCEFEEDBACK) sets a bit in gJoy_FF_mask 0x68615c. dwButtons goes to record+0x5c.
  - Then `GetObjectInfo` (+0x38, DIDEVICEOBJECTINSTANCEA dwSize 0x13c, dwHow=DIPH_BYOFFSET) for buttons at offset 0x30+i and axes at {0,4,8,0x0c,0x18,0x1c}. This only fills the masks at record+0/+4, whose only readers are the dev overlay (H).
  - It then calls `Acquire` again. Return 0, 1 and errors are only logged.
  - If gJoy_count > 0, gJoystick_index 0x595f88 = 0. Then FFB init 0x457760; it does nothing unless an FF bit is set (H).
- Then (0x51cecb, only if a joystick was found) (H):
  - InitJoyConfigs 0x45c410 mallocs a 0xec-byte config per device (record+0x64) and fills defaults in 0x45c370: name, button table i/−1, Y-enable 1, sensitivity 1.0/1.0, digital 0, FFB 50, **enable 0**.
  - LoadJoystickConfig 0x45c0b0 runs next.
- **No joystick found:** gJoystick_index stays −1. Every reader returns −1/0, the gJoy_array stays −1, the Controls joystick items are greyed (Controls_SetJoyItems 0x472210), and Controls_Ok refuses codes 107–142 (H).

### JOYSTICK.TXT (data folder; path from 0x513690 with the base 0x75ba60, M)
- Reader FindJoystickRecord 0x45b7e0: needs ≥ 0x20 bytes. It turns spaces in the device name into `_` and strstr-matches each line against it, then ReadJoystickRecord 0x45bbc0 parses the record (H).
- Writer 0x45b920 format strings (H): `&%s\n` (0x596498), `%d\n` = 32, 32 × `%d\n` (button table +0x54, used only by the dev overlay 0x45b3b0), `%d\n` +0xd4 (Y axis on), `%.3f, %.3f\n` (X/Y sensitivity +0xd8/+0xdc), `%d\n` +0xe0 (digital mode), `%d\n` +0xe4 (FFB gain %), `%d\n` +0xe8 (**enabled**), `\n`.
- After the load (H):
  - It copies sensitivity to 0x595f90/0x595f94, Y-on to 0x595f9c and gain to 0x595f84.
  - +0xe8 ≠ 0 calls EnableJoystick 0x45a070: index = saved 0x595f8c or 0, and the FFB flag 0x686168 is set if the device is FF. Otherwise DisableJoystick 0x45a0c0 (index → −1).
- SaveJoystickConfig 0x45bd90 runs on Controls_Ok (0x472eb6). It rewrites every device's record by temporarily switching the index, and creates the file ("wt") if it is missing (H). If the file exists but has no `&` record, it returns without writing (and without fclose) (H).
- No joystick setting exists in OPTIONS.TXT (H).

### Per-frame reads (all through gJoystick_index; −1 means none or disabled)
| Function | Calls | Result |
|---|---|---|
| DIGetJoyX 0x459050 (via PDGetJoy1X 0x51d230) | Poll (+0x64) must be 0, then GetDeviceState(+0x24, **0x50**) must be 0 | `ftol((lX − 32767)·sensX 0x595f90 + 32767)`, clamped 0..0xffff (0x45932c–0x459366) (H). −1 if 0x595f98 (constant 1) is 0 |
| DIGetJoyY 0x459370 (via 0x51d250) | same | same with lY, sensY 0x595f94; −1 if Y-on 0x595f9c = 0 (H) |
| DIGetJoyButtons 0x459690 | Poll must be ≥ 0, GetDeviceState ≥ 0 | 32-bit mask from rgbButtons[i] & 0x80 (H) |
- Fields read: **lX, lY and rgbButtons[0..31] only**. No POV, Z or rotation axes (H). The unreferenced full reader 0x458d50 (X, Y, Z, Rz, sliders, POV[0], buttons) is dead (M).
- Error paths: DIERR_INPUTLOST (0x8007001e) or DIERR_NOTACQUIRED (0x8007000c) → re-Acquire, log and return −1 (H).
- Every PollKeys does **three Poll + GetDeviceState pairs** (X, Y, buttons). PollKeys 0x481eb0, CyclePollKeys 0x4821c0, KeyIsDown 0x483040, PDKeyDown2 0x482590, PDAnyKeyDown 0x482a00, AnyKeyDown 0x482d70 and WaitForNoKeys 0x4833b0 can each poll several times per frame. They read the joystick even when the window is inactive: PDSetKeyArray's activity check returns only from itself (H).

### Axis values → game (PollKeys 0x481eb0; the gates 0x6621e4..0x6621f0 are static 1s and never written, H)
- gJoy_array 0x74b5c0[8] is set to −1 each poll.
- For X: if x < 0x8000, [0] left = (0x8000 − x)·2; else [1] right = 2x − 0x10000. Y does the same into [2] up and [3] down. Range 0..0x10000 (H).
- Joy-axis "keys" 143–146 are **one-poll pulses** set on the crossing poll only (x < 0x4000 / > 0xc000, previous X 0x68be44, previous Y 0x68c13c). The array 107..150 is zeroed at the start of each poll (H).
- Joy2 X/Y and buttons (WinMM) return −1/0 while DI is active: no joyGetDevCaps import, so caps 0x6ad0b8 stays 0 (H).

### PollCarControls 0x443e80 → car+0x12d4.. (player car 0x75bc2c, M)
- **Steering:**
  - If both Left (line 47) and Right (48) are mapped to codes < 143, the keys are tested first (0x443eb1) (H).
  - If no steering key is held: digital mode (+0xe0) gives a key bit when gJoy > 10. Analog mode gives left = gJoy[0] and right = gJoy[1], or 0/−1 when there is no joystick. They are stored at 0x75cf00/0x75cf04 (H).
  - **Correction to input.md:** analog is the fallback whenever no steering key is held. It isn't limited to joystick-mapped Left/Right. Mapping Left or Right to 143+ turns keyboard steering off.
- **Throttle:** if the Accelerate key is not held, acc = gJoy[2] (up) → 0x75cf08. Brake key not held → dec = gJoy[3] (down) → 0x75cf0c. Digital mode thresholds are > 10 (H).
- **Reversed-car flag 0x705be0** swaps left/right and acc/dec (0x444110) (H; flag meaning L).
- **Joystick enable / digital mode on the Controls screen:**
  - The digital/analog radio is **element 0x24 (digital, +0xe0 = 1) / 0x25 (analog)**, via 0x472400 (H).
  - The sliders are X sensitivity 0x45c810 (0..2), Y sensitivity 0x45c870 (0..2) and FFB gain 0x45c7b0 (0..100). They are called at 0x4729dd/0x4729fd/0x472a20 (H).
  - A negative sensitivity in JOYSTICK.TXT inverts that axis, from the formula above (H). That explains PCGW's "minus inverts Y".

## 2. Interfaces and every call the virtual device will see

DirectInput **version 0x500**. The game holds **IDirectInputDevice2A** (the base device from CreateDevice is used briefly, then released) (H).

| Object | Slot (offset) | Method | Call sites | Expected return |
|---|---|---|---|---|
| IDirectInputA (gDI_joy) | 4 (+0x10) | EnumDevices(4, cb, pvRef, 1) | 0x458911 (copy in 0x457760) | ignored |
| | 3 (+0x0c) | CreateDevice(&guidInstance, &dev, NULL) | 0x459cfb | 0 or the device is skipped |
| | 2 (+0x08) | Release | shutdown 0x4599ac | ignored |
| Device | 0 (+0x00) | QueryInterface(IID_IDirectInputDevice2A) | 0x459d6d | **0**, AddRef |
| | 2 (+0x08) | Release | 0x459d43, 0x459d87, 0x459e39/0x459ea0 (on SetProperty failure), 0x459eaa, 0x459990 | ignored |
| | 3 (+0x0c) | GetCapabilities(DIDEVCAPS 0x2c) | 0x458b5c | 0, else the masks are skipped |
| | 6 (+0x18) | SetProperty(RANGE X/Y, DEADZONE X/Y) | 0x459e09..0x459e89 | **0** (else double Release) |
| | 6 (+0x18) | SetProperty(DIPROP_FFGAIN=7) | 0x457b70, only if FF active | n/a |
| | 7 (+0x1c) | Acquire | 0x458ad5, 0x45893e, reacquire paths | 0 or 1 |
| | 8 (+0x20) | Unacquire | shutdown 0x459988 | ignored |
| | 9 (+0x24) | GetDeviceState(0x50, DIJOYSTATE*) | 0x45925a, 0x45957a, 0x459882 | **0** |
| | 11 (+0x2c) | SetDataFormat(c_dfDIJoystick) | 0x459d28 | **0** |
| | 13 (+0x34) | SetCooperativeLevel(hwnd, 9) | 0x459db3 | **0** (else enumeration stops) |
| | 14 (+0x38) | GetObjectInfo(0x13c, ofs, DIPH_BYOFFSET) | 0x458bbe, 0x458c2a | 0 for present objects, DIERR_OBJECTNOTFOUND otherwise (informational) |
| | 18 (+0x48) | CreateEffect | 0x457760, only FF | n/a |
| | 22 (+0x58) | SendForceFeedbackCommand | 0x457908 area, 0x457ff0, only FF | n/a |
| | 25 (+0x64) | **Poll** | 0x45908d, 0x4593ad, 0x4596b8 | **exactly 0** |

There are no other calls (H; slots 4, 5, 10, 12, 15–17, 19–21, 23, 24, 26 are never used). IFORCE2 (IFLoadProjectFile/IFCreateEffectStructs) runs only if 0x686168 is set, and that needs DIDC_FORCEFEEDBACK (H).

## 3. Axis semantics

- **One Y axis for both pedals:** up (small lY) = accelerate, down = brake/reverse (H). There is no separate throttle axis; lZ/lRz are never read (H).
- **Steering** (ControlCarSteering 0x4175e0):
  - When left > 0, curvature = left² · 2⁻³² (0x589764) · maxcurve (car+0x1258). When right ≥ 0, it is −right² · 2⁻³² · maxcurve. The response is **quadratic** (0x417981–0x4179c9, H).
  - Whenever left/right ≥ 0 this **overrides** the keyboard-computed curvature. Releasing a key with the stick centred snaps the curvature to 0 (M, stock behaviour).
  - Resolution is that of the 0..0x10000 integer.
- **Throttle** (ControlCar 0x415300):
  - acc ≥ 0 gives a torque factor of acc · 1.831e-5 (0x589658), 1.2 at full. That matches the keyboard's constant 1.2 (0x589660), so throttle is **linear** (0x4155b5–0x4155cd, H).
  - acc < 0 with no key gives no throttle.
- **Brake:** if dec > 0, brake = (dec >> 16) · k + base (0x415643–0x41566f, H). The clamp tops dec at 0xfffe, so analog brake is always the **base** level. The key brake ramps from base to base + k (0x415696..). Analog braking is effectively digital and weaker (M on the size, which is per car).
- **Reverse:** with no throttle (acc ≤ 0 and no key), a brake (key or dec > 0) at |speed| < 1 toggles reverse and swaps acc/dec (0x4154c5–0x415524, H).
- **Centre value matters:**
  - lY = 32767 at sensitivity 1 gives acc = 2 (> 0). That blocks the keyboard-brake reverse logic while a joystick is enabled (H for the arithmetic).
  - **lY = lX = 32768 at rest** gives acc = −1 / dec = 0 and left = −1 / right = 0, which is clean.
  - With a Y sensitivity < 1.0 the centre truncates to 32767 again (ftol), so the quirk returns. That is minor (H arithmetic).
- **Digital fallbacks:**
  - Controls "digital" mode (+0xe0): gJoy > 10 acts like a key (H).
  - Axis key codes 143–146 are one-poll pulses, useless for held actions (H).
- **Force feedback:** report `dwFlags = DIDC_ATTACHED` (no 0x100). Then 0x68615c = 0 and 0x686168 = 0, and every FFB path (0x457760 effect load, 0x457ff0, 0x457b70, 0x45c6b0, 0x45c720) is skipped (H). Nothing to stub. Rumble via XInput would need our own game-event hooks (L, out of scope).

## 4. Buttons → key codes

- **SetJoyButtonKeys 0x45b360** (fastcall, ecx = gKey_array 0x68bee0): if gJoystick_index ≠ −1 and the config exists, `gKey_array[107+i] = (mask >> i) & 1` for i = 0..31 (H).
  - The mapping is 1:1: Joy1 B1–B32 = codes 107–138. The JOYSTICK.TXT button table is not used here (H).
  - The value is 1, not the poll mark. KeyIsDown 0x483040 returns the raw value, so that works (H).
- Callers: PollKeys (0x48213f), CyclePollKeys, PDKeyDown2, PDAnyKeyDown, AnyKeyDown, KeyIsDown, WaitForNoKeys, 0x48363a (H).
- Bound buttons work in races through gKey_mapping/KeyIsDown, like any key (H).
- **Joy2 B1–B4 (139–142)** come only from WinMM and are always 0 under DI. Codes 147–150 (Joy2 axes) are always unset (H).
- **Menus:** the hard-coded PDKeyDown tests never use 107+. Joystick buttons count only for "any key":
  - attract/timeout resets via PDAnyKeyDown;
  - WaitForNoKeys, which spins while a pad button is held;
  - Controls capture, where PDAnyKeyDown scans 142→0, so a button wins over a key held at the same time (H).

## 5. Recipe

### (A) Virtual DirectInput joystick (proxy only, plus one 6-byte detour)
1. **Hooks.** In `OnDirectInput`, swap **IDirectInputA slot 4 (EnumDevices)** alongside slot 3. Both DI objects share the vtable (M; the log shows two DirectInputCreateA calls and one hook).
   - `h_EnumDevices`: when dwDevType is 4 (or 0) and the pad is enabled, call `cb(&g_inst, pvRef)` once and return DI_OK. Do **not** forward the real joysticks: exclusive acquisition, duplicate pads, the Poll DI_NOEFFECT problem.
   - `g_inst` is a DIDEVICEINSTANCEA with dwSize 0x244, fixed private guidInstance/guidProduct, and dwDevType = `DIDEVTYPE_JOYSTICK | (DIDEVTYPEJOYSTICK_GAMEPAD<<8)` = 0x404. Use a unique product **and** instance name (≤ 79 chars, no substring of another pad's name), e.g. "Carma2 HD Gamepad".
   - `h_CreateDevice`: if the GUID is ours, return our static object with refcount 1 **before** calling the original.
2. **Our object:** one static C++ object with a 27-slot IDirectInputDevice2A vtable (stdcall), handed out for IDirectInputDeviceA/2A and IUnknown.
   - QI: S_OK + AddRef for those IIDs (…680/…682-C92E-11CF-BFC7-444553540000), E_NOINTERFACE otherwise.
   - AddRef/Release: count, never free.
   - GetCapabilities: dwFlags = DIDC_ATTACHED, dwDevType 0x404, dwAxes 2, dwButtons 16, dwPOVs 0. Accept dwSize 0x18 or 0x2c.
   - SetProperty: store RANGE/DEADZONE and return 0.
   - Acquire: 0 the first time, then 1. Unacquire: 0.
   - SetDataFormat: 0 if dwDataSize == 0x50.
   - SetCooperativeLevel: 0.
   - GetObjectInfo: 0 for offsets 0, 4 and 0x30..0x3f; DIERR_OBJECTNOTFOUND otherwise.
   - Poll: **0**.
   - GetDeviceState: **0**, filling all 0x50 bytes.
   - Everything else: E_NOTIMPL (never called).
3. **Force-enable.** Two options:
   - Code: detour **LoadJoystickConfig 0x45c0b0** (first bytes `81 EC 00 01 00 00` = sub esp,0x100, the same pattern as the LoadKeyMapping detour). After the original returns: if gJoystick_index 0x595f88 == −1, call EnableJoystick 0x45a070 (cdecl, no args), then set `(*(int*)(GetJoyConfig 0x459f80()) + 0xe8) = 1` (H for the entry bytes and the logic).
   - Data: write a `&Carma2_HD_Gamepad` record (§1 format, enable 1) to data\JOYSTICK.TXT before 0x51ced4. Remember that the game deletes the file when the record is missing (H).
4. **DIJOYSTATE from XInput** (cache XInputGetState for 2–4 ms; poll a disconnected slot only every ~1 s):
   - **lX** = LX + 32768 after a radial deadzone (about 7849, then rescaled to the full range; the quadratic curve already softens the centre). Ignore the game's DIPROP_DEADZONE so there are no double deadzones. Rest is **32768**.
   - **lY:** v = RT − LT (each 0..255, threshold 30). v ≥ 0 gives `32768 − v·32768/255`; v < 0 gives `32768 + (−v)·32767/255`. RT full then gives acc = 0x10000 and LT full gives dec = 0xfffe. Pressing both cancels.
   - Recommended variant: put **only RT on lY** and inject **LT as the game's Brake key** (keymap line 50 → gScan_code) in the keyboard hook. The key brake ramps and reverses properly, and the analog brake is base-only anyway.
   - **lZ/lRx/lRy/lRz/sliders** = 32768/0 (unread). **rgdwPOV[0..3]** = 0xFFFFFFFF.
   - **rgbButtons:** B1 A, B2 B, B3 X, B4 Y, B5 LB, B6 RB, B7 Back, B8 Start, B9 LS, B10 RS, B11–B14 D-pad up/down/left/right, B15 LT > 50%, B16 RT > 50% (0x80 when down).
   - When disconnected or not foreground, return centred and released with DI_OK. The game reads in the background (coop BACKGROUND), so check that the foreground window is the game.
5. **Keyboard injection** (our GetDeviceState(256) hook, after `Translate`):
   - In menus (`frontend::InMenu()`): D-pad/left stick → DIK arrows, A → DIK_RETURN, B and Back → DIK_ESCAPE. Edge and repeat come from PDKeyDown2.
   - In races, by default inject per action through the existing TargetScan(line) mechanism, so a fresh install works without binding: A → Handbrake (54), B → Recover (60), X → UsePowerup (61), Y → Camera (75), LB/RB → PowerupPrev/Next (63/64), Start → line 0 (Esc), Back → Map (57), LS → Horn (59), RS → Repair (45), LT → Brake (50). Optionally the right stick → look lines 31–34.
   - Add a setting `Buttons=keys|joystick`: with `joystick`, report rgbButtons and inject nothing in races, so users bind Joy1 B1–B16 on the Controls screen. Never use both at once or a button fires two actions.
   - Inject nothing during `frontend::InKeyCapture()`.
6. **Risks:**
   - The enable detour depends on 0x45c0b0 being the only load path (also 0x511240/0x51127a, dev-only).
   - Stock quirks: curvature snaps to 0 when a steering key is released; sensitivity < 1 reintroduces the acc = 2 centre.
   - Controls_Ok then also writes JOYSTICK.TXT. That is harmless, and the record keeps enable 1.

### (B) Keyboard-only fallback (no virtual device)
- In the existing GetDeviceState(256) hook: the stick past about 30% → the game's Left/Right keys (lines 47/48). RT/LT → Accelerate/Brake (49/50). Buttons → the actions above; menus as in A5.
- No JOYSTICK.TXT, enable, Poll or DI-object work is needed. It can't conflict with real joysticks, and it reuses `Translate`/TargetScan entirely.
- **Lost:** proportional steering (keyboard steering ramps the turn rate instead of the quadratic stick-to-curvature map) and proportional throttle (on/off 1.2 instead of 0..1.2). Braking is unaffected (key brake ≥ analog brake).
- Pulse-width steering to fake analog fights the turn-rate ramp (L, not advised).

### Recommendation
**Build A with the RT-on-lY / LT-as-Brake-key variant and `Buttons=keys` by default,** and keep B as the path when `[Controller] Analog=0` or the virtual device is off.
- A keeps the game's own steering and throttle feel and uses only documented DirectInput 5 semantics on an object we fully control. The single code patch is a 6-byte detour the project already knows how to do.
- **Main risks:** a wrong enable state (verify in the log: "Joystick #0 acquired", then gJoystick_index 0 after LoadJoystickConfig), and background polling (gate on the foreground window).
- **Test plan:** check that the Controls screen shows the analog/digital radio and sliders, then drive with the stick centred and the keyboard brake to confirm reverse engages.

## names.tsv
```
00458860	DIJoystickInit	second IDirectInputA v5, EnumDevices(4,cb,ATTACHEDONLY), acquire all, index 0
00459c70	DIEnumJoystickCb	name+type, CreateDevice, c_dfDIJoystick, QI Device2A, coop 9, RANGE 0..ffff, DEADZONE 1000 on X/Y
004589d0	DIJoyQueryCaps	Acquire, GetCapabilities(0x2c), GetObjectInfo masks; FF bit -> 0x68615c
00458cb0	DIJoyAcquire	acquire slot, 1 ok / 0 fail / -1 none (no static caller)
00458d50	DIJoyReadFull	Poll+GetDeviceState X,Y,Z,Rz,sliders,POV,buttons (dead)
00459050	DIGetJoyX	Poll==0, GetDeviceState(0x50), (lX-32767)*sensX+32767 clamp
00459370	DIGetJoyY	same for lY, sensY, gated by 0x595f9c
00459690	DIGetJoyButtons	32-bit mask from rgbButtons
00459970	DIShutdownJoysticks	Unacquire+Release all, Release DI (no static caller)
00459a70	CycleJoystick	dev: next device, "Joystick %d: %s %d"
004599d0	GetJoyName	record name of current device
00459f80	GetJoyConfig	config ptr (record+0x64) or 0 when index -1
00459fb0	GetJoyButtonCount	record+0x5c
00459fd0	JoySlotPresent	gDI_joy_dev[i] != 0
00459fe0	ForEachJoyAllocConfig	malloc config per device, call init
0045a070	EnableJoystick	index = saved or 0; FFB flag if FF
0045a0c0	DisableJoystick	save index, index = -1
0045a0f0	JoystickActive	index != -1
0045b360	SetJoyButtonKeys	gKey_array[107+i] = button i (0/1)
0045b3b0	JoystickDebugOverlay	dev overlay (0x68b8e8)
0045b7e0	FindJoystickRecord	JOYSTICK.TXT name search (spaces -> _), needs >= 0x20 bytes
0045b920	WriteJoystickRecord	&name, 32, table, Yon, "%.3f, %.3f", digital, gain, enabled
0045bbc0	ReadJoystickRecord
0045bd90	SaveJoystickConfig	all devices; creates file "wt" if missing
0045c0b0	LoadJoystickConfig	record or DeleteFile; applies sens/Yon/gain; enable/disable by +0xe8
0045c240	ToggleJoystickDebug	dev
0045c370	DefaultJoyConfig	name, table i/-1, Yon 1, sens 1.0/1.0, digital 0, gain 50, enabled 0
0045c410	InitJoyConfigs	ForEachJoyAllocConfig(0xec, DefaultJoyConfig)
0045c420	CycleJoystickAndApply	dev
0045c460	ToggleJoyYAxis	+0xd4 and 0x595f9c
0045c550	GetJoyDigitalMode	+0xe0
0045c570	SetJoyDigitalMode	+0xe0 (Controls element 0x24 = 1, 0x25 = 0)
0045c590	FindJoyButtonForAction	dead
0045c780	StepFFBGain	dev: +25%
0045c7b0	SetJoyFFBGain	+0xe4 0..100
0045c7f0	GetJoyFFBGain
0045c810	SetJoySensX	+0xd8 0..2
0045c870	SetJoySensY	+0xdc 0..2
0045c8d0	GetJoySensX
0045c8f0	GetJoySensY
00457760	FFBInit	FF devices only: SendFFCommand, IFORCE2 FFB.IFR effects
00457b70	FFBSetGain	SetProperty(DIPROP_FFGAIN)
00457ff0	FFBReset	SendForceFeedbackCommand(2)
00458520	FFBLoadEffects	Basic Friction/Basic Force/Collision Force
004175e0	ControlCarSteering	M: quadratic joystick curvature (car+0x12d4/0x12d8)
00415300	ControlCarThrottle	M: acc*1.83e-5 torque, base brake, reverse toggle
00472400	Controls_SetJoyMode	element 0x24 -> digital, else analog
005112d0	Dev_ToggleJoystickEnable	edit-mode fn (table 0x5906e4), "Joystick Enabled/Disabled"
00511240	Dev_JoyDebugToggle	edit-mode fn
00686158	gDI_joy	IDirectInputA (joysticks)
0068615c	gJoy_FF_mask	bit per FF device
00686160	gJoy_count
00686168	gFFB_active
00686170	gJoy_enum_done
0079d9c0	gJoy_records	16 x 0x68: +0 btn mask,+4 axis mask,+8 type,+0xc name[80],+0x5c buttons,+0x60 cfg size,+0x64 cfg ptr
0079e060	gDI_joy_dev	IDirectInputDevice2A*[16]
00595f88	gJoystick_index	-1 none/disabled
00595f8c	gJoystick_saved_index
00595f90	gJoy_sens_x	float, default 1.0
00595f94	gJoy_sens_y	float, default 1.0
00595f98	gJoy_x_enabled	const 1
00595f9c	gJoy_y_enabled
00595f84	gFFB_gain
0068be44	gJoy1X_prev
0068c13c	gJoy1Y_prev
006621e4	gJoy_axis_present	4 x const 1 (J1X,J1Y,J2X,J2Y)
00762280	gJoy2Y_deadzone	8000
0075bc2c	gCurrent_car	M: +0x12d0 keys bits (0x75cefc), +0x12d4 left, +0x12d8 right, +0x12dc acc, +0x12e0 dec
0054d310	c_dfDIJoystick	DIDATAFORMAT 0x50 bytes, 44 objects
0058b398	IID_IDirectInputDevice2A
```
