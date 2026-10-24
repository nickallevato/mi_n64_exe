#!/usr/bin/env python3
"""Generate an N64Recomp data reference symbols TOML from splat's data/bss asm.

Decompiled C refers to the game's data at its original addresses; N64Recomp resolves those references by name from
this file. One section per input file, one symbol per `dlabel`. The sections have no matching code section, so
N64Recomp treats their symbols as absolute addresses.

    gen_decomp_syms.py OUT.toml [--renames a.txt,b.txt] DATA_ASM...

`--renames` takes the same `name = 0xADDR;` files as gen_syms.py; a data label at a renamed address gets that name.
"""
import re
import sys
from pathlib import Path

SECTION_RE = re.compile(r"^\.section (\.\w+)")
LABEL_RE = re.compile(r"^dlabel (\w+)")
DATA_RE = re.compile(r"/\* ([0-9A-Fa-f]+) ([0-9A-Fa-f]{8})(?: ([0-9A-Fa-f]+))? \*/")
RAW_RE = re.compile(r"^\s*/\* ([0-9A-Fa-f]+) \*/\s*$")
BSS_RE = re.compile(r"/\* ([0-9A-Fa-f]{8}) \*/ \.space 0x([0-9A-Fa-f]+)")
LABEL_VRAM_RE = re.compile(r"_([0-9A-Fa-f]{8})$")


def load_renames(paths):
    """Load `name = 0xADDR;` files; later files override earlier ones. A name given more than one address (n64sym
    false positives) is dropped, since neither address can be trusted for it."""
    renames = {}
    for path in filter(None, paths.split(",")):
        for line in Path(path).read_text().splitlines():
            line = line.split("//")[0].strip()
            m = re.match(r"(\w+)\s*=\s*0x([0-9A-Fa-f]+)", line)
            if m:
                renames[int(m.group(2), 16)] = m.group(1)
    addrs_by_name = {}
    for vram, name in renames.items():
        addrs_by_name.setdefault(name, []).append(vram)
    for name, addrs in addrs_by_name.items():
        if len(addrs) > 1:
            print(f"warning: {name} has {len(addrs)} addresses, not using it", file=sys.stderr)
            for vram in addrs:
                del renames[vram]
    return renames


def parse_section(path, renames):
    """Return (name, rom or None, vram, size, [(symbol, vram)]) for one asm file."""
    name = None
    labels = []
    rom_start = rom_end = vram_start = vram_end = None
    pending_raw = None  # rom of a data line whose bytes are on the next line (.asciz)
    for line in Path(path).read_text().splitlines():
        if m := SECTION_RE.match(line):
            name = m.group(1)
        elif m := LABEL_RE.match(line):
            labels.append(m.group(1))
        elif m := BSS_RE.search(line):
            vram, size = int(m.group(1), 16), int(m.group(2), 16)
            vram_start = vram if vram_start is None else vram_start
            vram_end = vram + size
        elif m := DATA_RE.search(line):
            rom, vram = int(m.group(1), 16), int(m.group(2), 16)
            if rom_start is None:
                rom_start, vram_start = rom, vram
            if m.group(3):
                rom_end = max(rom_end or 0, rom + len(m.group(3)) // 2)
                pending_raw = None
            else:
                pending_raw = rom
        elif pending_raw is not None and (m := RAW_RE.match(line)):
            rom_end = max(rom_end or 0, pending_raw + len(m.group(1)) // 2)
            pending_raw = None
    if name is None or vram_start is None:
        sys.exit(f"{path}: no .section or no addresses found")
    symbols = []
    for label in labels:
        m = LABEL_VRAM_RE.search(label)
        if not m:
            sys.exit(f"{path}: can't get an address from label {label}")
        vram = int(m.group(1), 16)
        symbols.append((renames.get(vram, label), vram))
    if rom_start is not None:
        return name, rom_start, vram_start, rom_end - rom_start, symbols
    return name, None, vram_start, vram_end - vram_start, symbols


def main():
    args = sys.argv[1:]
    renames = {}
    if "--renames" in args:
        i = args.index("--renames")
        renames = load_renames(args[i + 1])
        del args[i:i + 2]
    out_path, *asm_paths = args
    out = []
    seen = {}
    count = 0
    for path in asm_paths:
        name, rom, vram, size, symbols = parse_section(path, renames)
        for sym, _ in symbols:
            if sym in seen:
                sys.exit(f"duplicate data symbol {sym} in {seen[sym]} and {path}")
            seen[sym] = path
        out += ["[[section]]", f'name = "{name}"']
        if rom is not None:
            out.append(f"rom = 0x{rom:08X}")
        out += [f"vram = 0x{vram:08X}", f"size = 0x{size:X}", "", "symbols = ["]
        out += [f'    {{ name = "{sym}", vram = 0x{addr:08X} }},' for sym, addr in symbols]
        out += ["]", ""]
        count += len(symbols)
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Path(out_path).write_text("\n".join(out))
    print(f"wrote {count} data symbols to {out_path}")


if __name__ == "__main__":
    main()
