"""coop_motion.py -- how much did the entities move in a window of one instance's meter (M3)?

    python coop_motion.py --f join.ents [--from 300] [--to 330]

Reports, over the slots present in both the first and the last second of the window: the mean and the
median displacement, the max, and how many slots moved more than 1 and 10 units. This is the number
M3 compares between the join with AC4_COOP_NOSNAP=1 only and the join with AC4_COOP_NOSNAP=1
AC4_COOP_FREEZE=1: same conditions, one difference.
"""
import argparse
import sys


def load(path):
    series = {}
    with open(path, errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            if len(p) < 9:
                continue
            try:
                series.setdefault(float(p[0]), {})[int(p[1], 0)] = (float(p[4]), float(p[5]), float(p[6]))
            except ValueError:
                continue
    return series


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--f", required=True)
    ap.add_argument("--from", dest="t0", type=float, default=None)
    ap.add_argument("--to", dest="t1", type=float, default=None)
    a = ap.parse_args()
    s = load(a.f)
    if not s:
        print("empty meter")
        return 1
    keys = sorted(s)
    t0 = a.t0 if a.t0 is not None else keys[0]
    t1 = a.t1 if a.t1 is not None else keys[-1]
    first = min((k for k in keys if k >= t0), key=lambda x: abs(x - t0), default=None)
    last = min((k for k in keys if k <= t1), key=lambda x: abs(x - t1), default=None)
    if first is None or last is None or first == last:
        print("no window (t %.1f..%.1f, %d seconds)" % (t0, t1, len(keys)))
        return 1
    A, B = s[first], s[last]
    common = sorted(set(A) & set(B))
    d = []
    for k in common:
        ax, ay, az = A[k]
        bx, by, bz = B[k]
        d.append((((ax - bx) ** 2 + (ay - by) ** 2 + (az - bz) ** 2) ** 0.5, k))
    if not d:
        print("no common slot")
        return 1
    vals = sorted(x[0] for x in d)
    n = len(vals)
    med = vals[n // 2]
    moved1 = sum(1 for v in vals if v > 1.0)
    moved10 = sum(1 for v in vals if v > 10.0)
    print("window %.1f -> %.1f s (%.0f s), %d slots" % (first, last, last - first, n))
    print("mean displacement %.3f | median %.3f | max %.3f (%s)" % (sum(vals) / n, med, vals[-1],
          "slot %d" % max(d)[1]))
    print("moved >1 unit: %d (%.0f%%) | >10 units: %d (%.0f%%)"
          % (moved1, 100.0 * moved1 / n, moved10, 100.0 * moved10 / n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
