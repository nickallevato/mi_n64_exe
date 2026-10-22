"""Memory views for Mission: Impossible level geometry.

RamImage: an 8 MB RDRAM dump (ram.bin from level_capture.sh, words stored little-endian)
          converted to N64 big-endian bytes; addressed by KSEG0 (0x80xxxxxx) or physical address.
BlobImage: a single relocatable level blob (e.g. a decompressed ROM archive entry) mapped at a
          chosen base address, so the same parser works on ROM data.

Both expose segment resolution for F3DEX segmented addresses (0x0Sxxxxxx).
"""
import struct

import numpy as np


class Mem:
    def __init__(self, data, base):
        self.data = data          # bytes, big-endian N64 view
        self.base = base          # KSEG0 address of data[0]
        self.segments = [0] * 16  # physical base address per segment

    # --- address handling -------------------------------------------------
    def phys(self, addr):
        """Resolve a KSEG0, physical or segmented address to a KSEG0 address."""
        if addr >= 0x80000000:
            return addr & 0xFFFFFFFF
        seg = (addr >> 24) & 0x0F
        return 0x80000000 | ((self.segments[seg] + (addr & 0x00FFFFFF)) & 0x1FFFFFFF)

    def off(self, addr):
        a = self.phys(addr) - self.base
        if a < 0 or a >= len(self.data):
            raise IndexError('address %08x outside image' % addr)
        return a

    def valid(self, addr, size=1):
        try:
            a = self.off(addr)
        except IndexError:
            return False
        return a + size <= len(self.data)

    def bytes(self, addr, n):
        a = self.off(addr)
        return self.data[a:a + n]

    def u8(self, addr):
        return self.data[self.off(addr)]

    def u16(self, addr):
        return struct.unpack_from('>H', self.data, self.off(addr))[0]

    def s16(self, addr):
        return struct.unpack_from('>h', self.data, self.off(addr))[0]

    def u32(self, addr):
        return struct.unpack_from('>I', self.data, self.off(addr))[0]

    def s32(self, addr):
        return struct.unpack_from('>i', self.data, self.off(addr))[0]


def load_ram(path):
    """ram.bin (8 MB, each 32-bit word little-endian) -> Mem at 0x80000000."""
    w = np.fromfile(path, '<u4')
    return Mem(w.byteswap().tobytes(), 0x80000000)


def load_blob(data, base):
    return Mem(bytes(data), base)
