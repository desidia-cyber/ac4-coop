"""Launch-and-drive for E8, final: patient, no repeated presses.

    python e8_patient.py <shots-tag> [attempts]

Measured flow (runs e8a and e8i, this build):
  * Six taps (A B A+B A B A, ~1.8 s apart, starting ~11 s) dismiss the startup dialogs and open
    "Load game progress data?" (`cellSaveDataListLoad2: prefix 'BLUS30027GAMEDAT', 2 save(s) found`).
  * That list then WAITS while the attract movies play. Every movie decodes 8 pictures and then sits
    until it ends by itself (opening.pam 22.1 -> 189.8 s; title.pam -> 328.7 s in e8a).
  * The save is read only when the movies are done (e8a: ACDES.DAT at 330.5, MissionSelectScene 335.1).
  * ANY further press restarts the attract loop (e8i: 20 A taps produced opening.pam/title.pam pairs
    at 189/256/326/399 and the hub never arrived), so this driver presses nothing until the hub.
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
attempts = int(sys.argv[2]) if len(sys.argv) > 2 else 1
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


def attempt(n):
    print("=== attempt %d ===" % n, flush=True)
    launch()
    while clock() < 10:
        time.sleep(1)
    for b in ("A", "B", "A+B", "A", "B", "A"):
        tap(b)
    print("dialogs queued at clock %.1f; pressing nothing until the hub" % clock(), flush=True)

    end = time.time() + 600
    while time.time() < end:
        if "MissionSelectScene" in log_all():
            print("hub at clock %.1f" % clock(), flush=True)
            break
        time.sleep(4)
    else:
        print("no hub at clock %.1f" % clock(), flush=True)
        return False

    time.sleep(5)
    tap("A", gap=2200)      # the highlighted mission -> accept dialog
    time.sleep(1.2)
    tap("LEFT", gap=2000)   # CANCEL -> OK
    time.sleep(1.2)
    tap("A", gap=2200)      # accept
    end = time.time() + 480
    while time.time() < end:
        if "input context gameplay" in log_all():
            print("GAMEPLAY at clock %.1f" % clock(), flush=True)
            return True
        time.sleep(4)
    print("no gameplay at clock %.1f" % clock(), flush=True)
    return False


for n in range(1, attempts + 1):
    if attempt(n):
        print("READY: tag %s" % tag, flush=True)
        sys.exit(0)
    subprocess.run(["taskkill", "/F", "/IM", "ac4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
print("failed", flush=True)
sys.exit(1)
