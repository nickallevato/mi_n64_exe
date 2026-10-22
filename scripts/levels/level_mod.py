#!/usr/bin/env python3
"""Make an edited level geometry bank to load with MI_OVERRIDE_DIR (src/game/overrides.cpp).

Decodes a bank from the geometry archive (ROM 0x8FFCC0), applies edits in place (the size must stay the same) and
writes <out>/rom_XXXXXXXX.bin, named after the entry's ROM address, which the game loads instead of the original.

  python3 -I scripts/levels/level_mod.py --rom ROM --entry 5 --tint 1.0,0.35,0.35 -o build/mods/embassy_red
  python3 -I scripts/levels/level_mod.py --rom ROM --entry 5 --raise-above 300 --by 200 -o build/mods/embassy_tall
  MI_OVERRIDE_DIR=build/mods/embassy_red build/app/MIRecompiled

Edits:
  --tint R,G,B          multiply every vertex colour (baked lighting) by these factors
  --raise-above Y --by D move every vertex higher than Y up by D units (stretches walls, lifts ceilings);
                        visual only, collision is separate
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from geo_f3dex import Walker  # noqa: E402
from geo_level import parse_level  # noqa: E402
from geo_mem import load_blob  # noqa: E402
from geo_rom import read_entry  # noqa: E402

BASE = 0x80400000


def vertex_offsets(data):
    """Blob offsets of every vertex that the level's display lists load."""
    mem = load_blob(data, BASE)
    mem.segments[4] = BASE & 0x1FFFFFFF  # level display lists address the blob as segment 4
    level = parse_level(mem, BASE)
    walker = Walker(mem)
    for cell in level.cells:
        for _, dl, _ in cell['leaves']:
            walker.run(dl)
    for addr, count in walker.vtx_ranges:
        yield from (addr - BASE + 16 * i for i in range(count))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom', required=True)
    ap.add_argument('--entry', type=int, required=True, help='geometry bank (Embassy = 5)')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--tint')
    ap.add_argument('--raise-above', type=int)
    ap.add_argument('--by', type=int, default=0)
    args = ap.parse_args()

    rom = open(args.rom, 'rb').read()
    data, entry = read_entry(rom, args.entry)
    blob = bytearray(data)
    offsets = sorted(set(vertex_offsets(data)))
    print('bank %d: ROM 0x%08X, %d bytes, %d vertices' % (args.entry, entry['rom'], len(blob), len(offsets)))

    if args.tint:
        factors = [float(f) for f in args.tint.split(',')]
        for off in offsets:
            for c in range(3):
                blob[off + 12 + c] = min(255, int(blob[off + 12 + c] * factors[c]))
        print('tinted %d vertex colours by %s' % (len(offsets), factors))
    if args.raise_above is not None:
        moved = 0
        for off in offsets:
            y = struct.unpack_from('>h', blob, off + 2)[0]
            if y > args.raise_above:
                struct.pack_into('>h', blob, off + 2, max(-32768, min(32767, y + args.by)))
                moved += 1
        print('raised %d vertices above y=%d by %d' % (moved, args.raise_above, args.by))

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'rom_%08X.bin' % entry['rom'])
    open(path, 'wb').write(blob)
    print('wrote', path)


if __name__ == '__main__':
    main()
