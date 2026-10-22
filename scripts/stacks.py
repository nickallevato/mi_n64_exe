#!/usr/bin/env python3
"""Print a compact call stack for every recompiled game thread of a running MIRecompiled.

Usage: scripts/stacks.py [samples] [interval_seconds]
Attaches gdb (via sudo) to the running process; only frames from recompiled/runtime code are kept.
"""
import re
import subprocess
import sys
import time

KEEP = re.compile(r"func_|_recomp|^os|^__os|Sched_|Game_|game_|recomp_entrypoint|do_recv|do_send|yield|wait_for|vi_|pause_self")


def sample(pid):
    out = subprocess.run(
        ["sudo", "-n", "gdb", "-p", str(pid), "-batch", "-ex", "set pagination off", "-ex", "thread apply all bt 20"],
        capture_output=True, text=True).stdout
    threads = []
    name, frames = None, []
    for line in out.splitlines():
        m = re.match(r'Thread \d+ \(Thread 0x[0-9a-f]+ \(LWP \d+\) "([^"]*)"\)', line)
        if m:
            if name is not None:
                threads.append((name, frames))
            name, frames = m.group(1), []
            continue
        m = re.match(r"#\d+\s+(?:0x[0-9a-f]+ in )?([\w:~<>]+)", line)
        if m and name is not None:
            fn = m.group(1).split("::")[-1]
            if KEEP.search(fn):
                frames.append(fn)
    if name is not None:
        threads.append((name, frames))
    return [(n, f) for n, f in threads if n.startswith("[Game]")]


def main():
    samples = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    pid = subprocess.run(["pgrep", "-x", "MIRecompiled"], capture_output=True, text=True).stdout.split()
    if not pid:
        sys.exit("MIRecompiled is not running")
    if len(pid) > 1:
        sys.exit(f"several MIRecompiled processes are running: {pid}")
    for i in range(samples):
        print(f"--- sample {i + 1}")
        for name, frames in sorted(sample(pid[0])):
            print(f"{name:10s} {' <- '.join(frames)}")
        if i + 1 < samples:
            time.sleep(interval)


if __name__ == "__main__":
    main()
