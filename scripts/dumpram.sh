#!/usr/bin/env bash
# dumpram.sh <out.bin>: dump the running game's 8 MB RDRAM (native word order) via gdb.
pid=$(pgrep -x MIRecompiled | head -1)
sudo -n gdb -p "$pid" -batch -ex 'set $r = *(char**)&stats_rdram' -ex "dump binary memory $1 \$r \$r+0x800000" > /dev/null 2>&1
