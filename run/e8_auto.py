"""Launch-and-drive for E8, with retries (movie pipeline is flaky in this build).

    python e8_auto.py <shots-tag> [attempts]

Per attempt: fresh process, clear the startup dialogs (A at ~14/17/20 s, before any movie is loaded),
then verify the movie pipeline actually decodes (GetPicture lines grow); if it does not, kill and
retry. Once the boot movies are healthy: wait for opening.pam to close, then press A once a second
through the attract movie until the title menu answers, then LOAD GAME -> hub -> mission -> gameplay.

Measured rules this encodes (5 runs):
  * The startup "HDD access indicator" dialog blocks everything until A is pressed (run e8e sat on it
    for 450 s with no movies at all).
  * A button that arrives in the gap between two movies stops the movie pipeline for good: the next
    movie never produces a picture and the game waits forever ("timed wait expired after 3000.0 ms:
    '_libsail-control'", runs e8b/e8c).
  * A button during a movie is swallowed (run e8a: title.pam played its full 82 s through presses).
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


def movie_events(text):
    out, path = [], "?"
    for line in text.splitlines():
        if "movie/" in line and "cellFsOpen" in line:
            path = line.split("movie/")[-1].split("'")[0]
        elif "vdec] open handle" in line:
            out.append(("open", path))
        elif "vdec] close handle" in line:
            out.append(("close", path))
    return out


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
                            creationflags=0x00000008)  # DETACHED_PROCESS


def attempt(n):
    print("=== attempt %d ===" % n, flush=True)
    proc = launch()
    while clock() < 12:
        time.sleep(1)
    # startup dialogs: before the boot movies start (~30 s), with a wide margin
    for _ in range(3):
        press("A", lead=0.8, hold=0.15)
        time.sleep(2.4)
    print("dialogs poked at clock %.1f" % clock(), flush=True)

    # movie health: the first movie must keep decoding
    baseline = None
    for _ in range(60):
        t = log_text()
        pics = t.count("GetPicture")
        ev = movie_events(t)
        if baseline is None and ev and ev[-1][0] == "open":
            baseline = pics
        if baseline is not None and pics - baseline > 40:
            print("movie pipeline healthy at clock %.1f (%d pictures)" % (clock(), pics), flush=True)
            break
        if baseline is not None and "_libsail-control" in t[-3000:] and pics - baseline <= 8:
            print("MOVIE STALLED at clock %.1f (%d pictures)" % (clock(), pics), flush=True)
            return False
        time.sleep(1.5)
    else:
        print("no healthy movie at clock %.1f" % clock(), flush=True)
        return False

    # wait for the attract movie (the title.pam after opening.pam) to close
    deadline = time.time() + 600
    pressed = 0
    while time.time() < deadline:
        t = log_text()
        ev = movie_events(t)
        idx_open = None
        for i, (kind, path) in enumerate(ev):
            if kind == "open" and path == "opening.pam":
                idx_open = i
        started = False
        if idx_open is not None and any(k == "close" for k, _ in ev[idx_open + 1:]):
            started = True
        if started:
            # the title screen owns the pad now: one press a second until the menu answers
            press("A")
            pressed += 1
            time.sleep(0.9)
            tail = "\n".join(log_text().splitlines()[-10:])
            if "input context screen" in tail or "MissionSelectScene" in tail:
                print("title menu answered at clock %.1f (%d presses)" % (clock(), pressed), flush=True)
                break
            if "vdec] open handle" in tail and pressed > 3:
                pass  # a movie started: keep pressing, they are swallowed while it plays
        else:
            time.sleep(1.5)
    else:
        print("attract movie never opened (clock %.1f)" % clock(), flush=True)
        return False

    # hub
    end = time.time() + 120
    while time.time() < end and "MissionSelectScene" not in log_text():
        time.sleep(2)
    if "MissionSelectScene" not in log_text():
        print("NO HUB at clock %.1f" % clock(), flush=True)
        return False
    print("hub at clock %.1f" % clock(), flush=True)
    time.sleep(4)

    # mission -> accept dialog (CANCEL focused) -> OK
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
        # a second accept attempt if the dialog did not take
        if time.time() > end - 240:
            pass
        time.sleep(2)
    print("no gameplay at clock %.1f" % clock(), flush=True)
    return False


for n in range(1, attempts + 1):
    ok = attempt(n)
    if ok:
        print("READY: tag %s" % tag, flush=True)
        sys.exit(0)
    subprocess.run(["taskkill", "/F", "/IM", "ac4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
print("all attempts failed", flush=True)
sys.exit(1)
