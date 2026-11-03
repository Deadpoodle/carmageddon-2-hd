#!/usr/bin/env bash
# Carmageddon II HD: install on a Steam Deck (or any Linux PC running the
# Steam release through Proton).
#
# In Desktop Mode, open Konsole and paste:
#   curl -fsSL https://raw.githubusercontent.com/Deadpoodle/carmageddon-2-hd/main/tools/install-steamdeck.sh | bash
# (add "-s -- --uninstall" after bash to remove the patch again), or from an
# extracted release zip:
#   bash install-steamdeck.sh              install, or update to this version
#   bash install-steamdeck.sh --uninstall  remove the patch again
#
# It finds the game in any Steam library (internal storage or SD card), copies
# in the patch (dinput.dll and dgVoodoo.conf from this script's folder, or the
# latest release when the script is piped in), downloads dgVoodoo2's DDraw.dll and D3DImm.dll, and sets the
# game's launch option so Proton loads them:
#   WINEDLLOVERRIDES="dinput,ddraw,d3dimm=n,b" %command%
# Steam keeps launch options in its own files and rewrites them when it exits,
# so the script sets the option only while Steam is closed; otherwise it shows
# the line to paste into the game's Properties.
set -euo pipefail

APPID=282030
OVERRIDES='WINEDLLOVERRIDES="dinput,ddraw,d3dimm=n,b"'
RELEASE_ZIP=${C2HD_RELEASE_ZIP:-https://github.com/Deadpoodle/carmageddon-2-hd/releases/latest/download/carma2hd.zip}
DGV_ZIP=${C2HD_DGV_ZIP:-https://github.com/dege-diosg/dgVoodoo2/releases/download/v2.87.5/dgVoodoo2_87_5.zip}
DGV_DDRAW_SHA=612a24408a090a3c6f3886557fa18034ee742e94ad0a40ebdf854d2816176c2e
DGV_D3DIMM_SHA=93c534f2d17419ea78f15551f7e0aac78b3c503733a840914fa063708a5afe8e
PATCH_FILES=(dinput.dll DDraw.dll D3DImm.dll dgVoodoo.conf)

say() { printf '%s\n' "$*"; }
fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
# Questions read the terminal, so they work even when the script is piped in.
ask() { local a=""; read -r -p "$1 " a </dev/tty || true; printf '%s' "$a"; }

uninstall=0
case "${1:-}" in
    --uninstall) uninstall=1 ;;
    "") ;;
    *) fail "unknown option $1 (use --uninstall to remove the patch)" ;;
esac

command -v python3 >/dev/null || fail "python3 is needed (SteamOS has it)"
# The script's own folder; none when it is piped in (curl ... | bash).
here=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# --- Steam and the game ------------------------------------------------------

steam_root=""
for d in "${STEAM_ROOT:-}" "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root"; do
    if [ -n "$d" ] && [ -f "$d/steamapps/libraryfolders.vdf" ]; then steam_root="$(cd "$d" && pwd -P)"; break; fi
done
[ -n "$steam_root" ] || fail "Steam's folder wasn't found (looked in ~/.local/share/Steam and ~/.steam)"

# Every library's path from libraryfolders.vdf, then the one holding the game.
game=""
while IFS= read -r lib; do
    manifest="$lib/steamapps/appmanifest_$APPID.acf"
    [ -f "$manifest" ] || continue
    dir="$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)".*/\1/p' "$manifest" | head -n1)"
    if [ -n "$dir" ] && [ -f "$lib/steamapps/common/$dir/CARMA2_HW.EXE" ]; then game="$lib/steamapps/common/$dir"; break; fi
done < <({ printf '%s\n' "$steam_root"; sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$steam_root/steamapps/libraryfolders.vdf"; } | sed 's/\\\\/\\/g')
[ -n "$game" ] || fail "Carmageddon 2 isn't installed in any Steam library (install it from Steam first)"
say "Game: $game"

steam_running() { pgrep -x steam >/dev/null 2>&1 || pgrep -x steamwebhelper >/dev/null 2>&1; }

# --- Launch option -------------------------------------------------------------

# Adds (or with "remove", takes out) the overrides in the game's launch options,
# in every Steam user's localconfig.vdf, keeping whatever else is there.
# Each file is backed up first, as localconfig.vdf.c2hd-backup.
set_launch_option() {
    python3 - "$steam_root" "$APPID" "$OVERRIDES" "$1" <<'PY'
import glob, os, re, shutil, sys

root, appid, overrides, mode = sys.argv[1:5]

def parse(text):
    tokens = re.findall(r'"((?:[^"\\]|\\.)*)"|([{}])', text)
    pos = 0
    def block():
        nonlocal pos
        items = []
        while pos < len(tokens):
            s, brace = tokens[pos]
            pos += 1
            if brace == '}':
                return items
            key = s
            s2, brace2 = tokens[pos]
            pos += 1
            items.append([key, block() if brace2 == '{' else s2])
        return items
    return block()

def dump(items, depth=0):
    out = []
    tab = '\t' * depth
    for key, value in items:
        if isinstance(value, list):
            out.append(f'{tab}"{key}"\n{tab}{{\n{dump(value, depth + 1)}{tab}}}\n')
        else:
            out.append(f'{tab}"{key}"\t\t"{value}"\n')
    return ''.join(out)

def child(items, key, create):
    for k, v in items:
        if k.lower() == key.lower() and isinstance(v, list):
            return v
    if not create:
        return None
    v = []
    items.append([key, v])
    return v

files = glob.glob(os.path.join(root, 'userdata', '*', 'config', 'localconfig.vdf'))
if not files:
    print('  no Steam user settings found')
    sys.exit(1)
for path in files:
    with open(path, encoding='utf-8', errors='surrogateescape') as f:
        data = parse(f.read())
    node = data
    for key in ('UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', appid):
        node = child(node, key, mode == 'add')
        if node is None:
            break
    if node is None:
        continue
    entry = next((e for e in node if e[0] == 'LaunchOptions' and not isinstance(e[1], list)), None)
    old = entry[1] if entry else ''
    # The file escapes quotes as \": work on the plain text, escape at the end.
    # Any WINEDLLOVERRIDES already there keeps its other DLLs: ours are taken
    # out of it and (when adding) put back in front, as one dinput,...=n,b group.
    plain = old.replace('\\"', '"')
    found = re.search(r'WINEDLLOVERRIDES=("([^"]*)"|(\S*))\s*', plain)
    theirs = (found.group(2) if found.group(2) is not None else found.group(3)) if found else ''
    rest = (plain[:found.start()] + plain[found.end():]).strip() if found else plain.strip()
    ours_group = overrides.split('=', 1)[1].strip('"')
    ours = set(ours_group.split('=')[0].split(','))
    groups = []
    for group in filter(None, theirs.split(';')):
        names, _, how = group.partition('=')
        names = [n for n in names.split(',') if n and n.lower() not in ours]
        if names:
            groups.append(','.join(names) + ('=' + how if how else ''))
    if mode == 'add':
        groups.insert(0, ours_group)
        rest = rest if '%command%' in rest else (rest + ' %command%').strip()
    var = 'WINEDLLOVERRIDES="' + ';'.join(groups) + '"' if groups else ''
    plain = (var + ' ' + rest).strip()
    if mode != 'add' and plain == '%command%':
        plain = ''
    new = plain.replace('"', '\\"')
    if new == old:
        continue
    shutil.copyfile(path, path + '.c2hd-backup')
    if entry:
        entry[1] = new
    else:
        node.append(['LaunchOptions', new])
    with open(path, 'w', encoding='utf-8', errors='surrogateescape') as f:
        f.write(dump(data))
    print(f'  {path}: launch options now: {plain or "(none)"}')
PY
}

launch_option() {
    local mode="$1"
    if steam_running; then
        a="$(ask "Steam is running. Exit Steam now (Steam menu > Exit) and press Enter to let this script set the launch option, or type s to skip:")"
        if [ "$a" = "s" ] || steam_running; then
            if [ "$mode" = add ]; then
                say "Set it yourself: in Steam, Carmageddon 2 > Properties > General > Launch Options:"
                say "  $OVERRIDES %command%"
            else
                say "Remove WINEDLLOVERRIDES... from Carmageddon 2 > Properties > General > Launch Options."
            fi
            return
        fi
    fi
    set_launch_option "$mode" || say "Couldn't set the launch option: set it in the game's Properties (see above)."
}

# --- Uninstall -----------------------------------------------------------------

if [ "$uninstall" = 1 ]; then
    for f in "${PATCH_FILES[@]}" Carma2HD.ini Carma2HD.log; do rm -f "$game/$f"; done
    say "Patch files removed from the game folder."
    launch_option remove
    say "Done. Carmageddon 2 is back to the store's version."
    exit 0
fi

# --- The patch's own files -------------------------------------------------------

src=""
if [ -n "$here" ] && [ -f "$here/dinput.dll" ] && [ -f "$here/dgVoodoo.conf" ]; then
    src="$here"
    say "Patch: from $here"
else
    say "Patch: downloading the latest release..."
    if curl -fsSL -o "$work/carma2hd.zip" "$RELEASE_ZIP"; then
        python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$work/carma2hd.zip" "$work/patch"
        src="$(dirname "$(find "$work/patch" -name dinput.dll | head -n1)")"
    fi
    [ -n "$src" ] && [ -f "$src/dinput.dll" ] && [ -f "$src/dgVoodoo.conf" ] ||
        fail "couldn't download the patch. Put dinput.dll and dgVoodoo.conf next to this script (from the release zip) and run it again."
fi

# --- dgVoodoo2 -------------------------------------------------------------------

sha() { sha256sum "$1" | cut -d' ' -f1; }
if [ -f "$game/DDraw.dll" ] && [ "$(sha "$game/DDraw.dll")" = "$DGV_DDRAW_SHA" ] &&
   [ -f "$game/D3DImm.dll" ] && [ "$(sha "$game/D3DImm.dll")" = "$DGV_D3DIMM_SHA" ]; then
    say "dgVoodoo2: already installed"
else
    say "dgVoodoo2: downloading 2.87.5 from its GitHub releases..."
    curl -fsSL -o "$work/dgv.zip" "$DGV_ZIP" || fail "couldn't download dgVoodoo2 ($DGV_ZIP)"
    python3 - "$work/dgv.zip" "$work/dgv" <<'PY'
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
os.makedirs(sys.argv[2], exist_ok=True)
for name in z.namelist():
    parts = name.replace('\\', '/').split('/')
    if len(parts) >= 3 and parts[-3].lower() == 'ms' and parts[-2].lower() == 'x86' and parts[-1].lower() in ('ddraw.dll', 'd3dimm.dll'):
        with open(os.path.join(sys.argv[2], parts[-1]), 'wb') as f:
            f.write(z.read(name))
PY
    [ -f "$work/dgv/DDraw.dll" ] && [ "$(sha "$work/dgv/DDraw.dll")" = "$DGV_DDRAW_SHA" ] &&
        [ -f "$work/dgv/D3DImm.dll" ] && [ "$(sha "$work/dgv/D3DImm.dll")" = "$DGV_D3DIMM_SHA" ] ||
        fail "the dgVoodoo2 download isn't the expected 2.87.5 (MS/x86 DDraw.dll and D3DImm.dll)"
    cp "$work/dgv/DDraw.dll" "$work/dgv/D3DImm.dll" "$game/"
fi

cp "$src/dinput.dll" "$src/dgVoodoo.conf" "$game/"
say "Installed: ${PATCH_FILES[*]}"

launch_option add

say ""
say "Done. Start Carmageddon 2 from Steam as usual. The first start writes Carma2HD.ini in the game"
say "folder with every setting explained; Carma2HD.log there shows what the patch did."
say "Controls: use Steam Input's Gamepad layout. Steam + X brings up the keyboard for typing names."
