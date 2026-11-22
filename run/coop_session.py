"""coop_session.py -- one launch, then everything a co-op test needs, in order.

    python coop_session.py --tag host --shots <dir> --runs <dir> [--peer-tag join --peer-shots ... --peer-runs ...]
                           [--m2] [--m4]

Built because a single ac4 launch takes ~150 s to reach gameplay and the mission then ends by itself
~244 s later (measured: gameplay 1094.97 -> debrief 1338.89). So the expensive, fragile part (boot ->
hub -> mission) is done ONCE, verified, and then all the measurements run inside the live window:

  --m2   the AC body transform write test (coop_m2_write.py)
  --m4   the two-direction link measurement: drive this instance's p0 and sample the peer's
         position error once a second for 30 s, from the peer's own log

Nothing here presses SAVE GAME; the disc and the save data are never written.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_mission as M   # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--shots")
    ap.add_argument("--runs")
    ap.add_argument("--peer-tag", default="")
    ap.add_argument("--peer-shots", default="")
    ap.add_argument("--peer-runs", default="")
    ap.add_argument("--m2", action="store_true")
    ap.add_argument("--m4", action="store_true")
    ap.add_argument("--drive-seconds", type=float, default=34)
    a = ap.parse_args()

    i = M.Inst(a.tag, a.shots, a.runs)
    peer = M.Inst(a.peer_tag, a.peer_shots, a.peer_runs) if a.peer_tag else None

    print("=== enter gameplay (%s) ===" % a.tag, flush=True)
    if not M.to_gameplay(i):
        print("RESULT: no gameplay", flush=True)
        return 2
    remain = 244 - (i.clock() - (i.last_gameplay_time() or i.clock()))
    print("gameplay at clock %.1f; the mission ends by itself ~244 s after that" % i.clock(), flush=True)

    if a.m2:
        print("=== M2 write test ===", flush=True)
        subprocess.run([sys.executable, "-u", os.path.join(os.path.dirname(os.path.abspath(__file__)), "coop_m2_write.py"),
                        "--tag", a.tag, "--shots", i.shots, "--runs", i.runs], check=False)

    if a.m4 and peer:
        print("=== M4 direction A: drive %s p0, sample the peer's error ===" % a.tag, flush=True)
        t0 = peer.clock()
        # forward, turn, forward, turn - all on port 0 of THIS instance
        seq = [(4000, "none", 0, 26000, 0, 0), (2500, "none", 0, 0, 0, 0),
               (3000, "none", 0, 0, 26000, 0), (2500, "none", 0, 0, 0, 0),
               (4000, "none", 0, 26000, 0, 0), (3000, "none", 0, 0, 26000, 0),
               (4000, "none", 0, 0, 0, 0)]
        for ms, b, lx, ly, rx, ry in seq:
            i._w("0 p0 none 0 0 0 0")
            time.sleep(0.3)
            i._w("%d p0 %s %d %d %d %d" % (ms, b, lx, ly, rx, ry))
            time.sleep(0.3 + ms / 1000.0)
        i._w("0 p0 none 0 0 0 0")
        print("  drove %s p0 for %.0f s" % (a.tag, a.drive_seconds), flush=True)
        # sample the PEER's error once a second
        samples = []
        end = time.time() + a.drive_seconds
        while time.time() < end:
            txt = peer.log_text()
            last = None
            for line in txt.splitlines():
                if "COOP NET:" in line and "error" in line:
                    try:
                        last = float(line.rsplit("error", 1)[1].strip().split()[0])
                    except (ValueError, IndexError):
                        continue
            if last is not None:
                samples.append((peer.clock(), last))
            time.sleep(1.0)
        if samples:
            errs = [e for _, e in samples]
            print("  %s error over %d samples: min %.2f  max %.2f  mean %.2f" % (
                a.peer_tag, len(errs), min(errs), max(errs), sum(errs) / len(errs)), flush=True)
            for t, e in samples:
                print("    %8.1f  %7.2f" % (t, e), flush=True)
        else:
            print("  no error samples from the peer", flush=True)

    print("RESULT: done, clock %.1f" % i.clock(), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
