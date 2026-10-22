#!/usr/bin/env python3
"""Decode the Mission: Impossible (USA) mission/level tables from the ROM.

Tables (code-segment data, ROM offset = VRAM - 0x7FFFF400):
  D_8008AB28  5 mission entries, 0x14 bytes (menu order: Ice Hit, Recover NOC List, CIA Escape, Mole Hunt, Ice Storm)
  each mission's +0x10 points to a list of 0x14-byte stage entries, terminated by an entry with start == 0.
  D_8008A4E8  extra stage list (end sequences), D_8008A880 title-screen scene, D_8008A894 another scene.
Stage / mission entry (0x14 bytes):
  +0x00 u16 name: byte offset of the name in the UI string table (archive 0x30D890 entry 0)
  +0x02 u8  flags: 0x01 = unlocked ("LEVEL POSSIBLE"), 0x10/0x20/0x40/0x80 = see docs/levels-loading.md
  +0x03 u8  ?
  +0x04 u32 ROM start of the stage's Burp archive
  +0x08 u32 ROM end of the archive (= next archive start)
  +0x0C u16 ?  +0x0E u16 ?
  +0x10 u32 mission entries: pointer to the stage list; stage entries: u8[3] ?, +0x13 u8 (arg of func_80046910)
usage: level_table.py [rom]
"""
import struct, sys, zlib, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import burp

ROM = 'rom/Mission Impossible (USA).z64'
MISSIONS = ['Ice Hit', 'Recover NOC List', 'CIA Escape', 'Mole Hunt', 'Ice Storm']
GEO_ARCHIVE = 0x8FFCC0


def rd(d, vram, n):
    o = vram - 0x7FFFF400
    return d[o:o + n]


def entry(d, vram):
    b = rd(d, vram, 0x14)
    name, flags, b3, start, end, w0c, w0e, w10 = struct.unpack('>HBBIIHHI', b)
    return dict(vram=vram, name=name, flags=flags, b3=b3, start=start, end=end, w0c=w0c, w0e=w0e, w10=w10, raw=b)


def burp_entry(d, arc, idx):
    n = struct.unpack_from('>I', d, arc + 4)[0]
    a, size, nm, off, flag = struct.unpack_from('>IIIIB', d, arc + 8 + idx * 0x14)
    raw = d[arc + off:arc + off + size]
    return zlib.decompressobj(-15).decompress(raw) if flag else raw


def main():
    d = open(sys.argv[1] if len(sys.argv) > 1 else ROM, 'rb').read()
    strings = burp_entry(d, 0x30D890, 0)
    def s(off):
        return strings[off:].split(b'\0')[0].decode('latin1') if off < len(strings) else '?'
    def describe(e):
        geo = ''
        if e['start']:
            g = burp_entry(d, e['start'], 0).decode('latin1').strip()
            geo = f"geo bank {g:>2}"
        return (f"{e['vram']:#010x} name {e['name']:#06x} {s(e['name'])!r:26} flags {e['flags']:#04x} b3 {e['b3']:#04x} "
                f"rom {e['start']:#08x}-{e['end']:#08x} {geo} +0C {e['w0c']:#06x} +0E {e['w0e']:#06x} +10 {e['w10']:#010x}")
    for m in range(5):
        me = entry(d, 0x8008AB28 + m * 0x14)
        print(f"mission {m} {MISSIONS[m]}: {describe(me)}")
        v = me['w10']; i = 0
        while True:
            e = entry(d, v)
            if not e['start'] or not e['end']:
                break
            menu = 'menu' if not (e['flags'] & 0xE0) else '    '
            print(f"  [{i}] {menu} {describe(e)}")
            v += 0x14; i += 1
    print('other entries:')
    for v in list(range(0x8008A4E8, 0x8008A5B0, 0x14)) + [0x8008A880, 0x8008A894]:
        print('  ', describe(entry(d, v)))


if __name__ == '__main__':
    main()
