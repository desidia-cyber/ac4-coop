"""From the hub: accept the mission and wait for gameplay.

    python e8_go.py <shots-tag> [wait-after-gameplay-seconds]

The mission-accept dialog focuses CANCEL, so it takes A, LEFT, A (HANDOFF section 5.4).
"""
import glob
import os
import sys
import time

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
tag = sys.argv[1]
wait_after = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
shots = os.path.join(base, "run", "shots", tag)
rig = os.path.join(base, "run", "rig", "coop.txt")
runs = os.path.join(base, "run", "data", "runs")


def log_path():
    return sorted(glob.glob(os.path.join(runs, "*")), key=os.path.getmtime)[-1] + r"\ac4.log"


def log_text():
    try:
        return open(log_path(), errors="ignore").read()
    except OSError:
        return ""


def clock():
    try:
        return float(open(os.path.join(shots, "clock.txt")).read().strip())
    except OSError:
        return 0.0


def press(button, lead=1.8, settle=2.2):
    at = clock() + lead
    with open(rig, "a", newline="\n") as f:
        f.write("%.2f 0 %s 0 0 0 0\n" % (at, button))
        f.write("%.2f 0 none 0 0 0 0\n" % (at + 0.15))
    time.sleep(lead + settle)


def wait_log(text, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        if text in log_text():
            print("%s at clock %.1f" % (what, clock()), flush=True)
            return True
        time.sleep(2)
    print("TIMEOUT waiting for %s at clock %.1f" % (what, clock()), flush=True)
    return False


wait_log("MissionSelectScene", 120, "hub")
time.sleep(4)
press("A")        # mission -> accept dialog (CANCEL focused)
time.sleep(2)
press("LEFT")     # OK
press("A")        # accept
wait_log("Ac4SortieScene", 120, "sortie scene")
if wait_log("input context gameplay", 300, "gameplay"):
    print("clock at gameplay %.1f" % clock(), flush=True)

if wait_after > 0:
    end = time.time() + wait_after
    while time.time() < end:
        if "fatal]" in log_text():
            print("CRASH detected at clock %.1f" % clock(), flush=True)
            break
        time.sleep(3)
    print("end of wait at clock %.1f" % clock(), flush=True)
