#!/usr/bin/env python3
"""Turn a level capture's dma.log into a ROM map: which Burp archive entry each Rom_Read belongs to and where the
decoded block ended up in RAM (found by searching ram.bin for the decompressed bytes).

usage: rommap.py <capture_dir> [--all]   (capture_dir has dma.log and ram.bin, e.g. build/levels/embassy)
Without --all, only reads from the level's own load (from the last Game_LevelLoop archive open on) are printed.
Output columns: retrace, archive, entry index, ROM range (stored bytes), decoded size, packed?, loader, RAM address
of the decoded data in the dump ('-' = not found: freed, overwritten or modified in place after loading).
The RAM column is a heuristic (first/middle/last 64 decoded bytes searched in the dump): a "matched at +N" hit means the
start was rewritten (pointer relocation), but after the heap is reused it can also be a stale copy at another address.
For a level's own blocks, scripts/levels/level_globals.py reads the real pointers from the game's globals instead.
"""
import os, re, sys, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import burp

ROM = 'rom/Mission Impossible (USA).z64'

ARCHIVE_ROLE = {
    0x094440: 'audio banks (D_800938C8)',
    0x2FE2A0: 'music sequences (D_800938DC)',
    0x30D890: 'text (D_80093AB0)',
    0x310280: 'prop models .DOF (D_80093AC0)',
    0x487550: 'character models (D_800BA8E0)',
    0x63E510: 'animations (D_800BA8E4)',
    0x785160: '2D images: menus/HUD (D_8008C520)',
    0x7FF7C0: 'fonts (D_80093CF0)',
    0x801470: 'segment-1 models, items? (D_8009C764)',
    0x8FFCC0: 'level geometry banks (D_800A9840)',
}


def load_ram(path):
    return np.fromfile(path, '<u4').byteswap().tobytes()


def main():
    cap = sys.argv[1]
    show_all = '--all' in sys.argv
    d = open(ROM, 'rb').read()
    arcs = burp.archives(d)
    # entry data offset -> (archive, index, entry)
    by_data = {}
    for o, n, es in arcs:
        for i, e in enumerate(es):
            if e['size']:
                by_data.setdefault(e['data'], (o, i, e))
    heads = {o: n for o, n, es in arcs}
    ram = load_ram(os.path.join(cap, 'ram.bin'))
    lines = [l.split() for l in open(os.path.join(cap, 'dma.log'))]
    start = 0
    if not show_all:
        for k, l in enumerate(lines):
            if l[4] == 'func_80068820' and l[5] == 'Game_LevelLoop' and int(l[1], 16) in heads:
                start = k
    level_arc = None
    for l in lines[start:]:
        rt, src, dst, size = int(l[0]), int(l[1], 16), int(l[2], 16), int(l[3], 16)
        callers = ' '.join(l[4:])
        if src in heads:
            role = ARCHIVE_ROLE.get(src, 'level archive' if heads[src] in (11, 12) else '?')
            if 'Game_LevelLoop' in callers:
                level_arc = src
            print(f'{rt:5} open archive {src:#08x} ({heads[src]} entries) {role}  [{callers}]')
            continue
        if src - 8 in heads:
            continue  # entry table read of an archive just opened
        hit = by_data.get(src)
        if not hit:
            print(f'{rt:5} {src:#08x} size {size:#x} -> {dst:#010x} (not an entry start) [{callers}]')
            continue
        o, i, e = hit
        raw = d[e['data']:e['data'] + e['size']]
        dec = zlib.decompressobj(-15).decompress(raw) if e['flag'] else raw
        where = '-'
        probe_at = [0, len(dec) // 2, max(0, len(dec) - 64)]
        for p in probe_at:
            probe = dec[p:p + 64]
            if len(probe) < 16:
                continue
            k = ram.find(probe)
            if k >= 0 and k - p >= 0:
                where = f'{0x80000000 + k - p:#010x}' + ('' if p == 0 else f' (matched at +{p:#x})')
                break
        role = ARCHIVE_ROLE.get(o, 'level archive' if heads[o] in (11, 12) else '?')
        print(f'{rt:5} {o:#08x}[{i:3}] rom {e["data"]:#08x}-{e["data"] + e["size"]:#08x} dec {len(dec):#8x} '
              f'{"deflate" if e["flag"] else "stored "} ram {where:28} {role}  [{callers}]')


if __name__ == '__main__':
    main()
