#!/usr/bin/env python3
"""List / extract the "Burp" archives of the Mission: Impossible (USA) ROM.

Every asset in ROM 0x94440-0xBEC6B0 lives in one of 52 "Burp" archives (see docs/levels-loading.md).
Archive layout (big-endian), as read by Burp_Open (func_80068820):
  +0  'Burp'
  +4  u32 count
  +8  count x 0x14-byte entries:
        +0x00 u32 decoded size (bytes allocated by Burp_Load)
        +0x04 u32 stored size  (bytes in ROM; = decoded size when not packed)
        +0x08 u32 name offset  (relative to the archive start; 0 = archive has no names). Each name has a 2-byte
                               prefix (byte 1 = strlen + 2); the game points at the text after it.
        +0x0C u32 data offset  (relative to the archive start)
        +0x10 u8  packing: 0 = stored, 4 = raw deflate (RFC 1951, no zlib/gzip header; func_8006B5FC)
        +0x11 3 bytes padding (always 0)
  name strings (named archives only): from entries[0].name to the archive end.

usage:
  burp.py [--all]                 list archives (first 6 entries of big ones unless --all)
  burp.py --extract OUT_DIR       write every decoded entry to OUT_DIR/<archive>_<index>.bin
Run from the repository root (reads rom/Mission Impossible (USA).z64).
"""
import os, re, struct, sys, zlib

ROM = 'rom/Mission Impossible (USA).z64'


def archives(d):
    """[(rom offset, count, [entry dict])] for every 'Burp' archive in the ROM image d."""
    out = []
    for m in re.finditer(b'Burp', d):
        o = m.start()
        n = struct.unpack_from('>I', d, o + 4)[0]
        ents = []
        for i in range(n):
            a, size, name, data, flag = struct.unpack_from('>IIIIB', d, o + 8 + i * 0x14)
            nm = d[o + name + 2:o + name + 66].split(b'\0')[0].decode('latin1') if name else ''
            ents.append(dict(a=a, size=size, name_off=name, data=o + data, flag=flag, name=nm))
        out.append((o, n, ents))
    return out


def decode(d, e):
    """Decoded bytes of an entry (as Burp_Load leaves them in RAM, before any pointer fix-ups)."""
    raw = d[e['data']:e['data'] + e['size']]
    return zlib.decompressobj(-15).decompress(raw) if e['flag'] else raw


def main():
    d = open(ROM, 'rb').read()
    if '--extract' in sys.argv:
        out = sys.argv[sys.argv.index('--extract') + 1]
        os.makedirs(out, exist_ok=True)
        for o, n, ents in archives(d):
            for i, e in enumerate(ents):
                open(os.path.join(out, f'{o:06x}_{i:03d}.bin'), 'wb').write(decode(d, e))
        return
    for o, n, ents in archives(d):
        print(f'archive {o:#08x} count {n}')
        if n > 20 and '--all' not in sys.argv:
            ents = ents[:6]
        for i, e in enumerate(ents):
            print(f"  [{i:3}] {e['data']:#08x} stored {e['size']:#7x} decoded {e['a']:#7x} "
                  f"{'deflate' if e['flag'] else 'stored '} {e['name']}")


if __name__ == '__main__':
    main()
