"""coop_divergence.py -- compare two instances' entity meters, KEYED BY POOL SLOT.

The meter (coop_entities.cpp, AC4_COOP_METER=<file>) writes one header and one line per entity per
second:

    # t=<t> n=<n> pool=0x<base> stride=0x<stride>
    <t> <addr> <id> <vtable> <x> <y> <z> <hp> <alive> <ord>

The key is the entity ADDRESS (`<addr>`). research\\25 proposed a slot index derived from a 0x7080
stride, but M1 of research\\26 measured that the pool's spacing is only 0x7080 on average (gaps of
0x7080, 0x7170, 0x150e0 in one live mission), so no stride index exists; the address — and its rank
`<ord>` in the address-ordered list — is what both instances share.

    python coop_divergence.py --a host.ents --b join.ents [--offset S] [--rows 10] [--buckets 10]

`--offset` is A's clock minus B's, measured from the two logs' gameplay stamps; the analyser aligns B
onto A with bt = t - offset. The report per second is: entities present in only one instance, the
median / p90 / max position distance over the common slots, how many slots are beyond 10 units, and
how many hp fields disagree. `--buckets N` additionally aggregates the per-second rows into N-second
buckets (M4 asks for 10 s).
"""
import argparse
import collections
import sys


def load(path):
    """({t: {addr: (x, y, z, hp, alive)}}, lowest address seen)"""
    series = collections.defaultdict(dict)
    pool = 0
    with open(path, errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                if "lowest=" in line:
                    try:
                        pool = int(line.split("lowest=")[1].split()[0], 0)
                    except (IndexError, ValueError):
                        pass
                continue
            p = line.split()
            if len(p) < 9:
                continue
            try:
                t = float(p[0])
                key = int(p[1], 0)
                x, y, z = float(p[4]), float(p[5]), float(p[6])
                hp = float(p[7])
                alive = int(p[8])
            except ValueError:
                continue
            series[t][key] = (x, y, z, hp, alive)
    return series, pool


def first_mission_second(series, threshold=200):
    """The first second whose entity count reaches `threshold`: that is mission start for that
    instance. The two instances do NOT reach the mission at the same process clock (their menu walks
    differ by tens of seconds), so aligning by process clock alone compares two different mission
    ages; `--auto-offset` aligns the MISSION clocks instead."""
    for t in sorted(series):
        if len(series[t]) >= threshold:
            return t
    return None


def percentile(vals, q):
    if not vals:
        return 0.0
    s = sorted(vals)
    k = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[k]


def compare(A, B, offset):
    bkeys = sorted(B)
    rows = []
    for t in sorted(A):
        bt = t - offset
        if not bkeys:
            break
        near = min(bkeys, key=lambda x: abs(x - bt))
        if abs(near - bt) > 0.6:
            continue
        ea, eb = A[t], B[near]
        ka, kb = set(ea), set(eb)
        common = ka & kb
        dists, hp_bad, over10 = [], 0, 0
        worst = (0.0, None)
        for k in common:
            ax, ay, az, ahp, _ = ea[k]
            bx, by, bz, bhp, _ = eb[k]
            d = ((ax - bx) ** 2 + (ay - by) ** 2 + (az - bz) ** 2) ** 0.5
            dists.append(d)
            if d > 10.0:
                over10 += 1
            if abs(ahp - bhp) > 0.5:
                hp_bad += 1
            if d > worst[0]:
                worst = (d, k)
        rows.append({
            "t": t, "bt": near, "only_a": len(ka - kb), "only_b": len(kb - ka),
            "common": len(common), "median": percentile(dists, 0.5), "p90": percentile(dists, 0.9),
            "max": max(dists) if dists else 0.0, "over10": over10, "hp": hp_bad,
            "worst": worst,
        })
    return rows


def table(rows, limit):
    print("t_A      t_B    onlyA onlyB common  median      p90      max   >10u hpmis")
    if not rows:
        return
    step = max(1, len(rows) // max(1, limit))
    for r in rows[::step][:limit]:
        print("%7.1f %7.1f %5d %5d %6d %7.2f %8.2f %8.2f %5d %5d" % (
            r["t"], r["bt"], r["only_a"], r["only_b"], r["common"], r["median"], r["p90"], r["max"],
            r["over10"], r["hp"]))


def buckets(rows, width):
    print("\n%d-second buckets: t0       seconds common  median(mean)  median(p90)  max     >10u/s  hpmis/s" % width)
    groups = collections.OrderedDict()
    if not rows:
        return
    t0 = rows[0]["t"]
    for r in rows:
        groups.setdefault(int((r["t"] - t0) // width) * width, []).append(r)
    for off, g in groups.items():
        n = len(g)
        print("            %5d-%5d %4d %6.0f %11.2f %11.2f %8.2f %6.1f %7.1f" % (
            off, off + width, n, sum(x["common"] for x in g) / n,
            sum(x["median"] for x in g) / n, sum(x["p90"] for x in g) / n,
            max(x["max"] for x in g), sum(x["over10"] for x in g) / n, sum(x["hp"] for x in g) / n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--offset", type=float, default=0.0, help="seconds: A's clock minus B's")
    ap.add_argument("--auto-offset", action="store_true",
                    help="align by mission start (first second with >=200 entities) instead")
    ap.add_argument("--rows", type=int, default=10)
    ap.add_argument("--buckets", type=int, default=0)
    ap.add_argument("--from", dest="t_from", type=float, default=None, help="first A second to include")
    ap.add_argument("--to", dest="t_to", type=float, default=None, help="last A second to include")
    a = ap.parse_args()

    A, pool_a = load(a.a)
    B, pool_b = load(a.b)
    if not A or not B:
        print("empty input: A %d seconds, B %d seconds" % (len(A), len(B)))
        return 1
    print("lowest address A 0x%08x (%d seconds) | B 0x%08x (%d seconds) | offset %.2f s"
          % (pool_a, len(A), pool_b, len(B), a.offset))
    if a.auto_offset:
        ta, tb = first_mission_second(A), first_mission_second(B)
        if ta is None or tb is None:
            print("cannot find a populated second on both sides (A %s, B %s)" % (ta, tb))
            return 1
        a.offset = ta - tb
        print("mission start A t=%.1f, B t=%.1f -> mission-age offset %.2f s" % (ta, tb, a.offset))

    rows = compare(A, B, a.offset)
    if a.t_from is not None:
        rows = [r for r in rows if r["t"] >= a.t_from]
    if a.t_to is not None:
        rows = [r for r in rows if r["t"] <= a.t_to]
    if not rows:
        print("no overlapping seconds; check --offset")
        return 1
    table(rows, a.rows)
    if a.buckets:
        buckets(rows, a.buckets)

    n = len(rows)
    print("\nseconds compared %d | peak max distance %.2f | mean of per-second medians %.2f | mean p90 %.2f"
          % (n, max(r["max"] for r in rows), sum(r["median"] for r in rows) / n,
             sum(r["p90"] for r in rows) / n))
    print("entity-seconds in only one instance: A %d, B %d | hp mismatches %d (%.1f/s)"
          % (sum(r["only_a"] for r in rows), sum(r["only_b"] for r in rows), sum(r["hp"] for r in rows),
             sum(r["hp"] for r in rows) / n))
    print("entities beyond 10 units: %.1f per second of %d common (%.0f%%)"
          % (sum(r["over10"] for r in rows) / n, int(sum(r["common"] for r in rows) / n),
             100.0 * sum(r["over10"] for r in rows) / max(1, sum(r["common"] for r in rows))))
    first = next((r for r in rows if r["worst"][0] > 10.0), None)
    if first:
        print("first entity beyond 10 units: t=%.1f addr 0x%08x distance %.2f" % (
            first["t"], first["worst"][1], first["worst"][0]))
    else:
        print("no common entity ever exceeded 10 units")
    return 0


if __name__ == "__main__":
    sys.exit(main())
