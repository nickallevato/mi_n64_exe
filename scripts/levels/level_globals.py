#!/usr/bin/env python3
"""Print the RAM pointers of a loaded level's blocks from a capture's ram.bin (native word order).
usage: level_globals.py <capture_dir>"""
import os, struct, sys
import numpy as np

GLOBALS = [
    ('D_800A9654', 0x800A9654, 'level Burp archive handle (Burp_Open result)'),
    ('D_80096000', 0x80096000, 'geometry bank (level entry 0 -> archive 0x8FFCC0[n]); segment 4 base'),
    ('D_80096004', 0x80096004, 'geometry BSP root node'),
    ('D_800A98C4', 0x800A98C4, 'entry 1: SOOL script'),
    ('D_8008AF40', 0x8008AF40, 'entry 2'),
    ('D_800A98CC', 0x800A98CC, 'entry 3'),
    ('D_80093CD0', 0x80093CD0, 'entry 4 block'),
    ('D_800A9210', 0x800A9210, 'entry 5 (grid)'),
    ('D_800BAB98', 0x800BAB98, 'entry 6'),
    ('D_800BA8FC', 0x800BA8FC, 'entry 7: dialogue strings'),
    ('D_8009C770', 0x8009C770, 'entry 8: objective strings'),
    ('D_800B9934', 0x800B9934, 'entry 9'),
    ('D_800A9840', 0x800A9840, 'geometry-bank archive handle (0x8FFCC0)'),
    ('D_80093AC0', 0x80093AC0, '.DOF prop-model archive handle (0x310280)'),
    ('D_80093AD0', 0x80093AD0, 'DOFBB.BIN (0x310280[433])'),
    ('D_800BA97C', 0x800BA97C, 'animation table (0x63E510[254])'),
    ('D_800A9508', 0x800A9508, 'UI string table (0x30D890[0])'),
]


def main():
    cap = sys.argv[1]
    ram = np.fromfile(os.path.join(cap, 'ram.bin'), '<u4').byteswap().tobytes()
    w = lambda a: struct.unpack_from('>I', ram, a - 0x80000000)[0]
    for name, addr, desc in GLOBALS:
        print(f'{name} = {w(addr):#010x}  {desc}')
    arc = w(0x800A9654)
    if arc:
        n, ents = w(arc), w(arc + 4)
        print(f'level archive: {n} entries, table at {ents:#010x}, ROM base {w(arc + 0xC):#08x}')
        for i in range(n):
            e = ents + i * 0x14
            print(f'  [{i:2}] dec {w(e):#7x} stored {w(e + 4):#7x} rom {w(e + 0xC):#08x} flag {ram[e + 0x10 - 0x80000000]}')


if __name__ == '__main__':
    main()
