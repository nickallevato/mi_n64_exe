# Porting Playbook: the next N64 recomp

A handoff for the agent team starting the next port. This repo (Mission: Impossible, N64 USA) is the reference
implementation: it went from an empty repo to a 60 FPS, keyboard-and-mouse Linux release in two days with no
decomp to lean on. Copy its structure, scripts and fixes; don't rediscover them.

Read in this order: this file, then [README.md](../README.md) ("How it works" and "Dev tools"), then
[ROADMAP.md](ROADMAP.md), then `mi.us.toml`, `CMakeLists.txt` and `src/main/main.cpp`.

## Ground rules

- **No ROM data in git, ever.** `rom/`, `*.z64`, splat output (`split/asm/`, `split/assets/`), `RecompiledFuncs/`,
  `rsp/` and generated `syms.toml` are ignored. Commit only configs, symbol names, tools and our own code.
  Screenshots and footage of the game are the only game-derived files allowed.
- **Say plainly that AI made it.** The README has a "Yes, AI wrote this" section and a thank-you to the people whose
  hand-built work it stands on (N64Recomp, N64ModernRuntime, Zelda64Recomp, RT64, RecompFrontend, BanjoRecomp,
  splat, n64sym, the decomp scene). Keep both. Never call it "AI-assisted".
- **License: GPL-3** (`COPYING`), like Zelda64Recomp. Ship license text for every bundled font
  (`assets/OFL-*.txt`, `assets/promptfont/LICENSE.txt`).
- **Commit each milestone; push every commit to both remotes** (Gitea `origin`, GitHub `github`).
- **Verify by running the game**, not by reading code: `scripts/capture.sh` screenshots it headless. A feature is
  done when it's been seen working (and A/B-compared against a run without it when the effect is subtle).
- **Ask the user short, numbered, plain-language questions** when they need to test something or choose.

## The stack (pinned)

| Dependency | Pin | Why |
|---|---|---|
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (includes N64Recomp, RSPRecomp) | `ca568b6`, branch `gamemodes` | librecomp + ultramodern (threads, VI, audio, saves) |
| [RT64](https://github.com/rt64/rt64) | `6f1c2d9` | HLE renderer: hi-res, widescreen, MSAA, frame interpolation |
| [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) | `d0d90ba` | Launcher, config menus, input remapping, mod UI (BanjoRecomp's app layer) |

Local fixes to these live in `patches/deps/` and are applied by `CMakeLists.txt` at configure time. Carry all
four over; they are game-agnostic:

- `plume-wait-for-last-present.patch` + `rt64-skip-present-while-hidden.patch`: vsync (FIFO) presentation that
  doesn't freeze the game when the window is hidden (NVIDIA + Wayland).
- `rt64-lookat-bounds.patch`: crash in RT64's frame matching on out-of-range look-at entries.
- `N64ModernRuntime-keep-rdram-on-quit.patch`: game threads are never joined, so keep RDRAM mapped on quit.
  Before this, quitting crashed in 4 of 5 tries.

## Bootstrap pipeline

1. **ROM**: put the user's dump in `rom/`, record its sha1 in the splat yaml and the README, and its XXH3-64 in
   `main.cpp` (`rom_hash`).
2. **Map the ROM**: find the entrypoint, the main `.text` range, the RSP microcodes and where data starts. For each
   microcode, look for its ID string (e.g. `RSP Gfx ucode F3DEX...`). The graphics ucode must be one RT64
   supports (F3DEX/F3DEX2/S2DEX family). Audio ucode (`aspMain`/`n_aspMain`) goes through RSPRecomp. A custom
   graphics ucode is a stop sign: find that out on day 1.
3. **Disassemble with splat** (`split/<game>.yaml`, compiler guess KMC or GCC). If the game has overlays or
   compressed code (common in Rare games), map every code segment and its load address now: each one needs its
   own section in the recomp config and runtime registration (see `src/main/register_overlays.cpp`).
4. **Name libultra** with `n64sym -s`, cleaned into `symbols/libultra.txt`. n64sym can report jal-found
   addresses with a `0x7` prefix (base `0x7FFFF400` bug); correct them to `0x8`. Drop `*_text_XXXX` labels and
   false positives. Correct names matter: N64Recomp replaces or skips libultra functions by name so the runtime can
   supply its own. **Any hardware-waiting libultra function left unnamed hangs or crashes the game**, so add the
   ones n64sym misses (AI, SI, EEPROM, PI...) to `symbols/manual.txt`.
5. **Generate symbols and recompile**: `scripts/recomp.sh` runs `gen_syms.py` (splat asm → `syms.toml`), N64Recomp
   (`<game>.us.toml`) and RSPRecomp (`n_aspMain.us.toml`, with the audio dispatch table as indirect branch targets).
6. **App**: copy `CMakeLists.txt`, `src/main/`, `src/game/`, `include/`, `patches/`, `assets/`, rename `MI`/`mi_`,
   and point it at the new game. The app is modeled on BanjoRecomp's.

## Milestones, in the order they actually fell

From MI's git log, about two days end to end:

1. Recompile all main-segment code; app boots to the game's first screen.
2. Reach the menus: fix stalls (yield hooks), controller detection, startup ordering.
3. Reach gameplay; add a configurable 60 FPS.
4. Keyboard + mouse, analog camera, controls map, README.
5. Stability: hidden-window running, clean quit, audio crash, frame pacing, 60 FPS movement smoothing.
6. Level/asset RE, data overrides (first mod), debug level warp, developer Debug tab.
7. Linux release (AppImage + tar.gz built in an Ubuntu 22.04 container).

## Gotchas MI hit (expect most of them again)

- **Busy-wait loops**: games spin on globals that interrupt-driven threads update. ultramodern schedules threads
  cooperatively, so each spin needs a yield (`[[patches.hook]]`). `scripts/find_spins.py` finds candidates;
  `scripts/stacks.py` (gdb) shows where a stalled game is stuck.
- **Frame limiters**: the game's own limiter (MI: "at least 2 VIs per frame") is where 60 FPS comes from. Make the
  minimum configurable with a hook. Check game speed stays the same at 30 and 60 (logic on the VI clock or not).
- **30 Hz animation at 60 FPS**: root motion that steps once per keyframe stutters (move, stop, move). Add the
  partial step for the fraction of a keyframe played.
- **Analog camera / mouse**: time stick turning by game time, not wall clock. Mouse aim works best by writing the
  player's yaw directly and by adding deflection in each special camera mode (scopes, turrets).
- **Startup stalls**: initialize SDL audio before the file dialog, and use the XDG portal for the dialog
  (`NFD_PORTAL`), not in-process GTK. Either one could stall startup for 25+ s.
- **Wayland + NVIDIA**: use native Wayland; through XWayland, Vulkan presentation stalls to 0-2 FPS.
- **Audio**: the audio manager's minimum frame size compare is unsigned in the runtime; clamp with a signed
  compare or a negative size becomes a ~4 GB buffer (`std::bad_alloc`).
- **Modified ROMs are rejected** by the app's hash check, so mods load as runtime data overrides
  (`src/game/overrides.cpp`, hooks after the game's archive decoder).

## Tools to copy

| Tool | Use |
|---|---|
| `scripts/capture.sh` | Run headless in sway (real GPU), screenshot with grim, inject input via `MI_INPUT` |
| `scripts/level_test.sh` | Unlock everything, enter a level, random-input soak |
| `scripts/stacks.py` | Call stacks of the recompiled threads (gdb) |
| `scripts/find_spins.py` | Candidate busy-wait loops |
| `scripts/dumpram.sh`, `scripts/ramdiff_session.sh` | Find game state by diffing RAM |
| `MI_STATS`, `MI_WATCH=addr[:n]`, `MI_FRAMETIMES=file` | GFX tasks/VIs per second, watch RAM, frame pacing log |
| `MI_LEVEL`, Debug tab | Boot straight into a level |
| `scripts/release/build_linux.sh` | Release build in Docker |

## Feature checklist (Zelda64Recomp parity)

Boot, menus, saves, audio → gameplay → 60 FPS → hi-res/widescreen/MSAA → keyboard + mouse → analog camera →
frame pacing and clean quit → controls docs → developer tools and level warp → release packaging → mods
(data overrides, texture packs, mod tool). Track it in `docs/ROADMAP.md` with checkboxes and what was measured.

## Next targets

Picked by the user on 2026-10-22. Status comes from fan-run recomp lists (recompsoftware, recompedia,
readonlymemo), not checked repo by repo. Re-check each one before starting.

1. **Wave Race 64**: next up. An early port is listed but not playable yet (unverified). Decomp status unknown.
   First: ROM map and microcode check (step 2 above).
2. **Blast Corps**: after Wave Race. No port or decomp found. A Rare game, so expect compressed code segments.
3. **Kirby 64: The Crystal Shards**: on the list. A work-in-progress decomp exists
   ([Kirby64Ret/kirby64](https://github.com/Kirby64Ret/kirby64)) for names; one list says a port is in
   development (unverified).
4. **Lode Runner 3-D**: deferred. No decomp or port found.

Already taken (skip unless the plan is a clearly better port): Jet Force Gemini, GoldenEye 007, Majora's Mask,
Star Fox 64, Mario Kart 64, Paper Mario, Pokemon Snap, Harvest Moon 64, Bomberman 64/Hero, Goemon, Snowboard
Kids 1/2, Dr. Mario 64, Donkey Kong 64, Glover, Body Harvest, Shadows of the Empire, Pilotwings 64, Conker.
