"""List or extract a Carmageddon II .TWT archive.

Format (read from the files): u32 total size, u32 entry count, then per entry
u32 size + char name[52] (0xCD padding), then each file's data in order,
each padded to a multiple of 4 bytes.

usage: python -I twt.py <archive.twt> [out_dir]
"""
import os
import struct
import sys


def read_twt(path):
    with open(path, 'rb') as f:
        data = f.read()
    total, count = struct.unpack_from('<II', data, 0)
    entries = []
    for i in range(count):
        size, = struct.unpack_from('<I', data, 8 + i * 56)
        name = data[12 + i * 56:12 + i * 56 + 52].split(b'\0')[0].decode('latin-1')
        entries.append((name, size))
    pos = 8 + count * 56
    files = []
    for name, size in entries:
        files.append((name, data[pos:pos + size]))
        pos += (size + 3) & ~3
    if pos != len(data):
        print(f'warning: {path}: parsed {pos} of {len(data)} bytes (header says {total})', file=sys.stderr)
    return files


def main():
    files = read_twt(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else None
    for name, blob in files:
        print(f'{len(blob):8d}  {name}')
        if out:
            os.makedirs(out, exist_ok=True)
            with open(os.path.join(out, os.path.basename(name)), 'wb') as f:
                f.write(blob)


if __name__ == '__main__':
    main()
