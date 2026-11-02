"""Decode BRender pixelmap files (.PIX, .P08, .P16) from Carmageddon II to PNG.

Chunked format, big-endian: u32 id, u32 length, payload.
  0x12 file header; 0x03 pixelmap header (u8 type, u16 row_bytes, u16 width,
  u16 height, u16 origin_x, u16 origin_y, name\\0); 0x3D the same plus a u16
  mip offset before the name (its length field undercounts the name, so the
  next chunk is found after the name's NUL); 0x21 pixels (u32 count, u32 bytes
  per element, data); 0x00 end of pixelmap.
Types seen: 3 = 8-bit indexed (needs a palette), 5 = RGB565, 7 = XRGB8888 (palettes).

usage: python -I pix.py <file> <out_dir> [palette.pal]
"""
import os
import struct
import sys
import zlib


def read_pixelmaps(path):
    data = open(path, 'rb').read()
    pos, maps, cur = 0, [], None
    while pos + 8 <= len(data):
        cid, ln = struct.unpack_from('>II', data, pos)
        body = data[pos + 8:pos + 8 + ln]
        if cid in (0x03, 0x3D):
            typ, row, w, h, ox, oy = struct.unpack_from('>BHHHHH', data, pos + 8)
            start = pos + 8 + (13 if cid == 0x3D else 11)
            end = data.index(b'\0', start)
            name = data[start:end].decode('latin-1')
            cur = {'type': typ, 'row': row, 'w': w, 'h': h, 'name': name}
            pos = end + 1
            continue
        pos += 8 + ln
        if cid == 0x21 and cur is not None:
            count, size = struct.unpack_from('>II', body, 0)
            cur['pixels'] = body[8:8 + count * size]
            maps.append(cur)
            cur = None
    return maps


def read_palette(path):
    """A .PAL is itself a pixelmap file: 256 entries of 4-byte XRGB."""
    m = read_pixelmaps(path)[0]
    p = m['pixels']
    return [(p[i * 4 + 1], p[i * 4 + 2], p[i * 4 + 3]) for i in range(256)]


def to_rgb(m, pal=None):
    w, h, row, px = m['w'], m['h'], m['row'], m['pixels']
    out = bytearray()
    for y in range(h):
        line = px[y * row:(y + 1) * row]
        for x in range(w):
            if m['type'] == 3:
                out += bytes(pal[line[x]]) if pal else bytes([line[x]] * 3)
            else:
                v = struct.unpack_from('>H', line, x * 2)[0]
                out += bytes([((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31])
    return w, h, bytes(out)


def write_png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    maps = read_pixelmaps(sys.argv[1])
    pal = read_palette(sys.argv[3]) if len(sys.argv) > 3 else None
    os.makedirs(sys.argv[2], exist_ok=True)
    for m in maps:
        print(f"{m['name']:24s} type {m['type']} {m['w']}x{m['h']}")
        write_png(os.path.join(sys.argv[2], m['name'] + '.png'), *to_rgb(m, pal))


if __name__ == '__main__':
    main()
