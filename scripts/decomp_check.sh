#!/usr/bin/env bash
# Decomp check run: play each of the 20 menu levels (scripts/level_test.sh) in a developer build with the
# side-by-side check on, then merge the per-level reports into <out_dir>/report.csv for scripts/decomp_progress.py.
#   scripts/decomp_check.sh <out_dir> [play_seconds]
# Needs a build configured with -DMI_DECOMP=ON (docs/DECOMP.md).
set -uo pipefail
cd "$(dirname "$0")/.."
out=${1:?out_dir}; play=${2:-25}
mkdir -p "$out"
out=$(realpath "$out")

# The 20 menu levels as (mission, index in the mission's menu list), from scripts/levels/level_table.py:
# Ice Hit 2, Recover NOC List 7, CIA Escape 4, Mole Hunt 3, Ice Storm 4.
levels="0:0 0:1 1:0 1:1 1:2 1:3 1:4 1:5 1:6 2:0 2:1 2:2 2:3 3:0 3:1 3:2 4:0 4:1 4:2 4:3"

for level in $levels; do
    m=${level%:*}; l=${level#*:}
    dir="$out/m${m}_l${l}"
    rm -rf "$dir"; mkdir -p "$dir"
    MI_DECOMP_CHECK=1 MI_DECOMP_REPORT="$dir/report.csv" scripts/level_test.sh "$dir" "$m" "$l" "$play"
    mismatches=$(grep -c MISMATCH "$dir/game.log" 2>/dev/null)
    echo "  mismatch_lines=${mismatches:-0}"
done

python3 -I - "$out" <<'PY'
import csv, sys
from pathlib import Path
out = Path(sys.argv[1])
merged, order = {}, []
for report in sorted(out.glob("m*_l*/report.csv")):
    with open(report, newline="") as f:
        for row in csv.DictReader(f):
            name = row["name"]
            if name not in merged:
                merged[name] = dict(row, calls=0, checked=0, mismatching_calls=0, first_mismatch="")
                order.append(name)
            m = merged[name]
            for key in ("calls", "checked", "mismatching_calls"):
                m[key] += int(row[key])
            if not m["first_mismatch"] and row["first_mismatch"]:
                m["first_mismatch"] = f"{report.parent.name} {row['first_mismatch']}"
fields = ["name", "method", "state", "calls", "checked", "mismatching_calls", "first_mismatch"]
with open(out / "report.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    for name in order:
        w.writerow({k: merged[name][k] for k in fields})
print(f"merged {len(list(out.glob('m*_l*/report.csv')))} level reports into {out / 'report.csv'}")
PY
