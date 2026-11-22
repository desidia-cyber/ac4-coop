"""Live experiment on an already-running attract loop: press LEFT+A in the gap after a movie closes.

    python e8_gap.py <shots-tag> [tries]
"""
import glob
import os
import sys
import time

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
tag = sys.argv[1]
tries = int(sys.argv[2]) if len(sys.argv) > 2 else 6
shots = os.path.join(base, "run", "shots", tag)
runs = os.path.join(base, "run", "data", "runs")


def log_all():
    p = sorted(glob.glob(os.path.join(runs, "*")), key=os.path.getmtime)[-1] + r"\ac4.log"
    try:
        return open(p, errors="ignore").read()
    except OSError:
        return ""


def clock():
    try:
        return float(open(os.path.join(shots, "clock.txt")).read().strip())
    except OSError:
        return 0.0


def cmd(lines):
    with open(os.path.join(shots, "cmd.txt"), "a", newline="\n") as f:
        for line in lines:
            f.write(line + "\n")


closes = log_all().count("vdec] close handle")
print("start: %d closes, clock %.1f" % (closes, clock()), flush=True)
for i in range(tries * 60):
    t = log_all()
    if t.count("vdec] close handle") > closes:
        closes = t.count("vdec] close handle")
        print("close seen at clock %.1f -> LEFT, A" % clock(), flush=True)
        cmd(["120 LEFT", "350 none", "120 A", "600 none"])
        end = time.time() + 25
        while time.time() < end:
            if "MissionSelectScene" in log_all():
                print("HUB at clock %.1f" % clock(), flush=True)
                sys.exit(0)
            time.sleep(1)
        print("  no hub; %s" % ("save read" if "read ACDES.DAT" in log_all() else "list idle"))
    if "MissionSelectScene" in t:
        print("HUB at clock %.1f" % clock(), flush=True)
        sys.exit(0)
    time.sleep(0.5)
print("gave up at clock %.1f" % clock(), flush=True)
sys.exit(1)
