#!/usr/bin/env bash
# Build the decompiled game code: data symbols, MIPS ELF (decomp/), N64Recomp (decomp.toml), dispatch generator.
set -euo pipefail
cd "$(dirname "$0")/.."

python3 -I scripts/gen_decomp_syms.py build/decomp/datasyms.toml --renames symbols/libultra.txt,symbols/manual.txt \
    split/asm/data/84C90.data.s split/asm/data/94440.bss.s
make -s -C decomp
rm -rf RecompiledDecomp && mkdir -p RecompiledDecomp
./build/n64recomp/N64Recomp decomp.toml > build/decomp/recomp.log 2>&1 || { tail build/decomp/recomp.log; exit 1; }
if grep -i error build/decomp/recomp.log; then exit 1; fi
if [[ -f scripts/decomp_dispatch.py ]]; then
    python3 -I scripts/decomp_dispatch.py --list decomp/functions.txt --funcs RecompiledFuncs --decomp RecompiledDecomp \
        --syms syms.toml --hooks mi.us.toml --out build/decomp/gen
fi
echo "OK: $(grep -c '^RECOMP_FUNC' RecompiledDecomp/decomp.c) decompiled functions in RecompiledDecomp/"
