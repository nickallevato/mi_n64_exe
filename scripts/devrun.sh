#!/usr/bin/env bash
# Launch the game, keep its window raised, run a command against it after a delay, then kill it.
#   scripts/devrun.sh <seconds> [command ...]
# The game's log goes to build/devrun.log; extra env vars (MI_STATS, MI_WATCH, MI_INPUT, ...) pass through.
# The launcher stays in the foreground for the game's whole lifetime: orphaned launches stall during startup.
set -uo pipefail
cd "$(dirname "$0")/.."

delay=${1:?seconds}
shift

MI_ROM="${MI_ROM:-$PWD/rom/Mission Impossible (USA).z64}" SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}" \
    ./build/app/MIRecompiled > build/devrun.log 2>&1 &
pid=$!

for _ in $(seq 1 40); do
    win=$(xdotool search --pid "$pid" --name "Mission" 2>/dev/null | head -1)
    [[ -n "$win" ]] && break
    read -rt 0.25 <> <(:) || true
done
# The compositor throttles hidden windows to ~1 fps, which stalls the game; keep it raised.
[[ -n "${win:-}" ]] && xdotool windowactivate --sync "$win" windowraise "$win" 2>/dev/null

read -rt "$delay" <> <(:) || true
if [[ $# -gt 0 ]]; then
    "$@"
fi

kill -9 "$pid" 2>/dev/null  # the runtime ignores SIGTERM
wait "$pid" 2>/dev/null
