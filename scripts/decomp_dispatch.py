#!/usr/bin/env python3
"""Generate the dev-build glue that keeps both versions of each decompiled function callable.

For every function F in decomp/functions.txt:
- the recompiled original's definition is renamed F__orig, in copies of only the RecompiledFuncs files that define
  one (RecompiledFuncs/ itself is untouched, so release builds are unaffected),
- the decompiled version's definition in RecompiledDecomp/decomp.c is renamed F__decomp,
- dispatch.cpp defines F itself, which calls mi::decomp::call (src/decomp/dispatch.cpp) to pick one.
Only definition lines are renamed; calls keep the plain name, so every call goes through the dispatcher.

    decomp_dispatch.py --list decomp/functions.txt --funcs RecompiledFuncs --decomp RecompiledDecomp
                       --syms syms.toml --hooks mi.us.toml --out build/decomp/gen

functions.txt: `name system on|off check|read [flags]`, one per line; `#` starts a comment. Flags (comma-separated):
`hooks-moved=VRAM.HASH`, one per mi.us.toml hook in the function (the hook now lives in the decompiled C; the hash is
of the hook's text, and the build error prints the exact flag), and `ret64`, for a function returning a 64-bit value
(s64, u64, double), whose high return words the check then compares too.
"""
import argparse
import hashlib
import re
import shutil
import sys
import tomllib
from pathlib import Path

DEF_RE = re.compile(r"^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{$", re.M)
STATES = ("on", "off")
METHODS = {"check": "Check", "read": "Read"}
FLAGS = ("ret64",)
CALL_RE = re.compile(r"^\s+(\w+)\(rdram, ctx\);$", re.M)
# Runtime functions a check function may reach: they only compute or touch RAM through the rdram they're given. Any
# other call outside the game's own code (message queues, threads, DMA, native imports) may wait or act outside RAM,
# and check mode runs check functions twice.
PURE_NATIVE = {"memcpy_recomp", "bzero_recomp", "bcopy_recomp", "strlen_recomp", "strchr_recomp",
               "osVirtualToPhysical_recomp", "osWritebackDCache_recomp", "osWritebackDCacheAll_recomp",
               "osInvalDCache_recomp", "osInvalICache_recomp"}


def hook_flag(hook):
    """The hooks-moved flag for one [[patches.hook]]: its address and a hash of its text, so a hook added or changed
    after the decompiled C took it over fails the build."""
    digest = hashlib.sha1(hook.get("text", "").encode()).hexdigest()[:8]
    return f"hooks-moved={hook.get('before_vram', 0):08X}.{digest}"


def parse_list(path, errors):
    entries = []
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        line = line.split("#")[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) not in (4, 5):
            errors.append(f"{path}:{n}: expected `name system state method [flags]`: {line}")
            continue
        name, system, state, method = fields[:4]
        flags = fields[4].split(",") if len(fields) == 5 else []
        if state not in STATES:
            errors.append(f"{path}:{n}: state must be on or off, not {state}")
        if method not in METHODS:
            errors.append(f"{path}:{n}: method must be check or read, not {method}")
        for flag in flags:
            if flag not in FLAGS and not flag.startswith("hooks-moved="):
                errors.append(f"{path}:{n}: unknown flag {flag}")
        if any(e["name"] == name for e in entries):
            errors.append(f"{path}:{n}: {name} listed twice")
        entries.append({"name": name, "system": system, "state": state, "method": method, "flags": flags})
    return entries


def rename_definitions(text, names, suffix):
    return DEF_RE.sub(lambda m: m.group(0).replace(f" {m.group(1)}(", f" {m.group(1)}{suffix}(", 1)
                      if m.group(1) in names else m.group(0), text)


def function_bodies(text):
    """{name: body text} for each RECOMP_FUNC definition in a generated C file."""
    defs = list(DEF_RE.finditer(text))
    return {m.group(1): text[m.end():defs[i + 1].start() if i + 1 < len(defs) else len(text)]
            for i, m in enumerate(defs)}


def find_wait(start, orig_calls, decomp_calls, indirect):
    """Path from start to a call that isn't game code or a pure runtime helper, following both the original and the
    decompiled version of each function. Returns (path or None, functions with indirect calls on the way)."""
    seen, queue, indirect_seen = {start}, [[start]], []
    while queue:
        path = queue.pop(0)
        name = path[-1]
        if name in indirect:
            indirect_seen.append(name)
        for callee in sorted(orig_calls.get(name, set()) | decomp_calls.get(name, set())):
            if callee in seen:
                continue
            seen.add(callee)
            if callee not in orig_calls and callee not in decomp_calls:
                if callee not in PURE_NATIVE:
                    return path + [callee], indirect_seen
                continue
            queue.append(path + [callee])
    return None, indirect_seen


def dispatch_cpp(entries):
    out = ['// Generated by scripts/decomp_dispatch.py from decomp/functions.txt. Do not edit.',
           '#include "recomp.h"', '#include "decomp/dispatch.hpp"', '', 'extern "C" {']
    for e in entries:
        out.append(f'void {e["name"]}__orig(uint8_t* rdram, recomp_context* ctx);')
        out.append(f'void {e["name"]}__decomp(uint8_t* rdram, recomp_context* ctx);')
    out += ['}', '', 'const mi::decomp::Entry mi_decomp_entries[] = {']
    for e in entries:
        out.append(f'    {{ "{e["name"]}", {e["name"]}__orig, {e["name"]}__decomp, '
                   f'mi::decomp::Method::{METHODS[e["method"]]}, {"true" if "ret64" in e["flags"] else "false"} }},')
    out += ['};', f'const int mi_decomp_entry_count = {len(entries)};', '']
    for i, e in enumerate(entries):
        out.append(f'extern "C" void {e["name"]}(uint8_t* rdram, recomp_context* ctx) '
                   f'{{ mi::decomp::call({i}, rdram, ctx); }}')
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    for opt in ("list", "funcs", "decomp", "syms", "hooks", "out"):
        ap.add_argument(f"--{opt}", required=True)
    args = ap.parse_args()

    errors = []
    entries = parse_list(args.list, errors)
    names = {e["name"] for e in entries}

    syms = tomllib.loads(Path(args.syms).read_text())
    known = {f["name"] for s in syms["section"] for f in s["functions"]}
    hooks = tomllib.loads(Path(args.hooks).read_text()).get("patches", {}).get("hook", [])

    decomp_path = Path(args.decomp) / "decomp.c"
    decomp_text = decomp_path.read_text()
    decompiled = set(DEF_RE.findall(decomp_text))

    for e in entries:
        if e["name"] not in known:
            errors.append(f"{e['name']}: not a game function in {args.syms}")
        elif e["name"] not in decompiled:
            errors.append(f"{e['name']}: listed but not defined in {decomp_path} (run make -C decomp?)")
        expected = {hook_flag(hook): hook for hook in hooks if hook.get("func") == e["name"]}
        moved = {flag for flag in e["flags"] if flag.startswith("hooks-moved=")}
        for flag, hook in expected.items():
            if flag not in moved:
                errors.append(f"{e['name']}: mi.us.toml hooks it at 0x{hook.get('before_vram', 0):08X}; move the hook "
                              f"into the decompiled C and add the flag {flag}")
        for flag in sorted(moved - expected.keys()):
            vram = flag.split("=")[1].split(".")[0]
            errors.append(f"{e['name']}: {flag} matches no hook in mi.us.toml (the hook at 0x{vram} changed or was "
                          f"removed); update the decompiled C and the flag")
    # A decompiled game function must be listed, or it would replace the original with no switch. Other functions in
    # decomp.c are helpers of our own and need no line.
    for name in sorted((decompiled & known) - names):
        errors.append(f"{name}: decompiled but not listed in {args.list}")

    copies = {}
    orig_calls, indirect = {}, set()
    for path in sorted(Path(args.funcs).glob("*.c")):
        text = path.read_text()
        for name, body in function_bodies(text).items():
            orig_calls[name] = set(CALL_RE.findall(body))
            if "LOOKUP_FUNC" in body:
                indirect.add(name)
        defined = set(DEF_RE.findall(text)) & names
        if defined:
            copies[path.name] = rename_definitions(text, defined, "__orig")
    decomp_calls = {}
    for name, body in function_bodies(decomp_text).items():
        decomp_calls[name] = set(CALL_RE.findall(body))
        if "LOOKUP_FUNC" in body:
            indirect.add(name)
    for e in entries:
        if e["method"] != "check" or e["name"] not in known:
            continue
        path, indirect_seen = find_wait(e["name"], orig_calls, decomp_calls, indirect)
        if path:
            errors.append(f"{e['name']}: check function can reach {path[-1]} ({' -> '.join(path)}), which may wait "
                          f"or act outside RAM; check mode runs it twice. Make it read")
        for name in indirect_seen:
            print(f"warning: {e['name']}: {name} calls through a function pointer, which the wait check can't follow",
                  file=sys.stderr)
    found = set()
    for text in copies.values():
        found |= {n[:-len("__orig")] for n in re.findall(r"RECOMP_FUNC void (\w+__orig)\(", text)}
    for name in sorted(names & known - found):
        errors.append(f"{name}: no recompiled definition found in {args.funcs}")

    if errors:
        for err in errors:
            print(f"error: {err}", file=sys.stderr)
        sys.exit(1)

    out = Path(args.out)
    shutil.rmtree(out, ignore_errors=True)
    (out / "RecompiledFuncs").mkdir(parents=True)
    for name, text in copies.items():
        (out / "RecompiledFuncs" / name).write_text(text)
    (out / "sources.txt").write_text("".join(f"{name}\n" for name in copies))
    (out / "decomp.c").write_text(rename_definitions(decomp_text, names, "__decomp"))
    (out / "dispatch.cpp").write_text(dispatch_cpp(entries))
    print(f"dispatch: {len(entries)} functions, {len(copies)} recompiled files copied to {out}")


if __name__ == "__main__":
    main()
