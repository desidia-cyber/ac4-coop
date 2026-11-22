"""coop_movers.py -- the divergence table restricted to entities that actually MOVE.

    python coop_movers.py --a host.ents --b join.ents --offset D --from T0 --to T1

Half of m010's 320 pool entities never move at all, so a median over all of them is always 0 and hides
what the sync does. This tool reports, over the window:

  * how many entities the HOST moved more than 10 units during the window ("movers"),
  * the mean / median / p90 host-vs-join distance over the movers, second by second,
  * the same for the entities the host did not move.

Inputs are the address-keyed meters (see coop_divergence.py).
"""
import argparse
import sys

POST = 4  # x, y, z at p[4..6]; address at p[1]


def load(path):
    series = {}
    for line in open(path, errors="ignore"):
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


def near(keys, t):
    return min(keys, key=lambda x: abs(x - t)) if keys else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--offset", type=float, default=0.0)
    ap.add_argument("--from", dest="t0", type=float, required=True)
    ap.add_argument("--to", dest="t1", type=float, required=True)
    a = ap.parse_args()
    A, B = load(a.a), load(a.b)
    if not A or not B:
        print("empty meter")
        return 1
    ak, bk = sorted(A), sorted(B)
    first = near([k for k in ak if k >= a.t0], a.t0)
    last = near([k for k in ak if k <= a.t1], a.t1)
    if first is None or last is None or first == last:
        print("no window")
        return 1
    # The host's own movement over the window decides who is a "mover".
    moved = {}
    for addr in set(A[first]) & set(A[last]):
        p, q = A[first][addr], A[last][addr]
        moved[addr] = ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2) ** 0.5
    movers = {k for k, v in moved.items() if v > 10.0}

    def stats(keys):
        per_sec = []
        for t in ak:
            if t < first or t > last:
                continue
            bt = near(bk, t - a.offset)
            if bt is None:
                continue
            ea, eb = A[t], B[bt]
            d = []
            for k in keys:
                if k in ea and k in eb:
                    p, q = ea[k], eb[k]
                    d.append(((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2) ** 0.5)
            if d:
                d.sort()
                per_sec.append((sum(d) / len(d), d[len(d) // 2], d[int(0.9 * (len(d) - 1))], d[-1]))
        if not per_sec:
            return None
        n = len(per_sec)
        return (sum(x[0] for x in per_sec) / n, sum(x[1] for x in per_sec) / n,
                sum(x[2] for x in per_sec) / n, max(x[3] for x in per_sec), n)

    print("window %.1f..%.1f s | %d entities, %d movers (>10 units on the host)" % (
        first, last, len(moved), len(movers)))
    for label, keys in (("movers", movers), ("static", set(moved) - movers)):
        s = stats(keys)
        if s:
            print("%-7s n=%3d  mean %.2f | median %.2f | p90 %.2f | peak %.2f  (%d seconds)" % (
                label, len(keys), s[0], s[1], s[2], s[3], s[4]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
