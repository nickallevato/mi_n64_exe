# Levels: table, archives and loading (Mission: Impossible, N64 USA)

How the game finds and loads a level, as a base for level mods. Scope: the level table, the ROM
container format, the load sequence and which blocks belong to a level. Parsing geometry (display
lists, BSP contents) is covered elsewhere.

Marked **[verified]** = read from code and confirmed against ROM/RAM captures; **[code]** = read from
code only; **[guess]** = inferred from data shape or naming, not confirmed.

Tools (python3 -I, run from the repo root):

| script | what it does |
|---|---|
| `scripts/levels/burp.py` | lists the 52 Burp archives; `--extract DIR` writes every decoded entry |
| `scripts/levels/level_table.py` | decodes the mission/stage tables (names, ROM ranges, geometry bank, flags) |
| `scripts/levels/rommap.py <capture>` | annotates a capture's `dma.log` with archive/entry/role and finds the blocks in `ram.bin` |
| `scripts/levels/level_globals.py <capture>` | prints the RAM pointers of the loaded level's blocks from the game's globals |
| `scripts/levels/fn.py func_X` / `--callers func_X` | compact disassembly view / call sites (asm helper) |

## 1. Key facts

- **Assets are compressed.** Everything from ROM 0x94440 to 0xBEC6B0 is packed into 52 **"Burp"
  archives**. Most entries are **raw deflate** (RFC 1951, no zlib header; Python:
  `zlib.decompressobj(-15)`). The game's decoder is a port of gzip's `inflate.c`
  (`func_8006B5FC` → `inflate_block` `func_8006B358` → stored/fixed/dynamic). Earlier scans found no
  Yay0/Yaz0/MIO0 because none of those formats is used. **[verified]**: every packed entry in the ROM
  decodes to exactly its "decoded size" field.
- **There is no global directory.** Each archive carries its own entry table. The code finds the
  global archives through hard-coded ROM start/end constants (`lui/addiu` pairs of `D_xxxxxx`
  symbols, one opener function per archive). It finds level archives through the **stage table** in
  the data segment (ROM start/end per stage). **[verified]**
- **A level = one 11-entry Burp archive** (script, text, AI/zone data, ...), plus **one geometry bank**
  from the shared 41-entry geometry archive at 0x8FFCC0. Entry 0 of the level archive holds the
  bank number as ASCII decimal. Props (doors etc.), characters, animations, 2D images and music
  come from shared archives on demand. **[verified]**
- **Pointers inside blocks are offsets.** They are either relative to the block start, or
  `0x04xxxxxx` / `0x01xxxxxx` segment-tagged offsets. The loader turns them into absolute pointers
  in place (`base + (p & 0xFFFFFF)`). Display lists keep their segment addresses: segment 4 = the
  geometry bank and segment 1 = the model being drawn, set with G_MOVEWORD before drawing. A block
  can therefore load at any heap address. **[verified]**

## 2. Burp archive format

ROM layout, big-endian. **[verified]** (`Burp_Open` = `func_80068820`, checked against all 52 archives)

```
+0x00  char[4] 'Burp'
+0x04  u32 count
+0x08  count × 0x14-byte entries:
         +0x00 u32 decoded size   heap bytes Burp_Load allocates
         +0x04 u32 stored size    bytes in ROM (== decoded size when stored)
         +0x08 u32 name offset    relative to archive start; 0 when the archive has no names
         +0x0C u32 data offset    relative to archive start
         +0x10 u8  packing        0 = stored, 4 = raw deflate (only values used)
         +0x11 u8[3] 0
then the entry data, then (named archives only) the name strings. Each name has a 2-byte prefix
(0x49, strlen+2), and the game points at the text after the prefix.
```

`Burp_Open(romStart, romEnd)` allocates a 0x18-byte handle and reads the 8-byte header, then the
entry table. It reads the name table (from `entries[0].name` to `romEnd`) only when the archive has
names. It then fixes up each entry in RAM: `data += romStart` and `name → RAM pointer`. The handle
is `+0 count, +4 entries*, +8 names buffer*, +0xC romStart, +0x10 romEnd-romStart, +0x14 offset of
the name table`. `romEnd` matters only for named archives.

Archive API (all **[code]**, names proposed in `symbols/manual.txt`):

| addr | proposed name | role |
|---|---|---|
| 0x80068820 | Burp_Open(start, end) | returns handle, or 0 if the magic is wrong |
| 0x80068990 | Burp_Close | frees handle, entry table and names |
| 0x800689E0 | Burp_GetEntry(h, i) | `&entries[i]` or 0 |
| 0x80068A10 | Burp_GetName(h, i) | |
| 0x80068A50 | Burp_FindByName(h, name) | index or -1 (string compare `func_80083610`) |
| 0x80068AFC | Burp_CopyEntry(h, i, out) | copies the 0x14-byte entry |
| 0x80068B50 | Burp_GetCount(h) | |
| 0x80068B58 | Burp_ReadRaw(h, i, dst) | `Rom_Read` of the stored bytes |
| 0x80068BB4 | Burp_LoadTo(h, i, dst) | decode into the caller's buffer (music uses this) |
| 0x80068CF0 | Burp_Load(h, i) | `Heap_Alloc(decoded size)` and decode; returns the buffer or 0. Most loads use this. |
| 0x80068E98 / 0x80068FA0 / 0x80069170 | ...ByName variants | not called directly in the code |

Decompression: `Inflate_FromRom(src, dst, srcSize, tmp, tmpSize)` = `func_8006B5FC`. `Burp_Load`
allocates a temporary input buffer of `min(largest free block − 0x8000, storedSize rounded up to
even)` (needs > 0x8800 bytes free). It `Rom_Read`s the stored bytes into that buffer, inflates into
the destination, and frees the temp buffer. So in `dma.log`, a `func_8006B5FC` read's `dst` is the
**temp buffer**, not the block's final address. The final address is the return value of
`Burp_Load`, which is kept in a global; see `level_globals.py`. Inflate refills from ROM if the
input is bigger than the temp buffer (`inflate_block`/`inflate_codes` call `Rom_Read`). Both
`Rom_Read` and the inflater accept a **RAM source address** (0x8xxxxxxx → memcpy).

## 3. ROM map: all archives

The archives tile 0x94440–0xBEC6B0 back to back (gaps under 0x14 bytes). 0xBEC6B0–0xC00000 is
0xFF padding (80 KB free). 0x1050–0x94440 is code+data.

| ROM | n | opened by (boot, `Game_ThreadMain`) | handle | contents |
|---|---|---|---|---|
| 0x094440 | 4 | func_8000DAA0 (audio init) | D_800938C8 | [0] "N64 WaveTables" SOUND.WBK samples (stored, 2 MB), [1] "N64 PtrTablesV2" SOUND.WBK bank, [2] WaveTables M5C1.WBK (music samples, stored), [3] PtrTables M5C1. **[verified]** headers |
| 0x2FE2A0 | 12 | func_8000DAA0 | D_800938DC | music sequences, loaded with Burp_LoadTo into a fixed buffer (0x8015FDC8) **[verified]** |
| 0x30D890 | 5 | func_80028604 | D_80093AB0 | text: [0] UI strings (level names; kept at D_800A9508), [1] team bios, [2] equipment descriptions, [3] mission briefings, [4] credits **[verified]** |
| 0x310280 | 434 | func_80028CB8 | D_80093AC0 | **named** prop models `*.DOF` (doors, furniture, vehicles). [433] `DOFBB.BIN` is loaded every level (D_80093AD0) **[verified]** names; DOFBB role **[guess]** bounding boxes |
| 0x487550 | 160 | func_80053310 | D_800BA8E0 | character models: 3 `0x01xxxxxx` header pointers (relocated on load), vertices, RGBA16 textures. Cache of 30 (func_80053388) **[verified]** layout start |
| 0x63E510 | 255 | func_80054680 | D_800BA8E4 | animations; [254] = animation table D_800BA97C (12-byte entries, +4 root track made absolute on load). Cache of 12 (func_8005480C) **[verified]** |
| 0x785160 | 288 | func_800632B8 | D_8008C520 | 2D images: menu screens, HUD, icons (many 0x810 = 0x10 header + 32×32 RGBA16) **[guess]** from sizes |
| 0x7FF7C0 | 7 | func_80035CC0 | D_80093CF0 | fonts (glyph metric tables + images) **[guess]** |
| 0x801470 | 59 | func_80054B70 | D_8009C764 | more segment-1 models, cache of 10, loaded mid-level (held items/weapons?) **[guess]** |
| 0x82A280 … 0x8FEF90 | 11 / 12 | Game_LevelLoop | D_800A9654 | **level archives** (30; see §5) |
| 0x8FFCC0 | 41 | func_80046BF8 | D_800A9840 | **geometry banks**, indexed by level entry 0 (§6) |
| 0xBCC710 … 0xBE1E00 | 11 | Game_LevelLoop | D_800A9654 | more level archives (12: cut scenes, Ice Hit levels) |

`0x8F4CB0` (12 entries; geometry bank 05 = Embassy; dialogue "My name …"; entry 11 is extra text)
is **referenced by no table**. Its start only appears as Train roof's end address. It is an unused
level or scene, and possibly the original version of the scene at 0x8DE3A0 (D_8008A894), which
shares bank 05 and the same opening text.

## 4. The level (stage) table

All in the code segment's data. ROM offset = VRAM − 0x7FFFF400 (for example 0x8008AB28 → ROM
0x8B728). **[verified]** with `level_table.py`, the menu screenshots and the three captures.

### Entry layout (0x14 bytes, used for missions and stages)

```
+0x00 u16  name       byte offset into the UI string table (archive 0x30D890[0]); 0x07ED = "Test Level"
                      (placeholder for scenes), 0x07E1 "Cia 2", 0x07E7 "Cia 4"
+0x02 u8   flags      bit 0x01 = unlocked ("LEVEL POSSIBLE"; this is the byte the GameShark enablers set)
                      0x80 = non-interactive scene, run with mode 0
                      0x40, 0x20 = continuation part of the previous level, run with mode 1 (no briefing menu)
                      0x10 = the level continues into the next (hidden) entries [guess]
                      0x08 seen only on the Ice Storm mission entry (initial value) [meaning unknown]
                      Any of 0xE0 set → hidden from the level menu and never stored as the resume point.
+0x03 u8   0 everywhere
+0x04 u32  ROM start of the stage's Burp archive (0 = none; the list ends at the first entry with start == 0 or end == 0)
+0x08 u32  ROM end (= next archive start; passed to Burp_Open)
+0x0C u16  briefing: byte offset into the briefing text (0x30D890[3]) [verified for 6 entries]
+0x0E u16  0 everywhere
+0x10      mission entries: u32 pointer to the mission's stage list
           stage entries:   u32 0/1; its low byte (+0x13) goes to func_80046910 → D_800898F2
                            (gates the START-button handling in Game_LevelLoop) [meaning unknown]
```

### Tables

- `D_8008AB28` **MissionTable**: 5 entries, menu order (`D_800BADD8` = selected mission). A
  mission entry's +4/+8 is its **intro scene archive**, played only when starting at stage 0.
- each mission's +0x10 → its **stage list** (indexed by `D_800A9868`).
- `D_8008A4E8`: 10 entries = 2 per mission (Ice Hit and Ice Storm empty). Extra scenes picked
  through `D_8008AC28` after a stage returns 1 **[code; purpose guess: mission-end scenes]**.
- `D_8008A880`: the intro/title scene (bank 33, nearly empty geometry), run from
  `Game_ThreadMain` → `func_8004E47C` at boot (retrace ~492 in all captures). **[verified]**
- `D_8008A894`: scene in archive 0x8DE3A0 (bank 05), run when `func_8004F214` returns flag 4 after
  a completed stage **[code]**.
- `D_8008AB14`: 5 pointers (one per mission) to arrays of 0x14-byte rows, **one row per stage-list
  entry** (row counts match the lists: 2, 8, 9, 5, 6). Each row is up to 10 u16 ids ending in 0xFFFF,
  and scenes have an empty row. Almost every row starts with 0x0015. **[verified shape]** Probably the
  stage's starting equipment/inventory ids **[guess]**.

### How the menu maps to entries [verified]

`func_80049274` (mission/level select) moves the level cursor through the mission's stage list,
skipping entries with `flags & 0xE0` and wrapping at both ends. It stores mission → `D_800BADD8`
and **stage-list index** → `D_800A9868`, then calls `Game_RunMission` (`func_80049960`).
`Game_RunMission`:

1. If the stage index is 0 and the mission entry has an archive, plays the mission intro
   (`Game_LevelLoop(mission entry, 1, 0)`).
2. Loops over the stage list from `D_800A9868`: a 0x80 entry → `Game_LevelLoop(e, x, 0)`; a 0x60
   entry → mode 1; otherwise mode 3. Mode bit 1 shows the pre-level menu (objectives/briefing/team/
   equipment, `func_8003B390(0x14)`).
3. Game_LevelLoop's return value decides what happens next **[code; meanings partly guessed]**:
   0 = leave (abort); 1 = possibly play one of the mission's two `D_8008A4E8` scenes (failure or
   ending?); 2 = stage done, after which `func_8004F214(stage)` returns flags: 8 = run the next
   list entry, 4 = play scene `D_8008A894`, 2 = mission complete (`D_800BADD8++`). Other values move
   on to the next entry, and that entry becomes the resume point if it is menu-visible.

So "level N in the menu" is the N-th entry without 0xE0 flags. A level made of several entries
(Interrogation → Cia 2 → Hallway and Infirmary → Cia 4) runs back to back.

### Decoded entries (all 20 levels, plus the hidden scenes)

`bank` = geometry bank index (archive 0x8FFCC0). Size = decoded size of the level archive's script
(entry 1) as a rough complexity measure.

| mission (menu #) | list idx | entry VRAM | name | flags | level archive (ROM) | bank | briefing off |
|---|---|---|---|---|---|---|---|
| 0 ICE HIT | intro | 0x8008AB28 | ICE HIT | 0x00 | 0xBD53A0–0xBD7AC0 | 35 | – |
| | 0 | 0x8008A844 | **Lundkwist Base** | 0x00 | 0xBE1E00–0xBEC6B0 | 02 | 0x0025 |
| | 1 | 0x8008A858 | **Subpen** | 0x00 | 0xBD7AC0–0xBE1E00 | 00 | 0x02C2 |
| 1 RECOVER NOC LIST | intro | 0x8008AB3C | RECOVER NOC LIST | 0x00 | 0x82A280–0x82C0F0 | 36 | – |
| | 0 | 0x8008A650 | **Embassy Function** | 0x00 | 0x82C0F0–0x8385D0 | 05 | 0x0D22 |
| | 1 | 0x8008A664 | **Warehouse** | 0x00 | 0x84D3D0–0x854EE0 | 08 | 0x0F68 |
| | 2 | 0x8008A678 | **K.G.B. HQ** | 0x00 | 0x8385D0–0x848260 | 06 | 0x10E3 |
| | 3 | 0x8008A68C | **Security Hallway** | 0x00 | 0x848260–0x84D3D0 | 07 | 0x12C7 |
| | 4 | 0x8008A6A0 | **Sewage Control** | 0x00 | 0x854EE0–0x85DCD0 | 09 | 0x143B |
| | 5 | 0x8008A6B4 | **Escape** | 0x10 | 0x85DCD0–0x864460 | 07 | 0x1588 |
| | 6 | 0x8008A6C8 | (Test Level) | 0x20 | 0x864460–0x86C140 | 06 | 0x1588 |
| | 7 | 0x8008A6DC | **Fire Alarm** | 0x00 | 0x86C140–0x871B80 | 05 | 0x174A |
| 2 CIA ESCAPE | intro | 0x8008AB50 | CIA ESCAPE | 0x00 | none | – | – |
| | 0 | 0x8008A704 | **Interrogation** | 0x10 | 0x8A33B0–0x8A6AA0 | 10 | 0x18E0 |
| | 1 | 0x8008A718 | (Cia 2) | 0x40 | 0x8A6AA0–0x8B2E60 | 11 | 0x1A95 |
| | 2 | 0x8008A72C | (Hallway and Infirmary) | 0x20 | 0x8B2E60–0x8B7350 | 12 | 0x1BBD |
| | 3 | 0x8008A740 | (Cia 4) scene | 0x80 | 0x8B7350–0x8B7910 | 13 | – |
| | 4 | 0x8008A754 | **CIA Rooftop** | 0x00 | 0x8B7910–0x8C5B10 | 14 | 0x1C35 |
| | 5 | 0x8008A768 | (Test Level) scene | 0x80 | 0xBD1260–0xBD2840 | 38 | – |
| | 6 | 0x8008A77C | **Terminal Room** | 0x00 | 0x8C5B10–0x8C8DD0 | 15 | 0x1E77 |
| | 7 | 0x8008A790 | (Test Level) scene | 0x80 | 0xBD2840–0xBD3AD0 | 38 | – |
| | 8 | 0x8008A7A4 | **Rooftop Escape** | 0x00 | 0x8C8DD0–0x8D29F0 | 14 | 0x220A |
| 3 MOLE HUNT | intro | 0x8008AB64 | MOLE HUNT | 0x00 | none | – | – |
| | 0 | 0x8008A7CC | **Station** | 0x00 | 0x871B80–0x87B860 | 17 | 0x233F |
| | 1 | 0x8008A7E0 | **Train car** | 0x10 | 0x8D29F0–0x8DE3A0 | 18 | 0x24B7 |
| | 2 | 0x8008A7F4 | (Test Level) | 0x20 | 0x8E36D0–0x8EE760 | 20 | 0x24B7 |
| | 3 | 0x8008A808 | **Train roof** | 0x00 | 0x8EE760–0x8F4CB0 | 21 | 0x26CB |
| | 4 | 0x8008A81C | (Test Level) scene | 0x80 | 0xBCFBC0–0xBD1260 | 39 | – |
| 4 ICE STORM | intro | 0x8008AB78 | ICE STORM | 0x08 | 0x87B860–0x87D680 | 35 | – |
| | 0 | 0x8008A5C4 | **Subpen** | 0x00 | 0x87D680–0x88A7C0 | 00 | 0x04E9 |
| | 1 | 0x8008A5D8 | **Tunnel** | 0x00 | 0x88A7C0–0x88F810 | 01 | 0x087D |
| | 2 | 0x8008A5EC | (Test Level) scene | 0x80 | 0x88F810–0x890060 | 04 | – |
| | 3 | 0x8008A600 | **Mainland** | 0x00 | 0x890060–0x89DB30 | 02 | 0x098B |
| | 4 | 0x8008A614 | **Gunboat** | 0x00 | 0x89DB30–0x8A2530 | 03 | 0x0C0D |
| | 5 | 0x8008A628 | (Test Level) scene | 0x80 | 0x8A2530–0x8A33B0 | 22 | – |

Other: `D_8008A4E8` pairs: NOC 0xBCC710/0xBCD7D0 (bank 34), CIA 0xBCE950 (37)/0xBCF770 (13), Mole
Hunt 0xBD3AD0/0xBD4880 (21). `D_8008A880` title 0x8FEF90 (33). `D_8008A894` 0x8DE3A0 (05).

The GameShark "level enablers" in `scripts/level_test.sh` are exactly the `+0x02` flag bytes of
these entries: they set bit 0 and keep the type bits (0x11, 0x21, 0x41). 0x8008AB3E, 0x8008AB52
and 0x8008AB66 are the mission entries' flags.

Geometry banks shared between stages: 00 (both Subpens), 02 (Lundkwist Base, Mainland), 05
(Embassy Function, Fire Alarm, 0x8DE3A0, unused 0x8F4CB0), 06 (KGB HQ, NOC [6]), 07 (Security
Hallway, Escape), 14 (CIA Rooftop, Rooftop Escape), 21 (Train roof, Mole Hunt end scenes), 35
(Ice Hit and Ice Storm intros), 38 (CIA scenes). Unused or empty bank slots: 16, 19, 23–32, 40
(size 0).

**About the captures' names:** the menu wraps around and the capture script presses "up" 7 times.
`build/levels/lundkwist` is therefore Ice Hit stage 1, **Subpen** (0xBD7AC0, bank 00), not
Lundkwist Base. `build/levels/cia` is CIA Escape stage 4, **CIA Rooftop** (0x8B7910, bank 14), not
the first level. `embassy` is Embassy Function, as named. `scripts/level_capture.sh <m> <l>` lands
on menu row `(−7 + l) mod rows`.

## 5. Level archive: the 11 entries

The same index → role mapping holds for every level archive (0x8F4CB0 has a 12th text entry). The
loader is `Game_LevelLoop` (0x80047CF8) and the functions it calls; the archive handle is
`D_800A9654`.

| # | loaded by | kept in | content | pointer fix-up |
|---|---|---|---|---|
| 0 | Game_LevelLoop (inline; same code in func_80046F38) | freed | ASCII decimal **geometry bank index** ("05\r\n"); the first 2 chars go through atoi (func_80082BDC) **[verified]** | – |
| 1 | Script_Load func_8005A384 | D_800A98C4 | **"SOOL" script**: `'SOOL'`, u32 size, u16 0x48?, then a table of u32 offsets to routines; the biggest block (0x1C31A Embassy). Registered with the script VM (func_8005B4E0/func_8005B55C, callbacks func_80057140/func_8005B7C0/func_8005A268), started by func_8005B610 → D_800BB068 **[code]**; likely spawns actors and drives the mission logic **[guess]** | none at load (the RAM copy's first 64 bytes equal the ROM data); offsets used relative to the block |
| 2 | func_800566B0(arc, 2) | D_8008AF40 | small table set (0x38–0xFC), same container as #3 **[code]**; content unknown | func_80046778 |
| 3 | Script_Load | D_800A98CC | table set used by the script VM functions (0x80057xxx–0x80060xxx): records of s16 coordinates; probably script-referenced positions/markers **[guess]** | func_80046778 |
| 4 | func_80033430 | D_80093CD0 (records at +0x14 → D_800B9944) | `+0` offset of trailing data, `+8` count N, `+0xC` (→ D_80093CDC), `+0x10` (→ D_80093CE0), then N×0x10-byte records of s16 bounds. Used per frame by func_80033C6C and with the geometry leaves (func_800343F0/func_8003423C). Probably **zones/rooms (visibility or trigger volumes)** **[guess]** | header +0 → base+off |
| 5 | func_80006FB0 | header copied to D_800A9210 (0x2C bytes), grid copied to a heap buffer D_800A9238 | 6 floats (bounds/origin), u32 W, u32 H, u32 N, u32 M, then W×H bytes of cells, then more tables. Used by actor code 0x80006xxx–0x8000Cxxx and 0x8001F4xx. Probably **AI navigation grid + node routing table** **[guess]** | copied, block freed |
| 6 | func_800136D4 | D_800BAB98 | `+4` off, `+8` count, `+0xC` off, `+0x10` count K, then K offsets; s16 xyz triples. Used by 0x8000EDDC–0x80013AD8. Probably **paths/waypoints** **[guess]** | header +4/+0xC and the K offsets → base+off |
| 7 | Script_Load | D_800BA8FC | **dialogue/radio strings**, NUL-separated, `$` = line break **[verified]** | – |
| 8 | func_800551E0 (right after the archive opens) | D_8009C770 | **objectives**: per line one control byte (0x07, 0x02, 0x01, …) and a string **[verified]** text; control byte meaning unknown | – |
| 9 | func_8001DA88 | D_800B9934 (D_800B993C = +0xC table, D_800B9938 = +8 data) | `+0` count N (often equals #4's count), `+4`, `+8` offset, then N offsets. Used by func_8001DF60, func_800240BC, func_80025BD4 **[code]**, role unknown (per-zone lists?) | the N offsets → base+off |
| 10 | func_80029974 and func_80047CAC | freed | **level config**: `+0` max objects (×0xC8-byte object pool), `+2` → func_800303AC, `+4` → func_80035F80, `+6`/`+8` → D_80096010/D_8009600E (default 0x3E4/0x3E8; used next to the fog/segment setup in Game_LevelLoop, probably fog range **[guess]**), `+0xA` → D_800A98C0 (default 50) **[code]** | – |

### Load sequence (Game_LevelLoop, 0x80047CF8) [verified order from code and dma.log]

1. Clear state, wait for the GPU (`Gfx_WaitAllTasksDone`).
2. `Burp_Open(entry+4, entry+8)` → `D_800A9654` (dma.log: 8-byte header, then 11×0x14 table).
3. `func_800551E0`: entry 8 (objectives), needed by the pre-level menu.
4. Pre-level menu if mode bit 1 (`func_8003B390(0x14)`), otherwise `func_80035E04`. It loads 2D
   images from 0x785160 (entries 39, 110, 16, 156, 19, 58 in every capture). This is the wait
   between steps 3 and 5 in the captures (Embassy retrace 2660 → 2880).
5. `func_80033430` entry 4, `func_800136D4` entry 6, `func_8001DA88` entry 9, `func_80006FB0`
   entry 5, `func_80029974` entry 10.
6. Entry 0 → atoi → `Burp_Load(D_800A9840, bank)` → `D_80096000`; `func_80046DF4` relocates the BSP
   tree (§6); `func_800343F0` walks the leaves and allocates per-leaf data (count from entry 4).
7. `func_8002913C`: loads `DOFBB.BIN` (0x310280[433]) → D_80093AD0. It reads the bank's prop table
   (bank header +4) and looks up each name in the DOF archive with `Burp_FindByName`; the indices go
   to D_80093AC8, with a model cache D_80093ACC.
8. `func_8003A618`, `func_80026980`, then `Script_Load` (`func_8005A384`): entries 1, 2, 3, 7. The
   script start then pulls in character models (0x487550), DOF props, music (0x2FE2A0 via
   `Burp_LoadTo`), animations (0x63E510), 0x801470 models and HUD images over the next frames.
9. Per-frame loop. Segment 4 is set to `D_80096000` each frame (`G_MOVEWORD 0xBC001006` at
   0x80048298).

### ROM map of the captured levels

RAM = final address from the game's globals (`level_globals.py`); `dma.log` `dst` values for
deflated blocks are only the temp buffer.

**Ice Hit: Subpen** (`build/levels/lundkwist`), archive 0xBD7AC0–0xBE1E00, opened at retrace 2220,
blocks at 2439:

| # | ROM | stored | decoded | RAM |
|---|---|---|---|---|
| 0 | 0xBD7BA4 | 0x4 | 0x4 (stored) | freed |
| 1 SOOL | 0xBD7BA8–0xBDDC4B | 0x60A3 | 0x23774 | 0x80249E20 |
| 2 | 0xBDDC4C | 0xE0 | 0xFC | 0x801A4A30 |
| 3 | 0xBDDD2C | 0x75E | 0xCF8 | 0x801A4B40 |
| 4 | 0xBDE48C | 0x1572 | 0x3814 | 0x80184C90 |
| 5 | 0xBDFA00 | 0x180 | 0x1DB0 | copied |
| 6 | 0xBDFB80 | 0x11B | 0x13A | 0x80188538 |
| 7 | 0xBDFC9C | 0x353 | 0x6C6 | 0x8026D5A8 |
| 8 | 0xBDFFF0 | 0x52 | 0x5A | 0x80184C20 |
| 9 | 0xBE0044 | 0x1DA1 | 0x395A | 0x80188688 |
| 10 | 0xBE1DE8 | 0xA | 0x10 | freed |
| geo bank 00 | 0x8FFFFC–0x933E0F | 0x33E13 | 0x9CB60 | 0x801A9BB0 (root 0x801B173C) |

**Recover NOC List: Embassy Function** (`build/levels/embassy`): mission intro 0x82A280 at retrace
2105, archive 0x82C0F0–0x8385D0 opened at 2660, blocks at 2880:

| # | ROM | stored | decoded | RAM |
|---|---|---|---|---|
| 0 | 0x82C1D4 | 0x4 | 0x4 (stored) | freed |
| 1 SOOL | 0x82C1D8–0x835A46 | 0x986E | 0x1C31A | 0x8023F050 |
| 2 | 0x835A48 | 0x2B | 0x38 | 0x80188EC0 |
| 3 | 0x835A74 | 0x4EB | 0x870 | 0x801A5058 |
| 4 | 0x835F60 | 0x594 | 0xD6D | 0x80184CD0 |
| 5 | 0x8364F4 | 0x121 | 0x107C | copied |
| 6 | 0x836618 | 0x24B | 0x2D8 | 0x80185A98 |
| 7 | 0x836864 | 0xB24 | 0x1999 | 0x8025B380 |
| 8 | 0x837388 | 0x79 | 0xA0 | 0x80184C20 |
| 9 | 0x837404 | 0x11B3 | 0x2114 | 0x80185D80 |
| 10 | 0x8385B8 | 0xA | 0x10 | freed |
| geo bank 05 | 0x9810D8–0x9AACA1 | 0x29BC9 | 0x8EC10 | 0x801AD600 (root 0x801B323C) |

**CIA Escape: CIA Rooftop** (`build/levels/cia`), archive 0x8B7910–0x8C5B10 opened at 2582, blocks
at 2801:

| # | ROM | stored | decoded | RAM |
|---|---|---|---|---|
| 0 | 0x8B79F4 | 0x7 | 0x8 (deflate) | freed |
| 1 SOOL | 0x8B79FC–0x8C1ABF | 0xA0C3 | 0x2DABC | 0x80255210 |
| 2 | 0x8C1AC0 | 0x88 | 0xA8 | 0x8018EBA8 |
| 3 | 0x8C1B48 | 0x87F | 0xD18 | 0x80282CE0 |
| 4 | 0x8C23C8 | 0x129D | 0x2878 | 0x80184CE0 |
| 5 | 0x8C3668 | 0x21A | 0x3C88 | copied |
| 6 | 0x8C3884 | 0x17F | 0x1AE | 0x801875D8 |
| 7 | 0x8C3A04 | 0x2E0 | 0x612 | 0x801A5428 |
| 8 | 0x8C3CE4 | 0x7B | 0xB0 | 0x80184C20 |
| 9 | 0x8C3D60 | 0x1D92 | 0x3844 | 0x80187798 |
| 10 | 0x8C5AF4 | 0xC | 0x10 | freed |
| geo bank 14 | 0xA8EDA0–0xAAEEAD | 0x2010D | 0x967B0 | 0x801BA870 (root 0x801C478C) |

**Shared blocks loaded at level start** (from `rommap.py`; same in all three captures unless noted):
DOFBB.BIN 0x485394 (0x310280[433]); 2D images 0x785160[39, 110, 16, 156, 19, 58] for the pre-level
menu; per level, DOF props named in the bank's prop table (Embassy: 180–185, 207 = PORTENTREE1,
EMBVIRTUAL, PORTENTREE2, PORTEWCGIRL, PORTEWCMAN, PORTASC; Subpen: 16, 17, 19, …); characters
0x487550[…] (Embassy 19, 41, 37, 17, 2, 10, 3, 7, 22, 12, 11, 13, 14, 40, 9; Subpen 17, 44, 18, 27,
4, …; CIA Rooftop 28, 2, 17, 15, 89); music 0x2FE2A0[5, 6] (Embassy) / [3] (Subpen); animations
0x63E510[…] and HUD images streamed in during play. Boot-time blocks (before the menu): audio
0x94440[1, 3], fonts 0x7FF7C0[0, 3], the animation table 0x63E510[254] and the entry tables of
every global archive.

## 6. Pointers and relocation

| data | how pointers are stored | resolved by |
|---|---|---|
| Burp entry table | offsets from the archive start | Burp_Open adds the ROM start (RAM copy only) |
| geometry bank (0x8FFCC0[n]) | `0x04xxxxxx` = segment 4 + offset into the bank | `func_80046DF4` (Geo_Relocate): `hdr[0]` and `hdr[1]` → `base + (p & 0xFFFFFF)`; `*hdr[0]` → BSP root D_80096004; then a recursive walk of `{+0 left, +4 right, +8 s16 type}` nodes (`func_80046D04`/`func_80046C84`). A type-3 node's `+0` points to a struct whose `+0xC` is a nested tree. A top byte of 0x80 means already relocated. Display lists keep `0x04…` and segment 4 is set to the bank base each frame. |
| geometry bank header | `+0` → `{root ptr, bounds…}`, `+4` → **prop table**: 0x2C-byte records `char name[16]` (`"SUB1ETAGERE.DOF"`), `s32 ?` (0), `s32 x, y, z`, 3 × s32 (rotation?), terminated by an empty name (Subpen: 36 records) **[verified layout of name/xyz; rest guess]** | name looked up in the DOF archive by `func_8002913C` |
| character / item models (0x487550, 0x801470) | header pointers `0x01xxxxxx` | `func_80053388`: `base + (p & 0xFFFFFF)`; drawing sets segment 1 (and 0xA/0xB) to the model |
| animation table (0x63E510[254]) | `+4` offset from the table start | `func_80054680` adds the table base |
| level entries 2, 3, 4, 6, 9 | u32 byte offsets from the block start in a header / offset table | per-loader loops (table above) |
| SOOL script (entry 1) | offsets | used relative to the block at run time (not rewritten at load) **[code]** |

So a replacement block is position independent if it keeps these conventions: offsets from its own
start, `0x04`-tagged pointers in geometry and `0x01`-tagged pointers in models.

## 7. Injecting a modified block (what it takes)

1. Produce the decoded block (same format), then either keep it stored (packing 0, stored size =
   decoded size) or raw-deflate it (`zlib.compressobj(9, zlib.DEFLATED, -15)`, packing 4).
2. Point the archive entry at it. Entry `+0x0C` is a u32 offset from the archive start, so the data
   can sit anywhere after the archive: in the 80 KB of 0xFF padding at 0xBEC6B0, or past 12 MB.
   Update `+0x00` (decoded size; this is the heap allocation size) and `+0x04` (stored size).
   Nothing else indexes the data. The table's `+8` end address only matters for named archives.
3. **The ROM hash blocks an edited ROM file**: `src/main/main.cpp` checks XXH3 `0xD64AE25F3173CE5C`.
   Mods must be applied at run time. Two options:
   - Patch the ROM image in memory after validation (append the new data, rewrite the 0x14-byte
     entry), so the game's own `Rom_Read`/inflate see it. This needs a hook in the runtime's ROM
     loading; nothing in the game changes.
   - Patch the RAM copy of the entry table after `Burp_Open` returns, for example hook
     Game_LevelLoop at 0x80047DC8 (right after `Burp_Open`, `$v0` = handle) or `Burp_Load` entry.
     Set the entry's `+0x0C` to a **RAM address** (0x8xxxxxxx) holding the mod data, with packing
     0 and both sizes equal. `Rom_Read` and the inflater memcpy from RAM sources. This needs no
     ROM change.
4. Heap room: the heap is BSS end (0x800BB240) to `osMemSize`. The recomp reports 8 MB
   (`0x80000318` = 0x800000), and the captures peak around 0x8034xxxx, so a bigger geometry bank
   (the largest stock bank is 0xA0980 bytes) fits easily. A packed block also needs a temporary
   input buffer: `Burp_Load` needs more than 0x8800 bytes free in one block and uses
   `min(largest − 0x8000, stored size)`. Smaller buffers work and are refilled from ROM.
5. To add a new level rather than replace one: add a stage entry, or repoint an existing one
   (`+4`/`+8`). The stage tables are fixed-size in the data segment and the code indexes
   `D_8008AB28` with constant strides, so repointing is easier than adding. A new geometry bank can
   reuse an empty slot of 0x8FFCC0 (16, 19, 23–32, 40) and be referenced by writing its number in
   level entry 0.

## 8. Open questions

- Semantics of entries 2, 3, 4, 5, 6, 9: zones, nav grid, paths are guesses from data shape and
  callers. The SOOL bytecode (actor spawns, item/character ids) is not decoded; it is the next
  target for object-placement mods.
- Which code picks the character models (0x487550 indices) for a level: requests come through
  `func_80053CDC`/`func_80053648`/`func_800541F8`, presumably from the script; not traced.
- Stage flag 0x10, mission flag 0x08, stage `+0x13` (D_800898F2) and the `D_8008A4E8`/`D_8008AB14`
  tables are only partly understood.
- Contents of 0x801470 (segment-1 models: items/weapons?) and the exact role of DOFBB.BIN.
- Suggested instrumentation (not added): log the return value of `Burp_Load` (0x80068CF0) with
  `(handle->romStart, index)`, for example a `Rom_Read`-style hook at its `jr $ra`. That gives exact
  final RAM addresses for every block, including shared ones, without the heuristic search in
  `rommap.py`.
