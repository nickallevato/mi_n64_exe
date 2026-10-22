#!/usr/bin/env bash
# Launch the game with auto-boot, take window screenshots at intervals, then quit.
#   scripts/capture.sh <out_dir> <seconds_between_shots> <shot_count> [key ...]
# Each optional key (an xdotool key name, e.g. Return) is held for 1 s after the corresponding shot,
# so `capture.sh out 5 4 Return` presses Return after the first screenshot.
# Requires an X11/XWayland display, xdotool and ImageMagick's `import`.
set -uo pipefail
cd "$(dirname "$0")/.."

out_dir=${1:?out_dir}
interval=${2:-5}
count=${3:-3}
shift 3 || true
keys=("$@")

mkdir -p "$out_dir"
MI_ROM="${MI_ROM:-$PWD/rom/Mission Impossible (USA).z64}" SDL_VIDEODRIVER=x11 \
    ./build/app/MIRecompiled > "$out_dir/game.log" 2>&1 &
pid=$!

win=""
for _ in $(seq 1 60); do
    win=$(xdotool search --pid "$pid" --name "Mission" 2>/dev/null | head -1)
    [[ -n "$win" ]] && break
    kill -0 "$pid" 2>/dev/null || break
    read -rt 0.5 <> <(:) || true
done

# Keep the window raised and focused: the compositor throttles presentation of hidden windows to ~1 fps,
# which stalls the game (it waits on each frame).
[[ -n "$win" ]] && xdotool windowactivate --sync "$win" windowraise "$win" 2>/dev/null

for i in $(seq 1 "$count"); do
    read -rt "$interval" <> <(:) || true
    kill -0 "$pid" 2>/dev/null || { echo "game exited early"; break; }
    [[ -n "$win" ]] && import -window "$win" "$out_dir/shot_$i.png" 2>/dev/null
    key=${keys[$((i - 1))]:-}
    if [[ -n "$key" && -n "$win" ]]; then
        xdotool windowactivate --sync "$win" keydown "$key"
        read -rt 1 <> <(:) || true
        xdotool keyup "$key"
    fi
done

kill -9 "$pid" 2>/dev/null  # the runtime ignores SIGTERM
wait "$pid" 2>/dev/null
echo "exit: $? window: ${win:-none}"
ls "$out_dir"
