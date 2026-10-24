# Decomp: readable source for the game, one function at a time

The recomp runs the game, but its C (`RecompiledFuncs/`) is machine-translated register code. The decomp replaces
game functions with hand-written C that **behaves the same** (it doesn't have to compile to the same bytes). Each
decompiled function swaps in for its recompiled original in developer builds and is checked against it in the
running game. The source holds only code; the game's data still comes from the player's ROM.

Design: [superpowers/specs/2026-10-24-equivalent-decomp-design.md](superpowers/specs/2026-10-24-equivalent-decomp-design.md).
Release builds are not affected: they stay fully recompiled until the decomp is ready to ship.

## How it fits together

| Path | What it is |
|---|---|
| `decomp/src/<system>/*.c` | The decompiled game code, written as N64 C (game structs and pointers, libultra types) |
| `decomp/include/` | Shared structs, enums and prototypes |
| `decomp/functions.txt` | Every decompiled function: system, on/off, how it's checked |
| `decomp/imports.ld`, `src/decomp/imports.cpp` | Native functions the decompiled C can call |
| `decomp.toml` | N64Recomp config for the decomp (patch mode, like BanjoRecomp's `patches.toml`) |
| `scripts/decomp.sh` | Build it: data symbols, MIPS ELF (clang), N64Recomp, dispatch glue |
| `src/decomp/dispatch.cpp`, `compare.cpp` | Runtime: picks a version per function, runs the side-by-side check |
| `scripts/decomp_check.sh` | Check run over the 20 menu levels |
| `scripts/decomp_progress.py` | Progress report |

`scripts/decomp.sh` compiles `decomp/` for MIPS and runs it through N64Recomp. `scripts/decomp_dispatch.py` then renames
each decompiled function's recompiled original to `F__orig` and its decompiled version to `F__decomp` (definitions
only, in copies under `build/decomp/gen/`), and generates a native `F` that calls one of them. Every call to `F`,
including calls from other decompiled functions, goes through that dispatcher.

## Building

```sh
scripts/decomp.sh                        # after any change in decomp/ or decomp/functions.txt
cmake -B build/app -DMI_DECOMP=ON         # once
cmake --build build/app
```

The game log says how many functions run decompiled: `[decomp] 13 functions: 13 decomp, 0 original`.

## Decompiling a function

1. **Pick it.** Work inside one system, from the leaves of the call graph up, so the functions it calls are already
   readable. `python3 -I scripts/levels/fn.py func_XXXXXXXX` prints a function's asm (by its splat name);
   `--callers` lists its callers.
2. **Draft it.** [m2c](https://github.com/matt-kempster/m2c) gives a first draft from the splat asm (clone it into
   `tools/m2c`, which git ignores). Treat the draft as a starting point, not the answer.
3. **Write it** in `decomp/src/<system>/`, marked `DECOMP`, with real names and the shared structs. Declare game
   functions it calls in a header; calls and data references to the original game resolve by name (`syms.toml`,
   and data labels and names from `symbols/manual.txt`). Name a function or global the first time the decomp needs
   it, in `symbols/manual.txt`, then re-run `scripts/recomp.sh`. Avoid libc names (`strcmp`, `memcpy`, ...): N64Recomp
   renames them.
4. **Keep the original's behavior, quirks included.** Check what each callee returns and what the original does
   with it (the Burp loaders return `Heap_Free`'s result and ignore `Inflate_FromRom`'s error code). Keep odd
   behavior and write a comment saying so.
5. **List it** in `decomp/functions.txt`: `name system on check|read [flags]`.
6. **Move any hooks.** If `mi.us.toml` has a `[[patches.hook]]` in the function, call the same native code from the C
   (see "Native imports") and add a `hooks-moved=VRAM.HASH` flag for each hook; `scripts/decomp.sh` fails and prints
   the exact flag. The hash is of the hook's text, so adding or changing a hook in a decompiled function later fails
   the build until the C is updated too. Leave the hook in `mi.us.toml`: release builds and the `F__orig` version
   still use it.
7. **Build and check:** `scripts/decomp.sh`, rebuild, a quick `MI_LEVEL` run with `MI_DECOMP_CHECK=1`, then the full
   check run and the progress report.

### check or read

- **check**: the function only computes and writes RAM. Check mode runs it side by side with the original.
- **read**: anything that can wait on a message or switch threads (`osRecvMesg`, `osSendMesg`, scheduler waits,
  `Rom_Read`, which waits on DMA messages). Running it twice isn't safe, so it's checked by carefully reading it
  against the asm instead, plus the play-through. Put the reason on a comment line above it.
- **ret64** (flag): the function returns a 64-bit value (`s64`, `u64`, `double`), so the check also compares `$v1`
  and the odd float return registers, which are scratch otherwise.

### When a function is done

1. It builds and replaces the original.
2. The full check run shows it "checked in game", or it's `read` and was checked by reading it against the asm.
3. The 20-level check run has no crash or visible change with it on.
4. It reads as real code: named, typed, using the shared structs, no register-style temporaries, a short comment
   where the logic isn't obvious.
5. Any hook it had now lives in the C, and that feature was seen working.

## The side-by-side check

With `MI_DECOMP_CHECK=1`, each call to a `check` function copies RAM (game RAM and the patch area, 16 MB) twice,
runs the original on one copy and the decompiled version on the other, and compares:

- return values (`$v0`, `$f0`, `$f2`; `ret64` adds `$v1`, `$f1`, `$f3`),
- callee-saved registers (`$s0`-`$s7`, `$sp`, `$fp`, `$f20`-`$f31`),
- all 8 MB of game RAM, except the dead stack below the entry `$sp` (64 KB) and the o32 argument save area just above
  it (16 bytes), where the two versions' stack frames legitimately differ.

Then the original runs for real on live RAM, so the game always continues exactly as it would without the decomp.
The audio and graphics threads keep writing live RAM meanwhile, but they can't touch the private copies.

Differences are logged (`[decomp] MISMATCH Burp_GetEntry call #12: r2 orig=... decomp=...`, the first 5 calls per
function) and counted in the report.

| Variable | Effect |
|---|---|
| `MI_DECOMP_OFF=a,b` | Run the originals of these functions (find a bad one by bisecting) |
| `MI_DECOMP_LIST=path` | Read this list instead of `decomp/functions.txt` (on/off only; check/read is fixed at build time) |
| `MI_DECOMP_CHECK=1` | Side-by-side check every `check` function |
| `MI_DECOMP_CHECK_EVERY=N` | Check every Nth call per function (for hot functions) |
| `MI_DECOMP_REPORT=path` | Write the per-function counts (CSV) every 2 s and at exit |
| `MI_DECOMP_CHECK_SELFTEST=1` | Corrupt each decompiled result's `$v0` before comparing, to prove the check catches it |

### Check run and progress

```sh
scripts/decomp_check.sh build/decomp/check          # 20 levels, ~30 min
python3 -I scripts/decomp_progress.py --report build/decomp/check/report.csv
```

The progress report lists each function as `checked in game`, `checked by reading`, `never reached` (a `check`
function the run didn't call: make it reachable, or check it by reading and say why) or `MISMATCH` (exit code 1), and
sums functions and instructions per system.

## Native imports

Decompiled C calls native code through dummy addresses:

1. Declare it in `decomp/include/decomp.h` with N64 types: `void mi_override_entry_import(void* entry, void* buffer);`
2. Give it the next address in `decomp/imports.ld`: `mi_override_entry_import = 0x8F000000;`
3. Define the native side in `src/decomp/imports.cpp` as `extern "C" void name(uint8_t* rdram, recomp_context* ctx)`,
   reading the arguments from `ctx->r4`.. (`$a0`..).

N64Recomp turns the call into a direct call to that native function.

## Gotchas found so far

- `MI_OVERRIDE_DIR` and other paths given to `scripts/capture.sh` runs must be absolute: the game doesn't run from the
  repo root.
- `DECOMP` functions are `noinline`, or clang inlines one into another and the call skips the dispatcher.
- N64Recomp renames static helpers with an address suffix (`Burp_Decode_00000000`). They need no line in the list.
