"""N64 texture decoding from the (emulated, linear) TMEM snapshot captured by geo_f3dex.Walker.

Note: the walker copies LOADBLOCK data linearly and does not apply the odd-row dword swap, so the
data is read back linearly here (equivalent to what the RDP samples). 32-bit textures are kept
linear too (the real RDP splits RG/BA into the two TMEM halves).
"""
import numpy as np

FMT_RGBA, FMT_YUV, FMT_CI, FMT_IA, FMT_I = 0, 1, 2, 3, 4
FMT_NAMES = {0: 'RGBA', 1: 'YUV', 2: 'CI', 3: 'IA', 4: 'I'}
SIZ_BITS = {0: 4, 1: 8, 2: 16, 3: 32}


def _rgba5551(v):
    v = v.astype(np.uint32)
    r = (v >> 11) & 31
    g = (v >> 6) & 31
    b = (v >> 1) & 31
    a = (v & 1) * 255
    return np.stack([(r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), a], -1).astype(np.uint8)


def _ia16(v):
    v = v.astype(np.uint32)
    i = v >> 8
    a = v & 0xFF
    return np.stack([i, i, i, a], -1).astype(np.uint8)


def tile_dims(tile):
    """tile = Tile.key(): (fmt, siz, line, tmem, pal, cmt, maskt, shiftt, cms, masks, shifts,
    uls, ult, lrs, lrt). Returns (w, h) of the texture image to export (mask size if wrapping)."""
    (fmt, siz, line, tmem, pal, cmt, maskt, shiftt, cms, masks, shifts, uls, ult, lrs, lrt) = tile
    w = ((lrs - uls) >> 2) + 1
    h = ((lrt - ult) >> 2) + 1
    if masks:
        w = 1 << masks
    if maskt:
        h = 1 << maskt
    return max(w, 1), max(h, 1)


def decode(tile, tmem, tlut_type=0):
    """Decode the render tile from a 4096-byte TMEM snapshot -> (h, w, 4) uint8 RGBA."""
    (fmt, siz, line, tmem_addr, pal, cmt, maskt, shiftt, cms, masks, shifts, uls, ult, lrs, lrt) = tile
    w, h = tile_dims(tile)
    tm = np.frombuffer(tmem, np.uint8)
    bits = SIZ_BITS[siz]
    stride = line * 8 * (2 if siz == 3 else 1)
    if stride == 0:
        stride = (w * bits + 7) // 8
    base = tmem_addr * 8
    out = np.zeros((h, w, 4), np.uint8)
    rowbytes = (w * bits + 7) // 8
    rows = []
    for y in range(h):
        a = base + y * stride
        r = tm[a:a + rowbytes] if a < 4096 else np.zeros(0, np.uint8)
        if len(r) < rowbytes:
            r = np.concatenate([r, np.zeros(rowbytes - len(r), np.uint8)])
        rows.append(r)
    raw = np.stack(rows)  # (h, rowbytes)

    if bits == 4:
        px = np.stack([raw >> 4, raw & 15], -1).reshape(h, -1)[:, :w]
    elif bits == 8:
        px = raw[:, :w]
    elif bits == 16:
        px = (raw[:, 0::2].astype(np.uint16) << 8 | raw[:, 1::2])[:, :w]
    else:
        px = raw.reshape(h, w, 4)

    if fmt == FMT_RGBA and bits == 16:
        out = _rgba5551(px)
    elif fmt == FMT_RGBA and bits == 32:
        out = px.astype(np.uint8)
    elif fmt == FMT_CI:
        if bits == 4:
            idx = px.astype(np.uint32) + pal * 16
        else:
            idx = px.astype(np.uint32)
        pa = 0x800 + idx * 8
        val = (tm[np.minimum(pa, 4094)].astype(np.uint16) << 8) | tm[np.minimum(pa + 1, 4095)]
        out = _ia16(val) if tlut_type == 3 else _rgba5551(val)
    elif fmt == FMT_IA:
        if bits == 4:
            i = (px >> 1) & 7
            i = (i << 5) | (i << 2) | (i >> 1)
            a = (px & 1) * 255
        elif bits == 8:
            i = (px >> 4) * 17
            a = (px & 15) * 17
        else:
            i = px >> 8
            a = px & 0xFF
        out = np.stack([i, i, i, a], -1).astype(np.uint8)
    elif fmt == FMT_I:
        i = px * 17 if bits == 4 else px
        out = np.stack([i, i, i, i], -1).astype(np.uint8)
    else:
        out[...] = (255, 0, 255, 255)
    return out


def mirror_expand(img, cms, cmt, masks, maskt):
    """Bake mirrored wrapping into the image (2x wide/high) so plain repeat wrapping works."""
    if (cms & 1) and masks:
        img = np.concatenate([img, img[:, ::-1]], 1)
    if (cmt & 1) and maskt:
        img = np.concatenate([img, img[::-1]], 0)
    return img
