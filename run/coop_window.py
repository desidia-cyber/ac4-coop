"""coop_window.py -- drive BOTH instances into m010, then run one measurement window.

    python coop_window.py --phase idle   --seconds 120
    python coop_window.py --phase drive  --seconds 60      # HOST p0 walks forward and fires
    python coop_window.py --phase fire   --seconds 30      # JOIN p0 fires (M5)

The two instances are booted in parallel (each with its own camera-free tactical window), the moment
gameplay starts is logged for BOTH so the two logs' clocks can be aligned (--offset = host - join), and
screenshot pairs are copied into <research>\\e11_shots.

Nothing here presses SAVE GAME and nothing writes to the save data.
"""
import argparse
import glob
import os
import shutil
import sys
import threading
import time

import coop_mission as cm

B = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
RAW = os.path.join(B, "research", "e11_raw")
SHOTS = os.path.join(B, "research", "e11_shots")

INST = {
    "host": {"shots": os.path.join(B, "run", "shots", "host"), "runs": os.path.join(B, "run", "data", "runs")},
    "join": {"shots": os.path.join(B, "run", "shots", "join"), "runs": os.path.join(B, "run", "data2", "runs")},
}


ACTIVE = ("host", "join")


def gameplay_time(i):
    """The log time of the LAST gameplay entry (only while it is the newest context transition)."""
    return i.last_gameplay_time()


def in_mission(i, threshold=100):
    """Is the mission running? The entity walk says so directly: the walk logs its object count once a
    second, and a live m010 has ~318. The `input context` recency test is NOT reliable (a `screen`
    context line after gameplay makes it report "not in gameplay" while the mission is running) --
    that is what made the first two attempts miss their windows."""
    n = -1
    t = -1.0
    for line in i.log_text().splitlines():
        if "COOP ENT t=" not in line:
            continue
        try:
            t = float(line[line.index("t=") + 2:].split()[0])
            n = int(line[line.index("objects=") + 8:].split()[0])
        except (ValueError, IndexError):
            continue
    return n >= threshold, n, t


def wait_mission(i, timeout=420, tag=""):
    end = time.time() + timeout
    while time.time() < end:
        ok, n, t = in_mission(i)
        if ok:
            return True
        time.sleep(2)
    return False


def wait_clock(i, limit=240):
    """The rig writes <shots>\\clock.txt once the game is up; the menus cannot be driven before."""
    end = time.time() + limit
    while time.time() < end:
        if i.clock() > 1:
            return True
        time.sleep(0.5)
    return False


class Inst(cm.Inst):
    """coop_mission.Inst with one change: a live entity walk also counts as gameplay.

    `input context gameplay` is followed by `screen`/`menu` lines inside the mission, so the inherited
    recency test reports "not in gameplay" while the battle runs — which previously kept the menu walk
    pressing A (in gameplay that is FIRE) long after the mission had started. The entity walk is a
    direct measurement: ~318 objects means the battle is live.
    """

    def in_gameplay(self):
        return super().in_gameplay() or in_mission(self)[0]

    # coop_mission.Inst writes with _w(); this driver's phases use the coop_drive name.
    _write = cm.Inst._w

    def hold(self, buttons, ms, port=0, lx=0, ly=0, rx=0, ry=0, settle=0):
        """One held input for `ms` (a neutral line first: the rig coalesces identical lines)."""
        self._write("0 p%d none 0 0 0 0" % port)
        time.sleep(0.3)
        self._write("%d p%d %s %d %d %d %d" % (ms, port, buttons, lx, ly, rx, ry))
        time.sleep(0.3 + ms / 1000.0)
        if settle:
            self._write("0 p%d none 0 0 0 0" % port)
            time.sleep(settle / 1000.0)


def wait_pair(timeout=900, tags=("host", "join")):
    results = {}

    def one(tag):
        i = Inst(tag, INST[tag]["shots"], INST[tag]["runs"])
        if not wait_clock(i):
            print("[%s] no clock.txt after 240 s: the instance is not running" % tag, flush=True)
            results[tag] = (False, -1.0, 0.0)
            return
        ok = False
        for attempt in range(4):
            if cm.to_gameplay(i, log=lambda m: print("[%s] %s" % (tag, m), flush=True)):
                ok = True
                break
            print("[%s] walk attempt %d failed at clock %.1f; retrying" % (tag, attempt + 1, i.clock()), flush=True)
            time.sleep(4)
        results[tag] = (ok, gameplay_time(i), i.clock())

    th = [threading.Thread(target=one, args=(t,)) for t in tags]
    for t in th:
        t.start()
    for t in th:
        t.join(timeout)
    return results


def newest_shot(tag):
    files = glob.glob(os.path.join(INST[tag]["shots"], "shot_*.png"))
    return max(files, key=os.path.getmtime) if files else None


def snap_pair(label):
    out = []
    for tag in ACTIVE:
        s = newest_shot(tag)
        if not s:
            out.append((tag, None))
            continue
        dst = os.path.join(SHOTS, "%s_%s_%s" % (label, tag, os.path.basename(s)))
        shutil.copy2(s, dst)
        out.append((tag, dst))
    for tag, dst in out:
        print("  shot %-4s %s" % (tag, dst), flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=("idle", "drive", "fire", "m4m5", "drive_fire"), required=True)
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--label", default="")
    ap.add_argument("--only", default="", help="wait for just this instance (host|join)")
    a = ap.parse_args()
    label = a.label or a.phase
    os.makedirs(SHOTS, exist_ok=True)
    os.makedirs(RAW, exist_ok=True)

    global ACTIVE
    ACTIVE = (a.only,) if a.only else ("host", "join")
    res = wait_pair(tags=ACTIVE)
    for tag, (ok, t_game, clock) in res.items():
        print("RESULT %-4s gameplay=%s gameplay_t=%.1f clock=%.1f" % (tag, ok, t_game, clock), flush=True)
    if not all(v[0] for v in res.values()):
        print("RESULT: at least one instance did not reach gameplay")
        return 2

    host = Inst("host", INST["host"]["shots"], INST["host"]["runs"])
    join = Inst("join", INST["join"]["shots"], INST["join"]["runs"])

    snap_pair(label + "_start")
    t0 = time.time()
    if a.phase == "idle":
        print("window: both idle %d s" % a.seconds, flush=True)
        time.sleep(a.seconds)
    elif a.phase == "m4m5":
        # Run A (both idle), Run B (host drives + fires), M5 (join fires). One session so the three
        # windows share the same instance pair; the simulation has to stay alive for ~210 s.
        print("== Run A: both idle 120 s ==", flush=True)
        time.sleep(120)
        snap_pair("m4a_end")
        print("== Run B: HOST p0 forward + A for 60 s ==", flush=True)
        host.hold("A", 60000, ly=26000)
        snap_pair("m4b_end")
        print("== M5: JOIN p0 fire (A) with a slow sweep for 30 s ==", flush=True)
        join._write("0 p0 none 0 0 0 0")
        time.sleep(0.3)
        for k in range(10):
            rx = int(26000 * (1 if (k // 3) % 2 == 0 else -1) * ((k % 3) + 1) / 3.0)
            join._write("%d p0 A 0 0 %d 0" % (3000, rx))
            time.sleep(3)
        join._write("0 p0 none 0 0 0 0")
        snap_pair("m5_end")
    elif a.phase == "drive_fire":
        # M4 Run B (host p0 forward + A, 60 s) then M5 (join p0 fire, 30 s).
        print("== Run B: HOST p0 forward + A for 60 s ==", flush=True)
        host.hold("A", 60000, ly=26000)
        snap_pair("m4b_end")
        print("== M5: JOIN p0 fire (A) with a slow sweep for 30 s ==", flush=True)
        join._write("0 p0 none 0 0 0 0")
        time.sleep(0.3)
        for k in range(10):
            rx = int(26000 * (1 if (k // 3) % 2 == 0 else -1) * ((k % 3) + 1) / 3.0)
            join._write("%d p0 A 0 0 %d 0" % (3000, rx))
            time.sleep(3)
        join._write("0 p0 none 0 0 0 0")
        snap_pair("m5_end")
    elif a.phase == "drive":
        print("window: HOST p0 forward + A for %d s" % a.seconds, flush=True)
        host.hold("A", a.seconds * 1000, ly=26000)
    else:
        print("window: JOIN p0 fire (A) with a slow stick sweep for %d s" % a.seconds, flush=True)
        # A held for the whole window; the right stick sweeps so the shots cover an arc in front.
        join._write("0 p0 none 0 0 0 0")
        time.sleep(0.3)
        step = 3
        n = max(1, a.seconds // step)
        for k in range(n):
            rx = int(26000 * (1 if (k // 3) % 2 == 0 else -1) * ((k % 3) + 1) / 3.0)
            join._write("%d p0 A 0 0 %d 0" % (step * 1000, rx))
            time.sleep(step)
        join._write("0 p0 none 0 0 0 0")
    snap_pair(label + "_end")
    print("window done after %.0f s wall" % (time.time() - t0), flush=True)
    for tag, i in (("host", host), ("join", join)):
        print("RESULT %-4s clock=%.1f gameplay_t=%.1f" % (tag, i.clock(), gameplay_time(i)), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
