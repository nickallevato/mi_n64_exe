import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "gen_decomp_syms.py"

# Synthetic splat/spimdisasm-style output (made-up values, not ROM data).
DATA_ASM = """\
.include "macro.inc"

.section .data, "wa"

nonmatching D_80100000

dlabel D_80100000
    /* 1000 80100000 00000001 */ .word 0x00000001
    /* 1004 80100004 00000002 */ .word 0x00000002
enddlabel D_80100000

dlabel D_80100008
    /* 1008 80100008 3FF0000000000000 */ .double 1
enddlabel D_80100008
"""

BSS_ASM = """\
.include "macro.inc"

.section .bss, "wa"

nonmatching D_80200000, 0x2

dlabel D_80200000
    /* 80200000 */ .space 0x02

nonmatching D_80200004, 0xC

dlabel D_80200004
    /* 80200004 */ .space 0x0C
"""


def run(tmp, *asm):
    paths = []
    for i, text in enumerate(asm):
        p = Path(tmp) / f"in{i}.s"
        p.write_text(text)
        paths.append(str(p))
    out = Path(tmp) / "out.toml"
    proc = subprocess.run([sys.executable, "-I", str(SCRIPT), str(out), *paths], capture_output=True, text=True)
    return proc, out


class GenDecompSymsTest(unittest.TestCase):
    def test_labels_become_symbols(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc, out = run(tmp, DATA_ASM)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            section = tomllib.loads(out.read_text())["section"][0]
            self.assertEqual(section["symbols"], [
                {"name": "D_80100000", "vram": 0x80100000},
                {"name": "D_80100008", "vram": 0x80100008},
            ])

    def test_section_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc, out = run(tmp, DATA_ASM, BSS_ASM)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data, bss = tomllib.loads(out.read_text())["section"]
            self.assertEqual((data["name"], data["rom"], data["vram"], data["size"]), (".data", 0x1000, 0x80100000, 0x10))
            self.assertEqual((bss["name"], bss["vram"], bss["size"]), (".bss", 0x80200000, 0x10))
            self.assertNotIn("rom", bss)

    def test_renames_apply(self):
        with tempfile.TemporaryDirectory() as tmp:
            renames = Path(tmp) / "names.txt"
            renames.write_text("gFirst = 0x80100000;\n")
            out = Path(tmp) / "out.toml"
            asm = Path(tmp) / "in.s"
            asm.write_text(DATA_ASM)
            proc = subprocess.run([sys.executable, "-I", str(SCRIPT), str(out), "--renames", str(renames), str(asm)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            names = [s["name"] for s in tomllib.loads(out.read_text())["section"][0]["symbols"]]
            self.assertEqual(names, ["gFirst", "D_80100008"])

    def test_ambiguous_rename_keeps_labels(self):
        # A name given two addresses can't be trusted for either; both keep their D_ labels.
        with tempfile.TemporaryDirectory() as tmp:
            renames = Path(tmp) / "names.txt"
            renames.write_text("gTwice = 0x80100000;\ngTwice = 0x80100008;\n")
            out = Path(tmp) / "out.toml"
            asm = Path(tmp) / "in.s"
            asm.write_text(DATA_ASM)
            proc = subprocess.run([sys.executable, "-I", str(SCRIPT), str(out), "--renames", str(renames), str(asm)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            names = [s["name"] for s in tomllib.loads(out.read_text())["section"][0]["symbols"]]
            self.assertEqual(names, ["D_80100000", "D_80100008"])

    def test_duplicate_label_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc, _ = run(tmp, DATA_ASM, DATA_ASM)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("D_80100000", proc.stderr)


if __name__ == "__main__":
    unittest.main()
