import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "decomp_progress.py"

# Synthetic inputs (made-up functions).
SYMS = """\
[[section]]
name = ".text"
rom = 0x00001000
vram = 0x80000400
size = 0x1000

functions = [
    { name = "A_Checked", vram = 0x80000400, size = 0x10 },
    { name = "A_Read", vram = 0x80000410, size = 0x20 },
    { name = "B_Unreached", vram = 0x80000430, size = 0x40 },
    { name = "B_Bad", vram = 0x80000470, size = 0x80 },
    { name = "C_NotDone", vram = 0x800004F0, size = 0x100 },
]
"""

LISTING = """\
A_Checked    alpha  on  check
# waits on a message
A_Read       alpha  on  read
B_Unreached  beta   on  check
B_Bad        beta   on  check
"""

REPORT = """\
name,method,state,calls,checked,mismatching_calls,first_mismatch
A_Checked,check,on,5,5,0,
A_Read,read,on,3,0,0,
B_Unreached,check,on,0,0,0,
B_Bad,check,on,4,4,2,call 1: r2 orig=1 decomp=2
"""


def run(tmp, report=REPORT):
    root = Path(tmp)
    (root / "syms.toml").write_text(SYMS)
    (root / "functions.txt").write_text(LISTING)
    args = [sys.executable, "-I", str(SCRIPT), "--list", str(root / "functions.txt"), "--syms", str(root / "syms.toml")]
    if report is not None:
        (root / "report.csv").write_text(report)
        args += ["--report", str(root / "report.csv")]
    return subprocess.run(args, capture_output=True, text=True)


def status_of(output, name):
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[0] == name:
            return " ".join(fields[2:])
    return None


class DecompProgressTest(unittest.TestCase):
    def test_status_classes(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = run(tmp)
            self.assertEqual(status_of(proc.stdout, "A_Checked"), "checked in game")
            self.assertEqual(status_of(proc.stdout, "A_Read"), "checked by reading")
            self.assertEqual(status_of(proc.stdout, "B_Unreached"), "never reached")
            self.assertEqual(status_of(proc.stdout, "B_Bad"), "MISMATCH")

    def test_instruction_totals(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = run(tmp)
            # alpha: 0x10 + 0x20 bytes = 12 instructions; all: 4 of 5 functions, 60 of 124 instructions.
            self.assertIn("alpha: 2 functions, 12 instructions", proc.stdout)
            self.assertIn("beta: 2 functions, 48 instructions", proc.stdout)
            self.assertIn("all: 4 of 5 functions, 60 of 124 instructions", proc.stdout)

    def test_mismatch_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(run(tmp).returncode, 1)
            clean = REPORT.replace("B_Bad,check,on,4,4,2,call 1: r2 orig=1 decomp=2", "B_Bad,check,on,4,4,0,")
            self.assertEqual(run(tmp, clean).returncode, 0)

    def test_without_report(self):
        # No check run yet: check functions are "not checked", read ones still "checked by reading".
        with tempfile.TemporaryDirectory() as tmp:
            proc = run(tmp, None)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(status_of(proc.stdout, "A_Checked"), "not checked")
            self.assertEqual(status_of(proc.stdout, "A_Read"), "checked by reading")


if __name__ == "__main__":
    unittest.main()
