# Levels: overview

Groundwork for level mods: how *Mission: Impossible* stores, finds and draws its levels. Details:

- [levels-loading.md](levels-loading.md): the level table, the "Burp" archive format, the load sequence, ROM maps,
  pointer relocation and what it takes to inject a modified block.
- [levels-geometry.md](levels-geometry.md): the geometry format (k-d tree of cells, display lists, vertex and
  texture formats) and the OBJ exporter.

## In short

- **Assets are compressed.** ROM 0x94440-0xBEC6B0 holds 52 "Burp" archives: `'Burp'`, a u32 count, then 0x14-byte
  entries (decoded size, stored size, name offset, data offset, packing). Packing 4 is raw DEFLATE, which Python's
  `zlib.decompressobj(-15)` decodes exactly. The game's decoder (`Inflate_FromRom`) is a port of gzip's inflate.
- **The level table** is `D_8008AB28`: 5 missions in menu order, each pointing to a list of 0x14-byte stage entries
  (name string, flags, ROM start/end of the level's archive, briefing text). Flag 0x01 = unlocked; 0x80 = cutscene;
  0x20/0x40 = continues the previous level; hidden entries include several unused "Test Level" stages.
- **A level** is one 11-entry archive (geometry bank number, the "SOOL" level script, dialogue, objectives, a config
  block, and entries whose roles are still open) plus one geometry bank from the 41-entry archive at 0x8FFCC0.
- **Geometry** is one blob per bank, mapped as display list segment 4: a two-level k-d tree whose leaves are cells
  (bounding box + inner tree) and F3DEX display lists with baked vertex colours and RGBA16/32 textures. The blob
  also lists the props (`.DOF` models such as doors) with their positions.
- **Blocks hold offsets, not addresses**, and the loader relocates them, so a block can load anywhere in the heap.

## Tools

All python3 (`python3 -I`), run from the repo root:

| Script | What it does |
|---|---|
| `scripts/levels/level_export.py` | Export a level's geometry to OBJ + MTL + PNG textures, from the ROM (`--rom ROM --entry N`, `--all`) or a RAM dump |
| `scripts/levels/level_table.py` | Decode the mission and stage tables (names, ROM ranges, geometry bank, flags) |
| `scripts/levels/burp.py` | List the Burp archives; `--extract DIR` writes every decoded entry |
| `scripts/levels/rommap.py <capture>` | Annotate a capture's ROM read log with archive, entry and role |
| `scripts/level_capture.sh` | Capture a level headlessly: ROM read log (`MI_DMA_LOG`), RAM dump and screenshots |

## Loading edited data (overrides)

`MI_OVERRIDE_DIR=dir` makes the game load `dir/rom_XXXXXXXX.bin` in place of the archive entry whose data starts at
ROM address `XXXXXXXX` (as listed by `scripts/levels/burp.py`). The file is the decoded entry in big-endian byte order
and, for now, must be the same size as the original. Hooks at the end of `Burp_LoadTo` and `Burp_Load` copy it over
the decoded data before the game relocates or reads it (`src/game/overrides.cpp`); the game log says when one is
applied. `scripts/levels/level_mod.py` makes same-size geometry edits:

```sh
python3 -I scripts/levels/level_mod.py --rom "rom/Mission Impossible (USA).z64" --entry 5 --tint 1.0,0.35,0.35 -o build/mods/embassy_red
MI_OVERRIDE_DIR=build/mods/embassy_red build/app/MIRecompiled
```

Verified in game (Embassy Function): `--tint` recolours all level geometry; `--raise-above 450 --by 500` makes the
ballroom walls and doorway taller. Props (piano, vases) are separate models and stay unchanged, and collision is
separate data, so geometry edits are visual only.

### Level warp (`MI_LEVEL`)

`MI_LEVEL=<mission>,<stage>[,<difficulty>]` boots straight into a stage, skipping the logos, intro, title, save
select, difficulty and mission menus, the pre-mission OBJECTIVES/BRIEFING/TEAM screens and the objectives screen at
the start of the stage. Indices are those of `scripts/levels/level_table.py` (missions in menu order, stage = index in
the mission's stage list, hidden entries included); difficulty 0 = POSSIBLE (default), 1 = IMPOSSIBLE. A stage name
also works: `MI_LEVEL="Train roof"` (first match, case-insensitive) or `MI_LEVEL="Ice Storm/Subpen"`.

```sh
MI_LEVEL=1,0 MI_OVERRIDE_DIR=build/mods/embassy_red build/app/MIRecompiled   # Recover NOC List: Embassy Function
```

The stage is on screen within 3 seconds of boot; the level's own scripted opening (camera flythrough, dialogue) still
plays. How it works (`src/game/level_warp.cpp`): a hook in `Game_ThreadMain`, before the front end, sets the state
the menus would with no save device (all save slots unused, so the warp never writes to the EEPROM), the
difficulty, `CurMission` and `CurStage`, then calls `Game_RunMission` like the mission menu does. The rest of the
mission plays normally (later stages get their usual menus); when it ends or Ethan dies, the game goes back to the
logos and title screen. Continuation stages (flags 0x20/0x40) load and play on their own, but without any state the
previous part would have set up (not checked in detail); a scene (0x80) plays and then the mission continues with the
next stage. An invalid value is logged and the game boots normally.

## Toward a level mod

What works: decoding every archive, exporting any level's geometry with textures, knowing where each level's blocks
live, and loading edited same-size geometry in game. What's next:

1. An encoder from an edited mesh back to the geometry format (cells, k-d trees, display lists that cull on their
   bounding box, 32-vertex batches, texture loads), then recompress.
2. Overrides of a different size: patch the entry's decoded size when its archive is opened, so the game allocates
   room for the edited block.
3. Collision (not found yet), prop models (`.DOF`), and the SOOL script format for objectives and events.
4. The N64Recomp mod pipeline (`.nrm` mods) to package it.
