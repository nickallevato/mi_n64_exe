"""Level geometry blob ("segment 4" block) parser for Mission: Impossible (N64).

Layout (see docs/levels-geometry.md):
  header (12 bytes):
    +0 u32  rel offset of the WORLD record
    +4 u32  rel offset of the object placement list (0x2C-byte entries: name[20], s32 xyz, f32 rot xyz)
    +8 u32  flags/colour bytes (bytes 0..2 copied to D_80096012..14 by the loader)
  WORLD record:
    +0 u32  rel offset of the outer k-d tree root node
    +4 s32  xmin, +8 s32 zmin, +C s32 xmax, +10 s32 zmax (all +-100000 so far), +14 u32 count(=1)
  k-d node (12 bytes): u32 child0, u32 child1, s16 type, s16 split
    type 0/1/2 = split axis (x/y/z), child0 = coordinate < split? (unverified), type 3 = leaf
  outer leaf: u32 ptr -> CELL, u32 0, s16 3, s16 0
  CELL (0x14 bytes): s16 bbox[6] (min xyz, max xyz), u32 inner tree root, u32 id
  inner leaf: u32 segmented DL address (0x04xxxxxx), u32 0, s16 3, s16 0

Pointers in the ROM/blob form are offsets ORed with 0x04000000 (segment 4); the loader
(func_80046DF4 / func_80046D04 / func_80046C84) relocates the tree pointers to KSEG0 in place,
but leaves the DL pointers segmented. This parser accepts both forms.
"""


class Level:
    pass


def reloc(mem, base, p):
    if (p & 0xFF000000) == 0x80000000:
        return p
    return base + (p & 0x00FFFFFF)


def parse_tree(mem, base, node, leaf_cb, depth=0, path=''):
    """Walk a k-d tree; leaf_cb(node_addr, path) for each type-3 node. Returns node list."""
    nodes = []
    stack = [(node, depth, path)]
    seen = set()
    while stack:
        n, d, pth = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        c0, c1, typ, split = mem.u32(n), mem.u32(n + 4), mem.s16(n + 8), mem.s16(n + 10)
        if typ == 3:
            leaf_cb(n, pth)
            continue
        if typ not in (0, 1, 2) or d > 64:
            raise ValueError('bad k-d node %08x type %d' % (n, typ))
        nodes.append((n, typ, split, d))
        stack.append((reloc(mem, base, c1), d + 1, pth + '1'))
        stack.append((reloc(mem, base, c0), d + 1, pth + '0'))
    return nodes


def parse_level(mem, base):
    lv = Level()
    lv.base = base
    lv.world = reloc(mem, base, mem.u32(base))
    lv.objlist = reloc(mem, base, mem.u32(base + 4))
    lv.flags = mem.u32(base + 8)
    w = lv.world
    raw_root = mem.u32(w)
    lv.root = reloc(mem, base, raw_root) if raw_root else None
    lv.world_bounds = tuple(mem.s32(w + 4 + 4 * i) for i in range(4))
    lv.world_count = mem.u32(w + 0x14)

    lv.cells = []

    def outer_leaf(n, path):
        cell = reloc(mem, base, mem.u32(n))
        bbox = tuple(mem.s16(cell + 2 * i) for i in range(6))
        inner = reloc(mem, base, mem.u32(cell + 0xC))
        cid = mem.u32(cell + 0x10)
        leaves = []

        def inner_leaf(m, p2):
            leaves.append((m, mem.u32(m), p2))

        inodes = parse_tree(mem, base, inner, inner_leaf)
        lv.cells.append(dict(addr=cell, leafnode=n, path=path, bbox=bbox, inner=inner, id=cid,
                             leaves=leaves, inner_nodes=inodes))

    if lv.root is not None:
        lv.outer_nodes = parse_tree(mem, base, lv.root, outer_leaf)
    else:
        # No tree (seen in ROM entry 33, the title/logo scene): the game sets D_80096004 = 0.
        # Fall back to the leaf records that follow the header ({dl, 0, 3, 0} x N).
        lv.outer_nodes = []
        leaves = []
        a = base + 0xC
        while mem.valid(a, 12) and mem.s16(a + 8) == 3 and (mem.u32(a) >> 24) == 0x04:
            leaves.append((a, mem.u32(a), 'flat'))
            a += 12
        lv.cells.append(dict(addr=0, leafnode=0, path='', bbox=(0,) * 6, inner=0, id=0,
                             leaves=leaves, inner_nodes=[]))
    lv.objects = parse_objects(mem, lv.objlist)
    return lv


def parse_objects(mem, a):
    """Object placement list: 0x2C-byte entries {char name[20]; s32 x, y, z; f32 rx, ry, rz}
    (rotation in degrees; so far only ry is seen nonzero). Ends at the first entry whose name
    does not start with a printable character (guessed; the texture data follows)."""
    import struct
    out = []
    for i in range(1024):
        e = a + 0x2C * i
        if not mem.valid(e, 0x2C):
            break
        raw = mem.bytes(e, 20)
        if not (32 < raw[0] < 127):
            break
        name = raw.split(b'\0')[0].decode('latin1')
        pos = tuple(mem.s32(e + 20 + 4 * k) for k in range(3))
        rot = tuple(struct.unpack('>f', mem.bytes(e + 32 + 4 * k, 4))[0] for k in range(3))
        out.append(dict(addr=e, name=name, pos=pos, rot=rot))
    return out
