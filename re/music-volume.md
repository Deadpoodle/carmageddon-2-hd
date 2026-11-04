# Music volume

Read from the instructions and the Ghidra caches, and checked in hidden runs. Tags: **H** = read from the instructions, **M** = inference. Used by `src/menukeys.cpp` and `src/music.cpp`.

## The Options screen's bar (effects)

Options_Enter 0x474530 rebuilds the slider record at 0x763820 on every entry, with `next` (+0x20) reset to 0, and sets g_sliders 0x686820 to it (H).

| Off | Value | Meaning |
|---|---|---|
| +0 | 0 | byte, bit 0 = disabled |
| +4 | 10 | fill: first of three items |
| +8 | 6 | track: first of three items |
| +0xc | 0xec | range in pixels |
| +0x10 | EVolume / 255 | value, float 0..1 |
| +0x14 / +0x18 | 0, 1.0 | min, max |
| +0x1c | 0x474660 | callback, ecx = record |
| +0x20 | 0 | next |

- **Drawing:** the pieces are ordinary item quads; the update lays them out while 0x6864f4 is 0, which the enter clears (H).
- **Input:** mouse x clamps to 0..range, with a drag latch at 0x688444. Left/Right (0x46, 0x47) step range / 10; keypad keys don't. Focus is forced to fill + 2 (H).
- **Apply:** 0x474660 sets EVolume 0x75bbf4 = value × 255, calls 0x456530(0), and plays sample 0xbb8 (H).
- **Save:** exit 0x474690 → SaveOptions 0x48d190 writes EVolume and MVolume (H).

## Music

- **MVolume 0x75bbf0 is only a switch:** six `cmp [0x75bbf0], 0x80` (0x4555cf, 0x455b2f, 0x4564a3, 0x4565f9, 0x456881, 0x4568c9) stop the CD below 0x80 and start it from there up. Default 0xc8 (H).
- **On/Off words:** items 14/15, callbacks 0x4746a0 (255) and 0x4746e0 (0), each then calling 0x456530(1) (H).
- **No volume path in the game:** the CDA channel volume 0x568ed0 is an empty stub; the exe imports no aux*, waveOut* or mixer functions from winmm; "CD: Setting volume" only logs (H).
- **The ogg player** (the game folder's `winmm.dll`, Steam and GOG): `auxSetVolume` uses the low word as a percentage (left × 100 / 65535), skips repeated values, and scales samples per buffer, so a change takes effect during playback (M for the last part).

## options.txt items (stock)

0 Abort Race, 1 Abort Game, 2 Controls, 3 Settings, 4 Load Game, 5 Volumes label, 6-8 track, 9 Effects label, 10-12 fill, 13 Music label, 14 On, 15 Off, 16 OK, 17 QUIT GAME. Options_Enter uses items 0, 1, 4 and the radio by number (M: nothing else does). Up/Down follow item order. The screen file's count is checked against +0x104 only on the first load; FE_CreateMenu rereads the count after the enter (H).

## The patch

- On first entry: items 13-15 become a copy of the track, 16-18 a copy of the fill, both at the Music row; OK and QUIT GAME move to 19 and 20; the label to 21; count 22. Fill items go to group 0, and the radio's first/last are emptied, so the enter's On/Off select writes nothing and Return doesn't treat the bars as a radio row.
- On every entry: a second record chained after the effects bar.
- The six tests' 0x80 become 1, so only 0 stops the music. Saved values of 255 and 0 mean what they did.
- Callback: MVolume = value × 255, `auxSetVolume`, and 0x456530(1) only when crossing 0, which stops or restarts the CD.
- At start-up, after OPTIONS.TXT loads, the saved value is applied.
