"""Last-resort driver: press A inside the narrow window where the title menu takes input.

    python e8_last.py <shots-tag>

Evidence (run e8a): the load list is opened early (15-26 s) and then waits behind the attract
movies; the press that made it load was 2 s BEFORE title.pam closed (A at 326.5, close at 328.7,
ACDES.DAT at 330.5, MissionSelectScene at 335.1). A press at +9 s into the same movie was swallowed
(256.0), and presses spread over a whole cycle restart the attract loop (run e8i). So: wait for
title.pam to open, then tap A once a second from open+25 s until the save list is re-issued.
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


def tap(button, hold=150, gap=1800):
    cmd(["%d %s" % (hold, button), "%d none" % gap])


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


launch()
while clock() < 10:
    time.sleep(1)
for b in ("A", "B", "A+B", "A", "B", "A"):
    tap(b)
print("dialogs queued at clock %.1f" % clock(), flush=True)

# the first attract movie (title.pam) that follows opening.pam
opening_closed_at = None
while clock() < 420:
    t = log_all()
    lines = t.splitlines()
    idx_open, idx_close = None, None
    for i, line in enumerate(lines):
        if "movie/opening.pam" in line and "cellFsOpen" in line:
            idx_open = i
        if "vdec] close handle" in line and idx_open is not None and i > idx_open:
            idx_close = i
    if idx_close is not None:
        opening_closed_at = clock()
        print("opening.pam closed at clock %.1f" % opening_closed_at, flush=True)
        break
    time.sleep(3)
if opening_closed_at is None:
    print("opening.pam never closed", flush=True)
    sys.exit(1)

# wait for title.pam to open, then tap from +20 s
title_open_at = None
while clock() < opening_closed_at + 60:
    if "movie/title.pam" in log_all():
        title_open_at = clock()
        print("title.pam open at clock %.1f" % title_open_at, flush=True)
        break
    time.sleep(2)
if title_open_at is None:
    print("title.pam never opened", flush=True)
    sys.exit(1)

while clock() < title_open_at + 20:
    time.sleep(2)
print("tapping A from clock %.1f" % clock(), flush=True)
savedata_before = log_all().count("cellSaveDataListLoad2")
for i in range(90):
    tap("A")
    time.sleep(0.9)
    t = log_all()
    if t.count("cellSaveDataListLoad2") > savedata_before or "read ACDES.DAT" in t or "MissionSelectScene" in t:
        print("save list took the confirm at clock %.1f (tap %d)" % (clock(), i + 1), flush=True)
        break
    if "movie/opening.pam" in "\n".join(t.splitlines()[-6:]):
        print("attract loop restarted at clock %.1f" % clock(), flush=True)
        break

end = time.time() + 150
while time.time() < end and "MissionSelectScene" not in log_all():
    time.sleep(3)
if "MissionSelectScene" not in log_all():
    print("NO HUB at clock %.1f" % clock(), flush=True)
    sys.exit(1)
print("hub at clock %.1f" % clock(), flush=True)
time.sleep(5)

tap("A", gap=2200)
time.sleep(1.2)
tap("LEFT", gap=2000)
time.sleep(1.2)
tap("A", gap=2200)
end = time.time() + 480
while time.time() < end:
    if "input context gameplay" in log_all():
        print("GAMEPLAY at clock %.1f" % clock(), flush=True)
        sys.exit(0)
    time.sleep(4)
print("no gameplay at clock %.1f" % clock(), flush=True)
sys.exit(1)
