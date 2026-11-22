"""Launch-side driver for E8: NO input until the attract movie ends, then the title menu, hub, mission.

    python e8_run.py <shots-tag> [wait-after-gameplay-seconds]

Measured rule (this session, 4 runs): a button that arrives while a movie is loaded - or in the
~1.5 s gap between two movies - makes the movie pipeline stall for good ("timed wait expired after
3000.0 ms: '_libsail-control'" and no further GetPicture). The boot movies therefore play with no
input at all (run e8b's log proves they do), and input starts only in the window after the attract
movie (the second title.pam, the one that follows opening.pam) has closed.
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


def log_text():
    try:
        p = sorted(glob.glob(os.path.join(runs, "*")), key=os.path.getmtime)[-1] + r"\ac4.log"
        return open(p, errors="ignore").read()
    except (OSError, IndexError):
        return ""


def clock():
    try:
        return float(open(os.path.join(shots, "clock.txt")).read().strip())
    except OSError:
        return 0.0


def press(button, lead=0.2, hold=0.12, settle=0.0):
    at = clock() + lead
    with open(rig, "a", newline="\n") as f:
        f.write("%.2f 0 %s 0 0 0 0\n" % (at, button))
        f.write("%.2f 0 none 0 0 0 0\n" % (at + hold))
    if settle:
        time.sleep(settle)


def movie_events():
    out, path = [], "?"
    for line in log_text().splitlines():
        if "movie/" in line and "cellFsOpen" in line:
            path = line.split("movie/")[-1].split("'")[0]
        elif "vdec] open handle" in line:
            out.append(("open", path))
        elif "vdec] close handle" in line:
            out.append(("close", path))
    return out


def wait_log(text, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        if text in log_text():
            print("%s at clock %.1f" % (what, clock()), flush=True)
            return True
        time.sleep(1)
    print("TIMEOUT waiting for %s at clock %.1f" % (what, clock()), flush=True)
    return False


print("waiting for boot (no input until the attract movie ends)", flush=True)
while clock() < 20:
    time.sleep(2)

# The attract movie is the title.pam that opens after opening.pam has played and closed.
deadline = time.time() + 600
attract_open_at = None
while time.time() < deadline:
    ev = movie_events()
    idx_open = None
    for i, (kind, path) in enumerate(ev):
        if kind == "open" and path == "opening.pam":
            idx_open = i
    if idx_open is not None and any(k == "close" for k, _ in ev[idx_open + 1:]):
        for i in range(idx_open + 1, len(ev)):
            if ev[i][0] == "open" and ev[i][1] == "title.pam":
                attract_open_at = i
                break
    if attract_open_at is not None:
        if any(k == "close" for k, _ in ev[attract_open_at + 1:]):
            print("attract movie closed at clock %.1f" % clock(), flush=True)
            break
        # still playing: report once
        if not getattr(sys, "_reported", False):
            print("attract movie is playing (clock %.1f)" % clock(), flush=True)
            sys._reported = True
    time.sleep(1.5)
else:
    print("attract movie never closed (clock %.1f)" % clock(), flush=True)
    sys.exit(2)

answered = False
for i in range(24):
    press("A")
    time.sleep(0.55)
    tail = "\n".join(log_text().splitlines()[-8:])
    if "input context screen" in tail or "MissionSelectScene" in tail:
        answered = True
        print("title menu answered at clock %.1f (press %d)" % (clock(), i + 1), flush=True)
        break
    if "vdec] open handle" in tail:
        print("a new movie started before the menu answered (clock %.1f)" % clock(), flush=True)
        break
if not answered:
    print("title menu did not answer", flush=True)

wait_log("MissionSelectScene", 120, "hub")

time.sleep(4)
press("A", lead=1.8, settle=2.2)
press("LEFT", lead=1.8, settle=2.0)
press("A", lead=1.8, settle=2.2)
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
