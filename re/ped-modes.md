# Pedestrian modes (-BLOOD / -ZOMBIE / -ALIEN): what they do, and running them on stock art

Static analysis of `testrun\CARMA2_HW.EXE` (Steam, MSVC 5, base 0x400000), 2026-11-02.
Confidence: **H** read from the instructions (capstone), **M** decompiler or inference, **L** guess to test in play.

## Short answer

The exe has no zombie or alien behaviour of its own. A mode is nothing but five file/folder names (H). Nothing else in the code reads those five pointers. Nothing compares them with "ZOMBIES"/"ALIENS", and there is no separate mode flag: a raw byte scan of the whole image for every pointer, string and setter address finds only the setters and the readers listed below (H). Every difference between the modes (blood colour, gibs, screams, animations, wording, powerup text) comes from the data the names point at. Running `-zombie` or `-alien` on copies of the stock data therefore gives the stock game with nothing changed. It is still useful as a test that the plumbing works.

## 0. How a mode is set (H)

- Switch parser `0x51aaa0`: it calls the default setter `0x4d6f10` first (0x51aab7), then for each argument: `-ZOMBIE` → `0x4d6f50` (0x51abf1), `-BLOOD` → `0x4d6f30` (0x51ac2f), `-ALIEN` → `0x4d6f70` (0x51ac6d). The last switch on the line wins. Then `0x4924a0` → `InitialiseGame 0x47dd20` (0x51af09). Arguments seem to be upper-cased first (0x587ac0, M), so `-zombie` works.
- Each mode setter writes `0x6a0414` and then calls four one-line sub-setters, one per global:

| global | blood/default | zombie | alien | sub-setters (default, blood, zombie, alien) |
|---|---|---|---|---|
| 0x6a0414 ped dir | `PEDS` 0x65e62c | `ZOMBIES` 0x65e82c | `ALIENS` 0x65e834 | written inline by 0x4d6f10/30/50/70 |
| 0x684550 sound subdir | 0 (NULL) | `ZOMSOUND` 0x595f60 | `ALSOUND` 0x595f6c | 0x457570, 0x457580, 0x457590, 0x4575a0 |
| 0x6a0ad4 powerup file | `POWERUP.TXT` 0x65ec10 | `ZOMPOWERUP.TXT` 0x65ed10 | `ALPOWERUP.TXT` 0x65ed20 | 0x4e0c00, 0x4e0c10, 0x4e0c20, 0x4e0c30 |
| 0x68c718 text file | `TEXT.TXT` 0x657da0 | `ZOMTEXT.TXT` 0x658514 | `ALTEXT.TXT` 0x658520 | 0x48ea00, 0x48ea10, 0x48ea20, 0x48ea30 |
| 0x65852c pixelmap dir | `PIXELMAP` 0x657a18 | `ZOMPIXELMAP` 0x6585d4 | `ALPIXELMAP` 0x6585e0 | 0x48f7f0, 0x48f800, 0x48f810, 0x48f820 |

- The ped loader `0x4cadc0` sets the PEDS defaults again if 0x6a0414 is NULL (0x4cadc0..0x4cadec). The text and powerup loaders also put in their own defaults if their pointer is NULL (0x48cfd3, 0x4d96d3). That code never runs normally, because the parser always calls the default setter first.
- Side note for WIKI_LOG: `-AFE` sets 0x74d3dc and `-SCALEMOUSE` sets 0x6ad468 (H, 0x51aca8 and 0x51ace8). The log has `-SCALEMOUSE` against 0x74d3dc.

## 1. Every place the mode matters

`<data>` is `0x75ba60` = `GetShortPathName(GetCurrentDirectory())` + `\` + `DATA` (built in 0x51c700 and 0x4924c2, H). Path joining is done by `0x513690`. FatalError is `0x44c230(id, arg)`, with its messages in the table at `0x591708` (H).

| # | Reader | Global | Builds | Missing file | When it runs |
|---|---|---|---|---|---|
| 1 | `0x4cadc0` LoadPedSettings | 0x6a0414 | `<data>\<dir>\SETTINGS.TXT` | fatal **0xa0** "Can't open ped file '%'" (H) | once, from InitialiseGame 0x47e28f (H) |
| 2 | `0x4cb1e0` LoadGiblets (called at 0x4caf39) | 0x6a0414 | `<data>\<dir>\GIBLETS\` (whole folder loaded by 0x502cf0(3)), then the models named in SETTINGS.TXT | missing model: fatal **0xa9** "Can't find giblet model" (H, 0x4cb2f0 area) | once (start-up) |
| 3 | `0x4cb980` OpenPedFormFile | 0x6a0414 | `<data>\<dir>\FORMS\<name>` | caller 0x404f8d: fatal **0x86** "BONER ERROR: Unable to open file" (H) | when a ped type is loaded (M: per race / on first spawn) |
| 4 | `0x4cba10` OpenPedMoveFile | 0x6a0414 | `<data>\<dir>\MOVES\<name>` | caller 0x40738c: fatal 0x86 (H) | per ped-type load |
| 5 | `0x4cbb50` OpenPedMoveFile2 | 0x6a0414 | `<data>\<dir>\MOVES\<name>` | caller 0x405fda: fatal 0x86 (H) | per ped-type load |
| 6 | `0x4cbaa0` OpenPedPeepsFile | 0x6a0414 | opens archive `<data>\<dir>\PEEPS\<name>.TWT` (0x4b4df0), then the file `PEEPS\<name>` | caller 0x406acf: fatal 0x86 (H) | per ped-type load |
| 7 | `0x4cbc10` LoadPedPeepsArt | 0x6a0414 | `<data>\<dir>\PEEPS\<name>` (pixelmaps, materials and models via 0x502cf0(3)) | none at this point; a missing texture later gives 0xa8 "Can't find ped texture" (M) | per ped-type load |
| 8 | `0x4cbbb0` OpenPedRemapFile | 0x6a0414 | `<data>\<dir>\REMAPS\<name>` | caller 0x404474: fatal 0x86 (H) | per ped-type load |
| 9 | `0x4cc950` LoadPedSoundFile (at 0x4cc974, a callback in the table 0x65d778, slot 0x65d794) | 0x6a0414 | `<data>\<dir>\SOUND\<name>` (hard-coded `SOUND`, 0x595d9c) | fatal **0xa0** (H, 0x4cc9cb) | per ped-type load. The file lists 11 groups of sample IDs. Each ID goes to `0x501930`, which loads the sample from the global sound bank (0x568830) |
| 10 | `0x455080` InitSound (at 0x4550dc) | 0x684550 | passes the name to the S3 init `0x5651d0`; see section 3 | non-fatal: the whole game goes silent (H); a missing WAV is fatal (M) | once: guarded by 0x655dd8 (init value 1, cleared at 0x45512b), called from InitialiseGame 0x47e089 (H) |
| 11 | `0x48cfc0` LoadText | 0x68c718 | `<data>\<name>`, reads up to 300 lines into 0x6b5f40..0x6b63f0 | fatal **0x63** (99) "Can't open TEXT.TXT file" (H) | once, from InitialiseGame 0x47df73 (H) |
| 12 | `0x4d96c0` LoadPowerups (at 0x4d982c) | 0x6a0ad4 | `<data>\<name>` (the icons come from the fixed `<data>\<res>\POWERUPS[.TWT]`) | fatal **0x19** (25), with the misleading text "Can't load car resolution-independant file" (H, 0x4d9869) | once, from InitialiseGame 0x47e14c (H) |
| 13 | `0x48ec20` LoadPixelmapWithFallback (at 0x48ed68; wrappers 0x48ec00 and 0x48ee00) | 0x65852c | fallback folder `<data>\<name>\`; see section 4 | returns NULL; the caller decides (most are fatal, see section 4) | on every call; the generic set is loaded at start-up |

No other code touches the five globals or their strings (H, raw scan). File opens go through `0x491170` → `0x4b4780`. That function first looks the path up in the open TWT archives: a path matches an archive when it starts with the archive's folder, and the rest of the path must equal an entry name exactly. If nothing matches, it falls back to the CRT `fopen` 0x577170 (H). So everything under a sub-folder (`ZOMBIES\...`) must be loose files. Only files sitting directly in `data\` (ZOMTEXT.TXT, ZOMPOWERUP.TXT) can also come from the data archive.

## 2. Behaviour differences coded in the exe: none

- **Separate mode flag: none (H).** The setters write only the five pointers. The only code that reads them is the 13 readers above, and each one only uses them to build a path. No code compares with 0x65e62c, 0x65e82c or 0x65e834.
- **Debug HUD.** `"%d out of %d peds are active"` (0x65e80c, used at 0x4d6838) only happens to sit next to `ZOMBIES` in .data. The debug code never reads 0x6a0414 (H).
- **Blood and giblet colour: data (SETTINGS.TXT + GIBLETS).** `0x4cb1e0` reads, for each giblet size, an indexed colour (byte) and an `r,g,b` triple (0x48fe30) into the giblet material (+0x3c index, +8 packed RGB) (M). The stock values are `4` and `255,0,0`. The blood clouds are pixelmaps named in SETTINGS.TXT (`bigbl01..05`, napalm `ex0000x`) and live in `GIBLETS\PIX16|TIFFRGB`. The ped smear material (`PEDSMEAR.MAT`/`.PIX`) and the slick (`GIBSLICK`, `GIBSMEAR.MAT`) are there too. A zombie mode's green blood would sit in `ZOMBIES\SETTINGS.TXT` plus `ZOMBIES\GIBLETS\` (M).
- **Road gib smear: data in the mode pixelmap folder.** The skid code `0x4e9c40` (called at 0x47e27e) loads `GIBSMEAR.MAT`. The material's pixelmap `GIBSMEAR` exists only in `data\PIXELMAP\` (not in any resolution folder), so in zombie mode it comes from `ZOMPIXELMAP\` (H for the path, M that this is where the green trail lived).
- **Dormant "boring" smear flag (not tied to the modes).** `0x68b880` (copied to 0x75bb90 at 0x47dd2e) chooses between `{OILSMEAR.MAT, GIBSMEAR.MAT}` (0x65fe10) and `{OILSMEAR.MAT, ROBSMEAR.MAT}` (0x65fe08) at 0x4e9c58. It is 0 in the image and no code writes it (H). It looks like Carmageddon 1's "sausage eater" censored-smear switch (M, after Dethrace's skidmark code). `ROBSMEAR.MAT` is not in the Steam data. A 0x43f5d4 read of the same flag has no effect (both paths load LOADSCRN.PIX).
- **Sounds: data.** The ped sound files (`<dir>\SOUND\*SOUNDS.TXT`) list sample IDs. The IDs, file names, pitch and volume come from the global sound bank, and zombie/alien mode swaps that whole bank (section 3). Screams and splats can therefore differ only through those files.
- **Animation, movement, AI: data.** Skeletons are in `FORMS`, animations in `MOVES` (.BON/.SKL + move .TXT), and walk speed, turn timings, slope, obstacle distance, buoyancy and severing are in SETTINGS.TXT. The ped groups and the "animals" list for gore level are in SETTINGS.TXT too (M from the loader layout, H that they sit in the mode folder).
- **Scoring: data.** "Points/time score per ped" is in `<dir>\SETTINGS.TXT` (read by 0x4cadc0, M).
- **Wording: data.** The modes use `ZOMTEXT.TXT`/`ALTEXT.TXT` and `ZOMPOWERUP.TXT`/`ALPOWERUP.TXT`. The UK Zombie 2.0 patch has zombie wording in TEXT/POWERUP/RACES.TXT. `RACES.TXT` is **not** mode-switched, so race descriptions keep whatever the single RACES.TXT says (H: no mode reader builds a RACES name).
- The stock data already has alien-themed content that has nothing to do with `-alien`: `PEDS\MOVES\Alien01_move.BON`, the `GREY` ped ("Alien Grey", group 54), `GREYSOUNDS.TXT`, `SOUND\ALIENGETHIT_A3.wav`.

## 3. ZOMSOUND / ALSOUND: a sub-folder of data\SOUND that replaces the whole sound bank

- `0x455080` builds `<data>\SOUND\SOUND.TXT` and calls `0x5651d0(path, [0x68b87c], name)` (H).
- `0x5651d0`: if `name != 0`, then `0x7a05c0 = sprintf("%s%s", drive+dir of the SOUND.TXT path, name)` = `<data>\SOUND\ZOMSOUND` (0x5652b7..0x5652e3; 0x56a349 is a `_splitpath` drive+dir helper) (H). In blood mode `0x7a05c0` stays "" (cleared at 0x5652b0) (H).
- Bank description `0x568c2c` (from 0x567fe0): if `0x7a05c0` is set, it opens `0x7a05c0 + sep + basename(SOUND.TXT)` = **`<data>\SOUND\ZOMSOUND\SOUND.TXT`** instead of `<data>\SOUND\SOUND.TXT` (0x568d60..0x568dad, CRT `_open`) (H).
- Sample loader `0x56907c` (from 0x568830, used both for preload at bank parse and for lazy loads at play time): if `0x7a05c0` is set, it opens **`<data>\SOUND\ZOMSOUND\<basename of the WAV>`** with "%s\%s" (0x5690f5..0x56913d, CRT `fopen`, no TWT) (H). Ped sounds go the same way (0x4cc950 → 0x501930 → 0x568830), so they are redirected too.
- Failure modes:
  - `ZOMSOUND\SOUND.TXT` missing: 0x568c2c returns 0, 0x567fe0 returns 4, 0x5651d0 returns 5, and 0x455080 sets `0x762328 = 0x761ee4 = 0`: sound is off for the whole session, not fatal (H, 0x4550ef..0x4550fe).
  - A listed WAV missing: 0x568830 calls `0x527c80` (BrFailure "Could not load sample %s"). The game's diag handler `0x51c620` (installed by 0x51c6c0 during init) prints "BRender FAILURE" and quits through 0x51c110 (M). Flag-1 (preload) samples fail at start-up; the others fail the first time they play (M).
- The folder is absolute in practice: there is no fallback to `data\SOUND\` for a missing file (H).

## 4. ZOMPIXELMAP / ALPIXELMAP: the fallback folder for the generic (non-resolution) pixelmaps

`0x48ec20(name)` (H):
1. If the name is `none`, return 0.
2. Try `<data>\<res>\PIXELMAP\` (res = the table at 0x662224 indexed by 0x762324: `32X20X8` or `64X48X8`; `PIXELMAP` hard-coded at 0x48ec9a, **not** mode-switched). If a TWT is open (0x68c724), use 0x48ea40; otherwise use 0x4856c0, which tries `<folder>\PIX16|PIX8\<name>.PIX` and `<folder>\TIFFRGB|TIFFX\<name>.TIF` (0x485750, format strings 0x657288..0x6572b0).
3. If that fails: `<data>\<0x65852c>\` (= `data\PIXELMAP` in blood mode, `data\ZOMPIXELMAP` / `data\ALPIXELMAP` in the other modes), same sub-folder logic.
4. If that fails, return NULL.

So only the files that are absent from the resolution folders come from the mode folder. In the Steam data these are all 20 names in `data\PIXELMAP` (`ALTMETER, ARBANNER, CPNUMB, DESKID, GIBSMEAR, MUD1/2, OIL, OILSMEAR, REPULSE, ROAD1/2, SAND1/2, SKIDMARK, SMASHY, SMOKE, SNOW1/2, SNOWSKID`), none of which is shadowed by `64x48x8`/`32X20X8` (checked by name). Ped skins and blood-cloud art do **not** use this folder: they come from `<dir>\PEEPS` and `<dir>\GIBLETS`. The `REG\PIXELMAP` load (0x486e10) is not mode-switched either. Callers and what happens when the file is missing (H):
- `CPNUMB.PIX`, `SMASHY.PIX`, `ALTMETER.PIX` (0x496e30): fatal **0x75** "Can't find file".
- `REPULSE.PIX` (0x4d9ea0): fatal 0x75.
- `SMOKE.PIX` and the other smoke frames (0x4f9fc0): fatal **0x4f** "Can't load pixelmap file".
- Skid and smear materials (`GIBSMEAR`, `OILSMEAR`, `SKIDMARK`... via 0x4e9c40 → 0x4e9ce1): fatal 0x4f.
- `ARBANNER.PIX` (0x4e1250): skipped. Material textures through 0x501560 fall back to the registry (0x51eff0).

All of these load during InitialiseGame, so a missing `ZOMPIXELMAP` stops the game at start-up.

## 5. Minimal stock-art set for -zombie (alien: swap the names)

Everything goes under the game's `data\` folder (`<cwd>\DATA`). The game must be started with the game folder as its working directory.

| Needed | Copy from | Where | Loose / TWT |
|---|---|---|---|
| `data\ZOMBIES\` | the whole `data\PEDS\` tree (SETTINGS.TXT, FORMS, MOVES, PEEPS\*.TWT, REMAPS, SOUND, GIBLETS with PIX16/TIFFRGB) | `data\ZOMBIES\` (`data\ALIENS\`) | loose folder (no TWT applies to sub-paths); the PEEPS `.TWT` archives copy as they are |
| `data\SOUND\ZOMSOUND\` | everything in `data\SOUND\` (SOUND.TXT + every WAV it names) | `data\SOUND\ZOMSOUND\` (`ALSOUND`) | loose only (CRT open) |
| `ZOMTEXT.TXT` | TEXT.TXT taken out of the active data archive (tools/twt.py) | `data\ZOMTEXT.TXT` (`ALTEXT.TXT`) | loose works; an entry in the open data archive also works |
| `ZOMPOWERUP.TXT` | POWERUP.TXT, from the same archive | `data\ZOMPOWERUP.TXT` (`ALPOWERUP.TXT`) | loose or archive entry |
| `data\ZOMPIXELMAP\` | the whole `data\PIXELMAP\` tree (PIX16, tiffrgb, tiffx) | `data\ZOMPIXELMAP\` (`ALPIXELMAP`) | loose folder |

- Size note: this duplicates the PEDS tree and the full sound bank (~240 WAVs). You could save space by making the sound folder a directory junction, but that is untested (L).
- Which data archive: InitialiseGame opens `0x4b4df0(<cwd>\DATA)`, i.e. **`<cwd>\DATA.TWT`, the game-root archive** (H, 0x47ddac). Inside it, a lookup for `<cwd>\DATA\X.TXT` matches the entry `X.TXT`. No caller was found that opens `data\DATA.TWT` (the one inside `data\`). I checked the callers whose folder string is visible; several pass a computed path and were not resolved (M). If that holds, the stock game's TEXT/POWERUP are the root archive's copies (identical to UK Blood 2.0, per WIKI_LOG), and edits to `data\DATA.TWT` do nothing. That should be checked in play (L) before anyone edits either archive. It also means the UK Zombie 2.0 `TEXT.TXT`/`POWERUP.TXT` are the right base for `ZOMTEXT.TXT`/`ZOMPOWERUP.TXT` (M).
- With all five in place, the expected result is the stock game with identical behaviour (L: test in play).
- For a zombie look later, edit only the copies. For example: green `r,g,b` and index in `ZOMBIES\SETTINGS.TXT`, green `GIBLETS` and `ZOMPIXELMAP\GIBSMEAR`, distorted screams in `ZOMSOUND`, and the zombie patch's wording in ZOMTEXT/ZOMPOWERUP.

## 6. Switching at run time

- Effectively start-up only (H for the call sites): SETTINGS/GIBLETS (0x4cadc0), text (0x48cfc0), powerups (0x4d96c0), the sound bank (0x455080) and the generic pixelmaps (0x496e30, 0x4d9ea0, 0x4f9fc0, 0x4e9c40) each have exactly one caller, InitialiseGame 0x47dd20. `0x455080`'s S3 part is one-shot (0x655dd8), and `0x7a05c0` is set only inside S3 init.
- Only the ped-type readers (FORMS/MOVES/PEEPS/REMAPS/SOUND) read 0x6a0414 again at each ped-type load. Changing it between races would only partly switch, and only for ped types not already cached (M/L).
- A real between-races switch would need all of this (L, unverified):
  1. Call the setter.
  2. Reload text with 0x48cfc0. It leaks the old 300 strings but has no guard.
  3. Reload powerups with 0x4d96c0. It reallocates and re-registers the POWERUPS pixelmaps, so there is a risk of duplicates.
  4. Reload ped settings with 0x4cadc0. It reallocates the group/giblet tables and re-adds materials, so it is risky.
  5. Free and reload the 20 generic pixelmaps and the skid materials.
  6. Shut down S3, reset 0x655dd8 and call 0x455080 again.
  7. Flush the ped-type caches.
- Recommendation: apply the Humans/Zombies/Aliens choice on the next launch. The dinput proxy can set it before InitialiseGame. Either pass the switch, or replace the default call at 0x51aab7 (`call 0x4d6f10`) with the chosen setter, because the parser calls the default first and switches override it later.

## names.tsv

```
0x4d6f10	SetPedModeDefault	writes PEDS + 4 blood sub-setters; called first by switch parser
0x4d6f30	SetPedModeBlood	-BLOOD
0x4d6f50	SetPedModeZombie	-ZOMBIE
0x4d6f70	SetPedModeAlien	-ALIEN
0x457570	SetSoundModeDefault	0x684550=0
0x457580	SetSoundModeBlood	0x684550=0
0x457590	SetSoundModeZombie	0x684550="ZOMSOUND"
0x4575a0	SetSoundModeAlien	0x684550="ALSOUND"
0x4e0c00	SetPowerupFileDefault	POWERUP.TXT
0x4e0c10	SetPowerupFileBlood	POWERUP.TXT
0x4e0c20	SetPowerupFileZombie	ZOMPOWERUP.TXT
0x4e0c30	SetPowerupFileAlien	ALPOWERUP.TXT
0x48ea00	SetTextFileDefault	TEXT.TXT
0x48ea10	SetTextFileBlood	TEXT.TXT
0x48ea20	SetTextFileZombie	ZOMTEXT.TXT
0x48ea30	SetTextFileAlien	ALTEXT.TXT
0x48f7f0	SetPixelmapDirDefault	PIXELMAP
0x48f800	SetPixelmapDirBlood	PIXELMAP
0x48f810	SetPixelmapDirZombie	ZOMPIXELMAP
0x48f820	SetPixelmapDirAlien	ALPIXELMAP
0x6a0414	gPedDirName	PEDS/ZOMBIES/ALIENS
0x684550	gSoundSubdirName	NULL/ZOMSOUND/ALSOUND
0x6a0ad4	gPowerupFileName	POWERUP/ZOMPOWERUP/ALPOWERUP.TXT
0x68c718	gTextFileName	TEXT/ZOMTEXT/ALTEXT.TXT
0x65852c	gPixelmapFallbackDir	PIXELMAP/ZOMPIXELMAP/ALPIXELMAP
0x51aaa0	ParseCommandLine	switches, then calls 0x4924a0
0x4924a0	GameMain	builds data root 0x75ba60, calls InitialiseGame
0x47dd20	InitialiseGame	one-shot start-up loader; calls all mode readers
0x75ba60	gDataRoot	<short cwd>\DATA
0x44c230	FatalError	(id, arg); messages at 0x591708
0x591708	gFatalErrorMessages	table of char* by id
0x4cadc0	LoadPedSettings	<dir>\SETTINGS.TXT; fatal 0xa0
0x4cb1e0	LoadGiblets	<dir>\GIBLETS; giblet rgb/index from SETTINGS; fatal 0xa9
0x4cb980	OpenPedFormFile	<dir>\FORMS\name
0x4cba10	OpenPedMoveFile	<dir>\MOVES\name
0x4cbb50	OpenPedMoveFile2	<dir>\MOVES\name
0x4cbaa0	OpenPedPeepsFile	<dir>\PEEPS\name(.TWT)
0x4cbc10	LoadPedPeepsArt	<dir>\PEEPS\name art
0x4cbbb0	OpenPedRemapFile	<dir>\REMAPS\name
0x4cc950	LoadPedSoundFile	<dir>\SOUND\name; 11 groups of sample IDs; fatal 0xa0
0x65d778	gPedLoaderCallbacks	callback table installed by 0x403ed0
0x501930	LoadPedSample	adds sample ID to outlet, loads it via 0x568830
0x455080	InitSound	one-shot (0x655dd8); passes sound subdir to S3
0x655dd8	gSoundNeedsInit	init 1, cleared after first S3 init
0x5651d0	S3Init	builds 0x7a05c0 alt dir from name
0x7a05c0	gS3AltSoundDir	<data>\SOUND\ZOMSOUND or ""
0x567fe0	S3LoadSoundBank	reads SOUND.TXT, parses entries
0x568c2c	S3ReadDescriptionFile	redirected to alt dir when set
0x56828c	S3ParseSoundEntry	preloads flag-1 samples
0x568830	S3LoadSample	BrFailure "Could not load sample" on failure
0x56907c	S3OpenSampleFile	redirected to alt dir when set
0x56a349	SplitDriveDir	sprintf drive+dir
0x56a3a9	SplitNameExt	sprintf fname+ext
0x527c80	BrFailure	BRender failure -> diag handler
0x51c620	DiagFailureHandler	prints BRender FAILURE, quits
0x48cfc0	LoadText	<data>\TEXT.TXT etc; fatal 0x63
0x4d96c0	LoadPowerups	<data>\POWERUP.TXT etc; fatal 0x19
0x48ec00	LoadPixelmap	wrapper of 0x48ec20
0x48ec20	LoadPixelmapWithFallback	res PIXELMAP then mode PIXELMAP dir
0x4856c0	LoadPixelmapFromDir	tries PIX16/PIX8/TIFFRGB/TIFFX
0x491170	DRfopen	TWT lookup then fopen; registry SourceLocation fallback
0x4b4780	TWTfopen	match open archive by folder prefix, else CRT fopen
0x4b4df0	OpenTWTForDir	opens <dir>.TWT, pushes TWT state
0x4b45b0	LoadTWTArchive	reads <dir>.TWT into slot table 0x691b40
0x513690	PathCat	joins path components
0x4e9c40	InitSkids	smear materials; 0x75bb90 picks GIBSMEAR or ROBSMEAR
0x68b880	gBoringSmearMode	never written; 1 would use ROBSMEAR.MAT
0x75bb90	gBoringSmearModeCopy	copy of 0x68b880 made at init
```
