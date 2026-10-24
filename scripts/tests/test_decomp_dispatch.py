import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "decomp_dispatch.py"

# Synthetic N64Recomp-style output (made-up functions, not game code).
FUNCS_0 = """\
#include "recomp.h"
#include "funcs.h"

RECOMP_FUNC void Burp_Load(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 1;
;}

RECOMP_FUNC void Other(uint8_t* rdram, recomp_context* ctx) {
    Burp_Load(rdram, ctx);
;}
"""

FUNCS_1 = """\
#include "recomp.h"
#include "funcs.h"

RECOMP_FUNC void Untouched(uint8_t* rdram, recomp_context* ctx) {
    Burp_Load(rdram, ctx);
;}
"""

DECOMP_C = """\
#include "recomp.h"
#include "funcs.h"

RECOMP_FUNC void Burp_Load(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 1;
;}
"""

WAITER_C = """
RECOMP_FUNC void Waiter(uint8_t* rdram, recomp_context* ctx) {
    osRecvMesg_recomp(rdram, ctx);
;}
"""

HELPER_C = """
RECOMP_FUNC void Burp_Decode_00000000(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 1;
;}
"""

SYMS = """\
[[section]]
name = ".text"
rom = 0x00001000
vram = 0x80000400
size = 0x100

functions = [
    { name = "Burp_Load", vram = 0x80000400, size = 0x10 },
    { name = "Other", vram = 0x80000410, size = 0x10 },
    { name = "Untouched", vram = 0x80000420, size = 0x10 },
]
"""

HOOKS = """\
[[patches.hook]]
func = "Burp_Load"
before_vram = 0x80000408
text = "{ }"
"""

HOOK_2 = """
[[patches.hook]]
func = "Burp_Load"
before_vram = 0x8000040C
text = "{ more(); }"
"""


def moved(vram, text):
    """The hooks-moved flag for one hook: its address and the first 8 hex digits of the SHA-1 of its text."""
    return f"hooks-moved={vram:08X}.{hashlib.sha1(text.encode()).hexdigest()[:8]}"


class Fixture:
    def __init__(self, tmp, listing, hooks=""):
        self.root = Path(tmp)
        (self.root / "funcs").mkdir()
        (self.root / "funcs/funcs_0.c").write_text(FUNCS_0)
        (self.root / "funcs/funcs_1.c").write_text(FUNCS_1)
        (self.root / "decomp").mkdir()
        (self.root / "decomp/decomp.c").write_text(DECOMP_C)
        (self.root / "syms.toml").write_text(SYMS)
        (self.root / "hooks.toml").write_text(hooks)
        (self.root / "functions.txt").write_text(listing)
        self.out = self.root / "gen"

    def run(self):
        r = self.root
        return subprocess.run([sys.executable, "-I", str(SCRIPT), "--list", str(r / "functions.txt"),
                               "--funcs", str(r / "funcs"), "--decomp", str(r / "decomp"), "--syms", str(r / "syms.toml"),
                               "--hooks", str(r / "hooks.toml"), "--out", str(self.out)], capture_output=True, text=True)


class DecompDispatchTest(unittest.TestCase):
    def test_renames_definition_not_calls(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n")
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            text = (f.out / "RecompiledFuncs/funcs_0.c").read_text()
            self.assertIn("RECOMP_FUNC void Burp_Load__orig(uint8_t* rdram, recomp_context* ctx) {", text)
            self.assertIn("    Burp_Load(rdram, ctx);", text)
            self.assertNotIn("RECOMP_FUNC void Burp_Load(", text)
            self.assertIn("RECOMP_FUNC void Burp_Load__decomp(", (f.out / "decomp.c").read_text())
            self.assertIn('extern "C" void Burp_Load(uint8_t* rdram, recomp_context* ctx)', (f.out / "dispatch.cpp").read_text())

    def test_untouched_files_not_copied(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n")
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(sorted(p.name for p in (f.out / "RecompiledFuncs").iterdir()), ["funcs_0.c"])
            self.assertEqual((f.out / "sources.txt").read_text().split(), ["funcs_0.c"])

    def test_unknown_function_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Lod loading on check\n")
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("Burp_Lod", proc.stderr)

    def test_not_decompiled_fails(self):
        # Listed and in syms.toml, but nothing in decomp.c defines it.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Other loading on check\n")
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("Other", proc.stderr)

    def test_hooked_function_needs_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n", HOOKS)
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("Burp_Load", proc.stderr)
            self.assertIn("0x80000408", proc.stderr)
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, f"Burp_Load loading on check {moved(0x80000408, '{ }')}\n", HOOKS)
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_plain_hooks_moved_fails(self):
        # The flag must name each hook it covers, so a hook added or changed later can't slip through.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check hooks-moved\n", HOOKS)
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn(moved(0x80000408, "{ }"), proc.stderr)

    def test_new_hook_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, f"Burp_Load loading on check {moved(0x80000408, '{ }')}\n", HOOKS + HOOK_2)
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("0x8000040C", proc.stderr)

    def test_changed_hook_text_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, f"Burp_Load loading on check {moved(0x80000408, '{ old(); }')}\n", HOOKS)
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("0x80000408", proc.stderr)

    def test_stale_hook_flag_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, f"Burp_Load loading on check {moved(0x80000408, '{ }')}\n")
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("0x80000408", proc.stderr)

    def test_read_method_in_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "# waits on a message queue\nBurp_Load loading on read\n")
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("mi::decomp::Method::Read", (f.out / "dispatch.cpp").read_text())

    def test_unlisted_decompiled_function_fails(self):
        # Defined in decomp.c but not listed: it would replace the original with no switch, so it must be listed.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "# nothing listed\n")
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("Burp_Load", proc.stderr)

    def test_ret64_flag_in_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check ret64\n")
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("mi::decomp::Method::Check, true }", (f.out / "dispatch.cpp").read_text())
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n")
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("mi::decomp::Method::Check, false }", (f.out / "dispatch.cpp").read_text())

    def test_check_function_reaching_a_wait_fails(self):
        # Other -> Waiter -> osRecvMesg: a check function must never be able to wait, since check mode runs it twice.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n")
            (f.root / "funcs/funcs_1.c").write_text(FUNCS_1 + WAITER_C)
            (f.root / "decomp/decomp.c").write_text(DECOMP_C.replace("    ctx->r2 = 1;\n", "    Waiter(rdram, ctx);\n", 1))
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("Burp_Load", proc.stderr)
            self.assertIn("osRecvMesg", proc.stderr)

    def test_original_reaching_a_wait_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n")
            (f.root / "funcs/funcs_0.c").write_text(FUNCS_0.replace("    ctx->r2 = 1;\n", "    Waiter(rdram, ctx);\n", 1))
            (f.root / "funcs/funcs_1.c").write_text(FUNCS_1 + WAITER_C)
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("osRecvMesg", proc.stderr)

    def test_read_function_may_wait(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on read\n")
            (f.root / "funcs/funcs_1.c").write_text(FUNCS_1 + WAITER_C)
            (f.root / "decomp/decomp.c").write_text(DECOMP_C.replace("    ctx->r2 = 1;\n", "    Waiter(rdram, ctx);\n", 1))
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_helper_function_allowed(self):
        # A static helper in the decompiled C isn't a game function; it needs no line and is left as is.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading on check\n")
            (f.root / "decomp/decomp.c").write_text(DECOMP_C + HELPER_C)
            proc = f.run()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("RECOMP_FUNC void Burp_Decode_00000000(", (f.out / "decomp.c").read_text())

    def test_bad_line_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, "Burp_Load loading maybe check\n")
            proc = f.run()
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("maybe", proc.stderr)


if __name__ == "__main__":
    unittest.main()
