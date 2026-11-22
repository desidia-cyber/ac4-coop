"""Live: wait for a movie close and press START in the gap; report what the screen does.

    python e8_start.py <shots-tag> [tries]
"""
import glob
import os
import sys
import time

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
tag = sys.argv[1]
tries = int(sys.argv[2]) if len(sys.argv) > 2 else 4
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


def newest_shot():
    fs = sorted(glob.glob(os.path.join(shots, "shot_*.png")))
    return fs[-1] if fs else None


closes = log_all().count("vdec] close handle")
print("start: %d closes at clock %.1f" % (closes, clock()), flush=True)
for _ in range(tries * 120):
    t = log_all()
    if t.count("vdec] close handle") > closes:
        closes = t.count("vdec] close handle")
        before = newest_shot()
        print("close at clock %.1f -> START" % clock(), flush=True)
        cmd(["150 START", "900 none"])
        time.sleep(3)
        after = newest_shot()
        print("  shot %s -> %s ; menu=%s hub=%s" % (os.path.basename(before or "-"), os.path.basename(after or "-"),
                                                    "input context menu" in t, "MissionSelectScene" in t), flush=True)
        time.sleep(4)
        t2 = log_all()
        if "MissionSelectScene" in t2:
            print("HUB at clock %.1f" % clock(), flush=True)
            sys.exit(0)
        if "vdec] open handle" not in "\n".join(t2.splitlines()[-4:]):
            print("  no new movie started -> pressing A (LOAD GAME)" % clock(), flush=True)
            cmd(["150 A", "1500 none"])
            time.sleep(6)
            if "MissionSelectScene" in log_all():
                print("HUB at clock %.1f" % clock(), flush=True)
                sys.exit(0)
    if "MissionSelectScene" in t:
        print("HUB at clock %.1f" % clock(), flush=True)
        sys.exit(0)
    time.sleep(0.5)
print("gave up at clock %.1f" % clock(), flush=True)
sys.exit(1)
