#!/usr/bin/env bash
# Regenerate syms.toml from the splat disassembly and run N64Recomp.
set -euo pipefail
cd "$(dirname "$0")/.."

python3 -I scripts/gen_syms.py syms.toml symbols/libultra.txt,symbols/manual.txt \
    split/asm/1000.s split/asm/1050.s
rm -rf RecompiledFuncs && mkdir -p RecompiledFuncs
./tools/N64Recomp/build/N64Recomp mi.us.toml > recomp.log 2>&1 || { tail recomp.log; exit 1; }
grep -i error recomp.log && exit 1

# Syntax-check the output against N64Recomp's recomp.h
for f in RecompiledFuncs/*.c; do
    clang -std=c11 -fsyntax-only -w -I tools/N64Recomp/include -I RecompiledFuncs "$f"
done
echo "OK: $(grep -c '{ name =' syms.toml) functions recompiled into RecompiledFuncs/"
