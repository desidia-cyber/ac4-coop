"""Append a rig press to a running rig's script file, dated in the future.

    python rigpress.py <shots-tag> <button> [hold_ms] [lead_ms]

The rig reads <shots>\\clock.txt for its elapsed time and reloads its script when the file changes;
timeline entries only take effect from their timestamp on, so every press is dated lead_ms ahead of
the current clock. Two lines are written (press, then release) so the press cannot stay held.
"""
import os
import sys
import time

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
shots = os.path.join(base, "run", "shots", sys.argv[1])
rig = os.path.join(base, "run", "rig", "coop.txt")
button = sys.argv[2]
hold = (float(sys.argv[3]) if len(sys.argv) > 3 else 150.0) / 1000.0
lead = (float(sys.argv[4]) if len(sys.argv) > 4 else 2500.0) / 1000.0

with open(os.path.join(shots, "clock.txt")) as f:
    clock = float(f.read().strip())

press_at = clock + lead
with open(rig, "a", newline="\n") as f:
    f.write("%.2f 0 %s 0 0 0 0\n" % (press_at, button))
    f.write("%.2f 0 none 0 0 0 0\n" % (press_at + hold))
print("clock %.2f: %s at %.2f (+%.0f ms hold)" % (clock, button, press_at, hold * 1000))
time.sleep(lead + hold + 1.2)
