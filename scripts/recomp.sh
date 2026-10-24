#!/usr/bin/env bash
# Regenerate syms.toml from the splat disassembly and run N64Recomp.
set -euo pipefail
cd "$(dirname "$0")/.."

python3 -I scripts/gen_syms.py syms.toml symbols/libultra.txt,symbols/manual.txt \
    split/asm/1000.s split/asm/1050.s
rm -rf RecompiledFuncs && mkdir -p RecompiledFuncs
./build/n64recomp/N64Recomp mi.us.toml > recomp.log 2>&1 || { tail recomp.log; exit 1; }
grep -i error recomp.log && exit 1

# Syntax-check the output against N64Recomp's recomp.h
for f in RecompiledFuncs/*.c; do
    clang -std=c11 -fsyntax-only -w -Wno-implicit-function-declaration -I lib/N64ModernRuntime/N64Recomp/include -I RecompiledFuncs "$f"
done
echo "OK: $(grep -c '{ name =' syms.toml) functions recompiled into RecompiledFuncs/"

mkdir -p rsp
./build/n64recomp/RSPRecomp n_aspMain.us.toml
echo "OK: rsp/n_aspMain.cpp"

# Developer builds with decompiled functions copy some RecompiledFuncs files; refresh them (docs/DECOMP.md).
if [[ -d build/decomp/gen ]]; then
    scripts/decomp.sh
fi
