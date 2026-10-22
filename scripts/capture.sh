#!/usr/bin/env bash
# Run the game inside a headless sway compositor (real GPU, native Wayland, invisible to the desktop), take
# screenshots at intervals, optionally inject controller input, then shut everything down.
#   scripts/capture.sh <out_dir> <seconds_between_shots> <shot_count> [input ...]
# Each optional input (N64 buttons/stick for MI_INPUT, e.g. START or "A x=1") is held for 1 s after the
# corresponding shot, so `capture.sh out 5 4 START` presses Start after the first screenshot. Use "" to skip.
# Extra env vars (MI_STATS, MI_WATCH, ...) pass through to the game. CAPTURE_EXEC="cmd" runs a command (e.g.
# scripts/stacks.py) right after shot CAPTURE_EXEC_AT (default: the last shot), with CAPTURE_DIR and
# CAPTURE_WAYLAND_DISPLAY (for grim) set. Requires sway and grim.
set -uo pipefail
cd "$(dirname "$0")/.."

out_dir=${1:?out_dir}
interval=${2:-5}
count=${3:-3}
shift 3 || true
inputs=("$@")

mkdir -p "$out_dir"
out_dir=$(realpath "$out_dir")
input_file="$out_dir/input.txt"
: > "$input_file"

sway_dir=$(mktemp -d)
pid=""
sway_pid=""
# Never leave the game or compositor running, even if this script is killed (e.g. by timeout).
cleanup() {
    [[ -n "$pid" ]] && kill -9 "$pid" 2>/dev/null
    [[ -n "$sway_pid" ]] && kill -9 "$sway_pid" 2>/dev/null
    rm -rf "$sway_dir"
}
trap cleanup EXIT INT TERM
printf "output HEADLESS-1 resolution ${CAPTURE_RES:-1600x900}\ndefault_border none\n" > "$sway_dir/config"
SWAYSOCK="$sway_dir/ipc.sock" WLR_BACKENDS=headless WLR_RENDERER=vulkan WLR_LIBINPUT_NO_DEVICES=1 WAYLAND_DISPLAY= DISPLAY= \
    sway --unsupported-gpu -d -c "$sway_dir/config" > "$sway_dir/sway.log" 2>&1 &
sway_pid=$!

display=""
for _ in $(seq 1 40); do
    display=$(grep -oP "Running compositor on wayland display '\K[^']+" "$sway_dir/sway.log" 2>/dev/null)
    [[ -n "$display" ]] && break
    read -rt 0.25 <> <(:) || true
done
if [[ -z "$display" ]]; then
    echo "sway failed to start:"
    tail -5 "$sway_dir/sway.log"
    exit 1
fi

WAYLAND_DISPLAY="$display" DISPLAY= SDL_VIDEODRIVER=wayland MI_INPUT="$input_file" \
    MI_ROM="${MI_ROM:-$PWD/rom/Mission Impossible (USA).z64}" \
    "${CAPTURE_BIN:-./build/app/MIRecompiled}" > "$out_dir/game.log" 2>&1 &
pid=$!

for i in $(seq 1 "$count"); do
    read -rt "$interval" <> <(:) || true
    kill -0 "$pid" 2>/dev/null || { echo "game exited early"; break; }
    WAYLAND_DISPLAY="$display" grim "$out_dir/shot_$i.png" 2>/dev/null
    if [[ -n "${CAPTURE_EXEC:-}" && "$i" == "${CAPTURE_EXEC_AT:-$count}" ]]; then
        CAPTURE_WAYLAND_DISPLAY="$display" CAPTURE_DIR="$out_dir" bash -c "$CAPTURE_EXEC"
    fi
    input=${inputs[$((i - 1))]:-}
    if [[ -n "$input" ]]; then
        echo "$input" > "$input_file"
        read -rt 1 <> <(:) || true
        : > "$input_file"
    fi
done

ls "$out_dir"
