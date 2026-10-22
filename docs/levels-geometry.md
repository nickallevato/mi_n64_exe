# Level geometry format (Mission: Impossible, N64 USA)

This file covers the static level geometry: where it lives, how it is structured, and how
`scripts/levels/level_export.py` turns it into OBJ/MTL/PNG. For which level loads which archive
entry, and for the loader and archive tables, see `docs/levels-loading.md` (written by the other
agent).

Tools (all in `scripts/levels/`, run with `python3 -I`):

| file | purpose |
|---|---|
| `level_export.py` | CLI exporter: RAM dump, ROM entry or raw blob -> `level.obj`, `level.mtl`, `tex/*.png`, `level.json` |
| `geo_mem.py` | big-endian memory view of a RAM dump or blob, segment resolution |
| `geo_level.py` | level blob parser: header, k-d trees, cells, object placements |
| `geo_f3dex.py` | F3DEX 1.x display list walker: vertex cache, triangles, TMEM load emulation |
| `geo_texture.py` | texture decoders (RGBA16/32, CI4/8, IA4/8/16, I4/8), mirror baking |
| `geo_rom.py` | reads level blobs from the ROM's Burp archive (raw DEFLATE) |
| `geo_render.py` | numpy software rasterizer for previews (`view.png` uses the game camera from the RAM dump, plus `top.png` and `wire.png`) |

```
python3 -I scripts/levels/level_export.py build/levels/embassy/ram.bin        # -> build/levels/embassy/export/
python3 -I scripts/levels/level_export.py --rom "rom/Mission Impossible (USA).z64" --entry 5 -o out/
python3 -I scripts/levels/level_export.py --rom "rom/Mission Impossible (USA).z64" --all -o build/levels/rom_export
python3 -I scripts/levels/geo_render.py build/levels/embassy/export            # previews
```

## Verified

### Where the geometry lives

* **ROM**: the Burp archive at ROM `0x8FFCC0` ("Burp", u32 count = 41, then 0x14-byte entries:
  u32 uncompressed size, u32 stored size, u32 name offset, u32 data offset (relative to the archive),
  u8 method). Each non-empty entry is one level's geometry blob. Method 4 means **raw DEFLATE**
  (no zlib header). The game's decoder `func_8006B5FC`/`func_8006B358` is gzip's inflate. It reads
  1 bit for the last block and 2 bits for the block type, then dispatches to stored (`func_8006A118`),
  fixed (`func_8006A4BC`) or dynamic Huffman (`func_8006A6A8`). `zlib.decompressobj(-15)` gives the
  same bytes: the Embassy blob from ROM matches the RAM dump except for the relocated tree
  pointers. So the brief's "assets are not compressed" does not hold for this archive.
  The `dma.log` destinations for `func_8006B5FC` reads are the decompressor's **input buffer**,
  not the final address.
* Entry to level mapping, checked by blob size and by identical exports from RAM and ROM:
  entry 0 = Lundkwist Base (0x9CB60 bytes), entry 5 = Embassy Function (0x8EC10), entry 14 =
  CIA Escape level 1 (0x967B0). All 27 non-empty entries parse and export (see
  `build/levels/rom_export/contact_top.png`). Entry 33 (0x4E0 bytes, the logo scene) has no tree.
* **RAM**: the game loads entry N with `func_80068CF0(D_800A9840, N)` (archive directory copied to
  RAM, 0x80166428 in these dumps). The blob pointer is stored in **`D_80096000`** and
  `func_80046C6C` returns it. `func_80046DF4` relocates the tree in place. Its root node goes to
  **`D_80096004`** (0 if the level has no tree). `func_800343F0` then numbers the cells. Blob
  addresses in the captures: Lundkwist 0x801A9BB0, Embassy 0x801AD600, CIA 0x801BA870.
* **Segment 4 = the level blob.** The frame display list (two buffers, 0x8010F208 and 0x80117EA8)
  does `G_MOVEWORD BC001006 <phys blob address>` and later calls the leaf DLs with
  `G_DL 0x04xxxxxx`, each one preceded by `BA000602 00000000/000000C0` (an RGB dither change).
  Segment 1 holds the current object model (characters and DOF objects). Segments 8, 10, 11, 12
  and 14 are other per-object or global data. Level DLs only use segment 4.

### Blob layout (offsets relative to the blob; ROM pointers are `0x04000000 | offset`)

```
+0x0  u32 world        -> WORLD record
+0x4  u32 objects      -> object placement list
+0x8  u8 r,g,b,?       fog colour? (copied to D_80096012..14; 0 = no fog)
+0xC  ...              k-d nodes, inner leaves and cells (interleaved)
WORLD: u32 root (k-d node, 0 = none), s32 xmin, zmin, xmax, zmax, u32 1
object list: 0x2C-byte entries { char name[20]; s32 x,y,z; f32 rx,ry,rz (degrees) }, e.g. "EMB1BARDOOR.DOF"
texture data (RGBA16/RGBA32 texels referenced by G_SETTIMG 0x04xxxxxx)
per leaf: its vertex buffers (16-byte Vtx), then its display list
```
For Embassy: world 0x5C48, objects 0x5C60, textures 0x5DF8 to about 0x35000, vertices and DLs
0x35100 to 0x8EC10.

**k-d node** (12 bytes): `u32 child0, u32 child1, s16 type, s16 split`.
type **0 = X, 1 = Z, 2 = Y**, **3 = leaf**. child0 holds the geometry with coordinate <= split,
child1 the geometry with coordinate >= split. This was checked against every cell bbox in all
three levels (100% consistent), and against the traversal in `func_80047770`, which takes the
camera x/y/z and orders the children by the camera's side of the split.

Two-level tree:
* **outer tree** leaf `{u32 -> CELL, 0, s16 3, 0}`. The pointer is relocated at load.
* **CELL** (0x14 bytes): `s16 bbox[6]` (min x,y,z, max x,y,z), `u32 inner tree root`,
  `u16 flags`, `u16 index` (the index is written at load by `func_800343F0`).
  Flag bit 0 sends the cell to a different draw path (`func_80062AD8` instead of
  `func_800474A4`). Those cells use translucent render modes (see Guessed).
* **inner tree** leaf `{u32 DL (segmented 0x04xxxxxx, not relocated), 0, s16 3, 0}`. Most cells
  have one leaf. The walker reaches every G_VTX in the blob except one in CIA (most likely a
  false match in the scan).

Counts: Lundkwist 553 cells, 565 DLs, 3137 tris. Embassy 412 / 422 / 3940. CIA 727 / 727 / 3856.

### Display lists (F3DEX 1.x, F3DEX.NoN 1.21)

Each leaf DL has the same shape:
```
04 00 20 7F <bbox vtx>     G_VTX 8 verts at v0 = the leaf's bounding box corners
BE 00 00 00 00 00 00 0E    G_CULLDL 0..7
BA 00 14 02 ...            cycle type; B9 00 03 1D <render mode>; B6/B7 geometry mode; FC combiner
BB 00 00 01 FFFF FFFF      G_TEXTURE on, scale 1.0 (envmapped leaves: 0x0FE0 + G_LIGHTING|G_TEXTURE_GEN)
FD 10 00 00 04xxxxxx       SETTIMG RGBA16 (or FD18 = RGBA32)
F5 .. tile 7 / E6 / F3 LOADBLOCK / E7 / F5 render tile 0 / F2 SETTILESIZE
04 .. G_VTX (up to 32), B1 TRI2 / BF TRI1 ...   (texture switches happen between TRIs)
B8 ENDDL
```
* G_VTX: `04 | (v0*2)<<16 | n<<10 | (n*16-1)`, address in w1. TRI1 and TRI2 indices are
  byte/2 (matches `rt64_gbi_f3dex.cpp`). No G_DL calls inside the level DLs.
* Vertex: `s16 x,y,z; u16 flag; s16 s,t (10.5); u8 r,g,b,a`. Colours are baked lighting
  (G_LIGHTING off), mostly near 0x7F. In the envmapped leaves (Embassy: 14 DLs, e.g. the piano)
  the rgb bytes are normals.
* Textures: only **RGBA16 and RGBA32**, always loaded with LOADBLOCK. No CI/TLUT anywhere in the
  three levels (the decoders exist anyway). Tiles are mostly 16x16 to 32x32 with mask and wrap,
  often mirrored (`cms/cmt = 1`).
* Combiners: `40FE81/FFFFF3F9` and `40FFFF/FFFFF239` = TEXEL0*SHADE, `FFFFFF/FFFE793C` =
  SHADE (texture off), `20FE80/FFFFF3F9` (texel variant).
* Geometry mode: `0x205` (zbuffer, shade, smooth). Lundkwist and CIA add G_FOG (`0x10205`).
  No culling flags, so both sides are drawn.
* **Winding**: triangles are clockwise in world space (floors have normals pointing down by the
  right-hand rule). The exporter reverses them to CCW by default (`--keep-winding` keeps the
  original order). The game camera matrix is a proper rotation (det +1), so there is no mirroring.
* World: right-handed, **Y up**, about 1 unit = 1 cm (Ethan is roughly 180 units tall; this is a
  guess from proportions). The OBJ is written in raw game units.

### Export validation

`view.png` renders the OBJ, loaded back from disk, with the projection and modelview matrices taken
from the frame DL (`01030040`/`01020040` in the frame DL buffers, row-vector s15.16 matrices). For
Embassy and CIA it matches `level.png` closely: piano, vases, marble floor tiles, doorway, and the
green crates behind the alpha-tested mesh fence. Lundkwist matches less exactly, because the RAM
dump is taken a moment after the screenshot and the camera moved during the cutscene.

## Guessed / open

* Header +8: bytes 0..2 look like a fog colour (Lundkwist 18 20 30, CIA 08 10 18, Embassy 0).
  When it is nonzero the loader reads fog near/far from entry 10 of the per-level archive,
  otherwise it uses 996/1000.
* CELL flags: bit 0 = translucent or sorted pass (correlates with render modes `..4570`), bit 1
  unknown (seen with decal-like modes in Embassy). The per-cell 16-byte table
  `D_80093CD8` (filled with 0xFF at load) holds small index lists. Possibly objects or neighbours,
  not decoded.
* The traversal also does distance culling: when `D_800BAF80 && D_800A9852`, it drops children
  farther than `D_8009600A` along the split axis. `func_800340F0` is the per-cell visibility test
  (probably bbox against frustum). There are no portals or PVS in the blob itself.
* Object list terminator (the parser stops at the first non-printable name). DOF models are in other
  archives, not in this blob, and are not exported. Their placements are in `level.json`.
* UV mapping ignores the half-texel filter offset and clamp mode. Envmapped surfaces get an
  approximate sphere-map UV from their normals.

## What re-injection would need

1. Re-encode edited meshes as leaf DLs in the same pattern. First G_VTX must be the 8 bbox
   corners + G_CULLDL. Batches of at most 32 vertices, TRI2. Textures uploaded per material with
   the existing SETTIMG/LOADBLOCK/SETTILE sequence (TMEM 4 KB limit).
2. Rebuild the cells (bbox) and both k-d trees: split values per axis 0=X, 1=Z, 2=Y, with
   child0 <= split. Write pointers as `0x04000000 | offset`, update the header, WORLD and object list.
3. Raw-deflate the blob (`geo_rom.deflate_entry`) and write it into the Burp archive at
   0x8FFCC0. Update the entry's sizes. If it grows, the following entries (or the archive) have to
   move, which means data offsets have to be rewritten (see `docs/levels-loading.md`). The
   uncompressed size has to fit the game's heap. Untested: whether the game checks sizes or CRCs
   beyond the entry fields.
4. Collision is a separate question. Nothing in this blob looks like collision data: the tree only
   holds render DLs. Collision may be computed from these triangles, or it may live in another
   archive entry. Not investigated yet.
