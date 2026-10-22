#!/usr/bin/env python3
"""List tight polling loops in the splat disassembly: short backward branches whose loop body loads memory, makes
no calls and no stores, and has no induction variable (no register computed in the loop feeds the next iteration),
so only a change in memory made by someone else can end it. Under ultramodern's cooperative scheduling such loops never let the thread that
would end them run, so each real one needs a yield hook in mi.us.toml.

Usage: scripts/find_spins.py split/asm/1050.s
"""
import re
import sys

INSN = re.compile(r"/\* \w+ ([0-9A-F]{8}) [0-9A-F]{8} \*/\s+(\S+)\s*(.*)")
LABEL = re.compile(r"^\s*\.L([0-9A-F]{8}):")
GLABEL = re.compile(r"^glabel (\w+)")
LOADS = {"lb", "lbu", "lh", "lhu", "lw", "lwu", "ld"}
STORES = {"sb", "sh", "sw", "sd", "swc1", "sdc1"}


REG = re.compile(r"\$(\w+)")


def has_induction(body):
    """True if a register written by a non-load instruction in the loop is read before being written."""
    written, read_first = set(), set()
    alu_written = set()
    for _, _, op, args in body:
        regs = REG.findall(args.split("/*")[0])
        if not regs:
            continue
        if op.startswith("b") or op in STORES:
            dests, srcs = [], regs
        else:
            dests, srcs = regs[:1], regs[1:]
        for r in srcs:
            if r not in written and r != "zero":
                read_first.add(r)
        for r in dests:
            written.add(r)
            if op not in LOADS:
                alu_written.add(r)
    return bool(read_first & alu_written)


def main():
    func, func_start = None, 0
    insns = []  # (func, vram, op, args)
    labels = {}  # name -> vram
    for line in open(sys.argv[1]):
        m = GLABEL.match(line)
        if m:
            func = m.group(1)
            labels[func] = None  # resolved at the next instruction
            continue
        m = LABEL.match(line)
        if m:
            labels[".L" + m.group(1)] = int(m.group(1), 16)
            continue
        m = INSN.search(line)
        if m:
            vram = int(m.group(1), 16)
            if labels.get(func, 0) is None:
                labels[func] = vram
            insns.append((func, vram, m.group(2), m.group(3)))

    index = {vram: i for i, (_, vram, _, _) in enumerate(insns)}
    for i, (func, vram, op, args) in enumerate(insns):
        if not (op.startswith("b") and op != "break"):
            continue
        target_name = args.split(",")[-1].strip()
        target = labels.get(target_name)
        if target is None or target >= vram or vram - target > 0x40:
            continue
        body = insns[index[target]:i + 2]  # include the delay slot
        ops = [b[2] for b in body]
        if any(o in ("jal", "jalr") for o in ops) or any(o in STORES for o in ops):
            continue
        if not any(o in LOADS for o in ops):
            continue
        if has_induction(body):
            continue
        print(f"{func} loop {target:08X}-{vram:08X} branch {op} {args}")


if __name__ == "__main__":
    main()
