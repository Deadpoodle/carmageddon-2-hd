# Changelog

Carmageddon II HD follows [semantic versioning](https://semver.org/). Until a first release it stays at 0.x: a new feature stage raises the minor number, a fix the patch number. Each version is a git tag (`v0.14.0` and so on) on the commit that completed it. The main menu shows the version in its bottom right corner, after the game's own label.

The game itself is the Steam release, which labels itself `v1.02a`. It is the same build as the official "Super Patch V2.0" (both game exes were built on 30 November 1998). Steam's copy has its CD protection removed, plus an extra import for its CD-music player.

## 0.26.1 (2026-11-04)
- The GRAPHICS and HD tabs are divided by one straight line.

## 0.26.0 (2026-11-04)
- The game no longer restarts itself when HD tab settings change. The tab now shows "Settings will take effect after the game restarts" while a setting differs from what the game started with.
- Zombies: only skin changes now. Each skin's flesh tone is taken from its own faces, so khaki and tan clothes no longer turn grey-green with it.
- The HD tab's panel runs straight into the main panel, with no line through "HD".

## 0.25.0 (2026-11-04)
- HD tab settings no longer need a manual restart. Controls and Earlier groups apply at once, as before; leaving the tab with any other row changed saves and restarts the game by itself, without the opening films, straight back to your latest save. In a race, the restart waits for the main menu.
- The music bar takes effect at once. It used to wait behind the audio already queued.
- Zombies: skin is a greyer green, and clothes keep their colours, with a little grime.

## 0.24.0 (2026-11-04)
- A music volume bar under the effects bar on the Options and pause menus, in place of music On/Off: mouse, or Left/Right on keys and pad. All the way down stops the music. It needs the store's ogg music player in the game folder, which the Steam and GOG releases have.

## 0.23.0 (2026-11-04)
- Zombies. Pick Zombies under Pedestrians on the HD tab (`[Game] Pedestrians`), and from the next start of the game every pedestrian, animals included, shambles all the time, moans instead of screaming, and has a grubby grey-green skin and green blood. Groovin' and Panic still take over while they last. The text says ZOMBIE where it said pedestrian, as the official zombie patch did. No extra files are needed.

## 0.22.1 (2026-11-04)
- The right arrow beside GROUP no longer has a faint red box around it.

## 0.22.0 (2026-11-04)
- Finished race groups can be raced again. Arrows beside **GROUP** on the main menu page back through the groups you have finished; with GROUP selected, Left/Right (or the D-pad) do the same. The race list changes as you go, and a finished group's mission can be raced again. A race from an earlier group is a replay: you stay in the group you have reached, and winning an old mission no longer moves you on a group (the stock arrows, shown only after the whole game is finished, moved your progress). HD tab: **Earlier groups** (`[Game] EarlierGroups`, on by default).
- The game loads your latest save when it starts. This is the Graphics screen's own **Auto Load** row, now on by default (`[Game] AutoLoad`); the original started a new game every time unless it was switched on.

## 0.21.0 (2026-11-03)
- Pedestrians, traffic and opponents' detailed models no longer pop in just ahead of you. They follow the Draw distance setting like the scenery. At Max, pedestrians come alive out to 30 units instead of 12, traffic is drawn out to 43 instead of 15, and up to 64 traffic cars move at once instead of 12. Opponents switch to their full models at 57 units instead of 20. Original keeps the game's own distances.
- The debug overlay counts traffic: drawn / moving.

## 0.20.0 (2026-11-03)
- Pedestrians in network races (`[Network] Pedestrians`, on by default). The original game has none online. Each player's game runs its own pedestrians: they aren't shared between players, so each sees a different crowd, and killing every pedestrian no longer ends the race. They appear on the normal tracks (the race game types); most network arenas have none to spawn.

## 0.19.2 (2026-11-03)
- First public release, on GitHub's Releases page.
- Steam Deck: one line in Konsole installs the latest release (`curl -fsSL https://raw.githubusercontent.com/Deadpoodle/carmageddon-2-hd/main/tools/install-steamdeck.sh | bash`). Run from an extracted zip, the script still installs that copy.

## 0.19.1 (2026-11-03)
- Left/Right on the Change Car screen change the car. In 0.18.0 and 0.19.0 they went back to the main menu as if CANCEL had been pressed. The compiler had dropped the call that presses the arrow button. A build option now stops it doing that anywhere in the patch.

## 0.19.0 (2026-11-03)
- Steam Deck and Linux: `install-steamdeck.sh` installs the patch under Proton. It finds the game in any Steam library, fetches and checks dgVoodoo2, and sets the launch option that makes Proton load the patch (`WINEDLLOVERRIDES="dinput,ddraw,d3dimm=n,b"`). `--uninstall` takes it all out again.
- Checked at the Deck's 1280x800: menus, films, the race HUD and the map fill the 16:10 screen (768x480 Hor+).
- The log says when the game runs under Wine. It also warns when DirectDraw isn't dgVoodoo2's copy in the game folder, which on Linux means the launch option is incomplete.
- `tools/package.sh` builds the release zip, `carma2hd.zip`, from committed code only.

## 0.18.0 (2026-11-03)
- Every menu works without a mouse, from the keyboard or a controller. Change Car: Left/Right change the car, Up/Down pick OK or CANCEL. Load Game: Up/Down move along the saved games and scroll the list. Network game options: Left/Right change the starting credit and target. Esc (B on a pad) leaves Change Car and Load Game; in the lobby it moves to ABORT GAME, and a second Esc aborts. The original left these to the mouse; without one, Load Game couldn't even be left.
- Moving up or down the pause menu in a race no longer ends the race. The original activated Abort Race as soon as the keys landed on it.

## 0.17.2 (2026-11-03)
- The car picture on the main menu no longer shows a grid of thin lines. It is six tiles, and with smoothed textures each tile's edge picked up colour from its opposite edge. The same goes for the front end's other pictures.

## 0.17.1 (2026-11-03)
- The time bonus at the end of a race no longer goes to minus millions. The original game turns the time left into credits a step every 15 ms of game time. If the game clock was ever behind its last step, the step count wrapped around and the race timer gained time instead.

## 0.17.0 (2026-11-03)
- The host's lobby shows the address to give friends joining over the internet, and this PC's address on the home network. The address comes from the router (UPnP), or from a web service if the router doesn't say (`[Network] UPnP`, `PublicIPService`).
- Hosting opens the game's port on the router (UPnP) and closes it again afterwards, so most hosts no longer need to forward it by hand. The lobby says whether it worked (`[Network] OpenPort`).

## 0.16.1 (2026-11-03)
- The minimap shows the right part of the map. On wide screens it was cut from a spot about a sixth of the map to the right, so it showed the wrong area, and near the right of a track it went black or showed noise.
- No crash on the way back to the menus after a long race. The original game stops a "no slot" power-up effect without checking for it, and after a long race the memory it then touched could be non-zero.

## 0.16.0 (2026-11-03)
- Debug overlay: F2 in a race shows the frame rate, the car's and camera's positions, the minimap's map cut and more, above the minimap (`[Debug] Overlay`).

## 0.15.0 (2026-11-03)
- Internet races with three or more players. Joining players usually can't reach each other directly, so the host's copy passes their messages on. Only the host forwards a port.

## 0.14.4 (2026-11-03)
- Pedestrians no longer twitch. At high frame rates the game's physics kept putting a walking pedestrian's torso back where it had been, between animation steps, so the torso flicked back and forth and the pedestrian seemed to run on the spot. The original game had the same fault but at the frame rates of its day it never showed.
- `[Visuals] FrameRate` takes any fixed rate from 20 to 100.

## 0.14.3 (2026-11-03)
- A quick key tap skips a film. The game only looked at the keyboard every half second during films, so a short press was often missed. It now looks every 50 ms. The same goes for the game's other "press any key" waits.

## 0.14.2 (2026-11-03)
- The Wrecks gallery after a race works from the keyboard and pad. Left/Right (A/D, the D-pad) scroll the cars and Up/Down choose BUY or OK. The original screen only took the mouse.

## 0.14.1 (2026-11-03)
- Power-up icons no longer show thin lines down their sides. With smoothed textures, each edge picked up colour from the opposite edge.

## 0.14.0 (2026-11-03)
- Cutscenes fill the screen: 800x480 of a 16:9 frame instead of 640x384 (`[Visuals] MovieSize`).
- Their black is true black. It used to show as a faint green band.
- The main menu shows this patch's version beside the game's.

## 0.13.0 (2026-11-03)
- Menu and loading pictures are stretched to fill wide screens (`[Display] Backdrops`).
- The mouse reaches both edges of the screen in menus. It used to stop at the menu's left edge.

## 0.12.0 (2026-11-03)
- Key set E on the game's Controls screen holds the modern controls (W/A/S/D, E/Q/Z/G power-ups and more). You can remap it and the game saves it, like sets A–D.

## 0.11.1 (2026-11-03)
- Alt+Tab works. Windows' own compatibility fix for the game had blocked it.

## 0.11.0 (2026-11-03)
- The join address box has a cursor, and a status line with a 15-second search countdown. It checks the address, and when the host answers it selects the host's game and puts focus on JOIN.
- Full screen is borderless by default (`[Display] Fullscreen`).

## 0.10.1 (2026-11-03)
- The mouse reaches the whole screen in full screen.
- Alt+F4 and Close quit at once. Before, the game hung on Alt+F4, and Close left it running without a window.

## 0.10.0 (2026-11-03)
- Join address box on the Network screen.

## 0.9.0 (2026-11-03)
- Network play over UDP: LAN games on modern Windows, joining by IP address, several copies on one PC.

## 0.8.0 (2026-11-03)
- Sharper text and HUD art (Scale2x).

## 0.7.1 (2026-11-02)
- The CD music plays again.

## 0.7.0 (2026-11-02)
- Enhanced cutscenes: deband, sharpen and Lanczos-2 at 4x.

## 0.6.0 (2026-11-02)
- Anti-aliasing, anisotropic filtering with mipmaps, smooth colour, and a frame cap at the screen's refresh rate (100 at most).

## 0.5.1 (2026-11-02)
- The HD tab has its own tinted panel.
- Max draw distance by default.

## 0.5.0 (2026-11-02)
- Xbox-style controllers through XInput.

## 0.4.0 (2026-11-02)
- HD tab on the Graphics screen.

## 0.3.0 (2026-11-02)
- Modern control scheme and key remapping.

## 0.2.2 (2026-11-02)
- The HUD's top banner stays continuous on wide screens.

## 0.2.1 (2026-11-02)
- Menus centred in the wide frame, with the mouse mapped to match.

## 0.2.0 (2026-11-02)
- A wide back buffer with the HUD laid out across it (16:9, 21:9, 32:9).

## 0.1.0 (2026-11-02)
- Widescreen, first step: Hor+ view at the desktop's resolution, with Direct3D through dgVoodoo2 always.
