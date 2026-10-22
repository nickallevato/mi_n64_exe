# Mission: Impossible (N64) — static recompilation

Recompiles Mission: Impossible (USA, sha1 `7546d5df1fc93fed8205a066f1333fd7dd50c1e1`)
into C with [N64Recomp](https://github.com/N64Recomp/N64Recomp). There is no existing
decomp of this game, so the function list is generated from a splat disassembly instead of an ELF.

## ROM layout (main segment)

| What | ROM | VRAM |
|---|---|---|
| entrypoint | `0x1000` | `0x80000400` |
| `.text` (CPU code, 1697 funcs) | `0x1000`–`0x84A60` | `0x80000400`– |
| rspboot microcode | `0x84A60`–`0x84C90` | |
| data / rodata | `0x84C90`–`0x94440` | |
| bss (`0x27A00`) | — | `0x80093840` |
| assets (no Yay0/Yaz0/MIO0, no plain code found) | `0x94440`–`0xC00000` | |

`game_main` is at `0x8000E090`.

## Setup

```sh
# ROM (git-ignored)
mkdir -p rom && cp "<path>/Mission Impossible (USA).z64" rom/

# tools (git-ignored)
git clone --recurse-submodules https://github.com/N64Recomp/N64Recomp.git tools/N64Recomp
cmake -S tools/N64Recomp -B tools/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tools/N64Recomp/build
git clone https://github.com/shygoo/n64sym.git tools/n64sym && make -C tools/n64sym
python3 -m venv tools/venv && tools/venv/bin/pip install 'splat64[mips]'

# disassemble
cd split && ln -sf "../rom/Mission Impossible (USA).z64" baserom.z64 \
  && ../tools/venv/bin/python -m splat split missionimpossible.yaml && cd ..

# recompile (game code -> RecompiledFuncs/, audio microcode -> rsp/)
cmake -S lib/N64ModernRuntime/N64Recomp -B build/n64recomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/n64recomp --target N64Recomp RSPRecomp
scripts/recomp.sh
```

## Playing

Status: boots, runs through menus and missions at 60 FPS, with keyboard/mouse and an analog camera. See
[docs/ROADMAP.md](docs/ROADMAP.md).

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Move | W A S D | Left stick |
| Turn Ethan / camera | Mouse | Right stick (General > Analog Camera) |
| First-person view / aim (hold) | Right mouse, E | Right trigger |
| Shoot / punch | Left mouse, Q | Left trigger |
| Action / jump | Space | A |
| Inventory (weapons) | Left Shift | X |
| Crouch, item inventory | Left Ctrl, Down | Right shoulder |
| Sidestep while aiming | Left / Right arrows | Y / B |
| Zoom while aiming | Up | Right stick click |
| Camera mode | (unbound) | Left shoulder |
| Rotate camera | J / L | D-pad |
| Pause / IMF menu | Enter | Start |

Everything can be rebound under Controls. Settings that matter here:
- **Graphics > Frame Rate**: 60 FPS (default) or Original (30). The game times its logic by the video clock, so
  60 FPS doesn't change game speed. Resolution, MSAA and aspect ratio are RT64's options on the same tab.
- **General > Mouse Sensitivity**: 0 turns mouse turning off (and releases the cursor).
- **General > Analog Camera / Analog Camera Speed**.

## Building and running the app

Uses the BanjoRecomp app layout: RT64 renderer, N64ModernRuntime, and RecompFrontend (launcher, config
menus, input, mods). Libraries are pinned submodules under `lib/`.

```sh
git submodule update --init --recursive
sudo apt-get install cmake ninja-build clang lld libsdl2-dev libgtk-3-dev libfreetype-dev
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/app
MI_ROM="rom/Mission Impossible (USA).z64" build/app/MIRecompiled   # MI_ROM auto-boots; omit to use the launcher
```

Dev tools:
- `scripts/capture.sh` runs the game in a headless sway compositor (real GPU, invisible), takes screenshots with
  grim and injects controller input through `MI_INPUT`.
- `scripts/stacks.py` prints the recompiled game threads' call stacks (gdb).
- `scripts/find_spins.py` lists busy-wait loops that need yield hooks.
- `MI_STATS=1` prints graphics tasks and VIs per second; `MI_WATCH=addr[:n],...` prints game RAM each second.

## Runtime fixes

- **Busy-wait loops** (`[[patches.hook]]` in `mi.us.toml`): the game spins on globals that interrupt-driven
  threads update. ultramodern schedules game threads cooperatively, so those loops get a yield hook.
- **libultra functions n64sym missed** are named in `symbols/manual.txt` so the runtime replaces them (AI, SI,
  EEPROM long read, ...). An unreplaced libultra function that waits on hardware hangs or crashes.
- **Wayland**: on a Wayland session the app uses native Wayland. Through XWayland, NVIDIA's Vulkan presentation
  stalls (the game runs at ~0-2 fps).
- **Hidden windows**: FIFO presentation blocks while the window is hidden (NVIDIA + Wayland), freezing the game.
  `patches/deps/plume-prefer-mailbox.patch` (applied to RT64's plume by CMake) uses mailbox presentation when
  available; a hidden window keeps running at 60 FPS with normal CPU/GPU use.
- **Startup**: SDL audio is initialized before the file-dialog library, and the dialog uses the XDG portal
  instead of in-process GTK; either ordering issue or GTK init could stall startup for 25+ s.

## Symbols

- `symbols/libultra.txt` — libultra functions found by `n64sym -s`. n64sym reports
  jal-discovered addresses with a `0x7` prefix for this ROM (bug: base `0x7FFFF400 & 0xF0000000`);
  these were corrected to `0x8`. Internal `*_text_XXXX` labels and false positives were removed.
- `symbols/manual.txt` — hand-identified names; overrides `libultra.txt`.

Correct libultra names matter: N64Recomp skips/replaces functions by name
(`src/symbol_lists.cpp`) so that N64ModernRuntime can provide its own implementations.

## Next steps toward a running port

1. Build a runtime project on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
   (librecomp + ultramodern) with [RT64](https://github.com/rt64/rt64) for graphics —
   [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) is the reference.
2. Identify the graphics/audio microcodes the game uses (RT64 needs a supported F3DEX variant;
   the audio ucode needs RSPRecomp).
3. Identify remaining unnamed libultra functions as runtime errors surface.
