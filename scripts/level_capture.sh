#!/usr/bin/env bash
# Capture one level for reverse engineering: log every Rom_Read call from boot (MI_DMA_LOG), navigate to the level,
# take screenshots and dump the 8 MB of RDRAM (native word order) a few seconds in. Uses its own config folder.
#   scripts/level_capture.sh <out_dir> <mission 0-4> <level index in its list>
set -uo pipefail
cd "$(dirname "$0")/.."
out=$1; mission=$2; level=$3; mkdir -p "$out"
unlock=$(grep -o '0x8008A846[^"]*' scripts/level_test.sh | head -1)$(grep -o 'unlock+=",[^"]*' scripts/level_test.sh | sed 's/unlock+="//' | tr -d '\n')
cat > "$out/nav.sh" <<NAV
set -u
I=\$CAPTURE_DIR/input.txt
D=\$CAPTURE_WAYLAND_DISPLAY
press() { echo "\$1" > \$I; read -rt 0.3 <> <(:); : > \$I; read -rt \${2:-2.7} <> <(:); }
shot() { WAYLAND_DISPLAY=\$D grim -s 0.5 "\$CAPTURE_DIR/\$1.png"; }
read -rt 9 <> <(:)
press START; press A; press A; press A
for i in \$(seq 1 $mission); do press x=1; done
press A
for i in 1 2 3 4 5 6 7; do press y=1 0.6; done
for i in \$(seq 1 $level); do press y=-1 0.6; done
shot menu
echo "MENU_DONE \$(date +%s)" >> \$CAPTURE_DIR/marks.txt
press A 4; press START 4; press START 4; press A 6
shot level_start
read -rt 4 <> <(:)
shot level
$PWD/scripts/dumpram.sh \$CAPTURE_DIR/ram.bin
NAV
mkdir -p "$out/appdata"
# A fresh config folder doesn't auto-boot from MI_ROM on its first run; seed it with the stored ROM.
cp ~/.config/MIRecompiled/mi.n64.us.z64 "$out/appdata/"
APP_FOLDER_PATH="$out/appdata" MI_DMA_LOG="$out/dma.log" MI_POKE="$unlock" CAPTURE_EXEC="bash $out/nav.sh" CAPTURE_EXEC_AT=1 \
    timeout 200 scripts/capture.sh "$out" 3 1 > /dev/null 2>&1
rm -rf "$out/appdata" "$out/shot_1.png" "$out/input.txt"
echo "$out: $(wc -l < "$out/dma.log") reads, ram $(du -h "$out/ram.bin" | cut -f1), fatal=$(grep -c Fatal "$out/game.log")"
