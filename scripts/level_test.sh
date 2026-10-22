#!/usr/bin/env bash
# Smoke-test one level: unlock all levels, navigate the menus to mission <m> level <l>, play it with random input
# for a while, and report frame rate, crashes and screenshots.
#   scripts/level_test.sh <out_dir> <mission 0-4> <level index in its list> [play_seconds]
# Missions in menu order: 0 Ice Hit, 1 Recover NOC List, 2 CIA Escape, 3 Mole Hunt, 4 Ice Storm.
set -uo pipefail
cd "$(dirname "$0")/.."
out=${1:?out_dir}; mission=${2:?mission}; level=${3:?level}; play=${4:-25}
mkdir -p "$out"
# GameShark level enablers (USA): one flag byte per level in the level table.
unlock="0x8008A846=1,0x8008AB3E=1,0x8008A756=1,0x8008A77E=1,0x8008AB66=1,0x8008A5C6=1,0x8008A5DA=1,0x8008A602=1"
unlock+=",0x8008AB52=1,0x8008A7CE=1,0x8008AB2A=1,0x8008A652=1,0x8008A666=1,0x8008A67A=1,0x8008A68E=1,0x8008A6A2=1"
cat > "$out/nav.sh" <<NAV
set -u
I=\$CAPTURE_DIR/input.txt
press() { echo "\$1" > \$I; read -rt 0.3 <> <(:); : > \$I; read -rt \${2:-2.7} <> <(:); }
shot() { WAYLAND_DISPLAY=\$CAPTURE_WAYLAND_DISPLAY grim -s 0.5 "\$CAPTURE_DIR/\$1.png"; }
press START; press A; press A; press A
for i in \$(seq 1 $mission); do press x=1; done
press A
for i in 1 2 3 4 5 6 7; do press y=1 0.6; done
for i in \$(seq 1 $level); do press y=-1 0.6; done
shot menu
press A 4; press START 4; press START 4; press A 6
shot level_start
inputs=("y=1" "y=1 mx=8" "x=1" "R" "R mx=6" "R my=3" "Z" "A" "y=-1" "mx=-10" "R Z" "y=1 rx=0.6" "" "CD")
end=\$((SECONDS + $play))
while [[ \$SECONDS -lt \$end ]]; do
    echo "\${inputs[\$((RANDOM % \${#inputs[@]}))]}" > \$I
    read -rt 0.\$((4 + RANDOM % 5)) <> <(:) || true
done
: > \$I
shot level_end
NAV
MI_POKE="$unlock" MI_STATS=1 CAPTURE_EXEC="bash $out/nav.sh" CAPTURE_EXEC_AT=4 \
    timeout $((120 + play)) scripts/capture.sh "$out" 3 4 > /dev/null 2>&1
fps=$(grep stats "$out/game.log" | awk '{print $4}' | tail -$((play - 2)) | sort -n | awk '{a[NR]=$1} END {printf "min=%s median=%s", a[1], a[int(NR/2)+1]}')
echo "mission=$mission level=$level fatal=$(grep -c Fatal "$out/game.log") $fps"
