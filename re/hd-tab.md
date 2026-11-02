# "HD" tab on the Graphics screen: implementation recipe (CARMA2_HW.EXE, Steam build)

Static analysis only: the Ghidra caches in `re/ghidra-export/`, checked against the instructions with capstone/pefile on `testrun/CARMA2_HW.EXE`. Builds on `re/menus.md` §1–§5. Tags: **H** = read from the instructions or static data; **M** = decompiler output or a strong inference; **L** = a guess to check in play. Addresses are VAs (base 0x400000). Graphics screen = `S` = 0x63e528. Item *i* is at `S + 0x130 + i*0x158`. Radio entry for group *g* is at `S + 0x9be8 + (g-1)*0x2c`.

## 0. Summary

- No hooks are needed, only data. Swap `S+0x108` (0x63e630) and `S+0x10c` (0x63e634). On first entry, append 28 engine items (53..80) and 6 radio entries (groups 15..20), then raise the count `S+0x104` (0x63e62c) to 81. Draw, hit-test, focus walk, radio select and Left/Right all work on these items unchanged (H).
- The "Menu item number mismatch" check only runs inside the loader, and the loader only runs while `S+0xad1c==0`. The loader sets that flag itself, and nothing else ever writes it (H). Raising the count after the original enter has returned is therefore safe for the whole session, including later in-race visits.
- Labels can carry our own text inline: text id **0x404** plus the string in the item's `name[255]` at +0x59. The item is then auto-sized and drawn exactly like a stock text-id item (H).
- Tabs: two extra items. 53 "HD" sits right of GRAPHICS; 54 is a "GRAPHICS" clone (text id 0xF9) that replaces item 0 while HD is showing. **Item 0 must never become focusable.** The options handler auto-activates whatever keyboard focus lands on index 0 (H, §3.5).
- Draw distance: before calling the original exit, clear the selection on stock group 2 (items 8..11). Graphics_ApplyRadio then skips SetYon altogether, and SaveOptionsFile writes our Yon (H). Grey the stock Scenery Cut-off row while HD owns Yon.

## 1. Screen struct: what the engine reads, and what must change

### 1.1 Fields used for items, groups and radios (H unless marked)

| Offset (abs. addr) | Field | Read by | Our action |
|---|---|---|---|
| +0x104 (0x63e62c) | item count (53) | CreateMenu 0x46c9bc (read **after** enter, 0x46c9b0), DrawItems guard, both hit-tests, both focus walkers, Toggle 0x467890, GetSelectedInGroup 0x484f40, AutoSize 0x4666f0, loader check 0x466760 | set to 81 after appending |
| +0x108 / +0x10c (0x63e630 / 0x63e634) | enter / exit, `int __fastcall f(screen)` (call at 0x46c9b0 with ecx=screen; exit called from DestroyMenu 0x46ccb0) | CreateMenu / DestroyMenu | point at HD_Enter / HD_Exit |
| +0x110 (0x63e638) | update = 0x470c20 (shared with Options, Controls, NetworkOptions, Quit) | FE_UpdateScreen 0x46c105 | leave alone |
| +0x114 (0x63e63c) | parent (static Options 0x632c60) | Escape/back | leave alone |
| +0x118 (0x63e640) | saved focus (static 1 = OK) | saved at 0x4719dc / 0x471b09 **before** DestroyMenu; restored into g_focus 0x688770 at 0x471a6d / 0x471b33 **after** CreateMenu (i.e. after our enter) | clamp in HD_Enter (§7) |
| +0x124 / +0x128 / +0x12c (0x63e64c..0x63e654) | panel models A=8, B=0, C=0 | FE_SetupPanelMorph 0x470a90 at transition | optional C-5 tab (§4) |
| +0x130 + i·0x158 | items[0..98]; slot 99 (0x646b60) is the "none" sentinel copied from 0x604730 at load | everything | fill 53..80 |
| +0x87a4 / +0x87a8 | first/last item of scroll-list 0 (both 0 on Graphics) | 0x47154a / 0x471805: when keyboard focus lands in [first,last], it is activated automatically | never make item 0 focusable (§3.5) |
| +0x9be4 (0x64810c) | radio count (14) | **only written by the loader; never read** (grep and instruction scan) | raise to 20 for tidiness |
| +0x9be8 + (g−1)·0x2c | radio[g−1]: +0 id (label item), +4 total, **+8 first, +0xc last**, +0x10 selected, +0x14..+0x24 greybox | only +8/+0xc are read: SetRadioSelection 0x484f70 (`+0x9bc4+g*0x2c`, `+0x9bc8+g*0x2c`) and Left/Right (0x471665..0x4717b6). Greybox fields are never read | fill groups 15..20 (entries 14..19 at S+0x9e50 .. S+0x9f2c, step 0x2c) |
| +0xad18 (0x649240) | "number of groups" (12) | only tested ≠0 (Up walker 0x470c20, Down walker 0x471dd0): it switches on row-wise up/down | leave at 12 |
| +0xad1c (0x649244) | loaded flag | the only store is 0x4667e9 inside the loader (instruction scan of .text) | never touch |

### 1.2 Item record (stride 0x158), with the values to write for our items (H for the offsets)

| Off | Field | Labels | Option words | Tab items |
|---|---|---|---|---|
| +0x00 | text id: ≤0x400 = Interface.txt id; >0x400 draws `name` (0x46e185); **0x404** also auto-sizes (0x466711) | 0x404 | 0x404, or a stock id (0x38 "On", 0x39 "Off", 0x44 "Full") | 53: 0x404 "HD"; 54: 0xF9 |
| +0x04 | callback `int __fastcall cb(screen)`; 0x466450 = stub (`xor eax,eax; ret`) | 0x466450 | 0x466450 | HD_TabClick |
| +0x08 | target screen (1 = parent) | 0 | 0 | 0 |
| +0x0c | result code (1 sets g_fe_exit) | 0 | 0 | 0 |
| +0x10 / +0x14 | normal / highlight poly-font | 3 / 3 | 0 / 1 | inactive 6 / 7, active 5 / 5 |
| +0x18 / +0x1c | x, y (640×480 space) | §5 | §5 | §4 |
| +0x20 / +0x24 | w, h: AutoSize fills them from the **normal** font | 0 (auto) | 0 (auto) | 0 (auto) |
| +0x28 | state: 1 focusable, 0 label, −1 greyed | 0 (−1 to grey) | 1 (−1 to grey) | inactive 1, active 0 |
| +0x2c | visible | per page | per page | per page |
| +0x30 | pixelmap index | 0 | 0 | 0 |
| +0x34 | greyed backdrop (unused by draw) | 0 | 0 | 0 |
| +0x38 | selectable (radio member) | 0 | **1** | 0 |
| +0x3c | selected | 0 | via 0x484f70 | 0 |
| +0x40 | group id g → radio[g−1] | 0 | 15..20 | 0 |
| +0x44 | level-bar id | 0 | 0 | 0 |
| +0x48 | wrap (≠0 disables auto-size) | 0 | 0 | 0 |
| +0x4c | glow group (items sharing it pulse together) | 0 | 0 | 0 |
| +0x50 / +0x54 | original x, y (loader copy) | = x, y | = x, y | = x, y |
| +0x58 | flags (bit 0 = focused this frame, set and cleared by DrawItems) | 0 | 0 | 0 |
| +0x59 | `char name[255]` (up to +0x157), NUL-terminated | label text | word text | "HD" |

Items 53..98 are zero in the static image (H, dumped). Write every field anyway.

### 1.3 Limits sized to the item count (H)

- **Buttons:** g_buttons 0x687248 holds 100 slots × 0x2c. CreateMenu builds one per item (0x46cc19), then the "none" button at the fixed slot 99 (0x68834c, 0x46cc5a). So **items ≤ 99**. Our layout uses 81.
- **Radio array:** +0x9be8..+0xad18 = 100 entries.
- **Level bars:** separate array; not used.
- **Loader check** (0x466760, compare at the `ReadInt` after the title): runs only from an enter whose `+0xad1c==0` (Graphics_Enter 0x4747e6). Our append happens after the original enter returns, i.e. after the first and only load. **Raising the count can never trip the check** (H). Guard the append with `count==53`, not with +0xad1c.
- **Button rects are snapshots.** CreateMenu builds them from x, y, w, h (shorts, 0x46cbe7..0x46cc0f) right after enter. Items moved or resized after CreateMenu keep their old hit rects. So set positions and sizes in HD_Enter, and only flip +0x2c/+0x28/fonts afterwards (H).
- **Auto-size runs only on first load:** Graphics_Enter calls 0x4666f0 only inside the `+0xad1c==0` branch (0x4747f5). Call **0x4666f0(screen)** yourself once after appending (`__fastcall`, ecx=screen). It re-measures every item; the stock 53 come out identical (H).

## 2. Text, fonts, colours

- **Label resolution** (DrawItems 0x46e182..0x46e19a, H): id ≤0x400 → `FE_GetInterfaceText` 0x485110 (`ecx`=id; returns 0 if id > count 0x68c6e0, else `g_interface_text[id]` at 0x68c230). id >0x400 → `&item.name`.
- **Our own strings:** use 0x404 plus the string copied into +0x59. A pointer cannot be stored, because draw takes the inline buffer's address (H).
  - Spare ids 258..299 would need the count at 0x68c6e0 raised and table pointers written. FRONTEND_Setup reloads the text table, so this is not recommended (M).
  - Draw upper-cases a–z (PolyFont). The loader's `_`→space conversion does not apply to strings we write.
- **AutoSize** 0x4666f0 (H): for id ≤0x400 or ==0x404, with wrap==0 and text≠NULL:
  - w = `PolyFont_TextWidth(item.font_normal, text)` (0x465ca0, ecx=font, edx=text)
  - h = `PolyFont_Height(font_normal)` (0x463730)
  - Width is the sum of per-glyph widths. A glyph's width is its rightmost non-zero column + 1 in the font's PIXIES.P16 (loader 0x4643f0, H). Missing glyphs use the font.txt blank width (HAND15 10, SERP30 20). Spacing is 0.
- **Font index → file** (0x4642d0 calls, H): 0 HAND15U, 1 HAND15, 2 HAND15U, 3 HAND15, 4 SERP15, 5 SERP30, 6 SERP30U, 7 SERP30, 8 SERP38, 10 HAND10U, 11 HAND10.
- **Colours** (`data/FontCol.TXT`, top → bottom):
  - 0: green unlit (0,255,0 → 0,140,60)
  - 1: green lit (128,255,128 → 0,255,0)
  - 3: red lit (255,80,80 → 193,0,13)
  - 5: serp red 30 lit (same reds)
  - 6: serp green 30 unlit
  - 7: serp green 30 lit
- **Which font and alpha each state draws** (FE_DrawItems 0x46e020, H from the instructions). Each visible item first gets button actor render style 4; hidden items get style 1 and are skipped (0x46e170/0x46e2e6). Then, in order:
  1. **Focused/hovered** (flag bit 0, from g_focus): normal font at α 0.6 (0x46e1cd), then the highlight font at α 0.6·p, or 0.3+0.7·p if selected (0x46e216). Both left-aligned (mode 0). p = g_pulse 0x6886d0, a 750 ms triangle. The glow quad pulses (0x46e33c), and so does every item sharing its +0x4c glow group.
  2. **Greyed** (+0x28<0): normal font, α 0.4, left (0x46e279). **The selected word loses its highlight** because this branch comes first. The glow is set to 0x400000 only for pixelmap items.
  3. **Selected** (+0x3c): highlight font, **centred in [x, x+w]** (mode 2), α 1 (0x463850). w comes from the normal font, so 0 vs 1 (HAND15U vs HAND15) can differ by a pixel.
  4. **Otherwise:** normal font, left, α 1. The glow colour is 0x800000.
- **Measured widths** (engine algorithm on `testrun/data/64x48x8/FONTS/*.TWT`; M, ±1 px L):

| Text | px | Text | px |
|---|---|---|---|
| GRAPHICS (SERP30 / 30U) | 173 | HD (SERP30) | 48 |
| Resolution: / Widescreen: | 92 / 95 | HUD width: / Controls: | 88 / 78 |
| Pedestrians: / Draw distance: | 102 / 126 | (stock) Scenery Cut-off: | 137 |
| Auto / 1280x720 / 1920x1080 / 2560x1440 / 3440x1440 | 37 / 70 / 76 / 82 / 82 | On / Off / Full / 16:9 / 4:3 | 19 / 26 / 31 / 29 / 24 |
| Classic / Modern | 57 / 58 | Humans / Zombies / Aliens | 56 / 62 / 48 |
| Original / Far / Further / Max | 65 / 27 / 62 / 29 | heights: HAND15 16, SERP30 30 | |

## 3. Behaviour

1. **Activation (click, Return 0x33, KP-Enter 0x34; 0x47189c..0x4719af, H):**
   - Activation fires on Return or KP-Enter, or on a click (`GetMouseButton`==1) whose generic hit-test 0x4677d0 ≠ −1.
   - Then: `FE_ToggleFocusedItem(screen)` 0x467890 (ecx=screen, on g_focus) → if +0x0c==1, set g_fe_exit → target = +0x08 (1 → parent) → call the callback with **ecx=screen** (0x47197c).
   - If there is no target and the callback is not the stub, sound 0xbbc plays first (0x471957).
   - **A return of 4 → `return 0` (stay), at 0x47199f.** A stub return of 0 with no target also stays.
2. **Radio select (0x467890, H):**
   - If the focused item has group g, every item 0..count−1 with +0x40==g has +0x3c cleared, as long as the focused item is selectable (+0x38).
   - The focused item's +0x3c is then toggled (so it becomes 1) and sound 0xbbc plays (0x467946).
   - It does **not** check +0x28 or +0x2c. So never leave keyboard focus on a hidden or greyed word.
3. **Left 0x46/0x55, Right 0x47/0x57** (0x4715fb..0x4717b6, H):
   - These only move focus inside radio[g−1]: Right is last→first else +1; Left is first→last else −1. They do not select and do not check visibility or state.
   - So a group's items must be **contiguous indices**, and greying must be done **per row** (label and all words), never per word.
4. **Up 0x48/0x59, Down 0x49/0x53:**
   - Both walk index order with wrap-around and take only items with +0x28>0 and +0x2c≠0: Up at 0x471370 (`item+0x28` via `(i+1)*0x158`, visible `+0x15c`); Down in 0x471dd0.
   - With +0xad18≠0 they skip the rest of the current group. Up lands on a group's first item. Down lands on the first focusable item found (the row's first word).
   - Hidden and greyed items are skipped by **draw, both hit-tests (0x4677d0 needs +0x28≥1 and +0x2c; 0x471ba0 the same) and both walkers** (H).
5. **Auto-activation trap (H):**
   - After an Up or Down move in keyboard mode, if the new focus is within [+0x87a4, +0x87a8] it is Toggled and its callback is called (0x471534..0x471581 and 0x4717f0..0x47183b).
   - Graphics has no scroll lists, so both values are 0. Focus landing on **item 0** would therefore fire item 0's callback.
   - The Down walker also falls back to 0 when nothing is focusable (0x471dd0 tail).
   - So item 0 stays a non-focusable label; use the clone item 54 as the clickable "GRAPHICS" tab.
6. **Reading and setting values (H):**
   - `int __fastcall FE_GetSelectedInGroup(screen, g)` 0x484f40 returns the first item with +0x40==g and +0x3c≠0, or −1.
   - `void __fastcall FE_SetRadioSelection(screen, g, item)` 0x484f70 takes item on the stack and pops it itself (`ret 4`). It sets +0x3c = (i==item) for i in radio[g−1].first..last.
   - Reading or writing +0x3c directly is equivalent.
7. **Greying a row:** set +0x28 = −1 on its label and every word. It draws at α 0.4 with no highlight, cannot be hit or focused, and keeps its +0x3c. Restore with 0 (label) and 1 (words).
8. **Hiding:** set +0x2c = 0, which hides the glow quad (render style 1) and the text and removes the item from hit-test and focus. No rebuild is needed (H).

## 4. The tab header

- **Stock title:** item 0 is text 0xF9 "Graphics" at (60,60), fonts 5/5, state 0, w = 173 (measured), h = 30.
- **Panel:** A-8.dat (S+0x124 = 8). Its tab spans x≈61..244, y 51..84 (`re/menus.md` §2), so "HD" lands just right of the tab, over the backdrop (L: check the look).
- **Items:**
  - **53 "HD"**:
    - x = `item0.x + item0.w + 22`, with item0.w read at run time so other languages work, ≈255; y = 60.
    - Graphics page: fonts 6/7, state 1, callback HD_TabClick (looks and hovers like OK).
    - HD page: fonts 5/5, state 0 (red active title, not focusable).
  - **54 "GRAPHICS" clone**: text 0xF9, x,y = item 0's, fonts 6/7, state 1, callback HD_TabClick. Visible only on the HD page.
  - **Item 0:** visible on the Graphics page, hidden (+0x2c=0) on the HD page. Its callback stays the stub, and it is never focusable.
  - Both tabs keep fonts 6/7 for auto-size. Item 53 is measured with font 6 (SERP30U) and draws left-aligned with font 5 when active; same glyph widths (M).
- **Stock precedent:** `main.txt` pairs a red 5/5 title "SINGLE PLAYER" (214,68) with a small focusable 0/1 link "NETWORK" (214,95). An alternative inactive-tab style is fonts 0/1 below the title (L: taste).
- **Second panel model: not needed.**
  - If wanted later: models C-3 and C-5 are unused by every screen. Reshape C-5's vertices into a tab box at x≈250..310, y 51..84 and set S+0x12c (0x63e654) = 5 *before* Options→Graphics, so it fades and morphs in with A-8 (procedure in `re/menus.md` §5.5).
  - It does not morph on tab clicks, because there is no transition (M).

## 5. Where the rows fit

- **Stock rows** (graphics.txt, H): labels at x=60 and words at x=250/330/410/490 (fonts 0/1).
  - Car complexity y=100, Scenery Cut-off 120, Pop-up 140, Shadows 160, Sound 180, Traffic 200, Sky 220, Darkness/Fog 240.
  - Gore: label 260, words stacked at 260/275/290.
  - Labels at x=110: Animals 310, Flame thrower 330, Explosives 350, Auto load 370, Ambience 390. On/Off at 250/310.
  - OK at (540,393), fonts 6/7.
  - Occupied area: x 60..~595, y 100..~406, inside the panel x 51..596, y 51..433.
- **HD page:** reuse the first six stock row slots (labels x=60, fonts 3/3, h 16, 20 px pitch). The words' right edges are all <596.

| Row (group) | y | Words: x positions | Items (label; words) |
|---|---|---|---|
| Resolution (15) | 100 | Auto 170, 1280x720 221, 1920x1080 305, 2560x1440 395, 3440x1440 491 (14 px gaps, ends 573). Five words do not fit the 80 px grid. Alternative: two lines in the Gore style | 55; 56–60 |
| Widescreen (16) | 120 | On 250, Off 310 (stock On/Off spacing; ids 0x38/0x39) | 61; 62–63 |
| HUD width (17) | 140 | Auto 250, Full 330, 16:9 410, 4:3 490 | 64; 65–68 |
| Controls (18) | 160 | Classic 250, Modern 330 | 69; 70–71 |
| Pedestrians (19) | 180 | Humans 250, Zombies 330, Aliens 410: greyed (+0x28 = −1 on 72–75) or hidden | 72; 73–75 |
| Draw distance (20) | 200 | Original 250, Far 330, Further 410, Max 490 | 76; 77–80 |

Radio entries: g15 at S+0x9e50 (first 56, last 60), g16 at S+0x9e7c (62, 63), g17 at S+0x9ea8 (65, 68), g18 at S+0x9ed4 (70, 71), g19 at S+0x9f00 (73, 75), g20 at S+0x9f2c (77, 80). Set id = the label index and total = the word count.

Keyboard order on the HD page is OK → 54 GRAPHICS tab → rows → OK. On the Graphics page it is OK → 14 stock rows → 53 HD tab → OK.

## 6. Exit and Yon

- **Exit chain** (H): Graphics_Exit 0x474850 = `call 0x4b42c0` (ecx still = screen), then returns 1. ApplyRadio 0x4b42c0:
  - calls `FE_GetSelectedInGroup` for groups 1..14 only, then the setters, then **SaveOptionsFile 0x48d190** (no args, writes OPTIONS.TXT including `Yon %f` from GetYon);
  - for **group 2**: −1 (nothing selected) fails the range check at 0x4b4307 (`ja`), so **SetYon is skipped**. Items 8..11 push 35/30/25/20 and call SetYon 0x446b70 at 0x4b432d.
- **The original exit is safe with our rows:**
  - GetSelectedInGroup scans all 81 items, but only by group id, and ours are 15..20.
  - DestroyMenu frees buttons by g_button_count (H/M).
- **SetYon and GetYon:**
  - SetYon 0x446b70 is `void __stdcall(float)` (`ret 4`). It clamps to ≥5, writes both gCamera_list cameras' yon and gYon 0x761f4c, and applies live.
  - GetYon 0x446bb0 returns a float in st0.
- **Where to apply Yon:** in HD_Exit, *before* the original:
  - if the HD Draw distance is not Original: SetYon(value), then clear +0x3c on items 8..11;
  - the original then leaves Yon alone, and its single SaveOptionsFile persists our value.
  - Fallback if the selection is not cleared: call the original, then SetYon(value) and 0x48d190 again (two writes).
- **Yon at entry:** Graphics_LoadRadio 0x4b4090 (from the original enter) maps any Yon ≥40 to "nearest" (0x4b40bf..). In HD_Enter, after the original:
  - if HD owns Yon, or Yon ∉ {35,30,25,20}: clear items 8..11 and grey row 7..11;
  - otherwise restore states 0/1.
  - When the user picks Original again on the HD page and switches back with no stock word selected, select item 8 (35, the stock default) or leave it unselected (Yon unchanged). This is a design choice (L).
- **Values:** Far/Further/Max are the patch's choice (PCGamingWiki's tweak is 70; L).
  - The effective far plane is gYon × the per-track multiplier 0x655ddc.
  - The cell cull is yon × YonFactor (stock Scenery Pop-up row, `re/camera-hud.md` §4), so a big Yon with Pop-up "nearest" (0.125) gains little.
  - Changing yon rebuilds the sky (0x445cb0).

## 7. Recipe

**Install:** at DLL attach (before the front end runs), write `0x63e630 = HD_Enter` and `0x63e634 = HD_Exit`. Keep the originals 0x4747e0 and 0x474850. `.data` is writable (M).

**Signatures** (from the call-site bytes, H):

```
typedef int  (__fastcall *ScreenFn)(void* s);          // enter/exit/item callbacks: ecx=s, eax=result
void  __fastcall FE_AutoSizeItems(void* s);            // 0x4666f0
int   __fastcall FE_GetSelectedInGroup(void* s, int g);// 0x484f40
void  __fastcall FE_SetRadioSelection(void* s, int g, int item); // 0x484f70, callee pops 4
void  __stdcall  SetYon(float yon);                     // 0x446b70
float __cdecl    GetYon(void);                          // 0x446bb0 (st0)
void  __cdecl    SaveOptionsFile(void);                 // 0x48d190
const char* __fastcall FE_GetInterfaceText(int id);     // 0x485110
int   __fastcall PolyFont_TextWidth(int font, const char*); // 0x465ca0
int   g_focus @0x688770;  int g_mouse_mode @0x79ecb8;   int g_race_in_progress @0x75bba8 (M)
```

**HD_Enter(s)** (`__fastcall`, return 1):
1. `r = Graphics_Enter(s)` (0x4747e0). This loads on first use, auto-sizes, and runs Graphics_LoadRadio for groups 1..14.
2. If `count(+0x104) == 53`, append (first time only):
   - zero items 53..80 and fill them per §1.2, §4 and §5 (strings into +0x59; 53's x from item 0's x+w);
   - fill radio entries 14..19;
   - set +0x104 = 81 and +0x9be4 = 20;
   - call `FE_AutoSizeItems(s)`.
3. Every entry, show the Graphics page:
   - items 0..52: +0x2c = 1;
   - 53: fonts 6/7, state 1, visible;
   - 54..80: +0x2c = 0.
4. Set our radios from our settings: `FE_SetRadioSelection(s, 15+k, firstWord_k + value_k)` for each row. Grey Pedestrians (state −1 on 72..75) or keep it hidden.
5. Yon lock (§6): if HD owns Yon, or Yon is not a stock value, clear 8..11 +0x3c and grey 7..11 (state −1); else restore states 0/1.
6. Clamp the saved focus: if `+0x118` names an item that is hidden or has +0x28≤0, set `+0x118 = 1` (OK). The transition copies +0x118 into g_focus *after* this returns (0x471a6d / 0x471b33).
7. Return r.

**HD_TabClick(s)** (`__fastcall`, the callback of items 53 and 54):
- **To HD page:**
  - items 0 and 2..52: +0x2c = 0 (item 1 OK stays);
  - 53: fonts 5/5, state 0;
  - 54: visible;
  - 55..80: visible;
  - `g_focus = 54`.
- **To Graphics page:**
  - mirror the above (53 fonts 6/7, state 1; 54..80 hidden; 0..52 visible);
  - re-run the Yon lock from step 5 using the current group-20 selection;
  - `g_focus = 53`.
- **Return 4.** The stay is guaranteed; no transition, no backdrop reload, no exit call. Sound 0xbbc plays from the handler (0x471957).
- In mouse mode the next frame replaces g_focus with the hit item (or 99), so setting it is harmless (H).

**HD_Exit(s)** (`__fastcall`, return 1):
1. For g = 15..20: `v = FE_GetSelectedInGroup(s, g)`; value = `v − first` (−1 → keep the old value).
2. If DrawDistance ≠ Original: `SetYon(yonFor(dd))`, then clear +0x3c on items 8..11.
3. `Graphics_Exit(s)` (0x474850): applies 13 stock rows, then saves OPTIONS.TXT with our Yon.
4. Apply the live options (Controls), write `Carma2HD.ini`. Leave OPTIONS.TXT alone; its writer rewrites the whole file.
5. Optional: if +0x118 > 52, set it to 1. Step 6 of HD_Enter covers this anyway.

## 8. Risks

- **Item 0 and auto-activation:** focus on index 0 auto-activates (§3.5). Keep item 0 non-focusable at all times (H).
- **Hidden or greyed focus:** Toggle and Left/Right ignore visibility and state, so a keyboard focus parked on a hidden or greyed word could select it with Return. Clamp +0x118 on entry, set g_focus on every tab switch, and grey whole rows only (H).
- **Transitions:**
  - Options→Graphics and back run Destroy/Create with a 16-frame morph and no text drawn (stock).
  - Tab switches are instant, because no transition runs.
  - Graphics_Enter clears g_sliders, g_scroll_lists and g_spinners (0x474825..0x474831), so Options' volume slider does not leak in (H).
- **In-race path:** Options is the start screen (mode 1, parent 0); Graphics is reached only via Options item 3 (target pointer at 0x6331a0, the only reference to S in the exe; H). Same enter/exit.
  - SetYon applies to the live cameras (H). "Next start" rows can stay editable.
  - To grey them in race, use 0x75bba8 ≠ 0 = race in progress (Options_Enter uses it to enable Abort Race; M).
- **99-item cap:** 81 used, 18 spare (H).
- **Count guard:** the static struct persists for the session. Check `count==53` before appending, so re-entry and new front-end sessions never append twice (H).
- **Rect snapshot:** never move or resize items between CreateMenu and DestroyMenu (§1.3).
- **Shared handler:** 0x470c20 also serves Options, Controls, NetworkOptions and Quit. Only Graphics' own struct pointers change (H).
- **Layout:** the Resolution row's 14 px gaps and "HD" over the backdrop beside the A-8 tab need a look in play (L). The centred selected word in HAND15 vs the HAND15U-measured w may shift by 1 px (M).

## names.tsv

```
0x63e630	scr_Graphics.enter	-> HD_Enter (orig 0x4747e0 Graphics_Enter)
0x63e634	scr_Graphics.exit	-> HD_Exit (orig 0x474850 Graphics_Exit)
0x63e62c	scr_Graphics.count	53 stock; 81 with HD items
0x63e640	scr_Graphics.saved_focus	+0x118; copied to g_focus after CreateMenu (0x471a6d/0x471b33)
0x642d90	scr_Graphics.item53	first free item (S+0x130+53*0x158)
0x648110	scr_Graphics.radio	radio[0] (S+0x9be8, stride 0x2c); group g -> radio[g-1]
0x64810c	scr_Graphics.radio_count	written by loader only, never read
0x649244	scr_Graphics.loaded	+0xad1c; only store 0x4667e9 (in loader)
0x6331a0	scr_Options.item3.target	-> scr_Graphics (only reference to 0x63e528)
0x68834c	g_button_none	g_buttons slot 99, built after the item buttons
0x6845fc	g_ui_sound_outlet	ecx for PlaySound 0x455690 (0xbbc click) (M)
0x75bba8	g_race_in_progress	Options_Enter enables Abort Race when !=0 (M)
0x446b70	SetYon	__stdcall(float); clamp>=5, cameras + gYon 0x761f4c
0x446bb0	GetYon	float in st0
0x761f4c	gYon
0x4643f0	PolyFont_LoadFontFile	(name, size, index); glyph width = rightmost opaque column+1
0x4642d0	PolyFont_LoadAll	font index -> HAND15U/HAND15/SERP30/SERP30U...
0x471cf0	FE_FindSliderAtFocus	g_sliders entry covering g_focus
0x471d30	FE_FindScrollListOfItem
0x471d70	FE_ScrollListIndexOf
0x4720e0	FE_ScrollListRefresh	(M)
0x466b60	FE_GenericEnter	load-once + autosize, like Graphics_Enter without radios
0x4b4090	Graphics_LoadRadio	ecx=screen; Yon -> button via ftol((35-yon)*0.2)
0x4b42c0	Graphics_ApplyRadio	ecx=screen; group 2 == -1 skips SetYon; ends with SaveOptionsFile
```
