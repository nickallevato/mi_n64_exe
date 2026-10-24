#!/usr/bin/env python3
"""Decomp progress: how much of the game is decompiled, per system, and how each function was checked.

    decomp_progress.py [--list decomp/functions.txt] [--syms syms.toml] [--report report.csv]

--report is a check report (MI_DECOMP_REPORT, or the merged one from scripts/decomp_check.sh). Each function's status:
  checked in game     method check, checked at least once, never mismatched
  checked by reading  method read (never run twice; checked by reading it against the asm)
  never reached       method check, but the check run never called it
  not checked         method check, and no report given
  MISMATCH            the check found a difference
Exits 1 if any function is MISMATCH.
"""
import argparse
import csv
import sys
import tomllib
from pathlib import Path


def parse_list(path):
    entries = []
    for line in Path(path).read_text().splitlines():
        fields = line.split("#")[0].split()
        if len(fields) >= 4:
            entries.append({"name": fields[0], "system": fields[1], "method": fields[3]})
    return entries


def status(entry, report):
    if entry["method"] == "read":
        return "checked by reading"
    if report is None:
        return "not checked"
    row = report.get(entry["name"])
    if row is None or int(row["checked"]) == 0:
        return "never reached"
    if int(row["mismatching_calls"]) > 0:
        return "MISMATCH"
    return "checked in game"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", default="decomp/functions.txt")
    ap.add_argument("--syms", default="syms.toml")
    ap.add_argument("--report")
    args = ap.parse_args()

    syms = tomllib.loads(Path(args.syms).read_text())
    sizes = {f["name"]: f["size"] for s in syms["section"] for f in s["functions"]}
    entries = parse_list(args.list)
    report = None
    if args.report:
        with open(args.report, newline="") as f:
            report = {row["name"]: row for row in csv.DictReader(f)}

    width = max((len(e["name"]) for e in entries), default=4)
    systems = {}
    mismatch = False
    for e in entries:
        st = status(e, report)
        mismatch |= st == "MISMATCH"
        print(f"{e['name']:<{width}}  {e['system']:<10}  {st}")
        count, insns = systems.get(e["system"], (0, 0))
        systems[e["system"]] = (count + 1, insns + sizes.get(e["name"], 0) // 4)

    print()
    for system, (count, insns) in systems.items():
        print(f"{system}: {count} functions, {insns} instructions")
    done_insns = sum(insns for _, insns in systems.values())
    total_insns = sum(sizes.values()) // 4
    print(f"all: {len(entries)} of {len(sizes)} functions, {done_insns} of {total_insns} instructions")
    sys.exit(1 if mismatch else 0)


if __name__ == "__main__":
    main()
