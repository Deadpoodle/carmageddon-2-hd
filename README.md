# Carmageddon II HD

An unofficial patch for **Carmageddon II: Carpocalypse Now** (the Steam release) that makes the 1998 game run well on a modern PC. It brings modern resolutions with ultrawide support, modern controls and controller support, sharper visuals and network play that works on today's Windows.

It is a binary patch: a small `dinput.dll` that the game loads at start-up and that changes the game in memory. No game file is modified, and no game data is included here: you need your own copy of the game.

## Contents

- [Features](#features)
- [Installing](#installing)
- [Settings](#settings)
- [Controls](#controls)
- [Network play](#network-play)
- [Troubleshooting](#troubleshooting)
- [Not done yet](#not-done-yet)
- [Building](#building)
- [Credits](#credits)

Versions and what changed in each: [CHANGELOG.md](CHANGELOG.md). The main menu shows the version in its bottom right corner.

## Features

**Display**
- Your desktop's resolution (or any you choose), rendered by [dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2) through Direct3D 11.
- Widescreen and ultrawide (21:9, 32:9) with a wider view (Hor+), not a stretched one.
- The race HUD spread to the screen's edges (or kept to 16:9, 4:3 or 21:9), with the gaps in the top banner filled.
- Menu and loading pictures stretched to fill a wide screen, with the menus themselves kept at their original shape in the middle. The mouse reaches the whole screen, edge to edge.
- Scenery drawn up to about three times as far as the original.

**Visuals**
- Anti-aliasing (2x, 4x or 8x), 16x anisotropic texture filtering with mipmaps, and full-precision colour instead of 16-bit dithering.
- A frame cap at your screen's refresh rate, never above 100 fps. Above 100 the game's physics run fast.
- Cutscenes enlarged 4x with deband, light sharpening and Lanczos scaling, and shown as large as the screen allows (800x480 of a 16:9 frame instead of 640x384), on true black.
- Sharper text and HUD art: their textures are enlarged 2x with Scale2x.
- The CD music plays: a store compatibility shim had stopped it.

**Controls**
- A modern keyboard scheme (W/A/S/D or the arrows), and any action can be put on any key.
- Xbox-style controllers through XInput, in races and in menus.
- Menus work with the arrows, W/S, a controller or the mouse.

**Network**
- LAN games on modern Windows. The game only speaks IPX, which Windows no longer has; the patch carries it over UDP.
- Join a game over the internet by typing the host's address on the Network screen.
- Several copies on one PC, for testing.

**Fixes**
- Alt+F4, and Close from the taskbar, quit the game. In the original, Alt+F4 left the game hanging, and Close left it running without a window.
- Alt+Tab works. Windows' own compatibility fix for this game blocks it with a keyboard hook, which the patch declines. The game pauses while you're away.

All of this is on a new **HD** tab of the in-game Graphics screen (Options → Settings), or in `Carma2HD.ini`.

## Installing

There is no release package yet. Until there is:

1. Build `dinput.dll` (see [Building](#building)), or take a built copy.
2. Download [dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2/releases) (tested with 2.87.5). From its `MS\x86` folder, take `DDraw.dll` and `D3DImm.dll`.
3. Put `dinput.dll`, `DDraw.dll`, `D3DImm.dll` and this repo's [`dist/dgVoodoo.conf`](dist/dgVoodoo.conf) in the game's folder, next to `CARMA2_HW.EXE`.
   - To keep the store's copy untouched, copy the whole game folder somewhere else first and install there.
4. Start the game as usual: the store's Play button works. The first start writes `Carma2HD.ini` next to the game, with every setting explained.

To remove the patch, delete those four files and `Carma2HD.ini`.

## Settings

Change these on the **HD** tab (Options → Settings → HD) or in `Carma2HD.ini`. The defaults are in brackets.

| Setting | Choices | What it does |
|---|---|---|
| Resolution | **auto**, or e.g. `1920x1080`, `3440x1440` | Auto is your desktop's resolution. |
| Widescreen | **on**, off | On widens the view. Off keeps the original 4:3 picture with bars at the sides. |
| HUD width | **auto**, full, 16:9, 4:3 | How far the HUD spreads on a wide screen. Auto is the screen's width, up to 21:9. |
| Controls | **classic**, modern | See [Controls](#controls). |
| Draw distance | original, far, further, **max** | How far scenery is drawn. Original uses the Graphics tab's own setting. |
| Anti-aliasing | off, 2x, **4x**, 8x | Edge smoothing. |
| Textures | original, smooth, **sharpest** | Smooth is trilinear with mipmaps. Sharpest is 16x anisotropic with mipmaps. |
| Colour | 16-bit, **smooth** | Smooth is full-precision colour. 16-bit is the original dithered look. |
| Frame rate | 60, **auto** | Auto is your screen's refresh rate, capped at 100. |

These are only in `Carma2HD.ini`:
- `[Display]`: `Fullscreen`. `borderless` (the default) is a screen-size window: no display mode change, and Windows can draw over it. `exclusive` gives the screen to the game alone.
- `[Display]`: `Backdrops`. `stretch` (the default) stretches the menu and loading pictures to the full width. `bars` keeps the original 4:3 pictures between black bars.
- `[Visuals]`: `Movies` (enhanced or original), `MovieSize` (fill, the default, or original) and `SharpText` (on or off).
- `[Network]`: see [Network play](#network-play).
- `[Controller]`: `Enabled` (1 or 0).

## Controls

The game keeps four key sets, **A** to **D**, on its Controls screen (Options → Configure Controls, beside CONFIG in the Keys\Buttons panel). The patch adds a fifth, **E**: the modern controls. Choosing E there, or Controls → Modern on the HD tab, switches to it. Choosing A–D, or Classic, switches back. A is the game's default, with driving on the numeric keypad.

**Set E's keys:**

| Action | Key |
|---|---|
| Accelerate / Brake | W / S (the Up / Down arrows too) |
| Steer | A / D (the Left / Right arrows too) |
| Handbrake | Space |
| Use power-up | E |
| Next / previous power-up | Q / Z |
| Power-up list | G |
| Repair (hold) | F |
| Recover | R |
| Wheelspin | X |
| Camera / cockpit | C / V |
| Action replay | P |
| Look up, down, left, right | I, K, J, L |
| Map / mini-map | Tab / M |
| Horn | H |
| Target lock / cycle target | T / Y |
| Sound on/off | F9 (S in sets A–D) |

**Remapping:** on the Controls screen, pick the set, click an action, then press the new key. The game saves each set in its own file (`data\KEYMAP_4.TXT` for E). The game's rules apply: a key already used by another action leaves that action unset, and OK won't close the screen until it has a key again. With set E, the arrow keys keep driving whatever you've moved the driving actions to. Your driving keys also move around the menus; Return selects and Esc goes back.

**Extra keys** for any action go in `[Keys]` in `Carma2HD.ini`, with any set. For example, `Handbrake=SPACE,LSHIFT`. Key names are DirectInput names: `W`, `UP`, `SPACE`, `LSHIFT`, `LCONTROL`, `RETURN`, `NUMPAD8`, `F1` and so on. The full list of actions is in the ini file.

**Controller** (any XInput pad, e.g. an Xbox controller):

| Race | Button |
|---|---|
| Steer | Left stick |
| Accelerate / Brake | Right trigger / Left trigger |
| Handbrake | A |
| Recover | B |
| Use power-up | X |
| Camera | Y |
| Previous / next power-up | LB / RB |
| Map | Back |
| Horn / Repair | Left stick click / Right stick click |
| Pause menu | Start |

In menus, the D-pad or left stick moves, A or Start selects, and B or Back goes back.

## Network play

The game's network mode lets up to 12 players race each other. The patch makes it work on modern Windows:
- **On a local network (LAN)**, games are found automatically.
- **Over the internet**, you type the host's address.

Everyone, host or not: the first time you open the Network screen, Windows asks whether the game may use the network. Allow it, on private networks at least.

### Hosting

1. Choose **Network game** on the main menu.
2. Pick a game type and track on the left, then press **HOST**.
3. The lobby opens. Wait for the other players' names to appear, then press **START**.

To host **over the internet**, three extra things are needed:
- **Forward UDP port 47700** on your router to this PC. Your router's manual or admin page calls this "port forwarding". The port can be changed with `[Network] Port`.
- **Give the joining players your public IP address.** Searching "what is my IP" in a web browser shows it.

### Joining over a LAN

1. Choose **Network game**.
2. The host's game appears in the **Join a game** list within a few seconds. Click it, then press **JOIN**.

### Joining over the internet

1. Choose **Network game**.
2. Click the **IP:** box at the bottom of the **Join a game** panel. A blinking cursor appears, with *TYPE, THEN PRESS RETURN* under the box.
3. Type the host's address and press **Return**:
   - an IP address, e.g. `203.0.113.5`;
   - with a port if the host changed it, e.g. `203.0.113.5:47800`;
   - or a host name.
4. The line under the box shows what happens next:

   | Message | Meaning |
   |---|---|
   | NOT A VALID ADDRESS | It isn't an IP address (four numbers from 0 to 255 with dots between them), a name, or a port from 1 to 65535. Nothing is sent. |
   | LOOKING UP THE ADDRESS... | You typed a name; it's being looked up. |
   | ADDRESS NOT FOUND | The name couldn't be looked up. Check it. |
   | SEARCHING... 15 | Asking that address for a game, every 3 seconds. The number counts down the 15 seconds the search is shown for. |
   | FOUND: PRESS JOIN | The host answered. Its game is selected in the list with its details shown, and **JOIN** is ready: press Return or click JOIN. |
   | NO GAME FOUND THERE | Nothing answered in 15 seconds. See below. It keeps listening while you stay on this screen, so a game that's hosted later still shows up; to count down again, click the box and press Return. |
   | THAT GAME CANNOT BE JOINED | The host answered, but its game has already started or is full. |

**JOIN stays greyed out until the game list has a game in it.** That's the original game's rule. If it stays grey:
- check the host is on the Network screen and has pressed HOST;
- check the host has forwarded UDP port 47700;
- check the address is right.

Press **Esc** while typing to cancel. Press **Return** on an empty box to clear the address. Games on your LAN still show up while an address is set. The address is saved, as `[Network] JoinAddress`.

### Notes

- **Three or more players over the internet** may not all reach each other yet. In a race every player talks to every other player directly, so the joining players need to reach each other too. Two-player internet games and LAN games of any size work.
- **Players must all use this patch.** The Steam release's own network layer (ipxwrapper) can't talk to it. `[Network] Transport=ipxwrapper` puts the original back, for LAN games with unpatched copies.

### Network settings

| Setting | Default | Meaning |
|---|---|---|
| `Port` | 47700 | UDP port. The first copy on a PC uses it, and further copies the next ones, up to Port+7. |
| `JoinAddress` | (empty) | The address the IP box sets. |
| `Transport` | udp | `udp` is the patch; `ipxwrapper` is the store's original, LAN only. |

## Troubleshooting

- **What the patch did:** `Carma2HD.log` next to the game records each start-up: the settings it read, the patches it applied, and anything that failed.
- **If the game freezes or crashes:** the log records where. Please include it in any report.
- **To reset a setting:** delete `Carma2HD.ini`. The next start writes the defaults again.
- **The picture is in a corner, or the mouse stops partway across the screen:** make sure `dgVoodoo.conf` is in the game folder. The patch keeps its settings in step, but only if the file is there.

## Not done yet

- **Zombie and alien pedestrians**, the German and UK releases' modes. They need the original disc data to look right, so the option is greyed out.
- **3+ player internet games:** relaying through the host's copy.
- **AI opponents in network races.**
- **A release package.**

## Building

You need a 32-bit MinGW-w64 GCC. On Windows, use MSYS2's `mingw32` environment: `pacman -S mingw-w64-i686-gcc`. Then:

```bash
./build.sh
```

This writes `build/dinput.dll`. On Linux, run `CXX=i686-w64-mingw32-g++ ./build.sh`.

- **Source:** `src/`. Each module's opening comment says what it changes and why.
- **Reverse-engineering notes:** `re/`, with the addresses and how each part of the game works.
- **Test harness:** the `C2HD_*` environment variables in `src/main.cpp` run the game hidden and scripted (hover sweeps, key sweeps, a scripted pad), so changes are tested without using the screen. `tools/screendrive.py` runs real full-screen checks.

## Credits

- [dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2) by Dege: the Direct3D wrapper that renders the game.
- [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Carmageddon_II:_Carpocalypse_Now): its collected fixes and notes.
- Carmageddon II is © Stainless Games and SCi. This project is not affiliated with them.

Released into the public domain (see [LICENSE](LICENSE)).
