"""Launch-and-drive for E8: patient movie handling, input through cmd.txt.

    python e8_wait.py <shots-tag> [attempts]

Measured: in this build a movie decodes exactly 8 pictures and then sits (no further GetPicture)
until it ends on its own (run e8a: opening.pam 38.6 -> 243.8 s, title.pam 246.7 -> 328.7 s). That is
NOT a stall, so this driver never aborts on it: it waits for the close. Presses during a movie are
swallowed (e8a pressed A at 256 inside title.pam, which played on), and the press that counted in
e8a was 2 s before that movie closed, so the driver taps A once a second after opening.pam has
closed until the title menu answers.
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


def log_path():
    return sorted(glob.glob(os.path.join(runs, "*")), key=os.path.getmtime)[-1] + r"\ac4.log"


def log_all():
    try:
        return open(log_path(), errors="ignore").read()
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


def movie_events(text):
    ev, path = [], "?"
    for line in text.splitlines():
        if "movie/" in line and "cellFsOpen" in line:
            path = line.split("movie/")[-1].split("'")[0]
        elif "vdec] open handle" in line:
            ev.append(("open", path))
        elif "vdec] close handle" in line:
            ev.append(("close", path))
    return ev


def launch():
    subprocess.run(["taskkill", "/F", "/IM", "ac4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    os.makedirs(shots, exist_ok=True)
    for f in glob.glob(os.path.join(shots, "*")):
        try:
            os.remove(f)
        except OSError:
            pass
    with open(rig, "w", newline="\n") as f:  # static, never rewritten while running
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
    while clock() < 11:
        time.sleep(1)
    for b in ("A", "B", "A+B", "A", "B", "A"):
        tap(b, gap=1750)
    print("dialog batch queued at clock %.1f" % clock(), flush=True)

    # wait for the attract movie (title.pam after opening.pam) to close - patiently
    deadline = time.time() + 600
    opened_after_s1 = False
    while time.time() < deadline:
        ev = movie_events(log_all())
        idx = None
        for i, (kind, path) in enumerate(ev):
            if kind == "open" and path == "opening.pam":
                idx = i
        if idx is not None and any(k == "close" for k, _ in ev[idx + 1:]):
            opened_after_s1 = True
        if opened_after_s1:
            break
        time.sleep(3)
    if not opened_after_s1:
        print("opening.pam never closed (clock %.1f)" % clock(), flush=True)
        return False
    print("opening.pam closed at clock %.1f; tapping A through the attract movie" % clock(), flush=True)

    # the title menu takes input near/after the attract movie's end
    answered = False
    for i in range(30):
        tap("A", hold=150, gap=900, count=8)  # 8.4 s of taps
        time.sleep(8.6)
        t = log_all()
        tail = "\n".join(t.splitlines()[-12:])
        if "input context screen" in tail or "MissionSelectScene" in t:
            answered = True
            print("title menu answered at clock %.1f (batch %d)" % (clock(), i + 1), flush=True)
            break
    if not answered:
        print("menu never answered (clock %.1f)" % clock(), flush=True)
        return False

    end = time.time() + 120
    while time.time() < end and "MissionSelectScene" not in log_all():
        time.sleep(2)
    if "MissionSelectScene" not in log_all():
        print("NO HUB at clock %.1f" % clock(), flush=True)
        return False
    print("hub at clock %.1f" % clock(), flush=True)
    time.sleep(5)

    tap("A", gap=2200)
    tap("LEFT", gap=2000)
    tap("A", gap=2200)
    end = time.time() + 420
    while time.time() < end:
        if "input context gameplay" in log_all():
            print("GAMEPLAY at clock %.1f" % clock(), flush=True)
            return True
        time.sleep(3)
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
