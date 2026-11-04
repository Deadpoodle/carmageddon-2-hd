"""Preview of the zombie recolour: a before/after contact sheet of pedestrian
skins, from the game's own PEDS\\PEEPS\\<type>.TWT files. The formula here is the
one src/zombies.cpp uses; tune it here on stills, then port the numbers.

usage: python -I tools/zombify.py <game data dir> <out.png> [type ...]
Each row: skin, zombie skin, bloodied skin (<NAME>B), zombie bloodied skin.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402

import pix  # noqa: E402
import twt  # noqa: E402

DESATURATE = 0.70          # clothes: how much colour goes
TINT = (0.80, 0.92, 0.68)  # clothes: a grey-green cast
SKIN = (0.50, 0.62, 0.40)  # skin: the rotten colour, scaled by brightness
GRIME = 0.45               # how dark the blotches get
BLOOD_DIFF = 40            # a texel this different in <NAME>B may be blood...
BLOOD_RED = 1.6            # ...if red is this many times green and blue


def noise(x, y, cell, seed):
    """Smooth value noise in 0..1 (integer hash on a grid, bilinear between)."""
    def h(i, j):
        n = (i * 374761393 + j * 668265263 + seed * 2246822519) & 0xffffffff
        n = ((n ^ (n >> 13)) * 1274126177) & 0xffffffff
        return ((n ^ (n >> 16)) & 0xffff) / 65535
    gx, gy = x // cell, y // cell
    fx, fy = (x % cell) / cell, (y % cell) / cell
    a = h(gx, gy) * (1 - fx) + h(gx + 1, gy) * fx
    b = h(gx, gy + 1) * (1 - fx) + h(gx + 1, gy + 1) * fx
    return a * (1 - fy) + b * fy


def grime(x, y):
    """1 = clean, down to 1 - GRIME in the dirtiest blotches."""
    n = 0.6 * noise(x, y, 16, 1) + 0.3 * noise(x, y, 6, 2) + 0.1 * noise(x, y, 2, 3)
    n = max(0.0, min(1.0, (n - 0.35) / 0.35))
    return 1 - GRIME * (1 - n)


def is_skin(r, g, b):
    """Warm, moderately saturated: flesh tones, light or dark."""
    return r > g > b and r - b > 0.08 and (r - g) < 0.9 * (r - b) and r > 0.15 and g > 0.5 * r


def zombie(r, g, b, x, y):
    lum = 0.30 * r + 0.59 * g + 0.11 * b
    if is_skin(r, g, b):
        c = [SKIN[i] * (0.35 + 1.1 * lum) for i in range(3)]
    else:
        c = [(v * (1 - DESATURATE) + lum * DESATURATE) * TINT[i] for i, v in enumerate((r, g, b))]
    k = grime(x, y)
    return [min(1.0, v * k) for v in c]


def green_blood(r, g, b):
    return [g * 0.7 + 0.05, r * 0.72, b * 0.4]


def convert(m, base=None):
    """m's pixels as zombie RGB. base: the skin that m is the bloodied copy of."""
    w, h, rgb = pix.to_rgb(m)
    _, _, ref = pix.to_rgb(base) if base else (0, 0, None)
    out = bytearray(len(rgb))
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            r, g, b = rgb[i] / 255, rgb[i + 1] / 255, rgb[i + 2] / 255
            # Not every bloodied copy lines up with its skin, so it must be red too.
            blood = (ref and sum(abs(rgb[i + c] - ref[i + c]) for c in range(3)) > BLOOD_DIFF
                     and r > 0.2 and r > BLOOD_RED * max(g, b))
            c = green_blood(r, g, b) if blood else zombie(r, g, b, x, y)
            out[i:i + 3] = bytes(int(v * 255) for v in c)
    return w, h, bytes(out)


def image(w, h, rgb, size):
    return Image.frombytes('RGB', (w, h), rgb).resize((size, size), Image.NEAREST)


def main():
    data, out = sys.argv[1:3]
    types = sys.argv[3:] or ['Cop1', 'Clown1', 'FEM07', 'Chef2', 'DOCTOR3', 'Elephant']
    size = 192
    rows = []
    peeps = os.path.join(data, 'PEDS', 'PEEPS')
    files = {f.lower(): f for f in os.listdir(peeps)}
    for t in types:
        name = files.get(t.lower() + '.twt')
        if not name:
            print('no', t)
            continue
        parts = dict(twt.read_twt(os.path.join(peeps, name)))
        p16 = next(v for k, v in parts.items() if k.upper() == 'PIXIES.P16')
        with tempfile.NamedTemporaryFile(delete=False, suffix='.p16') as f:
            f.write(p16)
        maps = pix.read_pixelmaps(f.name)
        os.unlink(f.name)
        if not maps:
            print(t, 'has no pixelmaps')
            continue
        by = {m['name'].upper(): m for m in maps}
        skin = next((m for m in maps if m['name'].upper() + 'B' in by), maps[0])
        hit = by.get(skin['name'].upper() + 'B')
        row = [image(*pix.to_rgb(skin), size), image(*convert(skin), size)]
        if hit:
            row += [image(*pix.to_rgb(hit), size), image(*convert(hit, skin), size)]
        rows.append(row)
        print(t, [m['name'] for m in maps][:6], skin['w'], skin['h'])
    sheet = Image.new('RGB', (4 * (size + 8), len(rows) * (size + 8)), (20, 20, 20))
    for j, row in enumerate(rows):
        for i, im in enumerate(row):
            sheet.paste(im, (i * (size + 8) + 4, j * (size + 8) + 4))
    sheet.save(out)


if __name__ == '__main__':
    main()
