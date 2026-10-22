"""Read level geometry blobs straight from the ROM.

The level geometry blobs live in the "Burp" archive at ROM 0x8FFCC0 (41 entries; the game keeps
its directory at D_800A9840 and loads entry N with func_80068CF0(archive, N); the entry index for
the current level comes from func_80082BDC). Entry layout (0x14 bytes, big-endian):
  +0 u32 uncompressed size, +4 u32 stored size, +8 u32 name offset, +C u32 data offset
  (relative to the archive start), +10 u8 method (4 = compressed, 0 = stored).
Compressed entries are raw DEFLATE streams (no zlib header): the game's decoder func_8006B358
reads 1 bit "last block" + 2 bits block type and dispatches to stored (func_8006A118),
fixed-Huffman (func_8006A4BC) and dynamic-Huffman (func_8006A6A8) decoders, i.e. gzip's inflate.
zlib with wbits=-15 reproduces the game's output byte for byte (checked against the Embassy RAM
dump: only the relocated tree pointers differ).
See docs/levels-loading.md (other agent) for the archive/level tables.
"""
import struct
import zlib

GEO_ARCHIVE = 0x8FFCC0


def burp_entries(rom, archive=GEO_ARCHIVE):
    if rom[archive:archive + 4] != b'Burp':
        raise ValueError('no Burp archive at %#x' % archive)
    n = struct.unpack_from('>I', rom, archive + 4)[0]
    out = []
    for i in range(n):
        usize, csize, name, data, method = struct.unpack_from('>IIIIB', rom, archive + 8 + 0x14 * i)
        out.append(dict(index=i, usize=usize, csize=csize, rom=archive + data, method=method))
    return out


def read_entry(rom, index, archive=GEO_ARCHIVE):
    e = burp_entries(rom, archive)[index]
    raw = rom[e['rom']:e['rom'] + e['csize']]
    if e['method'] == 0:
        return raw[:e['usize']], e
    d = zlib.decompressobj(-15)
    out = d.decompress(raw)
    if len(out) != e['usize']:
        raise ValueError('entry %d: inflated %#x bytes, expected %#x' % (index, len(out), e['usize']))
    return out, e


def deflate_entry(data, level=9):
    """Raw-deflate data for re-injection (game decoder accepts any valid DEFLATE stream; untested)."""
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()
