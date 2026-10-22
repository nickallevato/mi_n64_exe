#!/usr/bin/env python3
"""Preview renderer for level_export.py output (numpy software rasterizer, no GPU needed).

Reads level.obj/level.mtl/tex/*.png (so it checks what Blender would import) and writes:
  view.png   perspective view using the game's own camera (projection + modelview matrices read
             from the frame display list in ram.bin), for side-by-side comparison with level.png
  top.png    orthographic top-down plan (looking down -Y), textured + vertex colours
  wire.png   top-down wireframe coloured by cell

Usage: python3 -I scripts/levels/geo_render.py build/levels/embassy/export [--ram ram.bin]
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FRAME_DLS = (0x8010F208, 0x80117EA8)   # the two frame display list buffers (both slots)


def load_obj(d):
    V, C, T, F = [], [], [], []
    mats, curmat, cell, curcell = {}, None, [], -1
    texnames = []
    for line in open(os.path.join(d, 'level.obj')):
        p = line.split()
        if not p:
            continue
        if p[0] == 'v':
            V.append([float(x) for x in p[1:4]])
            C.append([float(x) for x in p[4:7]] if len(p) >= 7 else [1, 1, 1])
        elif p[0] == 'vt':
            T.append([float(p[1]), float(p[2])])
        elif p[0] == 'o':
            curcell += 1
        elif p[0] == 'usemtl':
            curmat = p[1]
            if curmat not in mats:
                mats[curmat] = len(texnames)
                texnames.append(curmat)
        elif p[0] == 'f':
            vi = [int(x.split('/')[0]) - 1 for x in p[1:4]]
            ti = [int(x.split('/')[1]) - 1 for x in p[1:4]]
            F.append((vi, ti, mats[curmat], curcell))
    # materials -> textures
    maps = {}
    cur = None
    for line in open(os.path.join(d, 'level.mtl')):
        p = line.split()
        if not p:
            continue
        if p[0] == 'newmtl':
            cur = p[1]
        elif p[0] == 'map_Kd':
            maps[cur] = p[1]
    texs = []
    for n in texnames:
        if n in maps:
            texs.append(np.asarray(Image.open(os.path.join(d, maps[n])).convert('RGBA')).astype(np.float32) / 255)
        else:
            texs.append(None)
    V, C, T = np.array(V), np.array(C), np.array(T) if T else np.zeros((1, 2))
    vi = np.array([f[0] for f in F])
    ti = np.array([f[1] for f in F])
    return dict(pos=V[vi], col=C[vi], uv=T[ti], mat=np.array([f[2] for f in F]),
                cell=np.array([f[3] for f in F]), texs=texs, texnames=texnames)


def read_matrix(ram, addr):
    a = addr & 0x1FFFFFFF
    hi = struct.unpack('>16h', ram[a:a + 32])
    lo = struct.unpack('>16H', ram[a + 32:a + 64])
    return (np.array(hi) + np.array(lo) / 65536.0).reshape(4, 4)


def game_camera(ram_path, slot=0):
    w = np.fromfile(ram_path, '<u4')
    ram = w.byteswap().tobytes()
    a = FRAME_DLS[slot] & 0x1FFFFFFF
    proj = mv = None
    for i in range(0, 0x400, 8):
        w0, w1 = struct.unpack('>II', ram[a + i:a + i + 8])
        if w0 == 0x01030040:
            proj = read_matrix(ram, w1)
        elif w0 == 0x01020040:
            mv = read_matrix(ram, w1)
        if proj is not None and mv is not None:
            return mv, proj
    raise RuntimeError('camera matrices not found in frame DL')


def clip_near(P, attrs, eps=1e-3):
    """Clip triangles (N,3,4 clip coords) against w > eps. attrs: (N,3,K). Returns new arrays."""
    outP, outA = [], []
    for tri, at in zip(P, attrs):
        inside = tri[:, 3] > eps
        if inside.all():
            outP.append(tri)
            outA.append(at)
            continue
        if not inside.any():
            continue
        poly, pa = [], []
        for i in range(3):
            j = (i + 1) % 3
            a, b = tri[i], tri[j]
            if inside[i]:
                poly.append(a)
                pa.append(at[i])
            if inside[i] != inside[j]:
                t = (eps - a[3]) / (b[3] - a[3])
                poly.append(a + t * (b - a))
                pa.append(at[i] + t * (at[j] - at[i]))
        for k in range(1, len(poly) - 1):
            outP.append(np.array([poly[0], poly[k], poly[k + 1]]))
            outA.append(np.array([pa[0], pa[k], pa[k + 1]]))
    if not outP:
        return np.zeros((0, 3, 4)), np.zeros((0, 3, attrs.shape[2]))
    return np.array(outP), np.array(outA)


def rasterize(scr, invw, attrs, mats, texs, W, H, bg=(0.3, 0.3, 0.35)):
    """scr: (N,3,3) screen x,y,depth; invw: (N,3) 1/w (ones for ortho); attrs (N,3,K):
    [r,g,b,u,v]. Perspective-correct attribute interpolation, nearest texture sampling, z-buffer."""
    img = np.empty((H, W, 3), np.float32)
    img[:] = bg
    zb = np.full((H, W), np.inf, np.float32)
    for k in range(len(scr)):
        s = scr[k]
        x0 = max(int(np.floor(s[:, 0].min())), 0)
        x1 = min(int(np.ceil(s[:, 0].max())), W - 1)
        y0 = max(int(np.floor(s[:, 1].min())), 0)
        y1 = min(int(np.ceil(s[:, 1].max())), H - 1)
        if x0 > x1 or y0 > y1:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        (ax, ay), (bx, by), (cx, cy) = s[0, :2], s[1, :2], s[2, :2]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        l0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        l1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        l2 = 1 - l0 - l1
        m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not m.any():
            continue
        z = l0 * s[0, 2] + l1 * s[1, 2] + l2 * s[2, 2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= z < sub
        if not m.any():
            continue
        iw = invw[k]
        q = l0 * iw[0] + l1 * iw[1] + l2 * iw[2]
        a = attrs[k]
        def interp(c):
            return (l0 * a[0, c] * iw[0] + l1 * a[1, c] * iw[1] + l2 * a[2, c] * iw[2]) / q
        col = np.stack([interp(0), interp(1), interp(2)], -1)
        tex = texs[mats[k]]
        if tex is not None:
            th, tw = tex.shape[:2]
            u, v = interp(3), interp(4)
            tx = np.floor(u * tw).astype(np.int64) % tw
            ty = np.floor((1 - v) * th).astype(np.int64) % th
            tc = tex[ty, tx]
            m &= tc[..., 3] > 0.3
            col = col * tc[..., :3]
        sub[m] = z[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = col[m]
    return (np.clip(img, 0, 1) * 255).astype(np.uint8)


def view_image(o, mv, proj, W=800, H=450):
    N = len(o['pos'])
    hp = np.concatenate([o['pos'], np.ones((N, 3, 1))], -1)
    p = proj.copy()
    p[0, 0] = p[1, 1] / (W / H)
    clip = hp @ mv @ p
    attrs = np.concatenate([o['col'], o['uv'], np.repeat(o['mat'][:, None, None], 3, 1)], -1)
    clip, attrs = clip_near(clip, attrs, eps=1.0)
    w = clip[..., 3]
    ndc = clip[..., :3] / w[..., None]
    scr = np.stack([(ndc[..., 0] + 1) * 0.5 * W, (1 - ndc[..., 1]) * 0.5 * H, ndc[..., 2]], -1)
    mats = attrs[:, 0, 5].round().astype(int)
    return rasterize(scr, 1.0 / w, attrs[..., :5], mats, o['texs'], W, H, bg=(0.55, 0.55, 0.6))


def top_image(o, size=1400):
    # drop ceilings (faces whose normal points down; OBJ winding is CCW = front)
    P = o['pos']
    n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    keep = n[:, 1] / (np.linalg.norm(n, axis=1) + 1e-9) > -0.3
    o = {k: (v[keep] if isinstance(v, np.ndarray) and len(v) == len(P) else v) for k, v in o.items()}
    P = o['pos']
    lo, hi = P.reshape(-1, 3).min(0), P.reshape(-1, 3).max(0)
    ext = max(hi[0] - lo[0], hi[2] - lo[2])
    sc = (size - 20) / ext
    W = int((hi[0] - lo[0]) * sc) + 20
    H = int((hi[2] - lo[2]) * sc) + 20
    scr = np.stack([(P[..., 0] - lo[0]) * sc + 10, (P[..., 2] - lo[2]) * sc + 10, -P[..., 1]], -1)
    attrs = np.concatenate([o['col'], o['uv']], -1)
    img = rasterize(scr, np.ones(P.shape[:2]), attrs, o['mat'], o['texs'], W, H, bg=(0.1, 0.1, 0.12))
    return img, (lo, sc)


def wire_image(o, size=1400):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    P = o['pos']
    fig, ax = plt.subplots(figsize=(12, 12), dpi=120)
    polys = P[..., [0, 2]]
    rng = np.random.default_rng(1)
    cols = rng.random((o['cell'].max() + 1, 3)) * 0.7 + 0.2
    ax.add_collection(PolyCollection(polys, facecolors=cols[o['cell']] * 0.4 + 0.1,
                                     edgecolors=cols[o['cell']], linewidths=0.3))
    lo, hi = P.reshape(-1, 3).min(0), P.reshape(-1, 3).max(0)
    ax.set_xlim(lo[0], hi[0])
    ax.set_ylim(lo[2], hi[2])
    ax.invert_yaxis()
    ax.set_aspect('equal')
    ax.set_xlabel('x')
    ax.set_ylabel('z')
    ax.set_facecolor('black')
    ax.set_title('top-down wireframe, coloured by k-d cell (%d tris)' % len(P))
    return fig


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('export_dir')
    ap.add_argument('--ram', help='ram.bin for the game camera (default: ../ram.bin)')
    ap.add_argument('--slot', type=int, default=0)
    args = ap.parse_args()
    d = args.export_dir
    o = load_obj(d)
    ram = args.ram or os.path.join(d, '..', 'ram.bin')
    if os.path.exists(ram):
        mv, proj = game_camera(ram, args.slot)
        Image.fromarray(view_image(o, mv, proj)).save(os.path.join(d, 'view.png'))
    img, _ = top_image(o)
    Image.fromarray(img).save(os.path.join(d, 'top.png'))
    fig = wire_image(o)
    fig.savefig(os.path.join(d, 'wire.png'), bbox_inches='tight')
    print('wrote', ', '.join(os.path.join(d, n) for n in ('view.png', 'top.png', 'wire.png')))


if __name__ == '__main__':
    main()
