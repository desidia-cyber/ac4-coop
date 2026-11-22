"""Drive one campaign mission into gameplay, then wait for the mission end.

    python drive_mission.py <shots-tag> [wait-seconds]

Used for the E7 control run: the attract movie must be allowed to finish (skipping it leaves the
briefing movie black and stalled), the save is loaded from the title menu, the highlighted mission
(SWARMS OF RED EYES) is accepted, and the briefing is left to play through.
"""
import glob
import os
import sys
import time
from PIL import Image

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


def newest():
    fs = sorted(glob.glob(os.path.join(shots, "shot_*.png")))
    return fs[-1] if fs else None


def menu_score():
    p = newest()
    if not p:
        return 0
    px = Image.open(p).convert("RGB").load()
    g = 0
    for y in range(505, 566, 2):
        for x in range(540, 761, 2):
            r, gg, b = px[x, y]
            if r > 120 and r - b > 50:
                g += 1
    return g


def press(button, lead=1.5):
    at = clock() + lead
    with open(rig, "a", newline="\n") as f:
        f.write("%.2f 0 %s 0 0 0 0\n" % (at, button))
        f.write("%.2f 0 none 0 0 0 0\n" % (at + 0.15))
    time.sleep(lead + 2.5)


def wait_flag(text, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        if text in log_text():
            print("%s at clock %.1f" % (what, clock()), flush=True)
            return True
        time.sleep(2)
    print("TIMEOUT waiting for %s at clock %.1f" % (what, clock()), flush=True)
    return False


print("waiting for boot", flush=True)
while clock() < 45:
    time.sleep(2)

n = log_text().count("vdec] close handle")
print("waiting for the attract movie to finish", flush=True)
end = time.time() + 900
while time.time() < end and log_text().count("vdec] close handle") <= n:
    time.sleep(2)

for _ in range(6):
    press("START")
    time.sleep(1.5)
    if menu_score() > 40:
        print("menu at clock %.1f" % clock(), flush=True)
        break

press("A")  # LOAD GAME
wait_flag("MissionSelectScene", 60, "hub")
time.sleep(5)
press("A")      # mission -> accept dialog
time.sleep(2)
press("LEFT")   # OK
press("A")      # accept
wait_flag("input context gameplay", 180, "gameplay")
print("clock at gameplay %.1f" % clock(), flush=True)

if wait_after > 0:
    end = time.time() + wait_after
    while time.time() < end:
        if "fatal]" in log_text():
            print("CRASH detected at clock %.1f" % clock(), flush=True)
            break
        if not os.path.exists(os.path.join(shots, "clock.txt")):
            break
        time.sleep(3)
    print("end of wait at clock %.1f" % clock(), flush=True)
