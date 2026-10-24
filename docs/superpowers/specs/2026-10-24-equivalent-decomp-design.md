# Equivalent decomp of Mission: Impossible (N64): design

Status: draft for review, 2026-10-24.

## Why

The recomp runs the game, but its C (`RecompiledFuncs/`) is machine-translated register code nobody can read or
change as game logic. The user wants **usable, workable, code-only source** for the game so they can:

1. build new levels with new maps and geometry,
2. then make a whole expansion pack of new missions (original maps and scenarios inspired by spy films),
3. and maybe, much later, multiplayer (deferred; network play is off the list for now).

This document covers only the first of four projects: the decomp itself. The other three (level pipeline, first new
level, expansion pack) get their own designs later and build on this one.

## What it is

An **equivalent decomp**: hand-written C, one function at a time, that behaves the same as the original but does not
have to compile to the same bytes. Each decompiled function replaces its recompiled version in the running game and
is checked there. In the end the decompiled C *is* the game's source; the recompiled C is gone for every function
that matters.

The source contains only code. Assets keep coming from the player's own ROM, as now.

## Goals

- Readable C for the game systems the level and expansion work needs, in this order of importance:
  1. **Mission logic**: the SOOL script VM (functions around 0x80057xxx–0x80060xxx, `Script_Load`), objectives,
     triggers, alarms, and what the script does when it spawns actors.
  2. **Objects and enemies**: the object pool (`Object_UpdateAll`, `Object_UpdateMovement`, 0xC8-byte objects),
     guard AI, doors and props (`.DOF`).
  3. **Level loading**: `Burp_*`, `Level_Load*`, `Script_Load`, the load sequence in `docs/levels-loading.md`, the
     level table (`D_8008AB28`) and `Game_RunMission` / `Game_MissionSelect` / `Mission_BriefingMenu`.
  4. **Geometry, collision and visibility**: the k-d tree and cell walk, display list submission, and collision
     (not found yet).
  5. **Player, camera, input, HUD**: lower priority; touched only where the above needs it. (These are also what
     multiplayer would need later.)
- Real names, structs and enums for everything decompiled, in shared headers.
- A written spec for each data format the decomp uncovers (SOOL bytecode, collision, object records), in `docs/`,
  because the level pipeline is built from those specs.
- A progress report: functions and instructions decompiled, per system and overall.

## Non-goals

- **Byte matching.** No matching compiler, asm-differ or permuter work. Equivalence is checked by behavior.
- **100% coverage.** libultra, audio, physics details, menus and anything the level/expansion work doesn't touch can
  stay recompiled until there's a reason. (libultra is replaced by the runtime anyway.)
- **A native port without the recomp runtime.** The decomp runs inside the existing app.
- **Building a real N64 ROM** from the source. Nice if it falls out later, not a goal.
- **Network multiplayer.** Deferred.

## Anti-goals (failure even if it "works")

- **The shipping app breaks or changes behavior.** Players must never get a decompiled function that hasn't passed
  its checks. A wrong function must be easy to switch off.
- **ROM data in git.** Same rule as today: no asm, no splat output, no extracted data in the repo. Decompiled C
  written by us is fine.
- **Code that's still unreadable.** C that is just the asm with C syntax (`v0 = a0 + 0x14`, no types, `func_` names)
  doesn't count as decompiled. A function is done only when it's readable by the bar below.
- **Losing the QOL features.** The 28 `[[patches.hook]]` entries in `mi.us.toml` hook into the middle of recompiled
  functions (`Game_LevelLoop`, `Player_Control`, `Burp_Load`, `Camera_SniperControl` and 18 others). When one of
  those functions is decompiled, its hook must move into the C as an explicit call, and the feature must still
  work.

## Constraints (from the project)

- Same ground rules as `docs/PORTING_PLAYBOOK.md`: GPL-3, say plainly that AI made it ("AI-made", never
  "AI-assisted"), commit each milestone, push every commit to both remotes, verify by running the game.
- Stack stays pinned (N64ModernRuntime `gamemodes`, RT64, RecompFrontend).
- The splat disassembly in `split/` and the symbol map in `syms.toml` / `symbols/manual.txt` are the inputs.
- The user directs; AI does the work. The user tests things when asked and answers short, numbered,
  plain-language questions.

## How it works (decided)

### Swapping functions in: the patches pipeline

This is the open roadmap item "Patches pipeline: `patches/` C compiled for MIPS, recompiled with `patches.toml`
(like Banjo's PatchesLib)", and the decomp is built on it. BanjoRecomp is the working reference
(`tools/BanjoRecomp/patches/Makefile`, `patches.toml`, `RECOMP_PATCH`):

- Decompiled C is written as **N64 C**: game structs and pointers, libultra types, as the original developers would
  have written it.
- It is compiled with clang for MIPS (`-target mips -mips2 -mabi=32`), linked into a patch ELF, and recompiled by
  N64Recomp in patch mode. Each decompiled function is marked as replacing the original of the same name, and the
  runtime calls it instead of the recompiled one.
- This keeps the source free of recomp details (no `ctx->r4`, no `rdram` offsets) and keeps the option of building
  it for real hardware later.

### Checking a function: behavior, not bytes

A decompiled function passes when it does the same thing as the original, checked in the running game:

- **Primary check, side-by-side calls:** a test mode that, when the game calls a function under test, runs the
  original recompiled version and the decompiled version from the same starting state (registers and RAM) and
  compares the results (return values and RAM written). Any difference is logged with the function, the call and the
  bytes that differ. Running the levels under this mode exercises each function on real game states.
- **Secondary check, play-through:** the existing tools (`scripts/level_test.sh` for all 20 levels, `capture.sh`
  screenshots, `ramdiff_session.sh`) run with the decompiled functions switched on, to catch anything the
  side-by-side check misses (crashes, visual differences, functions that are hard to run twice).
- **Per-function switch:** each decompiled function can be turned on or off without rebuilding (a list read at
  startup), so a bad one can be found by bisecting and switched off.
- **Both versions must stay callable.** In the stock pipeline a patch replaces the original at link time
  (`RECOMP_FUNC` is a weak symbol in N64Recomp's `recomp.h`), so the original is gone. The side-by-side check and
  the on/off switch need a dispatch layer or a patched N64Recomp that keeps the original reachable. How is the
  builder's call.
- **Coverage (decided by the user): accept what the automatic tests reach.** No record-and-replay tool and no
  required manual play-throughs. Code the 20-level random-input runs never reach (objectives, alarms, cutscenes,
  mission endings, rarer AI states) is checked by careful reading against the asm instead, and marked as such in
  the progress report so it's clear which functions were checked in game and which by reading only. The builder
  may widen the automatic runs cheaply (longer runs, `MI_LEVEL` warps into later stages, scripted inputs that reach
  known events) where that's easy.

### Release builds (our call)

Release builds stay fully recompiled. Decompiled functions, the dispatch layer, the side-by-side checker and the
on/off list exist only in developer builds until the expansion work needs decompiled code to ship. A function that
behaves the same gains players nothing, so shipping it early only adds risk.

### Game data tables (our call)

The original data tables (the level table `D_8008AB28`, script and opcode tables, object and AI tables, strings)
stay in the ROM. Decompiled C refers to them as typed externs at their original addresses and does not copy them
into git. A system's tables count as done when their layout is typed in a header and written up in `docs/`, not
when their contents are in C. New levels and missions (later projects) are added through our own tables in code,
which the decompiled lookups consult alongside or in place of the ROM's, so the expansion builds on this decomp
without putting ROM data in git.

### What "done" means

**A function is done** when:

1. it compiles in the patches pipeline and replaces the original,
2. it passed the side-by-side check on every call seen across a test run of all 20 levels, or, if those runs
   never reach it, it was checked by reading it against the asm and is marked "checked by reading" in the
   progress report,
3. the 20-level play-through shows no crash or visible change with it on,
4. it reads as real code: named, typed, using shared structs, no leftover register-style temporaries, a short
   comment where the logic isn't obvious,
5. any `[[patches.hook]]` that was in it now lives in the C and its feature was seen working.

**A system is done** when every function on its list is done, its data formats are written up in `docs/`, and the
progress report shows it complete.

### Order of work

1. **Pipeline first:** patches build, function replacement, the side-by-side checker and the on/off list, proved on
   a few small functions (the `Burp_*` family is a good size).
2. **Then systems in the goal order above:** mission logic, objects and enemies, level loading, geometry and
   collision, player/camera as needed.
3. Inside a system, work from the leaves of the call graph up, so each function's callees are already readable.

### Where it lives

- `decomp/` at the repo root holds the game source: `decomp/src/<system>/*.c` and `decomp/include/` for shared
  structs, enums and globals. (Our call: `src/` is the native app and `patches/` holds app-side QOL patches; the
  game's own source gets its own home.)
- The progress report is a script under `scripts/` that reads which functions `decomp/` replaces and sums
  functions and instructions per system from `syms.toml`.
- Data-format specs go next to the existing level docs in `docs/`.

## Later projects (not designed here)

For context only; each gets its own design:

- **Level pipeline:** Blender (driven by AI through Python, headless) → exporter to MI's geometry, collision and
  texture formats; object and trigger placement; lifting the same-size limit on data overrides. A private stress test:
  convert a GoldenEye map extracted from the user's own GoldenEye ROM into MI's format. That data stays local and
  gitignored and never ships; shipped maps are original.
- **First new level:** prove the whole chain end to end.
- **Expansion pack:** more original levels, mission list entries and briefings, shipped as a pack that runs on the
  player's own ROM.

## Left to the builder

- Tooling for first drafts (m2c on the splat asm, decomp.me-style workflow, AI loops), where the tools live
  (`tools/` is gitignored).
- Header sources for libultra types (ultralib, the runtime's `ultramodern` headers, or our own minimal set).
- How the side-by-side checker captures and restores state, how it handles functions that call others or have side
  effects outside RAM, and how often it samples hot functions.
- Whether play-through runs can be made deterministic enough to compare RAM frame by frame.
- The dispatch mechanism that keeps original and decompiled versions both callable.
- How the on/off list is stored and exposed in developer builds (env var, file, Debug tab).
- How each system's function list is drawn up (call graph from the system's known entry points).
- Naming conventions, file split inside each system, and the progress report's format.
