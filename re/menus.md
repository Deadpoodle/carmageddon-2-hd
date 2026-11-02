# Front-end menus and the Graphics screen (CARMA2_HW.EXE, Steam build)

Static analysis only (Ghidra export, plus capstone/pefile on the real bytes). Confidence tags: **H** = read from the instructions or the exe's static data; **M** = decompiler output or an inference from it; **L** = a guess to test in play. All addresses are VAs (image base 0x400000). Fastcall is the norm here: `ecx` holds the first argument, `edx` the second, and the rest go on the stack.

## 0. Summary

- Every front-end screen is a static 0xB8C8-byte struct in `.data`, named by its first field. The struct holds compiled-in **callbacks and navigation** (enter, exit, update, per-item callback, per-item target screen). The `INTRFACE\<name>.TXT` file only fills **layout** (positions, fonts, flags, groups, radio, scroller and level-bar specs) (H).
- On screen, every item is two things. One is a BRender quad "button" actor, which serves as the hit rect and the glow. The other is poly-font text, redrawn every frame from the item's fields. The dark tab-shaped panel is a **3D menu model (A-8.dat)** that morphs between screens. It is not a pixelmap (H/M).
- A second page can be built **in place on the Graphics struct**. It has room for 99 items (53 used). Hidden items are skipped by drawing, hit-testing and keyboard focus. Radio behaviour is driven entirely by group ids. The only code we need is three swapped data pointers (enter, exit, tab callback). No hook on the draw or hit-test functions is required (H/M, see §5).

## 1. How a screen is defined and run

### 1.1 Screen struct (tFE_screen, size 0xB8C8) (H unless marked)

| Off | Field |
|---|---|
| +0x000 | `char name[0x100]`. The file is `<data>\INTRFACE\<name>.TXT`. The loader overwrites it with the first token of the file's title line, e.g. "Options Menu" becomes "Options". |
| +0x100 | timeout in ms. 0 = none; NetworkSummary uses 30000. When it expires, the screen behaves as if Escape were pressed. |
| +0x104 | item count. Must equal the count in the file, otherwise the fatal error "Error - Menu item number mismatch" (FatalError 0x527c80, which aborts). |
| +0x108 | `enter(screen)`. Called by CreateMenu before the buttons are built. Returns 1. |
| +0x10c | `exit(screen)`. Called by DestroyMenu. |
| +0x110 | `update(screen)`. Per-frame input. 0 means the generic code inside FE_UpdateScreen 0x46c0d0 is used. Returns 0 to stay, or a code (§4). |
| +0x114 | parent. Runtime back-link that "back"/Escape follows. |
| +0x118 | saved focus item index, restored when the screen is re-entered. |
| +0x11c | flags (bit 1 is read; no effect found) (M). |
| +0x124/+0x128/+0x12c | indices of the A, B and C panel models (menumodels.txt). 0 = hidden. |
| +0x130 | `items[99]`, stride 0x158 |
| +0x8638 | item[99]: "no item" sentinel, copied from the template at 0x604730 on load. Focus 99 means "none" (mouse mode). |
| +0x8790 / +0x8794 | scroll-list count and entries (stride 0x34). The runtime copies of the first/last visible items are at +0x87a0..+0x87c0. |
| +0x9be4 / +0x9be8 | radio-group count and entries (stride 0x2c): +0 id (label item), +4 total, +8 first item, +0xc last item, +0x10 selected, +0x14..+0x20 greybox x/y/w/h, +0x24 greybox flag. **An item's group id g maps to radio entry g-1.** |
| +0xad18 | "number of groups" (2nd header line of the file). Non-zero turns on row-wise up/down. |
| +0xad1c | loaded flag. The file is read **once per session**. |
| +0xae24 | backdrop name, e.g. `graph.tif` |
| +0xaf24 / +0xb884 | level-bar entries (stride 0x18) and their count |
| +0xb8c0 | set to 1 on the parent when a child is first loaded (M) |
| +0xb8c4 | last update result, read by the main loop |

### 1.2 Item struct (tFE_item, 0x158) (H for the offsets; meanings M where noted)

| Off | Field | Source |
|---|---|---|
| +0x00 | text id: an index into the Interface.txt table. **>0x400 means draw `name` instead.** 0x402 = name as a static label; 0x404 = name, auto-sized like a text id. 0x401, 0x403 and others = special or none (M). | static |
| +0x04 | `callback(screen)`, fastcall, returns a code. **4 = stay on this screen.** 0x466450 = null stub (`return 0`). | static |
| +0x08 | target screen pointer. **1 = parent (back).** 0 = none. | static |
| +0x0c | result code, used when there is no callback. 1 sets g_fe_exit 0x6883a8. | static |
| +0x10 / +0x14 | normal / highlight poly-font index | file |
| +0x18,+0x1c / +0x20,+0x24 | x,y / w,h in 640x480 space. w,h are replaced by the text size at load unless wrap≠0. | file |
| +0x28 | state: 1 = focusable, 0 = label, **-1 = greyed** (alpha 0.4, not focusable). Options_Enter writes -1 to grey out Abort Race and Load Game. | static/runtime |
| +0x2c | visible: drawn, hit-tested and focusable only when ≠0 | static |
| +0x30 | menuimages.txt pixelmap index (image button); 0 = none | file |
| +0x34 | "greyed-out backdrop" | file |
| +0x38 | "selectable": 1 = an option, i.e. a radio member | file |
| +0x3c | selected (radio state) | runtime |
| +0x40 | group id (1-based, maps to radio[g-1]) | file |
| +0x44 | level-bar id | file |
| +0x48 | wrap text (also turns auto-size off) | file |
| +0x4c | "disable glow?" in the file. The code uses it as a **glow group**: items sharing a non-zero value pulse together (M). | file |
| +0x50,+0x54 | original x,y (copied at load) | load |
| +0x58 | byte flags. Bit 0 = focused this frame. | runtime |
| +0x59 | `char name[255]`. File token with `_` turned into a space; `XXX` gives an empty name. | file |

### 1.3 The .TXT format (VERSION 3) and loader FE_LoadScreenFile 0x466760 (H)

- The path is built from 0x75ba60 (data dir) + `INTRFACE` + name + `.TXT`, and the file is opened with 0x491170. The first line must be `VERSION 3` (error codes 0xb0, 0xb1, 0xb2 through 0x44c230).
- Tokens are split on `\t`, space, `,` and `/` (the set at 0x58f41c), so `60,100 // xpos` parses. Helpers: ReadToken 0x490630, ReadInt 0x48fb00, ReadIntPair 0x48fdc0.
- **Order:**
  1. Title token.
  2. Item count (checked against +0x104).
  3. Number of groups (+0xad18).
  4. Backdrop name (+0xae24).
  5. Per item, 11 lines: name; x,y; w,h; font normal,highlight; pixelmap; greyed backdrop; selectable; group; level bar; wrap; glow.
  6. Scroller count, then each scroller as 11 ints in pairs.
  7. Radio count, then each group as 10 ints in the order id,total / first,last / selected,greybox / gx,gy / gw,gh.
  8. Level-bar count, then each bar as 4 ints.
- The file gives no callbacks, targets, text ids or focusable/visible flags. Those are compiled into the struct.
- Every screen's enter function runs the loader only if +0xad1c==0, then calls FE_AutoSizeItems 0x4666f0. That routine sets w = PolyFont_TextWidth(font normal, text) and h = PolyFont_Height(font normal) for text ids ≤0x400 or ==0x404, when wrap==0. **So hit rects equal the text extents** (H).
- Keyboard order is simply the item order in the file (§1.6).

### 1.4 Text

- Interface.txt is loaded once by FE_LoadInterfaceText 0x484fd0 into g_interface_text[300] at 0x68c230, with the count at 0x68c6e0. Every non-blank line that is not a `//` comment counts, from 0. Steam's file has **258 entries, leaving ids 258..299 free** (H).
- Lookup: FE_GetInterfaceText 0x485110 (ecx = id).
- Graphics ids: 0xF9 "Graphics" (title), 0x22..0x2F labels, 0x34..0x37 Always/Usually/Near/Never, 0x38/0x39 On/Off, 0xE2 "Auto Load:", 0xE3 "Ambience:", 0xF8 "Explosives:", 7 "ok" (H).

### 1.5 Loading, drawing and animating a screen (H for the call order; M for render details)

- **FE_MainLoop 0x46d8e0** (fastcall mode) runs the front end. Callers: 0x494540 and 0x494570 (main-menu states), 0x4e32b0/0x4e3440, and 0x503c50 (race flow).
- It first runs **FRONTEND_Setup 0x46d1c0**, which:
  - loads the text, poly fonts (InitPolyFonts 0x463d40), MENUIMAGES.TXT (→ g_menu_images 0x686850) and MENUMODELS.TXT (13 A, 11 B and 6 C models into tables 0x6870b8, 0x687188 and 0x687058, stride 0x10);
  - creates three "Backdrop" panel actors 0x687030/34/38 (FE_CreateBackdropActors 0x46cf10). Each is translated by (300,-250,-2.5) and scaled 1.25 (values set in 0x46d1c0);
  - picks the start screen from the mode: 0 Main, 1 Options (in-race), 2 Wrecks, 3 NetSync, 4 NetworkSummary, 5 Credits, 6 newgame. The start screen's parent is set to 0.
- **Each frame:**
  1. Blit the backdrop pixelmap g_backdrop 0x686f8c (0x5191b0).
  2. If no transition is running, call **FE_UpdateScreen 0x46c0d0** and store its result in +0xb8c4. FE_UpdateScreen calls screen+0x110 if set, else runs the generic input code.
  3. Clear depth.
  4. If a transition is running (g_transition 0x686ef8 > 0), morph the A/B/C panels one step (FE_MorphModel 0x46f5b0, 16 steps) and fade their materials.
  5. Render the 3D scene: 0x522f30(world 0x68650c, camera 0x686f94, ...). The camera is an orthographic 640x480 view (M).
  6. If no transition: **FE_DrawItems 0x46e020** (text and glow), then FE_DrawCursorAndThrottle 0x43e0a0. The throttle busy-waits until 21 ms have passed, which is the **48 fps menu cap** (H).
  7. Flip (0x51c520) (M).
  8. Act on the result (§4).
- **FE_CreateMenu 0x46c970** (ecx = screen):
  1. Calls enter (+0x108).
  2. Reloads the backdrop from `INTRFACE\BACKDROP\<base>\PIXIES.P16` or `<base>.TWT` (FE_LoadBackdropPixelmap 0x46abf0). It keeps only the pixelmap whose name equals the base name and frees the rest.
  3. Builds one quad actor per item with FE_CreateButton 0x46c5c0 (x, y, w, h, pixelmap or a blank 8x8). It loops up to **+0x104 read after enter**, so enter may add items. Buttons live in g_buttons 0x687248 (100 slots × 0x2c: actor, model, material, pixelmap, pixelmap2, colour...). The count goes to 0x68683c, and slot 99 is the cursor/none button.
- **FE_DestroyMenu 0x46ccb0** calls exit (+0x10c) and frees the buttons.
- **FE_DrawItems 0x46e020**, for each visible item (+0x2c), with fonts and alphas H from the instructions:
  - the button's render style goes to 4 (shown) or 1 (hidden);
  - focused item (flag bit 0): text in the normal font at α 0.6, then the highlight font at α = 0.6·p (or 0.3+0.7·p if selected). p = g_pulse 0x6886d0, a 750 ms triangle wave from 0 to 1. Text through PolyFont_DrawTextAlpha 0x463ad0, left-aligned;
  - greyed (+0x28<0): normal font, α 0.4;
  - selected (+0x3c): highlight font, **centred** in [x, x+w] (mode 2) via PolyFont_DrawText 0x463850;
  - otherwise: normal font, left-aligned (mode 0);
  - the glow quad colour is 0x800000 normally, 0x400000 when dim, and (0.3+0.7p)·255<<16 for the focused item and its glow group (M: these look like material opacity values).
  - Because w is the auto-sized text width, "centred" lands almost where "left" does (M; L to eyeball).

### 1.6 Input, hit-testing and focus (H unless marked)

- **Keys** go through KeyPressed 0x482550 (ecx = code). A key code equals its line index in `KEYNAMES.TXT` minus 2 (H, checked against all 12 codes the handlers use):
  - Up 0x48 / KP8 0x59, Down 0x49 / KP2 0x53
  - Left 0x46 / KP4 0x55, Right 0x47 / KP6 0x57
  - Esc 0x3f, Return 0x33, KP-Enter 0x34
- **Mouse:** GetMousePos 0x483c10 and GetMouseButton 0x4824c0 (1 = click; M). Moving the mouse sets g_mouse_mode 0x79ecb8, which a keypress clears. In mouse mode, focus = the item under the cursor, or 99.
- **Hit-test:** FE_HitTestItem 0x4677d0 (generic) and FE_HitTestItemOrSlider 0x471ba0 (options screens) test x∈[x, x+w], y∈[y, y+h] against the button quad built from the item rect. They require +0x28>0 and +0x2c≠0. Sliders are tested afterwards.
- **Focus** lives in g_focus 0x688770.
  - Up/Down step through items in **index order** with wrap-around. They skip items that are not focusable (+0x28≤0) or not visible, and items in the current item's group. On reaching a group they land on its first item (M). Code: generic 0x46c0d0, options 0x470c20 (Up loop at 0x471370; Down via 0x471dd0).
  - Left/Right (options handler only) move inside the row, wrapping at radio[g-1].first/last.
  - Moving focus does not change a value.
- **Activation** (Return, KP-Enter or a click on an item) goes through FE_OptionsScreenUpdate 0x4718f8:
  1. FE_ToggleFocusedItem 0x467890. If the item is an option (+0x38) with a group, it clears +0x3c on every item of that group and sets it on this one, i.e. **a radio select driven purely by data**. Level-bar items light their segments instead.
  2. If +0x0c==1, set g_fe_exit.
  3. target = +0x08, with 1 meaning the parent.
  4. Call the callback, playing click sound 0xbbc when the item has no target and a non-stub callback (PlaySound 0x455690, M). A return of **4 stays** on the screen.
  5. If there is a target, transition (§4).
- **Escape:** Controls gets a special check (0x472d80). Otherwise it plays sound 0xbbf and goes to the parent, or returns 1 if there is none.

## 2. The Graphics screen

- **Struct 0x63e528 "Graphics"**: 53 items; enter 0x4747e0, exit 0x474850, update 0x470c20 (the handler shared with Options, Controls, NetworkOptions and Quit); static parent Options 0x632c60; saved focus 1 (OK); models A-8, B-0, C-0 (H).
- **Definition:** `data\INTRFACE\graphics.txt`. The Steam copy is byte-identical to the 2.0 patch copy.
  - Item 0 is `SETTINGS`, the title: text 0xF9 "Graphics", font 5/5, at 60,60, not focusable.
  - Item 1 is `OK`: fonts 6/7 at 540,393, target = 1 (back).
  - Then 14 rows. Each has a label (fonts 3/3, not focusable, group 0) followed by its option words (fonts 0/1, selectable, group 1..14).
  - 14 radio entries in group order (H).
- **Fonts:** poly-font index → colours from `data\FontCol.TXT` (top/bottom vertex RGB) (H).
  - 0: hand green 15pt, unlit (0,255,0 → 0,140,60): option words
  - 1: hand green 15pt, lit (128,255,128 → 0,255,0): selected and hover words
  - 3: hand red 15pt, lit (255,80,80 → 193,0,13): row labels
  - 5: serp red 30pt, lit: the "GRAPHICS" title
  - 6/7: serp green 30pt, unlit/lit: OK
  - Glyphs come from `64x48x8\FONTS` (HAND15, HAND15U, SERP30, SERP30U...), lazy-loaded by 0x4642d0 (M).
- **Rows → item indices → getter/setter** (H for the calls; M for what each setter does). The enter function calls Graphics_LoadRadio 0x4b4090. That reads each getter and calls FE_SetRadioSelection 0x484f70 (ecx = screen, edx = group id, stack = item). The exit function calls Graphics_ApplyRadio 0x4b42c0, which reads FE_GetSelectedInGroup 0x484f40 (ecx = screen, edx = group, returns an item or -1), calls the setters, and finally **SaveOptionsFile 0x48d190**, which rewrites all of `OPTIONS.TXT`.

| Row (group) | Items | Get / Set |
|---|---|---|
| Car complexity (1) | 3-6 | 0x448f20 / 0x448f10 (0..3) |
| Scenery cut-off (2) | 8-11 | Yon 0x446bb0 / 0x446b70: 35, 30, 25, 20 |
| Scenery pop-up (3) | 13-16 | 0x40dfe0 / 0x40dff0: 1.0, 0.5, 0.25, 0.125 (probably YonFactor, M) |
| Shadows (4) | 18-20 | 0x4e9950 / 0x4e9940 (2 = all, 1 = your car, 0 = none) |
| Sound detail (5) | 22-24 | 0x4569e0 / 0x456950 |
| Gore (6) | 26-28 | 0x4d6fc0 / 0x4d7030 |
| Sky texture (7) | 30-31 | 0x446e00 / 0x446e10 |
| Darkness/fog (8) | 33-34 | 0x446f90 / 0x446fa0 |
| Traffic (9) | 36-37 | 0x44ed00 / 0x44ecf0 |
| Animals (10) | 39-40 | 0x4d6fd0 / 0x4d7000 |
| Flame thrower (11) | 42-43 | 0x4d6fe0 / 0x4d7010 |
| Explosives (12) | 45-46 | 0x4d6ff0 / 0x4d7020 |
| Auto load (13) | 48-49 | global 0x68b864 |
| Ambience (14) | 51-52 | global 0x655dc4 |

- **Yon reset (H):** the enter function maps Yon to a button by truncating (35−Yon)×0.2.
  - Yon 31..39 maps to "distant", which saves 35.
  - **Yon ≥ 40 falls to the default case, "nearest", which saves 20.**
  - So just opening and leaving Graphics turns PCGamingWiki's `Yon 70` tweak into 20.
- **OK, Escape or any exit** leads to DestroyMenu → exit 0x474850 → 0x4b42c0, which applies everything and saves OPTIONS.TXT. Escape applies the changes too; there is no cancel (H).
- **Title and tab art:**
  - The red "GRAPHICS" is item 0 drawn in poly-font 5, not a pixelmap.
  - The dark panel with the tab is **menu model `INTRFACE\A-8.dat`** (297 vertices). After the 1.25 scale and (300,250) offset it spans x 51..596, y 51..433, with the tab at x≈61..244, y 51..84. It is rendered as "Backdrop" actor A with Backdrop_material1 (H for the vertices; M for the transform, which matches the art below).
  - The backdrop pixelmap is "Graph" from `INTRFACE\Backdrop\graph.TWT` (PIXIES.P16/.P08). That file also holds "graphl", the same picture with the panel composited in. The loader frees it, so it is designer reference only (H).

## 3. Adding a second tab/page

- **Switching definition at run time:**
  - Item targets are pointers, so a click can take you to any struct.
  - A new screen needs its own 0xB8C8 struct plus its own `.TXT`, because the file name is the struct's name.
  - Every switch does DestroyMenu + CreateMenu: the backdrop reloads from disk, the exit callback fires (Graphics saves OPTIONS.TXT), and a **16-frame morph runs during which no text is drawn**.
  - Parent links are rewritten: on a forward move the target's +0x114 becomes the source screen. That breaks "OK goes to Options" for sibling tabs unless it is fixed afterwards.
  - Workable, but it flickers and needs plumbing (H/M).
- **Rows by data only:** not possible.
  - The count in the struct must match the file, or the game hits a fatal error.
  - New items need static fields the file does not supply: text id, callback, focusable, visible. With those all zero, the text would be id 0, "New game".
  - So rows have to be added by **our code**. They can still be pure engine items, with no draw or hit-test hooks:
    1. Wrap enter.
    2. Call the original.
    3. Fill items 53..98 and their radio entries.
    4. Bump +0x104 and +0x9be4.
    5. Call FE_AutoSizeItems.
  - CreateMenu reads the count after enter, so the extra buttons get built (H).
- **Hook points**, should we ever need them:

| Address | Function |
|---|---|
| 0x46e020 | FE_DrawItems (fastcall screen); after it, extra drawing |
| 0x4677d0 / 0x471ba0 | hit-test (screen, x, y → item or -1) |
| 0x467890 | FE_ToggleFocusedItem (select) |
| 0x470c20 | per-frame input for options screens. Wrap it by swapping the struct's +0x110 pointer, not by patching code. |
| 0x482550 | KeyPressed |
| 0x46c970 / 0x46ccb0 | CreateMenu / DestroyMenu |

- **Text drawing**, to match exactly (H for the signatures):
  - `PolyFont_DrawText 0x463850`: ecx = font, edx = text, then x0, y0, x1, y1, mode, 1. Callee pops 0x18. Mode 0 = left at x0, 2 = centred in [x0, x1]. It forwards to the alpha version with α 1.0.
  - `PolyFont_DrawTextAlpha 0x463ad0`: same arguments plus a `double alpha`; pops 0x20. It word-wraps to x1−x0, maps a–z to A–Z, and a CR forces a line break. It draws through PolyFont_DrawLine 0x465380 (string actors under gString_root_actor; M).
  - `PolyFont_TextWidth 0x465ca0` (ecx = font, edx = text) and `PolyFont_Height 0x463730` (ecx = font).
  - Font records: 0x76d9b0 + font×0x1c68. +0 is line height, +4 default glyph width, +0x10 loaded flag. Per-character entries start at +0x1c, 0x1c bytes each: +0 present, +8 width.

## 4. Navigation graph and transitions (H from the static structs)

- **Main 0x5a80f0** (A2 B2 C1)
  - "Menu" → **Options**
  - "Network game" → **Network** → "Options" → **NetworkOptions**
  - car/driver items → **ChangeCar**
  - "Start" → **newgame**
  - "Single Player" is a callback
- **Options 0x632c60** (A6)
  - "Configure Controls" → **Controls** 0x604940 (A7)
  - "Settings" (text 0xED) → **Graphics** 0x63e528 (A8)
  - "Load Game" → **LoadGame** 0x5ed5a0
  - "Quit" → **Quit** 0x649df0 (A12)
  - "ok" → back
  - "Abort Race", "Abort Game" and the music On/Off are callbacks
  - The volume slider is registered at run time in g_sliders 0x686820 by Options_Enter 0x474530
- **Other screens:** Credits, Wrecks, NetSync and NetworkSummary are entered by mode. StartGame 0x59c828 is the race-start screen.
- **Sounds** (M): 0xbbe forward, 0xbbf back.
- **A transition**:
  1. save focus in +0x118 and the scroller state;
  2. DestroyMenu(old), which calls exit;
  3. set g_current_screen 0x688abc;
  4. CreateMenu(new), which calls enter;
  5. if new ≠ old's parent, set new.parent = old;
  6. FE_SetupPanelMorph 0x470a90(old, new) for A/B/C. A model index of 0 fades out or stays hidden; a non-zero index fades in. g_transition = 16;
  7. restore focus (Quit always 0).
- **Update results** (FE_MainLoop):
  - 0: stay
  - 1: go to StartGame
  - 3: re-run FRONTEND_Setup
  - any other non-zero: leave the front end; FE_MainLoop returns (result≠2)
  - In-race pause starts at Options with parent 0, so Escape returns to the race (M).

## 5. Recommendation: an in-place "HD" page on the Graphics screen

This needs no data-file edits and no code patches, only pointer swaps in `.data` plus our functions.

1. **Pointer swaps:** set Graphics +0x108 (at 0x63e630) to `HD_Enter` and +0x10c (at 0x63e634) to `HD_Exit`.
2. **HD_Enter(screen)** (fastcall):
   1. Call 0x4747e0.
   2. First time only (count==53), append:
      - item 53, the **"HD" tab**: text id 0x404, name "HD", fonts 6/7, at ≈(265,60), focusable, callback `HD_TabClick`;
      - the HD rows as items 54..98. Labels use 0x404, fonts 3/3, +0x28=0. Option words use 0x404, fonts 0/1, +0x28=1, +0x38=1, group 15+, callback 0x466450;
      - radio entries 14+ (first/last), with +0x9be4 bumped.
   3. Call 0x4666f0 to auto-size.
   4. On every entry: show the GRAPHICS page (rows 2..52 visible, HD rows +0x2c=0), set our radios with 0x484f70, and clamp +0x118 to a visible item.
3. **HD_TabClick(screen):**
   - Toggle +0x2c across the two row sets.
   - Swap the tab looks. Active title: fonts 5/5, +0x28=0. Inactive tab: fonts 6/7, +0x28=1. Item 0 also gets the tab callback while HD is showing.
   - Set g_focus 0x688770 to the other tab.
   - **Return 4**, so there is no transition and nothing reloads. The click sound comes free.
4. **HD_Exit(screen):**
   1. Call 0x474850, which applies and saves OPTIONS.TXT.
   2. Read our groups with 0x484f40 and write them to our own `Carma2HD.ini`, not OPTIONS.TXT. The writer rewrites the whole file and we have not checked how the reader handles unknown keys.
   3. If HD owns draw distance, re-apply Yon after the original (setter 0x446b70) and call 0x48d190 again, or fix the ≥40 → 20 mapping above.
5. **Tab art (optional, native look):**
   - Models C-3 and C-5 are used by no screen's header, and the C, B and A model tables are full (6, 11 and 13 slots, packed back to back).
   - After FRONTEND_Setup, reshape the vertices of **C-5** (table 0x687058 + 5×0x10 → br_model, vertices at model+8, stride 0x28, x at +0 and y at +4, count u16 at +0x10, then 0x51f950(model, 1)) into a 44-vertex tab box at about x 250..350, y 51..84. Set Graphics +0x12c = 5.
   - The tab then fades and morphs in with the panel. It uses the panel's own material, so it matches A-8.

**Risks:**
- At most 46 extra items: 99 per screen, and 100 button slots including the cursor (H).
- Hidden rows depend on +0x2c being honoured by draw, hit-test and both focus walkers. That is H/M from the code, but check keyboard wrap-around in play (L).
- The Graphics file loads only once, so the appended items must survive re-entry. They do, because the struct is static (H). Never let the original loader run with count≠53.
- 0x470c20 is shared by five screens: never patch it, only swap struct pointers.
- The menu works in 640x480 space. Any HD resolution change must keep the menu camera and mouse scaling (`-SCALEMOUSE` flag 0x74d3dc) in agreement (M).
- Settings that cannot change mid-race need greying (+0x28=-1) when entered from the in-race Options (L).
- Centred drawing of the selected word: check the alignment of our words visually (L).

## names.tsv

```
0x46d8e0	FE_MainLoop	front-end frame loop; fastcall mode; returns bool
0x46d1c0	FRONTEND_Setup	text, fonts, menu images/models; picks the start screen by mode 0..6
0x466760	FE_LoadScreenFile	reads INTRFACE\<name>.TXT VERSION 3 into the screen struct
0x4666f0	FE_AutoSizeItems	w,h = text size for text id<=0x400 or ==0x404 when wrap==0
0x46c970	FE_CreateMenu	calls enter, loads backdrop, builds one button per item (count read after enter)
0x46ccb0	FE_DestroyMenu	calls exit, frees buttons
0x46c0d0	FE_UpdateScreen	per-frame input; calls screen+0x110 or the generic code
0x470c20	FE_OptionsScreenUpdate	input handler shared by Options/Graphics/Controls/NetworkOptions/Quit
0x46e020	FE_DrawItems	per-frame item text and glow
0x46c5c0	FE_CreateButton	quad actor for an item rect (hit area and glow)
0x46cf10	FE_CreateBackdropActors	world/camera and the three panel actors A/B/C
0x46abf0	FE_LoadBackdropPixelmap	PIXIES.P16; keeps the pixelmap named like the backdrop base
0x470a90	FE_SetupPanelMorph	(old,new) A/B/C model morph; g_transition=16
0x46f5b0	FE_MorphModel	lerps model vertices
0x467890	FE_ToggleFocusedItem	radio select in group / level-bar fill
0x4677d0	FE_HitTestItem	screen,x,y -> item or -1
0x471ba0	FE_HitTestItemOrSlider	the same plus slider pieces
0x471dd0	FE_NextFocusable	Down-key walker (M)
0x467a30	FE_PrevFocusable	previous focusable visible item
0x484f40	FE_GetSelectedInGroup	ecx=screen edx=group id -> item or -1
0x484f70	FE_SetRadioSelection	ecx=screen edx=group id, stack item
0x484fd0	FE_LoadInterfaceText	INTRFACE\interface.txt -> 300-slot table
0x485110	FE_GetInterfaceText	ecx=id -> char*
0x466450	FE_NullCallback	return 0
0x43e0a0	FE_DrawCursorAndThrottle	mouse cursor; 21 ms wait = 48 fps menu cap
0x482550	KeyPressed	ecx=key code (KEYNAMES.TXT index-2)
0x4824c0	GetMouseButton	1=click (M)
0x483c10	GetMousePos	(M)
0x463850	PolyFont_DrawText	ecx=font edx=text x0 y0 x1 y1 mode(0 left,2 centre) 1
0x463ad0	PolyFont_DrawTextAlpha	as DrawText plus double alpha
0x465380	PolyFont_DrawLine	one line of string actors (M)
0x465ca0	PolyFont_TextWidth	ecx=font edx=text
0x463730	PolyFont_Height	ecx=font
0x4642d0	PolyFont_LoadFont	lazy glyph load for fonts 0-11 (M)
0x463d40	InitPolyFonts	reads FontCol.TXT
0x4747e0	Graphics_Enter	load once, auto-size, Graphics_LoadRadio
0x474850	Graphics_Exit	-> Graphics_ApplyRadio
0x4b4090	Graphics_LoadRadio	option getters -> radio items
0x4b42c0	Graphics_ApplyRadio	radio -> setters, then SaveOptionsFile
0x48d190	SaveOptionsFile	rewrites data\OPTIONS.TXT
0x474530	Options_Enter	registers volume slider; greys Abort Race/Load Game
0x474690	Options_Exit
0x527c80	FatalError	printf-style, aborts
0x455690	PlaySound	ecx=outlet edx=sound id (M)
0x490630	ReadToken	text-file token reader
0x48fb00	ReadInt
0x48fdc0	ReadIntPair
0x448f20	GetCarComplexity	setter 0x448f10
0x446bb0	GetYon	setter 0x446b70 (float)
0x40dfe0	GetYonFactor	setter 0x40dff0 (M: scenery pop-up)
0x688abc	g_current_screen	tFE_screen*
0x688770	g_focus_item	99 = none
0x79ecb8	g_mouse_mode
0x6883a8	g_fe_exit
0x68683c	g_button_count
0x687248	g_buttons	100 x 0x2c
0x686f8c	g_backdrop_pixmap
0x686ef8	g_transition	frames left of the panel morph
0x6886d0	g_pulse	double 0..1, 750 ms triangle wave
0x68c230	g_interface_text	char*[300]; count at 0x68c6e0
0x686850	g_menu_images	menuimages.txt pixelmaps
0x6870b8	g_menu_models_A	13 x 0x10; B at 0x687188 (11), C at 0x687058 (6)
0x686820	g_sliders	runtime slider list
0x688ab0	g_scroll_lists	runtime list
0x686508	g_spinners	runtime left/value/right triplets
0x76d9b0	g_poly_fonts	stride 0x1c68
0x5a80f0	scr_Main
0x5b39b8	scr_Network
0x5bf280	scr_newgame
0x5cab48	scr_Credits
0x5d6410	scr_ChangeCar
0x5e1cd8	scr_Game
0x5ed5a0	scr_LoadGame
0x5f8e68	scr_Wrecks
0x604940	scr_Controls
0x610208	scr_NetworkOptions
0x61bad0	scr_NetSync
0x627398	scr_NetworkSummary
0x632c60	scr_Options
0x63e528	scr_Graphics
0x649df0	scr_Quit
0x59c828	scr_StartGame
```
