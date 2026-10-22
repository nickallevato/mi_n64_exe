#!/usr/bin/env bash
# Run from capture.sh's CAPTURE_EXEC: snapshot game RAM with gdb around held inputs.
#   ramdiff_session.sh <out_dir> <input_file> [label input seconds]...
# Writes <out_dir>/<label>_before.bin and <label>_after.bin for each step.
set -uo pipefail
out=$1; input_file=$2; shift 2
pid=$(pgrep -x MIRecompiled | head -1)
snap() {
    sudo -n gdb -p "$pid" -batch -ex 'set $r = *(char**)&stats_rdram' \
        -ex "dump binary memory $1 \$r \$r+0x800000" > /dev/null 2>&1
}
while [[ $# -ge 3 ]]; do
    label=$1; input=$2; secs=$3; shift 3
    snap "$out/${label}_before.bin"
    echo "$input" > "$input_file"
    read -rt "$secs" <> <(:) || true
    : > "$input_file"
    read -rt 0.5 <> <(:) || true
    snap "$out/${label}_after.bin"
done
