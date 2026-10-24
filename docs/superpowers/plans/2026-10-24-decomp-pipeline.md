# Decomp pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the pipeline that swaps hand-written C in for recompiled game functions in developer builds, checks
each one against the original in the running game, and prove it on the `Burp_*` archive functions.

**Architecture:** Decompiled N64 C in `decomp/` is compiled with clang for MIPS into an ELF, which N64Recomp turns
into native C in patch mode (like BanjoRecomp's PatchesLib). A generator script renames the original recompiled
definition of each decompiled function `F` to `F__orig` and the decompiled one to `F__decomp` (definitions only,
calls untouched), then emits a native `F` that dispatches between them. The dispatcher reads an on/off list at
startup and, in check mode, runs both versions from the same state and compares the results. All of it exists only
when CMake is configured with `-DMI_DECOMP=ON`; release builds are unchanged.

**Tech Stack:** clang 21 + ld.lld (`-target mips -mips2 -mabi=32`), N64Recomp (patch/ELF mode, built in
`build/n64recomp/`), CMake, C++20 (app side), Python 3 (generators, `unittest`).

**Spec:** `docs/superpowers/specs/2026-10-24-equivalent-decomp-design.md`

This plan covers the spec's "Pipeline first" step: the pipeline and the `Burp_*` family as its proof. Each game
system (mission logic, objects, level loading, geometry, player) gets its own plan afterwards, built on the
procedure this plan writes into `docs/DECOMP.md`.

## Global Constraints

- No ROM data in git: no asm, splat output, extracted data or ROM-derived tables. Generated files go under
  `build/` or gitignored output dirs (`RecompiledDecomp/`).
- Release builds stay fully recompiled: with `MI_DECOMP` off (the default), no decomp code, dispatcher or `__orig`
  symbol is compiled in. `scripts/release/build_linux.sh` is not changed.
- Decompiled C is N64 C: game structs, pointers and libultra types, no `ctx->`/`rdram` in `decomp/`.
- Original data tables stay in the ROM: decompiled C declares them as typed `extern`s at their original addresses.
- A function is done only when it passes the spec's five-point bar ("What 'done' means").
- Every `[[patches.hook]]` in a decompiled function moves into its C as an explicit call; its feature is seen working.
- GPL-3; say "AI-made", never "AI-assisted"; commit each task; push every commit to both remotes (`origin`, `github`).
- Verify by running the game (`scripts/capture.sh`, `scripts/level_test.sh`, `MI_LEVEL`).

## Review Focus

1. **A function that waits on a message or thread runs twice in check mode** (anything reaching `osRecvMesg`,
   `osSendMesg`, `osYieldThread` or a scheduler wait, e.g. through `Rom_Read`). Expected: it never hangs or
   double-consumes; functions marked `read` are never run twice. Pinned in Task 4 (dispatcher test) and Task 5/6
   (each Burp function's method is chosen after reading its callees).
2. **Release build picks up decomp code.** Expected: a default configure builds no `__orig`/`__decomp` symbols.
   Pinned in Task 3, Step 6.
3. **Stack frames differ between versions.** Expected: writes below the entry `$sp` (dead callee frames) don't count
   as mismatches, but writes at or above it (caller's out-params) do. Pinned in Task 4 (compare-window test).
4. **A typo or stale name in `decomp/functions.txt` or `MI_DECOMP_OFF`.** Expected: the generator fails the build
   for a listed function that isn't in `syms.toml` or the decomp ELF; the app logs a warning for an unknown
   `MI_DECOMP_OFF` name. Pinned in Task 3 (generator tests) and Task 3, Step 5.
5. **A hooked function is decompiled but its hook isn't moved.** Expected: the generator fails unless the line is
   marked `hooks-moved`. Pinned in Task 3 (generator test) and Task 5.

---

## File structure

| Path | Responsibility |
|---|---|
| `decomp/Makefile` | Build `build/decomp/decomp.elf` from `decomp/src/**/*.c` with clang/ld.lld |
| `decomp/decomp.ld` | Section layout for the ELF (copy of `tools/BanjoRecomp/patches/patches.ld`) |
| `decomp/imports.ld` | Dummy addresses (`0x8F000000+`) for native functions the C calls |
| `decomp/include/decomp.h` | `DECOMP` marker (`__attribute__((section(".recomp_patch")))`), import declarations |
| `decomp/include/ultra64.h` | libultra types for MIPS builds (see Task 2, Step 3) |
| `decomp/include/burp.h` | Burp archive structs and function prototypes |
| `decomp/src/loading/burp.c` | Decompiled `Burp_*` functions |
| `decomp/functions.txt` | One line per decompiled function: name, system, state, method, flags |
| `decomp.toml` | N64Recomp config for the decomp ELF (repo root, like Banjo's `patches.toml`) |
| `scripts/gen_decomp_syms.py` | Write `build/decomp/datasyms.toml` (data reference symbols) from splat data asm |
| `scripts/decomp_dispatch.py` | Read `functions.txt`, write renamed copies and `dispatch.cpp` under `build/decomp/gen/` |
| `scripts/decomp.sh` | Run Make, N64Recomp (`decomp.toml`) and the generators in order |
| `scripts/decomp_check.sh` | Check run: all 20 menu levels with `MI_DECOMP_CHECK=1`, merged report |
| `scripts/decomp_progress.py` | Progress report from `functions.txt`, `syms.toml` and the check report |
| `scripts/tests/test_*.py` | `unittest` tests for the three Python scripts (synthetic fixtures only) |
| `src/decomp/dispatch.hpp`, `dispatch.cpp` | Runtime: on/off list, dispatch, side-by-side check, report |
| `src/decomp/imports.cpp` | Native adapters for `decomp/imports.ld` entries |
| `src/main/register_decomp.cpp` | Register the decomp patch sections and manual patch symbols |
| `CMakeLists.txt` | `option(MI_DECOMP ...)` and the dev-only targets |
| `docs/DECOMP.md` | How to decompile a function, the readability bar, check runs |

### `decomp/functions.txt` format (used by Tasks 3–7)

```
# name            system   state  method  [flags]
Burp_GetCount     loading  on     check
Burp_LoadTo       loading  on     check   hooks-moved
```

- `state`: `on` | `off` (the default before env overrides).
- `method`: `check` (side-by-side in check mode) | `read` (never run twice; checked by reading against the asm). A
  `read` line is preceded by a `#` comment giving the reason.
- `flags`: optional, comma-separated; only `hooks-moved` exists.

### Environment variables (dev builds)

| Variable | Effect |
|---|---|
| `MI_DECOMP_LIST=path` | List to read instead of `${CMAKE_SOURCE_DIR}/decomp/functions.txt` (baked in) |
| `MI_DECOMP_OFF=a,b` | Force these functions to the original |
| `MI_DECOMP_CHECK=1` | Side-by-side check every `check` function |
| `MI_DECOMP_CHECK_EVERY=N` | Check every Nth call per function (default 1) |
| `MI_DECOMP_REPORT=path` | Write the per-function check report (CSV) at exit and every 10 s |
| `MI_DECOMP_CHECK_SELFTEST=1` | Corrupt the decompiled result's `$v0` before comparing (proves detection) |

---

### Task 1: Data reference symbols

**Files:**
- Create: `scripts/gen_decomp_syms.py`
- Test: `scripts/tests/test_gen_decomp_syms.py`

**Interfaces:**
- Produces: `python3 -I scripts/gen_decomp_syms.py OUT.toml DATA_ASM...` writes an N64Recomp data reference file:
  one `[[section]]` per input file (`name`, `rom`, `vram`, `size`) with `symbols = [{ name, vram }]`, the format
  read by N64Recomp's `read_data_reference_syms` (`lib/N64ModernRuntime/N64Recomp/src/config.cpp:734`).
- Input: `split/asm/data/84C90.data.s` (`dlabel D_XXXXXXXX` lines plus the splat `/* ROM VRAM ... */` comments) and
  `split/asm/data/94440.bss.s`. Symbols named in `symbols/manual.txt` with `type:data` (if any) are added too.

- [ ] **Step 1: Write the failing tests** with a synthetic two-label `.data.s` fixture written in the test (no ROM
  bytes):
  - `test_labels_become_symbols`: both `dlabel`s appear with their vram (`0x8008AB28` from `D_8008AB28`).
  - `test_section_bounds`: section `rom`/`vram`/`size` come from the first and last address comments.
  - `test_duplicate_label_fails`: the same label twice → non-zero exit naming it.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s scripts/tests -p 'test_gen_decomp_syms.py'` → FAIL.
- [ ] **Step 3: Implement** `scripts/gen_decomp_syms.py` in the style of `scripts/gen_syms.py`.
- [ ] **Step 4: Run** the tests → PASS. Then run it on the real asm into `build/decomp/datasyms.toml` and confirm
  `D_8008AB28` is in it.
- [ ] **Step 5: Commit** (`scripts/gen_decomp_syms.py`, the test).

### Task 2: MIPS toolchain and the first decompiled function

**Files:**
- Create: `decomp/Makefile`, `decomp/decomp.ld`, `decomp/imports.ld`, `decomp/include/decomp.h`,
  `decomp/include/ultra64.h`, `decomp/include/burp.h`, `decomp/src/loading/burp.c`, `decomp.toml`,
  `scripts/decomp.sh`
- Modify: `.gitignore` (add `RecompiledDecomp/`)

**Interfaces:**
- Consumes: `build/decomp/datasyms.toml` (Task 1), `syms.toml`, `build/n64recomp/N64Recomp`.
- Produces: `scripts/decomp.sh` → `build/decomp/decomp.elf`, `build/decomp/decomp.bin`,
  `RecompiledDecomp/patches.c`, `RecompiledDecomp/recomp_overlays.inl`, `RecompiledDecomp/funcs.h`.
  `DECOMP` macro marks a function as replacing the original of the same name.

- [ ] **Step 1: Toolchain files.** `decomp/Makefile` uses the CFLAGS of `tools/BanjoRecomp/patches/Makefile`
  verbatim (`-target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division
  -fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset -funsigned-char
  -fno-builtin-sinf -fno-builtin-cosf`), `-nostdinc -D_LANGUAGE_C -DMIPS -I include`, and its LDFLAGS with
  `-T decomp.ld -T imports.ld`. Output to `../build/decomp/`. `decomp.ld` is Banjo's `patches.ld` unchanged.
  `imports.ld` starts with `__start = 0x80000000;` and no imports yet.
- [ ] **Step 2: `decomp.toml`**, modeled on `tools/BanjoRecomp/patches.toml`: `elf_path =
  "build/decomp/decomp.elf"`, `output_func_path = "RecompiledDecomp"`, `single_file_output = true`,
  `use_absolute_symbols = true`, `func_reference_syms_file = "syms.toml"`, `data_reference_syms_files =
  ["build/decomp/datasyms.toml"]`, `output_binary_path = "build/decomp/decomp.bin"`,
  `unpaired_lo16_warnings = false`, `strict_patch_mode = true`. No exports.
- [ ] **Step 3: Headers.** `ultra64.h`: try `lib/N64ModernRuntime/ultramodern/include/ultramodern/ultra64.h` under
  `-DMIPS` first; if it doesn't compile for MIPS, write a minimal header with only the types the decomp uses
  (`u8`…`u64`, `s8`…`s64`, `f32`, `OSMesgQueue`, …). `burp.h`: the archive header (`'Burp'`, u32 count) and the
  0x14-byte entry (decoded size, stored size, name offset, data offset, packing) from `docs/LEVELS.md`, plus any
  handle struct `Burp_*` uses (read from the asm).
- [ ] **Step 4: Decompile `Burp_GetCount`** (vram `0x80068B50`, size 0x8) into
  `decomp/src/loading/burp.c`, marked `DECOMP`, reading the asm with `scripts/levels/fn.py Burp_GetCount`.
- [ ] **Step 5: `scripts/decomp.sh`** runs, in order: `gen_decomp_syms.py`, `make -C decomp`, `N64Recomp
  decomp.toml`, `decomp_dispatch.py` (added in Task 3; skip it if absent for now). `set -euo pipefail`; fail on
  any `error` in N64Recomp's output, like `scripts/recomp.sh`.
- [ ] **Step 6: Verify.** `scripts/decomp.sh` exits 0; `grep -c "void Burp_GetCount" RecompiledDecomp/patches.c`
  → 1; `RecompiledDecomp/recomp_overlays.inl` has a patch section table.
- [ ] **Step 7: Commit** (all created files and `.gitignore`; not `RecompiledDecomp/` or `build/`).

### Task 3: Dispatch generator and dev build

**Files:**
- Create: `decomp/functions.txt`, `scripts/decomp_dispatch.py`, `scripts/tests/test_decomp_dispatch.py`,
  `src/decomp/dispatch.hpp`, `src/decomp/dispatch.cpp` (on/off and plain dispatch only; checking is Task 4),
  `src/main/register_decomp.cpp`
- Modify: `CMakeLists.txt`, `src/main/main.cpp` (call `mi::register_decomp()` after `mi::register_overlays()`,
  line ~568, under `#ifdef MI_DECOMP`), `scripts/decomp.sh` (run the generator)

**Interfaces:**
- Consumes: `RecompiledFuncs/*.c`, `RecompiledDecomp/patches.c`, `syms.toml`, `mi.us.toml`, `functions.txt`.
- Produces:
  - `python3 -I scripts/decomp_dispatch.py --list decomp/functions.txt --funcs RecompiledFuncs --decomp
    RecompiledDecomp --syms syms.toml --hooks mi.us.toml --out build/decomp/gen` writes:
    - `gen/RecompiledFuncs/funcs_N.c`: copies of only the files defining a listed function, with
      `RECOMP_FUNC void F(uint8_t* rdram, recomp_context* ctx) {` → `... F__orig(...) {` (definition lines only),
    - `gen/patches.c`: `RecompiledDecomp/patches.c` with each `F` definition renamed `F__decomp`,
    - `gen/dispatch.cpp`: for each function in list order, `extern "C" void F(uint8_t* rdram, recomp_context* ctx)
      { mi::decomp::call(ID, rdram, ctx); }`, and the table `const mi::decomp::Entry mi_decomp_entries[]` of
      `{ "F", F__orig, F__decomp, method }` plus `mi_decomp_entry_count`,
    - `gen/sources.txt`: the original file names replaced by copies (CMake drops those from `RecompiledFuncs`).
  - `src/decomp/dispatch.hpp`:
    `namespace mi::decomp { enum class Method { Check, Read }; struct Entry { const char* name; recomp_func_t*
    orig; recomp_func_t* decomp; Method method; }; void init(); void call(int id, uint8_t* rdram,
    recomp_context* ctx); }`
  - `mi::register_decomp()` in `src/main/register_decomp.cpp`: `recomp::overlays::register_patches(...)` and
    `register_manual_patch_symbols(...)` with the tables from `RecompiledDecomp/recomp_overlays.inl` and the
    embedded `build/decomp/decomp.bin`, like `tools/BanjoRecomp/src/main/register_patches.cpp`.

- [ ] **Step 1: Write the failing generator tests** (synthetic `.c`/`.toml` fixtures in the test):
  - `test_renames_definition_not_calls`: a fixture with `RECOMP_FUNC void Burp_Load(...) {` and a call
    `Burp_Load(rdram, ctx);` → only the definition becomes `Burp_Load__orig`.
  - `test_untouched_files_not_copied`: files without a listed function aren't in `gen/RecompiledFuncs`.
  - `test_unknown_function_fails`: a listed name missing from `syms.toml` or `patches.c` → non-zero exit naming it.
  - `test_hooked_function_needs_flag`: a listed function with a `[[patches.hook]]` in the hooks file and no
    `hooks-moved` flag → non-zero exit naming the function and hook vram; with the flag → OK.
  - `test_read_method_in_table`: a `read` line gives `Method::Read` in `dispatch.cpp`.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s scripts/tests -p 'test_decomp_dispatch.py'` → FAIL.
- [ ] **Step 3: Implement** `scripts/decomp_dispatch.py`; add `Burp_GetCount loading on check` to
  `decomp/functions.txt` with a header comment describing the format.
- [ ] **Step 4: Implement** the runtime: `init()` reads the list (`MI_DECOMP_LIST` or the baked path), applies
  `MI_DECOMP_OFF`, logs one line `[decomp] N functions: X decomp, Y original`; `call()` runs `decomp` or `orig`
  by state. CMake: `option(MI_DECOMP "Developer build with decompiled functions" OFF)`; when ON, add a
  `MIDecomp` library (gen copies, `gen/patches.c` with `-fno-strict-aliasing`, `gen/dispatch.cpp`,
  `src/decomp/*.cpp`, `register_decomp.cpp`, the embedded bin), exclude `gen/sources.txt` files from
  `RecompiledFuncs`, define `MI_DECOMP` and the baked list path.
- [ ] **Step 5: Verify in game.** `scripts/decomp.sh && cmake -B build/app -DMI_DECOMP=ON && cmake --build
  build/app`; `MI_LEVEL=1,0` capture: log shows `[decomp] 1 functions: 1 decomp, 0 original`, Embassy Function
  on screen. With `MI_DECOMP_OFF=Burp_GetCount`: `0 decomp, 1 original`. With `MI_DECOMP_OFF=Nope`: a warning
  naming `Nope`.
- [ ] **Step 6: Verify release is untouched.** Configure a separate default dir (`cmake -B build/plain`), build,
  `nm -C build/plain/MIRecompiled | grep -c '__orig\|__decomp\|mi::decomp'` → 0.
- [ ] **Step 7: Commit.**

### Task 4: Side-by-side checker

**Files:**
- Modify: `src/decomp/dispatch.cpp`, `src/decomp/dispatch.hpp`, `CMakeLists.txt` (`enable_testing()` and
  `add_test(NAME decomp_compare ...)`, both inside the `MI_DECOMP` block; the project has no tests yet)
- Create: `src/decomp/compare.hpp`, `src/decomp/compare.cpp` (pure comparison, testable without the game),
  `src/decomp/test_compare.cpp` (a small CTest executable, dev builds only)

**Interfaces:**
- Produces:
  - `struct Mismatch { std::string what; uint32_t addr; uint64_t orig; uint64_t decomp; };`
  - `std::vector<Mismatch> mi::decomp::compare(const recomp_context& orig_ctx, const recomp_context& dec_ctx,
    const uint8_t* orig_ram, const uint8_t* dec_ram, size_t ram_size, uint32_t entry_sp);`
    Compares `r2`, `r3`, `r16`–`r23`, `r29`, `r30`, `f0`, `f2`, `f20`–`f30` (even), and RAM
    `0x80000000`–`0x80800000` except `[entry_sp - kStackSlack, entry_sp)`, `kStackSlack = 0x10000`. Adjacent
    differing bytes merge into one RAM mismatch. Cap at 16 mismatches per call.
  - Report CSV (`MI_DECOMP_REPORT`): `name,method,state,calls,checked,mismatching_calls,first_mismatch`.

Check algorithm in `call()` for a `check` function that is `on`, when `MI_DECOMP_CHECK=1`, this call is sampled
(`MI_DECOMP_CHECK_EVERY`) and no check is already running on this thread (nested dispatched calls just run their
selected version):

```
save ctx0 = *ctx; copy rdram → before
orig(rdram, ctx);  ctxA = *ctx; copy rdram → after_orig
*ctx = ctx0; copy before → rdram
decomp(rdram, ctx); [SELFTEST: ctx->r2 ^= 1]
mismatches = compare(ctxA, *ctx, after_orig, rdram, ..., ctx0.r29)
*ctx = ctxA; copy after_orig → rdram      // the game always continues with the original's result
log + count
```

The three 8 MB buffers are allocated once. `read` functions and `off` functions are never run twice.

- [ ] **Step 1: Write the failing compare tests** in `test_compare.cpp`:
  - `identical_states_match` → 0 mismatches.
  - `v0_differs` → one mismatch `what == "r2"`.
  - `write_below_entry_sp_ignored`: differ at `entry_sp - 8` → 0 mismatches.
  - `write_at_entry_sp_counts`: differ at `entry_sp` (caller's frame) → 1 RAM mismatch at that address.
  - `adjacent_bytes_merge`: 4 differing bytes in a row → 1 mismatch.
  - `caller_saved_ignored`: differ only in `r8` → 0.
- [ ] **Step 2: Run** `ctest --test-dir build/app -R decomp_compare` → FAIL.
- [ ] **Step 3: Implement** `compare()`; then the check path in `call()`, the counters, the log line
  `[decomp] MISMATCH F call #n: <what> @addr orig=… decomp=…`, and the report writer.
- [ ] **Step 4: Run** the compare tests → PASS.
- [ ] **Step 5: Verify in game.** Dev build, `MI_DECOMP_CHECK=1 MI_DECOMP_REPORT=build/decomp/report.csv
  MI_LEVEL=1,0` capture: no MISMATCH lines; report row for `Burp_GetCount` has `checked > 0` and
  `mismatching_calls = 0`. With `MI_DECOMP_CHECK_SELFTEST=1` added: MISMATCH lines for `Burp_GetCount` on `r2`,
  and the level still loads normally (original result kept).
- [ ] **Step 6: Commit.**

### Task 5: Native imports and the hooked loaders

**Files:**
- Modify: `decomp/imports.ld`, `decomp/include/decomp.h`, `decomp/src/loading/burp.c`, `decomp/include/burp.h`,
  `decomp/functions.txt`
- Create: `src/decomp/imports.cpp`

**Interfaces:**
- Produces: `void mi_override_entry_import(BurpEntry* entry, void* buffer);` declared in `decomp.h`, at
  `0x8F000000` in `imports.ld`; native `extern "C" void mi_override_entry_import(uint8_t* rdram, recomp_context*
  ctx)` in `imports.cpp` calls the existing `mi_override_entry(rdram, (uint32_t)ctx->r4, (uint32_t)ctx->r5)`
  (`src/game/overrides.cpp`). Later imports take the next addresses (`0x8F000004`, …).
- Consumes: the hooks for `Burp_LoadTo` (`before_vram = 0x80068CD4`) and `Burp_Load` (`0x80068E64`) in
  `mi.us.toml`: after an entry is decoded, if it succeeded and the entry is non-null, call the override.

- [ ] **Step 1: Read the callees** of `Burp_LoadTo` and `Burp_Load` (`scripts/levels/fn.py`); if any path reaches
  a message or thread wait (Review Focus 1), mark that function `read` with a `#` reason; otherwise `check`.
- [ ] **Step 2: Decompile** `Burp_LoadTo` and `Burp_Load` into `burp.c`, with the override call where the hook was.
  Add both to `functions.txt` with `hooks-moved`. Leave `mi.us.toml` alone (release and `__orig` still use the hooks).
- [ ] **Step 3: Verify the generator guard.** Temporarily drop `hooks-moved` from `Burp_Load`: `scripts/decomp.sh`
  fails naming `Burp_Load` and `0x80068E64`. Restore it.
- [ ] **Step 4: Verify the override feature with decomp on.** Build `build/mods/embassy_red` as in
  `docs/LEVELS.md`, dev build, `MI_OVERRIDE_DIR=build/mods/embassy_red MI_LEVEL=1,0` capture: the log shows the
  override applied and the screenshot shows the red Embassy. Repeat with
  `MI_DECOMP_OFF=Burp_LoadTo,Burp_Load` → same result (the original path still works).
- [ ] **Step 5: Verify the check.** Same run with `MI_DECOMP_CHECK=1`: no MISMATCH for either function (if `check`).
- [ ] **Step 6: Commit.**

### Task 6: The rest of the Burp family

**Files:**
- Modify: `decomp/src/loading/burp.c`, `decomp/include/burp.h`, `decomp/functions.txt`

**Interfaces:**
- Consumes: Task 2 headers, Task 5 import pattern.
- Produces: readable, typed C for `Burp_Open`, `Burp_Close`, `Burp_GetEntry`, `Burp_GetName`, `Burp_FindByName`,
  `Burp_CopyEntry`, `Burp_ReadRaw`, `Burp_ReadRawByName`, `Burp_LoadToByName`, `Burp_LoadByName`, all listed
  `on` in `functions.txt` with a method chosen as in Task 5, Step 1.

- [ ] **Step 1: Decompile** the ten functions, leaves of the call graph first, using shared structs from `burp.h`
  and no register-style temporaries (the spec's readability bar).
- [ ] **Step 2: Verify** one level (`MI_LEVEL=1,0`) with `MI_DECOMP_CHECK=1`: no MISMATCH, every `check` function
  has `checked > 0` or is noted for Task 7's full run.
- [ ] **Step 3: Commit.**

### Task 7: Check run, progress report and docs

**Files:**
- Create: `scripts/decomp_check.sh`, `scripts/decomp_progress.py`, `scripts/tests/test_decomp_progress.py`,
  `docs/DECOMP.md`
- Modify: `docs/ROADMAP.md` (patches pipeline item, new decomp section), `README.md` (one line under dev tools
  pointing at `docs/DECOMP.md`)

**Interfaces:**
- Consumes: report CSV (Task 4), `functions.txt`, `syms.toml`.
- Produces:
  - `scripts/decomp_check.sh <out_dir>`: runs `scripts/level_test.sh` for each of the 20 menu levels (the
    `(mission, level)` pairs from `scripts/levels/level_table.py`, the same set as the Debug tab's Start in Level)
    with `MI_DECOMP_CHECK=1` and a per-level `MI_DECOMP_REPORT`, then merges the reports into
    `<out_dir>/report.csv` (sum calls/checked/mismatching per function) and prints any level with `fatal>0`.
  - `python3 -I scripts/decomp_progress.py [--report report.csv]`: per system and overall, functions and
    instructions (size/4 from `syms.toml`) decompiled out of the system's total, and each function's status:
    `checked in game` (`check`, checked > 0, 0 mismatching), `checked by reading` (`read`), `never reached`
    (`check`, checked = 0), `MISMATCH` (mismatching > 0). Exit 1 if any `MISMATCH`.

- [ ] **Step 1: Write the failing progress tests** (synthetic list, syms and report):
  - `test_status_classes`: one function in each of the four statuses → each labeled correctly.
  - `test_instruction_totals`: totals equal the sum of `size/4` for the listed functions.
  - `test_mismatch_exit_code`: any `MISMATCH` → exit 1.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s scripts/tests -p 'test_decomp_progress.py'` → FAIL.
- [ ] **Step 3: Implement** `scripts/decomp_progress.py` and `scripts/decomp_check.sh`.
- [ ] **Step 4: Run** the tests → PASS.
- [ ] **Step 5: Full check run.** `scripts/decomp_check.sh build/decomp/check`; then
  `python3 -I scripts/decomp_progress.py --report build/decomp/check/report.csv`: all 13 Burp functions
  `checked in game` or `checked by reading`; any `never reached` gets switched to `read` with a reason after
  reading it against the asm; 20 levels, `fatal=0`.
- [ ] **Step 6: Write `docs/DECOMP.md`**: the per-function procedure (draft with m2c cloned into `tools/m2c`, write
  the C in `decomp/src/<system>/`, add the `functions.txt` line, move any hooks and add `hooks-moved`,
  `scripts/decomp.sh`, dev build, check run, progress report), the readability bar copied from the spec, the
  `check`/`read` rule (anything reaching a message or thread wait is `read`), the env vars table above, and how
  to add a native import. Update `docs/ROADMAP.md`: tick the patches pipeline item with what was measured, and
  add a "Decomp" section listing the systems in the spec's order with `loading: Burp_* done`.
- [ ] **Step 7: Commit and push** to `origin` and `github`.
