# Race groups, progress and the main menu's race list

Read from the instructions (objdump of `CARMA2_HW.EXE`) and the Ghidra caches, and checked in hidden runs. Addresses are VAs. Used by `src/groups.cpp`.

## Progress

- `RACES.TXT` (in `DATA.TWT`) lists 40 races in 10 groups: three races, then a mission (boundary race). The race list is at 0x74d660, 0x244 per race, count 0x74d4a0. Fields used: +0x184 times won, +0x190 mission flag, +0x240 the race's group (a pointer into the group table).
- The group table is at `[0x68b8a0]`, 0xc per group: race count, first race, mission. Number of groups: 0x7634ec.
- **Group pointer 0x74d5e4**: the group in progress. Current race: 0x75bbf8. Mission due: 0x761a60. Game complete: 0x75bc04.
- **RaceResult 0x4e3440** (fastcall result; only caller 0x504025; result 3 = won):
  1. `race+0x184 += 1` for the race just run (`[0x762438]`, its race-list entry).
  2. If the game is complete: a random race (0x513520), and the pointer goes to its group.
  3. If the race is a mission: pointer += one group and mission due = 0. Past the last group: game complete, TOPRANK.SMK (0x43f390) and FE_MainLoop.
  4. Otherwise: the next race is the group's first race won 0 times; if none is left, the mission becomes due (0x761a60 = 1) and current.
- The pointer moves only in step 3, so in the stock game a finished group can't be raced again.

## Saves

- `SAVEDGAMES.ARS`: a list of 0x328-byte records. Writer 0x4919a0, reader 0x491ce0 (magic 0x12345678). Fields:

| Offset | Holds |
|---|---|
| +4 | driver name (0x75d590) |
| +0x12 | car file (0x75d5de) |
| +0x74 | times won, per race (race+0x184) |
| +0x204 | credits |
| +0x20c | game complete |
| +0x210 / +0x214 | owned cars |
| +0x308 | current race |
| +0x30c | mission due |
| +0x310.. | APO |

- **No group is stored.** Loading sets the pointer to the saved race's group.
- When the game quits with unsaved progress (0x68b8ec), 0x491ac0 appends a record (via 0x491f70, 0x492050). So the file grows by one save per session.
- **Auto Load** (OPTIONS.TXT `AutoLoad`, global 0x68b864, Graphics row 13, items 48/49): the race flow 0x503c50, state 2, calls 0x491e50 once (flag 0x660ce8). 0x491e50 loads the **last** record (0x491f2a: `base + (count-1)*0x328`). With AutoLoad 0, which is the shipped default, every start is a new game.

## The main menu (0x5a80f0)

- **Callbacks:** enter 0x469a40, update 0x46a0e0.
- **Items:**

| Item | What it is |
|---|---|
| 2 | `GROUP n` label |
| 3 | up arrow (arrowup, menu image 8, 380,140, callback 0x46a010) |
| 4..7 | race rows: the scroll window, radio group 3, callback 0x469ea0 |
| 8 | down arrow (arrowdn, image 9, 380,231, callback 0x46a080) |
| 21 | START |

- **List fill:** 0x467b30 reads the pointer and shows group (pointer index % 10) + 1. Its rows are races `4g .. 4g+3`. The selected row is the one that is 0x75bbf8. A row that is a mission uses fonts 2/3. Item 7 (the mission) gets state 1 when the mission is due or the game is complete, else -1. The enter has its own copy of the fill. 0x467a70 (call 0x467a87) refreshes the list for the main menu.
- **Race pick 0x469ea0:** finds the race by the focused row's name. It sets 0x75bbf8, sets the **pointer to that race's group**, and writes the description into item 22 (only when not in mouse mode). The update calls it as the focus moves onto a row (select on move).
- **Stock arrows:**
  - 0x46a010 and 0x46a080 move the pointer one group, between 0 and 9, with no unlock check, and are rate-limited to 400 ms.
  - At the top of every update (0x46a0e8..0x46a152), the update sets items 3 and 8 to state and visible = (game complete), so they show only after the game is finished.
  - The same code sets item 7's state: 1 if the mission is due or the game is complete, else -1.
  - So these arrows are the original's way back to earlier groups, after the end.
- **Buttons:**
  - FE_CreateMenu builds one quad per item after the enter (FE_CreateButton 0x46c5c0, g_buttons 0x687248, 0x2c each).
  - The picture is copied, and UVs are size / power-of-two size, so a picture is mapped 1:1. A smaller quad crops it.
  - The driver stretches a picture whose size isn't a power of two up to one, so a 10x20 picture is shown cropped too.
  - Only list rows' quads follow later changes to w/h (0x466ce0). The label's quad keeps the width it had at the enter, and the hit test (0x4677d0) uses the quads.

## What the patch does (groups.cpp)

- **Update states:** jne → jmp at 0x46a0fc (`75 1a` → `eb 54`) skips the update's per-frame states for the arrows and the mission row while Earlier groups is on. The patch sets them itself: arrows shown where there's a group to go to, and a past group's mission row selectable.
- **Arrows:** items 3 and 8 become left/right arrows beside GROUP. They are 16x32 copies of menu images 18/19 with the arrow averaged down to 10x20 in the middle. The copies are lent to the image table while the buttons are built.
- **GROUP row:** the label is focusable, and the three items share a glow group. Up/Down pass over the arrows, and Left/Right on GROUP page.
- **Paging:** lends the pointer to 0x467b30, then picks a race of the shown group through 0x469ea0.
- **Progress:** the pointer is kept on the first group whose mission hasn't been won. It is reset after every pick and on every enter, which also undoes a load of a save whose race is in an earlier group.
- **Replays:** RaceResult (via the call at 0x504025) sees a race from another group with its mission flag cleared. So it runs step 4 on the group in progress, and an old mission doesn't move the player on.
- **Auto Load:** follows `[Game] AutoLoad` (default 1) after OPTIONS.TXT loads. The Graphics screen's exit writes it back.
