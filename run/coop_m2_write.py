"""coop_m2_write.py -- the M2 WRITE test, run inside one live gameplay window.

    python coop_m2_write.py --tag host [--offs 0x80,0x40,0x1c0] [--delta 50]

The mission in this save ends by itself ~244 s after gameplay starts (measured: run
20261115-095929, gameplay at 1094.97, debrief at 1338.89), so the test has to run fast and must
START FROM GAMEPLAY. Sequence per candidate offset:
    1. both ACs idle 2 s; read the body2 position row
    2. write +delta into that offset for ONE frame (coop_body.cpp AC4_COOP_POKE_FILE)
    3. 1 s and 3 s later: read the same row again, and take a screenshot each time
The verdict per offset: whether the value stays changed (the object owns it), snaps back (a copy, the
authority is elsewhere), or the AC relocates visibly (the write moves the AC).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_mission as M   # noqa: E402  (classify / Inst)

POS = (0x80, 0x84, 0x88)


def body2_pos(log_text):
    """The last COOP DUMP body2 position row from the log, and its log time."""
    last, t = None, None
    for line in log_text.splitlines():
        if "COOP DUMP body2 +0x080" in line:
            try:
                t = float(line[1:line.index("]")])
                last = [float(x) for x in line.split("+0x080")[1].split()[:4]]
            except (ValueError, IndexError):
                continue
    return last, t


def poke(i, off, delta):
    p = os.path.join(i.shots, "poke.txt")
    with open(p, "w", newline="\n") as f:
        f.write("0x%x %g\n" % (off, delta))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--shots")
    ap.add_argument("--runs")
    ap.add_argument("--offs", default="0x80,0x40,0x1c0")
    ap.add_argument("--delta", type=float, default=50.0)
    a = ap.parse_args()
    i = M.Inst(a.tag, a.shots, a.runs)
    if not i.in_gameplay():
        print("NOT IN GAMEPLAY (clock %.1f) - run coop_mission.py first" % i.clock())
        return 2
    for off in [int(x, 0) for x in a.offs.split(",")]:
        before, tb = body2_pos(i.log_text())
        print("\n=== offset 0x%x, delta %+g ===" % (off, a.delta))
        print("  before  %s  (t=%.1f)" % (["%.2f" % v for v in (before or [])], tb or -1))
        shot0 = i.shot(8)
        poke(i, off, a.delta)
        time.sleep(1.1)
        s1 = i.shot(8)
        mid, tm = body2_pos(i.log_text())
        print("  +1.1 s  %s  (t=%.1f)  screen %s" % (
            ["%.2f" % v for v in (mid or [])], tm or -1, M.classify(s1)[0] if s1 else "?"))
        time.sleep(2.0)
        s3 = i.shot(8)
        after, ta = body2_pos(i.log_text())
        print("  +3.1 s  %s  (t=%.1f)  screen %s" % (
            ["%.2f" % v for v in (after or [])], ta or -1, M.classify(s3)[0] if s3 else "?"))
        if before and after:
            d = [after[k] - before[k] for k in range(3)]
            stayed = max(abs(d[k] - (a.delta if POS[k % 3] == off else 0.0)) for k in range(3))
            print("  delta   %s  (stayed: %s)" % (["%.2f" % v for v in d], "yes" if stayed < 2.0 else "no"))
        for s, tag in ((shot0, "before"), (s1, "plus1s"), (s3, "plus3s")):
            if s:
                print("  shot    %s -> %s" % (tag, s))
        time.sleep(1.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
