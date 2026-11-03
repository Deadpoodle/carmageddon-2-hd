# Changelog

Carmageddon II HD follows [semantic versioning](https://semver.org/). Until a first release it stays at 0.x: a new feature stage raises the minor number, a fix the patch number. Each version is a git tag (`v0.14.0` and so on) on the commit that completed it. The main menu shows the version in its bottom right corner, after the game's own label.

The game itself is the Steam release, which labels itself `v1.02a`. It is the same build as the official "Super Patch V2.0" (both game exes were built on 30 November 1998). Steam's copy has its CD protection removed, plus an extra import for its CD-music player.

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
