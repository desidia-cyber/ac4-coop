"""Press START until the title menu is on screen, then stop.

    python wait_menu.py <shots-tag> [max-presses]

The title menu's selected row ("LOAD GAME") is the only gold band at that height, so its presence in
the band x 540..760, y 505..565 is the test. Reads the newest shot_NNN.png after each press.
"""
import glob
import os
import sys
import time
from PIL import Image

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
shots = os.path.join(base, "run", "shots", sys.argv[1])
rig = os.path.join(base, "run", "rig", "coop.txt")
max_presses = int(sys.argv[2]) if len(sys.argv) > 2 else 12


def clock():
    with open(os.path.join(shots, "clock.txt")) as f:
        return float(f.read().strip())


def newest():
    files = sorted(glob.glob(os.path.join(shots, "shot_*.png")))
    return files[-1] if files else None


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


def press(button):
    c = clock()
    at = c + 2.0
    with open(rig, "a", newline="\n") as f:
        f.write("%.2f 0 %s 0 0 0 0\n" % (at, button))
        f.write("%.2f 0 none 0 0 0 0\n" % (at + 0.15))
    time.sleep(4.0)


for i in range(max_presses):
    path = newest()
    if path:
        score = menu_score(path)
        print("clock %.1f: menu score %d (%s)" % (clock(), score, os.path.basename(path)))
        if score > 40:
            print("MENU UP")
            sys.exit(0)
    press("START")
print("no menu after %d presses" % max_presses)
sys.exit(1)
