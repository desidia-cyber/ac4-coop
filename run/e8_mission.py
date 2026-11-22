"""Drive one campaign mission into gameplay, never skipping the attract movie.

    python e8_mission.py <shots-tag> [wait-after-gameplay-seconds]

Difference from drive_mission.py: the title menu is detected by *frame stability* (research 19
section 6.1: pressing START while the attract movie plays leaves the mission briefing movie black and
stalled forever), not by a gold-row score that a movie frame can fake.
"""
import glob
import hashlib
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


def digest(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def menu_score(path):
    im = Image.open(path).convert("RGB")
    px = im.load()
    gold = 0
    for y in range(505, 566, 2):
        for x in range(540, 761, 2):
            r, g, b = px[x, y]
            if r > 120 and r - b > 50:
                gold += 1
    return gold


def press(button, lead=2.0, settle=2.5):
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


print("waiting for boot", flush=True)
while clock() < 40:
    time.sleep(2)

# 1. title menu by frame stability: the attract movie must finish by itself
stable, last = 0, None
for _ in range(200):
    p = newest()
    if not p:
        time.sleep(3)
        continue
    d = digest(p)
    if d == last:
        stable += 1
    else:
        stable, last = 0, d
    if stable >= 3:
        p = newest()
        score = menu_score(p)
        print("clock %.1f: frame static, menu score %d" % (clock(), score), flush=True)
        if score > 40:
            print("MENU UP", flush=True)
            break
        press("START")
        p = newest()
        if p and menu_score(p) > 40:
            print("MENU UP after START (clock %.1f)" % clock(), flush=True)
            break
        stable = 0
    time.sleep(2)
else:
    print("no menu", flush=True)
    sys.exit(1)

# 2. LOAD GAME (the default row) -> hub
press("A")
if not wait_log("MissionSelectScene", 120, "hub"):
    press("A")
    wait_log("MissionSelectScene", 120, "hub")
time.sleep(4)

# 3. the highlighted mission -> accept dialog (CANCEL is focused) -> OK
press("A")
time.sleep(2)
press("LEFT")
press("A")
wait_log("Ac4SortieScene", 120, "sortie scene")
if wait_log("input context gameplay", 240, "gameplay"):
    print("clock at gameplay %.1f" % clock(), flush=True)

if wait_after > 0:
    end = time.time() + wait_after
    while time.time() < end:
        if "fatal]" in log_text():
            print("CRASH detected at clock %.1f" % clock(), flush=True)
            break
        time.sleep(3)
    print("end of wait at clock %.1f" % clock(), flush=True)
