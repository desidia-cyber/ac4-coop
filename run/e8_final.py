"""Launch-and-drive for E8: input through cmd.txt only (no rig-script reloads), fast stall abort.

    python e8_final.py <shots-tag> [attempts]

Input path: cmd.txt (<ms> <buttons>) is a queue the rig appends to; writing it does NOT reload the rig
script (rig.cpp load_commands), while writing the script does (load_script clears the owned ports).
research/16 section (e) 1 and this session's stalls point at the script path, so this driver never
touches it.

Measured stall signature: a movie that stops producing "vdec] ... GetPicture" lines; ~50 s later the
log shows "timed wait expired after 3000.0 ms: '_libsail-control'" and the game never proceeds.
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
attempts = int(sys.argv[2]) if len(sys.argv) > 2 else 3
shots = os.path.join(base, "run", "shots", tag)
rig = os.path.join(base, "run", "rig", "coop.txt")


def log_tail(kb=96):
    try:
        p = sorted(glob.glob(os.path.join(runs, "*")), key=os.path.getmtime)[-1] + r"\ac4.log"
        n = os.path.getsize(p)
        with open(p, "rb") as f:
            f.seek(max(0, n - kb * 1024))
            return f.read().decode("utf-8", "ignore")
    except (OSError, IndexError):
        return ""


def log_all():
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


def cmd(lines):
    with open(os.path.join(shots, "cmd.txt"), "a", newline="\n") as f:
        for line in lines:
            f.write(line + "\n")


def tap(button, hold=150, gap=1800, count=1):
    out = []
    for _ in range(count):
        out.append("%d %s" % (hold, button))
        out.append("%d none" % gap)
    cmd(out)


def events(tail):
    ev, path, pics = [], "?", 0
    for line in tail.splitlines():
        if "movie/" in line and "cellFsOpen" in line:
            path = line.split("movie/")[-1].split("'")[0]
        elif "vdec] open handle" in line:
            ev.append(("open", path))
        elif "vdec] close handle" in line:
            ev.append(("close", path))
        elif "GetPicture" in line:
            pics += 1
    return ev, pics


def launch():
    subprocess.run(["taskkill", "/F", "/IM", "ac4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    os.makedirs(shots, exist_ok=True)
    for f in glob.glob(os.path.join(shots, "*")):
        try:
            os.remove(f)
        except OSError:
            pass
    with open(rig, "w", newline="\n") as f:  # static: owned ports only, never rewritten again
        f.write("0.0 0 none 0 0 0 0\n0.0 1 none 0 0 0 0\n")
    with open(os.path.join(shots, "cmd.txt"), "w") as f:
        f.write("")
    args = [exe, "--data", os.path.join(base, "run", "data"), "--disc", os.path.join(base, "disc"),
            "--flash", os.path.join(base, "run", "data", "dev_flash"), "--safe-mode", "--windowed",
            "--fps", "60", "--hidden", "--seconds", "1800", "--rig", rig, "--rig-shots", shots,
            "--rig-shot-every", "3", "--keep-runs", "6",
            "--test-overlay", os.path.join(base, "payload", "overlay")]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            creationflags=0x00000008)


def attempt(n):
    print("=== attempt %d ===" % n, flush=True)
    launch()
    while clock() < 11:
        time.sleep(1)
    # startup dialogs: one batch, first press at +2.2 s, then every ~1.95 s
    tap("A", gap=1750)
    tap("B", gap=1750)
    tap("A+B", gap=1750)
    tap("A", gap=1750)
    tap("B", gap=1750)
    tap("A", gap=3000)
    print("dialog batch queued at clock %.1f" % clock(), flush=True)

    deadline = time.time() + 700
    last_pics, last_change = -1, time.time()
    opening_closed = False
    while time.time() < deadline:
        tail = log_tail()
        ev, pics = events(tail)
        kind, path = ev[-1] if ev else (None, None)
        if pics != last_pics:
            last_pics, last_change = pics, time.time()
        if kind == "open" and time.time() - last_change > 20:
            print("MOVIE STALLED on %s at clock %.1f" % (path, clock()), flush=True)
            return False
        if kind == "close" and path == "opening.pam":
            opening_closed = True
        if opening_closed and kind == "close" and path == "title.pam":
            print("attract movie closed at clock %.1f; queueing LOAD GAME presses" % clock(), flush=True)
            tap("A", hold=150, gap=700, count=36)
            break
        time.sleep(2)
    else:
        print("no attract movie close at clock %.1f" % clock(), flush=True)
        return False

    end = time.time() + 90
    while time.time() < end and "MissionSelectScene" not in log_tail(200):
        time.sleep(2)
    if "input context gameplay" in log_all() and "MissionSelectScene" not in log_all():
        pass
    if "MissionSelectScene" not in log_all():
        print("NO HUB at clock %.1f" % clock(), flush=True)
        return False
    print("hub at clock %.1f" % clock(), flush=True)
    time.sleep(5)

    # the mission accept dialog focuses CANCEL: A, LEFT, A
    tap("A", gap=2000)
    tap("LEFT", gap=1800)
    tap("A", gap=2000)
    end = time.time() + 300
    while time.time() < end:
        tail = log_tail()
        if "input context gameplay" in log_all():
            print("GAMEPLAY at clock %.1f" % clock(), flush=True)
            return True
        ev, pics = events(tail)
        kind, path = ev[-1] if ev else (None, None)
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
