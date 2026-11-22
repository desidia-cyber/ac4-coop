"""Launch-and-drive for E8 with a fast stall detector and automatic relaunch.

    python e8_auto2.py <shots-tag> [attempts]

Per attempt: launch, clear the startup dialog with the e8a recipe (A B A+B A B A at ~13-26 s), then
watch the movie: a movie that stops producing pictures for 15 s is dead ("_libsail-control" waits
expire ~50 s later) and the attempt is abandoned, which costs ~1 min instead of a wasted run.

When the boot movies play through: the attract movie after opening.pam is the one that ends into the
title menu, so its close is the window where the pad reaches the menu (measured in run e8a, where the
A pressed 2 s before the close opened "Load game progress data?").
"""
import glob
import os
import subprocess
import sys
import time

base = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
exe = os.path.join(base, r"ac4-port\local\build\release\ac4.exe")
runs = os.path.join(base, "run", "data", "runs")

tag = sys.argv[1]
attempts = int(sys.argv[2]) if len(sys.argv) > 2 else 2
shots = os.path.join(base, "run", "shots", tag)
rig = os.path.join(base, "run", "rig", "coop.txt")


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


def press(button, lead=0.25, hold=0.12):
    at = clock() + lead
    with open(rig, "a", newline="\n") as f:
        f.write("%.2f 0 %s 0 0 0 0\n" % (at, button))
        f.write("%.2f 0 none 0 0 0 0\n" % (at + hold))


def movie_state(text):
    """(last_event_kind, last_movie_path, pictures)"""
    ev, path = [], "?"
    for line in text.splitlines():
        if "movie/" in line and "cellFsOpen" in line:
            path = line.split("movie/")[-1].split("'")[0]
        elif "vdec] open handle" in line:
            ev.append(("open", path))
        elif "vdec] close handle" in line:
            ev.append(("close", path))
    return (ev[-1] if ev else (None, None)), ev, text.count("GetPicture")


def launch():
    subprocess.run(["taskkill", "/F", "/IM", "ac4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    os.makedirs(shots, exist_ok=True)
    for f in glob.glob(os.path.join(shots, "*")):
        try:
            os.remove(f)
        except OSError:
            pass
    with open(rig, "w", newline="\n") as f:
        f.write("0.0 0 none 0 0 0 0\n0.0 1 none 0 0 0 0\n")
    with open(os.path.join(shots, "cmd.txt"), "w") as f:
        f.write("")
    args = [exe, "--data", os.path.join(base, "run", "data"), "--disc", os.path.join(base, "disc"),
            "--flash", os.path.join(base, "run", "data", "dev_flash"), "--safe-mode", "--windowed",
            "--fps", "60", "--hidden", "--seconds", "1800", "--rig", rig, "--rig-shots", shots,
            "--rig-shot-every", "2", "--keep-runs", "6",
            "--test-overlay", os.path.join(base, "payload", "overlay")]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            creationflags=0x00000008)


def attempt(n):
    print("=== attempt %d ===" % n, flush=True)
    launch()
    while clock() < 12:
        time.sleep(1)
    for b in ("A", "B", "A+B", "A", "B", "A"):
        press(b, lead=0.8, hold=0.15)
        time.sleep(2.3)
    print("startup dialog answered at clock %.1f" % clock(), flush=True)

    deadline = time.time() + 700
    last_pics, last_change = -1, time.time()
    opening_closed = False
    answered = False
    while time.time() < deadline:
        t = log_text()
        (kind, path), ev, pics = movie_state(t)
        if pics != last_pics:
            last_pics, last_change = pics, time.time()
        if kind == "open" and time.time() - last_change > 15:
            print("MOVIE STALLED on %s at clock %.1f" % (path, clock()), flush=True)
            return False
        if kind == "close" and path == "opening.pam":
            opening_closed = True
        if opening_closed and kind == "close" and path == "title.pam":
            print("attract movie closed at clock %.1f, pressing LOAD GAME" % clock(), flush=True)
            for i in range(14):
                press("A")
                time.sleep(0.8)
                tail = "\n".join(log_text().splitlines()[-8:])
                if "input context screen" in tail or "MissionSelectScene" in tail:
                    answered = True
                    print("title menu answered at clock %.1f (press %d)" % (clock(), i + 1), flush=True)
                    break
            if not answered:
                print("menu did not answer", flush=True)
                return False
            break
        time.sleep(1.2)
    else:
        print("no attract movie close at clock %.1f" % clock(), flush=True)
        return False

    end = time.time() + 120
    while time.time() < end and "MissionSelectScene" not in log_text():
        time.sleep(2)
    if "MissionSelectScene" not in log_text():
        print("NO HUB at clock %.1f" % clock(), flush=True)
        return False
    print("hub at clock %.1f" % clock(), flush=True)
    time.sleep(4)

    press("A", lead=1.8)
    time.sleep(2.2)
    press("LEFT", lead=1.8)
    time.sleep(2.0)
    press("A", lead=1.8)
    end = time.time() + 300
    while time.time() < end:
        if "input context gameplay" in log_text():
            print("GAMEPLAY at clock %.1f" % clock(), flush=True)
            return True
        if "MOVIE STALLED" in log_text():
            break
        time.sleep(2)
    print("no gameplay at clock %.1f" % clock(), flush=True)
    return False


for n in range(1, attempts + 1):
    if attempt(n):
        print("READY: tag %s" % tag, flush=True)
        sys.exit(0)
    subprocess.run(["taskkill", "/F", "/IM", "ac4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
print("all attempts failed", flush=True)
sys.exit(1)
