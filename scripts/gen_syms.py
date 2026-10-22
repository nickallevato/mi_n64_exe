#!/usr/bin/env python3
"""Generate an N64Recomp symbols TOML from splat's asm output.

Reads `nonmatching <name>, <size>` lines and the `/* ROM VRAM WORD */` comments
that follow each function to recover rom/vram/size for every function.
"""
import re
import sys
from pathlib import Path

SECTION_NAME = ".text"
SECTION_ROM = 0x1000
SECTION_VRAM = 0x80000400
SECTION_END_ROM = 0x84A60  # rspboot microcode (RSP code, not CPU) follows at 0x84A60-0x84C90

FUNC_RE = re.compile(r"^nonmatching (\w+), 0x([0-9A-Fa-f]+)")
INSN_RE = re.compile(r"/\* ([0-9A-Fa-f]+) ([0-9A-Fa-f]{8}) [0-9A-Fa-f]{8} \*/")


def parse_funcs(asm_paths, renames):
    funcs = []
    pending = None
    for path in asm_paths:
        for line in Path(path).read_text().splitlines():
            m = FUNC_RE.match(line)
            if m:
                pending = (m.group(1), int(m.group(2), 16))
                continue
            if pending:
                m = INSN_RE.search(line)
                if m:
                    name, size = pending
                    vram = int(m.group(2), 16)
                    funcs.append((renames.get(vram, name), vram, size))
                    pending = None
    return sorted(funcs, key=lambda f: f[1])


def load_renames(paths):
    """Load `name = 0xADDR;` files; later files override earlier ones."""
    renames = {}
    for path in filter(None, paths.split(",")):
        for line in Path(path).read_text().splitlines():
            line = line.split("//")[0].strip()
            m = re.match(r"(\w+)\s*=\s*0x([0-9A-Fa-f]+)", line)
            if m:
                renames[int(m.group(2), 16)] = m.group(1)
    return renames


def main():
    out_path, renames_path, *asm_paths = sys.argv[1:]
    text_end_vram = SECTION_VRAM + SECTION_END_ROM - SECTION_ROM
    funcs = [f for f in parse_funcs(asm_paths, load_renames(renames_path)) if f[1] < text_end_vram]
    out = [
        "[[section]]",
        f'name = "{SECTION_NAME}"',
        f"rom = 0x{SECTION_ROM:08X}",
        f"vram = 0x{SECTION_VRAM:08X}",
        f"size = 0x{SECTION_END_ROM - SECTION_ROM:X}",
        "",
        "functions = [",
    ]
    for name, vram, size in funcs:
        out.append(f'    {{ name = "{name}", vram = 0x{vram:08X}, size = 0x{size:X} }},')
    out.append("]")
    Path(out_path).write_text("\n".join(out) + "\n")
    print(f"wrote {len(funcs)} functions to {out_path}")


if __name__ == "__main__":
    main()
