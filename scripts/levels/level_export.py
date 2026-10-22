#!/usr/bin/env python3
"""Export a Mission: Impossible (N64) level's static geometry to OBJ + MTL + PNG textures.

Usage:
  python3 -I scripts/levels/level_export.py build/levels/embassy/ram.bin [-o OUTDIR]
        [--base 0x801AD600] [--scale 1.0] [--no-textures] [--split cells|none]
  python3 -I scripts/levels/level_export.py --rom "rom/Mission Impossible (USA).z64" --entry 5 [-o OUTDIR]
  python3 -I scripts/levels/level_export.py --rom ROM --all -o build/levels/rom_export
  python3 -I scripts/levels/level_export.py --blob level.bin [...]   (decompressed level blob)

Input is an RDRAM dump (ram.bin from scripts/level_capture.sh; the level blob pointer is read
from D_80096000), an entry of the level geometry archive in the ROM (Burp archive at 0x8FFCC0,
raw DEFLATE; Embassy = 5, Lundkwist = 0, CIA Escape 1 = 14), or a raw decompressed blob.

Output (OUTDIR, default <ram.bin dir>/export):
  level.obj   one object per k-d tree cell ("cell_<idx>"), vertex colours as "v x y z r g b"
  level.mtl   one material per texture (+ "envmap" / "shade" materials), map_Kd/map_d PNGs
  tex/*.png   decoded textures (mirror wrapping baked in)
  level.json  cells, bounding boxes, DL addresses, textures, object (DOF) placements
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from geo_mem import load_ram, load_blob  # noqa: E402
from geo_level import parse_level  # noqa: E402
from geo_f3dex import Walker  # noqa: E402
import geo_rom  # noqa: E402
import geo_texture as gt  # noqa: E402

LEVEL_PTR = 0x80096000   # D_80096000: KSEG0 pointer to the loaded level geometry blob
G_LIGHTING = 0x20000
G_TEXTURE_GEN = 0x40000


def write_png(path, img):
    """Minimal RGBA PNG writer (no PIL dependency)."""
    import zlib
    import struct
    h, w, _ = img.shape
    raw = b''.join(b'\0' + img[y].tobytes() for y in range(h))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


BLOB_BASE = 0x80400000   # arbitrary KSEG0 address to map ROM/blob data at


def load_level(args):
    if args.rom:
        data, e = geo_rom.read_entry(open(args.rom, 'rb').read(), args.entry)
        base = args.base if args.base is not None else BLOB_BASE
        mem = load_blob(data, base)
    elif args.blob:
        data = open(args.blob, 'rb').read()
        base = args.base if args.base is not None else BLOB_BASE
        mem = load_blob(data, base)
    else:
        mem = load_ram(args.ram)
        base = args.base if args.base is not None else mem.u32(LEVEL_PTR)
    mem.segments[4] = base & 0x1FFFFFFF
    return mem, base


class Material:
    def __init__(self, name, kind, tex=None, img=None, uvdiv=(1, 1), tile=None, alpha=False):
        self.name, self.kind, self.tex, self.img = name, kind, tex, img
        self.uvdiv, self.tile, self.alpha = uvdiv, tile, alpha


def extract(mem, base, want_textures=True):
    """Walk the level and return (level, triangles, materials).

    triangles: list of dicts with pos (3x3 float), col (3x4 uint8), uv (3x2 float), mat name,
    cell index, normals flag."""
    lv = parse_level(mem, base)
    w = Walker(mem)
    for ci, c in enumerate(lv.cells):
        for li, (node, dl, path) in enumerate(c['leaves']):
            w.run(dl, tag=(ci, li))

    mats = {}
    tex_imgs = {}

    def material_for(t):
        lit = bool(t['geom'] & G_LIGHTING)
        texgen = lit and bool(t['geom'] & G_TEXTURE_GEN)
        if t['tex'] is None:
            name = 'shade'
            if name not in mats:
                mats[name] = Material(name, 'shade')
            return mats[name], texgen, lit
        key = t['tex']
        if key not in tex_imgs:
            ts, tmem = w.tmem_cache[key]
            tile = ts.tile
            img = gt.decode(tile, tmem, ts.tlut_type)
            (fmt, siz, line, tmem_a, pal, cmt, maskt, shiftt, cms, masks, shifts,
             uls, ult, lrs, lrt) = tile
            img2 = gt.mirror_expand(img, cms, cmt, masks, maskt)
            src = ts.tmem_src
            addr = src[1] if src else 0
            # name by offset inside the level blob (stable between RAM dumps and ROM blobs)
            off = addr - mem.phys(base) if mem.phys(base) <= addr < mem.phys(base) + 0x1000000 else addr
            nm = 'tex_%06x_%s%d_%dx%d' % (off & 0xFFFFFF, gt.FMT_NAMES.get(fmt, '?'),
                                          gt.SIZ_BITS[siz], img.shape[1], img.shape[0])
            if (cms & 1) or (cmt & 1):
                nm += '_m%s%s' % ('s' if cms & 1 else '', 't' if cmt & 1 else '')
            tex_imgs[key] = (nm, img2, tile, src)
        nm, img2, tile, src = tex_imgs[key]
        if texgen:
            nm = nm + '_env'
        if nm not in mats:
            mats[nm] = Material(nm, 'env' if texgen else 'tex', tex=key, img=img2, tile=tile,
                                alpha=bool((img2[..., 3] < 255).any()))
            mats[nm].src = src
        return mats[nm], texgen, lit

    tris = []
    for t in w.tris:
        mat, texgen, lit = material_for(t)
        v = t['v']
        pos = np.array([[p[0], p[1], p[2]] for p in v], np.float64)
        col = np.array([[p[5], p[6], p[7], p[8]] for p in v], np.uint8)
        uv = np.zeros((3, 2))
        if mat.kind == 'tex':
            tile = mat.tile
            (fmt, siz, line, tmem_a, pal, cmt, maskt, shiftt, cms, masks, shifts,
             uls, ult, lrs, lrt) = tile
            sc, tc = t['scale']
            sc = 1.0 if sc == 0xFFFF else sc / 65536.0
            tc = 1.0 if tc == 0xFFFF else tc / 65536.0
            st = np.array([[p[3], p[4]] for p in v], np.float64) / 32.0
            st[:, 0] *= sc
            st[:, 1] *= tc
            for k, sh in ((0, shifts), (1, shiftt)):
                if 1 <= sh <= 10:
                    st[:, k] /= (1 << sh)
                elif sh >= 11:
                    st[:, k] *= (1 << (16 - sh))
            st[:, 0] -= uls / 4.0
            st[:, 1] -= ult / 4.0
            H, W = mat.img.shape[:2]
            uv[:, 0] = st[:, 0] / W
            uv[:, 1] = 1.0 - st[:, 1] / H
        elif mat.kind == 'env':
            # texgen: vertex rgb are a signed normal; approximate a sphere map from it
            n = col[:, :3].astype(np.int8).astype(np.float64) / 128.0
            uv[:, 0] = 0.5 + 0.5 * n[:, 0]
            uv[:, 1] = 0.5 + 0.5 * n[:, 1]
        if lit:
            col = col.copy()
            col[:, :3] = 200   # rgb are normals, not colours
        tris.append(dict(pos=pos, col=col, uv=uv, mat=mat.name, cell=t['tag'][0],
                         leaf=t['tag'][1], lit=lit))
    return lv, tris, mats, w


def write_obj(outdir, lv, tris, mats, scale=1.0, split='cells', textures=True, keep_winding=False):
    os.makedirs(outdir, exist_ok=True)
    texdir = os.path.join(outdir, 'tex')
    if textures:
        os.makedirs(texdir, exist_ok=True)
    with open(os.path.join(outdir, 'level.mtl'), 'w') as f:
        for m in mats.values():
            f.write('newmtl %s\nKa 0 0 0\nKd 1 1 1\nKs 0 0 0\nillum 1\n' % m.name)
            if m.kind != 'shade' and textures:
                fn = 'tex/%s.png' % m.name.replace('_env', '')
                if not os.path.exists(os.path.join(outdir, fn)):
                    write_png(os.path.join(outdir, fn), np.ascontiguousarray(m.img))
                f.write('map_Kd %s\n' % fn)
                if m.alpha:
                    f.write('map_d %s\n' % fn)
            f.write('\n')

    vmap, vlist, tmap, tlist = {}, [], {}, []
    lines = ['# Mission: Impossible level export (scripts/levels/level_export.py)',
             '# coordinates: game units, Y up', 'mtllib level.mtl']
    order = sorted(range(len(tris)), key=lambda i: (tris[i]['cell'], tris[i]['leaf']))
    cur_obj, cur_mat = None, None
    faces = []
    for i in order:
        t = tris[i]
        obj = 'cell_%03d' % t['cell'] if split == 'cells' else 'level'
        if obj != cur_obj:
            faces.append('o %s' % obj)
            cur_obj, cur_mat = obj, None
        if t['mat'] != cur_mat:
            faces.append('usemtl %s' % t['mat'])
            cur_mat = t['mat']
        idx = []
        # The game's triangles are clockwise in world space (front faces point "inward" by the
        # usual CCW convention; the level DLs draw without culling). Reverse to CCW for Blender.
        for k in ((0, 1, 2) if keep_winding else (0, 2, 1)):
            p = tuple(t['pos'][k]) + tuple(int(c) for c in t['col'][k][:3])
            if p not in vmap:
                vmap[p] = len(vlist) + 1
                vlist.append(p)
            uvk = (round(float(t['uv'][k][0]), 6), round(float(t['uv'][k][1]), 6))
            if uvk not in tmap:
                tmap[uvk] = len(tlist) + 1
                tlist.append(uvk)
            idx.append('%d/%d' % (vmap[p], tmap[uvk]))
        faces.append('f ' + ' '.join(idx))
    with open(os.path.join(outdir, 'level.obj'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
        for p in vlist:
            f.write('v %g %g %g %.4f %.4f %.4f\n' % (p[0] * scale, p[1] * scale, p[2] * scale,
                                                    p[3] / 255, p[4] / 255, p[5] / 255))
        for uv in tlist:
            f.write('vt %.6f %.6f\n' % uv)
        f.write('\n'.join(faces) + '\n')
    return len(vlist)


def write_json(outdir, lv, tris, mats, mem):
    cells = []
    for ci, c in enumerate(lv.cells):
        cells.append(dict(index=ci, addr='%08x' % c['addr'], id='%08x' % c['id'], path=c['path'],
                          bbox=c['bbox'], ntris=sum(1 for t in tris if t['cell'] == ci),
                          leaves=[dict(node='%08x' % n, dl='%08x' % dl, path=p)
                                  for (n, dl, p) in c['leaves']]))
    out = dict(base='%08x' % lv.base, world='%08x' % lv.world, root='%08x' % (lv.root or 0),
               world_bounds=lv.world_bounds, flags='%08x' % lv.flags, ntris=len(tris),
               cells=cells,
               materials={m.name: dict(kind=m.kind, src=[x if isinstance(x, int) and x < 256 else
                                                         ('%08x' % x if isinstance(x, int) else x)
                                                         for x in (getattr(m, 'src', None) or [])],
                                       alpha=m.alpha) for m in mats.values()},
               objects=[dict(name=o['name'], pos=o['pos'], rot=o['rot']) for o in lv.objects])
    with open(os.path.join(outdir, 'level.json'), 'w') as f:
        json.dump(out, f, indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('ram', nargs='?', help='ram.bin RDRAM dump')
    ap.add_argument('--blob', help='raw decompressed level geometry blob instead of a RAM dump')
    ap.add_argument('--rom', help='ROM image (.z64); use with --entry N or --all')
    ap.add_argument('--entry', type=int, default=5, help='level geometry archive entry (default 5)')
    ap.add_argument('--all', action='store_true', help='with --rom: export every non-empty entry')
    ap.add_argument('--base', type=lambda s: int(s, 0), help='level blob KSEG0 address')
    ap.add_argument('-o', '--out', help='output directory')
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--split', choices=('cells', 'none'), default='cells')
    ap.add_argument('--no-textures', action='store_true')
    ap.add_argument('--keep-winding', action='store_true',
                    help="keep the game's clockwise triangle winding (default: flip to CCW)")
    args = ap.parse_args()
    if not (args.ram or args.blob or args.rom):
        ap.error('need ram.bin, --rom or --blob')
    if args.rom and args.all:
        rom = open(args.rom, 'rb').read()
        root = args.out or 'build/levels/rom_export'
        for e in geo_rom.burp_entries(rom):
            if e['usize'] == 0:
                continue
            args.entry = e['index']
            args.out = os.path.join(root, 'entry_%02d' % e['index'])
            try:
                export_one(args)
            except Exception as ex:   # keep going; report
                print('entry %d: FAILED %s' % (e['index'], ex))
        return
    export_one(args)


def export_one(args):
    mem, base = load_level(args)
    src = args.ram or args.blob or args.rom
    out = args.out or os.path.join(os.path.dirname(os.path.abspath(src)), 'export')
    lv, tris, mats, w = extract(mem, base)
    nv = write_obj(out, lv, tris, mats, args.scale, args.split, not args.no_textures, args.keep_winding)
    write_json(out, lv, tris, mats, mem)
    unk = {k: v for k, v in w.stats.items() if isinstance(k, str)}
    print('%s blob %08x: %d cells, %d leaf DLs, %d triangles, %d vertices, %d materials -> %s'
          % ('rom entry %d' % args.entry if args.rom else 'level', base, len(lv.cells), sum(len(c['leaves']) for c in lv.cells), len(tris), nv,
             len(mats), out))
    if unk:
        print('warnings:', unk)


if __name__ == '__main__':
    main()
