"""Minimal F3DEX (1.x, as used by F3DEX.NoN 1.21) display-list walker for geometry extraction.

Walks a DL like the RSP would (G_DL call/branch, G_ENDDL, segments via G_MOVEWORD), keeps a
32-entry vertex cache (G_VTX), and emits one record per triangle (G_TRI1, G_TRI2, G_QUAD) with the
render state that was current at that moment (texture image/tile/TLUT state, combiner, modes).
Also emulates TMEM loads (LOADBLOCK / LOADTILE / LOADTLUT) so textures can be decoded afterwards.

Command encodings (F3DEX 1.x, checked against lib/rt64/src/gbi/rt64_gbi_f3dex.cpp):
  04 VTX      w0 = 04 | (v0*2)<<16 | n<<10 | (n*16-1); w1 = address
  BF TRI1     w1 = flag<<24 | a*2<<16 | b*2<<8 | c*2
  B1 TRI2     w0 = B1 | a*2<<16 | b*2<<8 | c*2, w1 = 00 | d*2<<16 | e*2<<8 | f*2
  B5 QUAD     w1 = a*2<<24 | b*2<<16 | c*2<<8 | d*2   (tris abc, acd)
"""
import collections
import struct

NVTX = 32


class Tile:
    __slots__ = ('fmt', 'siz', 'line', 'tmem', 'pal', 'cmt', 'maskt', 'shiftt', 'cms', 'masks',
                 'shifts', 'uls', 'ult', 'lrs', 'lrt')

    def __init__(self):
        for k in self.__slots__:
            setattr(self, k, 0)

    def key(self):
        return tuple(getattr(self, k) for k in self.__slots__)


class TexState:
    """Snapshot of what is needed to decode the texture of the current render tile."""

    def __init__(self, tile, tile_key, tmem_src, tlut_src, tlut_type, scale, on):
        self.tile = tile_key            # Tile.key() of the render tile
        self.tmem_src = tmem_src        # tuple describing where TMEM data came from
        self.tlut_src = tlut_src
        self.tlut_type = tlut_type      # 0 none, 2 RGBA16, 3 IA16
        self.scale = scale              # (sc, tc) from G_TEXTURE
        self.on = on

    def key(self):
        return (self.tile, self.tmem_src, self.tlut_src, self.tlut_type)


class Walker:
    def __init__(self, mem, max_cmds=2_000_000):
        self.mem = mem
        self.max_cmds = max_cmds
        self.reset()

    def reset(self):
        self.vtx = [None] * NVTX
        self.tiles = [Tile() for _ in range(8)]
        self.timg = (0, 0, 1, 0)        # fmt, siz, width, addr
        self.tmem = bytearray(4096)
        self.tmem_src = {}              # tmem word addr -> source description
        self.tex = (0, 0, 0, 0)          # tile, level, on, (sc, tc)
        self.tex_scale = (0xFFFF, 0xFFFF)
        self.othermode_h = 0
        self.othermode_l = 0
        self.geom = 0
        self.combine = (0, 0)
        self.prim = 0
        self.env = 0
        self.tris = []
        self.stats = collections.Counter()
        self.dls_visited = set()
        self.vtx_ranges = set()
        self.tmem_cache = {}

    # -------------------------------------------------------------------
    def run(self, dl_addr, tag=None):
        mem = self.mem
        stack = []
        pc = mem.phys(dl_addr)
        self.dls_visited.add(pc)
        n = 0
        while True:
            n += 1
            if n > self.max_cmds:
                raise RuntimeError('DL too long at %08x' % pc)
            w0 = mem.u32(pc)
            w1 = mem.u32(pc + 4)
            pc += 8
            op = w0 >> 24
            self.stats[op] += 1
            if op == 0x04:      # G_VTX
                cnt = (w0 >> 10) & 0x3F
                v0 = (w0 >> 17) & 0x7F
                a = mem.phys(w1)
                self.vtx_ranges.add((a, cnt))
                for i in range(cnt):
                    if v0 + i >= NVTX:
                        break
                    raw = mem.bytes(a + 16 * i, 16)
                    x, y, z, flag, s, t, r, g, b, al = struct.unpack('>hhhHhhBBBB', raw)
                    self.vtx[v0 + i] = (x, y, z, s, t, r, g, b, al)
            elif op == 0xBF:    # G_TRI1
                self._tri(((w1 >> 17) & 0x7F, (w1 >> 9) & 0x7F, (w1 >> 1) & 0x7F), tag)
            elif op == 0xB1:    # G_TRI2
                self._tri(((w0 >> 17) & 0x7F, (w0 >> 9) & 0x7F, (w0 >> 1) & 0x7F), tag)
                self._tri(((w1 >> 17) & 0x7F, (w1 >> 9) & 0x7F, (w1 >> 1) & 0x7F), tag)
            elif op == 0xB5:    # G_QUAD
                a, b, c, d = (w1 >> 25) & 0x7F, (w1 >> 17) & 0x7F, (w1 >> 9) & 0x7F, (w1 >> 1) & 0x7F
                self._tri((a, b, c), tag)
                self._tri((a, c, d), tag)
            elif op == 0x06:    # G_DL
                tgt = mem.phys(w1)
                self.dls_visited.add(tgt)
                if ((w0 >> 16) & 0xFF) == 0:
                    stack.append(pc)
                pc = tgt
            elif op == 0xB8:    # G_ENDDL
                if not stack:
                    return
                pc = stack.pop()
            elif op == 0xBC:    # G_MOVEWORD
                if (w0 & 0xFF) == 6:
                    self.mem.segments[((w0 >> 8) & 0xFFFF) // 4 & 0xF] = w1 & 0x1FFFFFFF
            elif op == 0xBB:    # G_TEXTURE
                self.tex = ((w0 >> 8) & 7, (w0 >> 11) & 7, w0 & 0xFF)
                self.tex_scale = (w1 >> 16, w1 & 0xFFFF)
            elif op == 0xBA:    # G_SETOTHERMODE_H
                ln, sh = w0 & 0xFF, (w0 >> 8) & 0xFF
                m = ((1 << ln) - 1) << sh
                self.othermode_h = (self.othermode_h & ~m) | (w1 & m)
            elif op == 0xB9:    # G_SETOTHERMODE_L
                ln, sh = w0 & 0xFF, (w0 >> 8) & 0xFF
                m = ((1 << ln) - 1) << sh
                self.othermode_l = (self.othermode_l & ~m) | (w1 & m)
            elif op == 0xB7:
                self.geom |= w1
            elif op == 0xB6:
                self.geom &= ~w1
            elif op == 0xFC:
                self.combine = (w0 & 0xFFFFFF, w1)
            elif op == 0xFA:
                self.prim = w1
            elif op == 0xFB:
                self.env = w1
            elif op == 0xFD:    # SETTIMG
                self.timg = ((w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 0xFFF) + 1, mem.phys(w1))
            elif op == 0xF5:    # SETTILE
                t = self.tiles[(w1 >> 24) & 7]
                t.fmt, t.siz = (w0 >> 21) & 7, (w0 >> 19) & 3
                t.line, t.tmem = (w0 >> 9) & 0x1FF, w0 & 0x1FF
                t.pal, t.cmt, t.maskt, t.shiftt = (w1 >> 20) & 0xF, (w1 >> 18) & 3, (w1 >> 14) & 0xF, (w1 >> 10) & 0xF
                t.cms, t.masks, t.shifts = (w1 >> 8) & 3, (w1 >> 4) & 0xF, w1 & 0xF
            elif op == 0xF2:    # SETTILESIZE
                t = self.tiles[(w1 >> 24) & 7]
                t.uls, t.ult = (w0 >> 12) & 0xFFF, w0 & 0xFFF
                t.lrs, t.lrt = (w1 >> 12) & 0xFFF, w1 & 0xFFF
            elif op == 0xF3:    # LOADBLOCK
                self._loadblock(self.tiles[(w1 >> 24) & 7], (w0 >> 12) & 0xFFF, w0 & 0xFFF,
                                (w1 >> 12) & 0xFFF, w1 & 0xFFF)
            elif op == 0xF4:    # LOADTILE
                t = self.tiles[(w1 >> 24) & 7]
                t.uls, t.ult = (w0 >> 12) & 0xFFF, w0 & 0xFFF
                t.lrs, t.lrt = (w1 >> 12) & 0xFFF, w1 & 0xFFF
                self._loadtile(t)
            elif op == 0xF0:    # LOADTLUT
                self._loadtlut(self.tiles[(w1 >> 24) & 7], (w0 >> 12) & 0xFFF, (w1 >> 12) & 0xFFF)
            elif op in (0x00, 0xBE, 0xE7, 0xE6, 0xE8, 0xE9, 0xF7, 0xF8, 0xF9, 0x01, 0x03, 0xBD,
                        0xED, 0xFE, 0xFF, 0xF6, 0xE4, 0xE5, 0xB4, 0xB3, 0xB2, 0xB0, 0xEF, 0xEE):
                pass
            else:
                self.stats['unknown_%02x' % op] += 1

    # -------------------------------------------------------------------
    def _loadblock(self, t, uls, ult, lrs, dxt):
        fmt, siz, width, addr = self.timg
        # bytes: number of texels * texel size; 4-bit loads are done as 16-bit by convention
        tsz = {0: 0.5, 1: 1, 2: 2, 3: 4}[siz]
        nbytes = int((lrs - uls + 1) * tsz)
        start = addr + int(uls * tsz)  # ult is 0 in practice
        dst = t.tmem * 8
        nbytes = min(nbytes, 4096 - dst)
        if nbytes > 0 and self.mem.valid(start, nbytes):
            self.tmem[dst:dst + nbytes] = self.mem.bytes(start, nbytes)
        self.tmem_src[t.tmem] = ('block', start, nbytes)

    def _loadtile(self, t):
        fmt, siz, width, addr = self.timg
        tsz = {0: 0.5, 1: 1, 2: 2, 3: 4}[siz]
        uls, ult, lrs, lrt = t.uls >> 2, t.ult >> 2, t.lrs >> 2, t.lrt >> 2
        rowbytes = int((lrs - uls + 1) * tsz)
        stride = int(width * tsz)
        for r in range(lrt - ult + 1):
            src = addr + int((ult + r) * stride + uls * tsz)
            dst = t.tmem * 8 + r * t.line * 8
            if dst + rowbytes > 4096 or not self.mem.valid(src, rowbytes):
                break
            self.tmem[dst:dst + rowbytes] = self.mem.bytes(src, rowbytes)
        self.tmem_src[t.tmem] = ('tile', addr, width, siz, t.uls, t.ult, t.lrs, t.lrt, t.line)

    def _loadtlut(self, t, uls, lrs):
        fmt, siz, width, addr = self.timg
        cnt = ((lrs >> 2) - (uls >> 2)) + 1
        base = t.tmem * 8
        for i in range(cnt):
            d = base + i * 8
            if d + 2 > 4096 or not self.mem.valid(addr + 2 * i, 2):
                break
            self.tmem[d:d + 2] = self.mem.bytes(addr + 2 * i, 2)
        self.tmem_src[('tlut', t.tmem)] = (addr, cnt)

    def tex_state(self):
        tile_i, level, on = self.tex
        t = self.tiles[tile_i]
        src = self.tmem_src.get(t.tmem)
        tlut_type = (self.othermode_h >> 14) & 3
        tl = None
        if t.siz <= 1 and t.fmt == 2:   # CI
            tl = tuple(sorted((k[1], v) for k, v in self.tmem_src.items()
                              if isinstance(k, tuple)))
        return TexState(tile_i, t.key(), src, tl, tlut_type, self.tex_scale, on)

    def tmem_snapshot(self):
        return bytes(self.tmem)

    def _tri(self, idx, tag):
        vs = [self.vtx[i] if i < NVTX else None for i in idx]
        if any(v is None for v in vs):
            self.stats['bad_tri'] += 1
            return
        textured = bool(self.tex[2])
        ts = self.tex_state() if textured else None
        key = None
        if ts is not None:
            key = ts.key()
            if key not in self.tmem_cache:
                self.tmem_cache[key] = (ts, bytes(self.tmem))
        self.tris.append(dict(v=vs, tex=key, geom=self.geom, combine=self.combine,
                              om=(self.othermode_h, self.othermode_l), prim=self.prim,
                              env=self.env, scale=self.tex_scale, tag=tag))
