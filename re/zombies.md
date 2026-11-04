# Zombie pedestrians of our own

Read from the instructions (objdump of `CARMA2_HW.EXE`) and the Ghidra caches, and checked in hidden runs. Tags: **H** = read from the instructions or data, **M** = decompiler output or inference. Used by `src/zombies.cpp`. `re/ped-modes.md` covers the game's own `-zombie` mode, which needs UK-disc art.

## Drunk pedestrians (the shamble)

- Power-up 62, "DRUNK pedestrians!" (group text "Piss-artist pedestrians!"): action 44, got 0x4de900, lose 0x4de990, 30 s, group 1024 (H).
- The effect is one global, **0x7447b8**, read at two sites (H):
  - SetPedMove 0x4cbd48: `cmp [flag], ebx`, `je` at 0x4cbd4e. If set, the requested move goes through column 4 of table 0x65d7f4.
  - The per-frame check 0x4d4d64, inside 0x4d3740: `mov ecx,[flag]; test ecx,ecx`, `je` at 0x4d4d6c.
- Column 4 sends moves 0–14, 30, 31, 40–42, 50–52, 111 and 112 to 31 (pissed_person). Falls, get-ups, hits and deaths (70+) aren't remapped (H).
- Nothing is stored per pedestrian, so it reaches every pedestrian, including ones spawned later (H).
- Neighbours: Groovin' 0x7447a8, Panic 0x7447f8, tested after drunk at both sites (H). InitPeds 0x4d5bd0 clears all three every race (H).
- **Patch:** each read is diverted to a stub that leaves ZF clear ("drunk") unless Groovin' or Panic is set. The real power-up still works. No power-up entry is touched, so there's no icon or timer.

## Pedestrian sounds (the moans)

- All pedestrian sounds go through PlayPedSound 0x4cd880 (fastcall ped, group; stack loop, car; `ret 8`), with 11 groups per ped type (H).
- Table 0x65d758 `{0,0,0,0,1,0,1,1,1,1,1}` marks the voice groups: 4 live-punt scream, 6 about-to-die scream, 7 whimper, 8 plead, 9 fall, 10 out of breath (H for the bytes, M for the meanings).
- Pitch is `65536.0 / gPedScale` (0x69bc24, so giant pedestrians are already lower). The double 0x58aa58 is read by two `fdivr`, operands at 0x4cd953 and 0x4cd999, and only there (H).
- It reaches S3StartSound2 0x564e9b or S3StartSound3D 0x566048 as rate = sample rate × pitch / 65536. The 3D update 0x56711c keeps it through doppler (M).
- **Patch:** both operands point at our double, set per call by a detour of 0x4cd880: 65536 × 0.7 for voice groups, else 65536. Sounds last about 1.43× as long, and a voice never interrupts, so screams come a little less often (M).

## Pedestrian skins

- `PEDS\PEEPS\<type>.TWT`, `PIXIES.P16`: RGB565, mostly 128², no shade table (H). `<NAME>B` is a bloodied copy, stored right after `<NAME>`. Not every copy lines up with its skin (FEM07's doesn't).
- Path: 0x406ab0 → LoadPedPeepsArt 0x4cbc10 (call at 0x406ba1) → 0x502cf0 → 0x5024f0 → BrMapAdd, call at 0x502641. Duplicates are freed instead of added. The storage is cleared each race, so skins load per race (H/M).
- **Patch:** a flag around 0x4cbc10, and the BrMapAdd call recolours type 5 pixelmaps before the first upload. Formula in `tools/zombify.py` and `zombies.cpp`: skin goes to a dead grey-green, clothes keep their colour, and blotchy grime (hashed value noise) darkens everything. Colour alone can't find skin: khaki and tan clothes share its hue, and in a race they turned green. But every human skin has one layout (three faces across the top 30%, a hand at the bottom left, arms down the left, torso right of them), so each texture's own flesh tone is the median red and green share of the flesh-coloured texels in the face band. A texel counts as skin by how near it is to that tone, fading out to avoid speckle, with a wider match in the face band and a narrower one on the torso. Animals have too little flesh colour in that band, so any flesh-like colour counts. In `<NAME>B`, a texel that differs from `<NAME>` and is red is blood.

## Blood

No colour constant in the code; it's all data (M).

| Effect | Source |
|---|---|
| Giblets | LoadGiblets 0x4cb1e0 (call at 0x4caf39, once at start-up, via LoadPedSettings 0x4cadc0): chunk models' first face material, colour at material+8. Table [0x694274], count [0x69bc18], stride 0x18, count byte +2, models +0xc; spurt model [0x694278]. Textures GIBS2/GIBS3 (RGB565) |
| Blood clouds | BIGBL01–05 (BL0001–5 load too), ARGB4444 (alpha in the top nibble) |
| Pool under a body | GIBSLICK |
| Drag smear | PEDSMEAR |
| Tyre trails | GIBSMEAR through LoadPixelmap 0x48ec00, call at 0x4e9ce1. OILSMEAR comes through the same call |

- All the PEDS\GIBLETS art passes the BrMapAdd call during LoadGiblets. **Patch:** a second flag there recolours it; the trail call recolours only GIBSMEAR; chunk colours are overwritten after LoadGiblets.
- Because blood art loads once at start-up, the setting is read once, at the start, and nothing is installed for Humans. The HD tab says when a change waits for a restart.

## Wording

- The official UK zombie patch differs from the UK blood patch only in TEXT.TXT (3 lines), POWERUP.TXT (about 20 names) and comments in RACES.TXT. The Steam texts are a later build, so its files can't simply be swapped in.
- TEXT.TXT: strings in the table at **0x6b5f40** (null-ended; 0x68c230 is Interface.txt's). Each is allocated to its own length, so they're only rewritten in place when the result fits. Power-up names: inline at entry+0x50 (0x40 bytes), entries at [0x6a0a54], 0xac each, count 0x6a0ad0.
- **Patch:** on the main menu's first enter, "pedestrian(s)" in any case becomes ZOMBIE(s) and the word "Peds" becomes Zombies. 24 strings change, the same ones as the official patch.
