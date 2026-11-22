"""Wait for the title menu without skipping the attract movie.

    python wait_title.py <shots-tag>

The attract movie must be allowed to finish: skipping it left the next movie (the mission briefing)
black and stalled in two runs. So this waits until the frame stops changing (the movie has ended or
the title card is up), presses START once, and then checks for the menu's gold "LOAD GAME" row.
"""
import glob
import hashlib
import os
import sys
import time
from PIL import Image

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
shots = os.path.join(base, "run", "shots", sys.argv[1])
rig = os.path.join(base, "run", "rig", "coop.txt")


def clock():
    with open(os.path.join(shots, "clock.txt")) as f:
        return float(f.read().strip())


def shots_now():
    return sorted(glob.glob(os.path.join(shots, "shot_*.png")))


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


def press(button, lead=2.0):
    at = clock() + lead
    with open(rig, "a", newline="\n") as f:
        f.write("%.2f 0 %s 0 0 0 0\n" % (at, button))
        f.write("%.2f 0 none 0 0 0 0\n" % (at + 0.15))
    time.sleep(lead + 2.0)


stable = 0
last = None
for _ in range(120):  # up to ~10 minutes
    fs = shots_now()
    if not fs:
        time.sleep(3)
        continue
    d = digest(fs[-1])
    if d == last:
        stable += 1
    else:
        stable = 0
        last = d
    if stable >= 3:  # about 6 s without a new frame: the movie has stopped
        score = menu_score(fs[-1])
        print("clock %.1f: frame static, menu score %d" % (clock(), score))
        if score > 40:
            print("MENU UP")
            sys.exit(0)
        press("START")
        fs = shots_now()
        if fs and menu_score(fs[-1]) > 40:
            print("MENU UP after START (clock %.1f)" % clock())
            sys.exit(0)
        stable = 0
    time.sleep(2)
print("no menu")
sys.exit(1)
