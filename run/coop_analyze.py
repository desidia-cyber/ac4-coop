"""Read the AC4_COOP_DUMP lines out of a run log and report which floats move.

    python coop_analyze.py <ac4.log> [--obj body1] [--from T0] [--to T1] [--top 40] [--ptrs]
    python coop_analyze.py <ac4.log> --track 0x80,0x84,0x88 --from T0 --to T1 [--other body2]
    python coop_analyze.py <ac4.log> --pairs

The dump (coop_body.cpp) writes, once a second:
    [ T I ... coop] COOP DUMP body1 +0x000  <4 floats>     (0x180 bytes, 16 at a time)
    [ T I ... coop] COOP DUMP body1 pointers (N): +0x..:0x........

body1 and body2 are sampled a millisecond apart, so samples are matched to the nearest time, never
by exact equality. The last sample of a live log can be short (it was being written); it is skipped.
"""
import argparse
import re
import sys

LINE = re.compile(r"^\[\s*([0-9.]+)\s.*?COOP DUMP (\w+) \+0x([0-9a-f]+)\s+(.*)$")
PTRS = re.compile(r"^\[\s*([0-9.]+)\s.*?COOP DUMP (\w+) pointers \((\d+)\):(.*)$")
PTR = re.compile(r"\+0x([0-9a-f]+):0x([0-9a-f]{8})")
HDR = re.compile(r"^\[\s*([0-9.]+)\s.*?COOP DUMP (\w+) 0x([0-9a-f]+)")


def parse(path):
    """{object: {sample_index: {offset: float}}}, body pointer per object, and the sample times.

    The log's timestamp field has 0.1 ms resolution, so one dump's 16 lines carry up to 16 slightly
    different times. Samples are therefore keyed by their order (one "COOP DUMP <obj> 0x..." header
    line starts a sample) and the header's time is kept as that sample's time.
    """
    vals, times, ptrs, body_of = {}, {}, {}, {}
    seq = {}
    for line in open(path, errors="ignore"):
        line = line.rstrip("\n")
        m = HDR.match(line)
        if m:
            obj = m.group(2)
            body_of[obj] = int(m.group(3), 16)
            idx = seq.get(obj, 0)
            seq[obj] = idx + 1
            vals.setdefault(obj, {})[idx] = {}
            times.setdefault(obj, {})[idx] = float(m.group(1))
            continue
        m = LINE.match(line)
        if m:
            obj, off, rest = m.group(2), int(m.group(3), 16), m.group(4)
            parts = rest.split()
            if len(parts) != 4:
                continue          # a truncated line
            try:
                fs = [float(x) for x in parts]
            except ValueError:
                continue
            idx = seq.get(obj, 0) - 1
            d = vals.setdefault(obj, {}).setdefault(idx if idx >= 0 else 0, {})
            for k, f in enumerate(fs):
                d[off + 4 * k] = f
            continue
        m = PTRS.match(line)
        if m:
            obj, rest = m.group(2), m.group(4)
            d = ptrs.setdefault(obj, {})
            for off, tgt in PTR.findall(rest):
                d[int(off, 16)] = int(tgt, 16)
    return vals, times, ptrs, body_of


def nearest(times, t):
    if not times:
        return None
    lo, hi = 0, len(times) - 1
    best = times[0]
    while lo <= hi:
        mid = (lo + hi) // 2
        if abs(times[mid] - t) < abs(best - t):
            best = times[mid]
        if times[mid] < t:
            lo = mid + 1
        else:
            hi = mid - 1
    return best


def report(vals, times, obj, t0, t1, top):
    idxs = [i for i in sorted(vals.get(obj, {})) if (t0 is None or times[obj][i] >= t0) and (t1 is None or times[obj][i] <= t1)]
    if not idxs:
        print("no samples for %s in the window" % obj)
        return
    offs = sorted({o for i in idxs for o in vals[obj][i]})
    rows = []
    for o in offs:
        series = [vals[obj][i][o] for i in idxs if o in vals[obj][i]]
        if not series:
            continue
        lo, hi = min(series), max(series)
        distinct = len({round(v, 6) for v in series})
        rows.append((hi - lo, o, series[0], series[-1], lo, hi, distinct, len(series)))
    rows.sort(reverse=True)
    print("%s: %d samples, %.1f..%.1f s" % (obj, len(idxs), times[obj][idxs[0]], times[obj][idxs[-1]]))
    print("  offset   first        last         min          max          range        distinct")
    for rng, o, first, last, lo, hi, distinct, n in rows[:top]:
        print("  +0x%03x %12.4f %12.4f %12.4f %12.4f %12.4f %5d/%-5d" % (o, first, last, lo, hi, rng, distinct, n))
    print("  constant offsets: %d of %d" % (len([r for r in rows if r[6] == 1]), len(rows)))


def track(vals, times, obj, offs, t0, t1, other=None, step=1.0):
    idxs = [i for i in sorted(vals.get(obj, {})) if (t0 is None or times[obj][i] >= t0) and (t1 is None or times[obj][i] <= t1)]
    if not idxs:
        print("no samples")
        return
    otime = {times[other][i]: i for i in vals.get(other, {})} if other else {}
    otl = sorted(otime)
    hdr = "  t   | " + " | ".join("%s+0x%x" % (obj, o) for o in offs)
    if other:
        hdr += " | " + " | ".join("%s+0x%x" % (other, o) for o in offs)
    print(hdr)
    t = times[obj][idxs[0]]
    end = times[obj][idxs[-1]]
    while t <= end:
        i = nearest([times[obj][k] for k in idxs], t)
        k = [x for x in idxs if times[obj][x] == i][0]
        d = vals[obj][k]
        line = "%6.1f | %s" % (times[obj][k], " ".join("%9.2f" % d.get(o, float("nan")) if o in d else "    -    " for o in offs))
        if other and otl:
            on = nearest(otl, t)
            e = vals[other][otime[on]]
            line += " | " + " ".join("%9.2f" % e.get(o, float("nan")) if o in e else "    -    " for o in offs)
        print(line)
        t += step


def pairs(vals, times, a, b, t0, t1, step=1.0):
    """Two objects' world-position rows side by side, matched by nearest sample time."""
    if not vals.get(a) or not vals.get(b):
        print("need both objects")
        return
    for o in (0x80, 0x84, 0x88):
        pass
    ta, tb = sorted(vals[a]), sorted(vals[b])
    print("  t   | %-9s %-25s | %-9s %-25s | dist" % (a, "pos x,y,z", b, "pos x,y,z"))
    t = times[a][ta[0]] if t0 is None else t0
    end = times[a][ta[-1]] if t1 is None else t1
    while t <= end:
        ia = ta[min(range(len(ta)), key=lambda j: abs(times[a][ta[j]] - t))]
        ib = tb[min(range(len(tb)), key=lambda j: abs(times[b][tb[j]] - t))]
        da, db = vals[a][ia], vals[b][ib]
        pa = [da.get(o) for o in (0x80, 0x84, 0x88)]
        pb = [db.get(o) for o in (0x80, 0x84, 0x88)]
        if all(v is not None for v in pa + pb):
            dist = sum((x - y) ** 2 for x, y in zip(pa, pb)) ** 0.5
            print("%6.1f | %8.2f %8.2f %8.2f | %8.2f %8.2f %8.2f | %7.2f" % (t, *pa, *pb, dist))
        t += step


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--obj", default="body1")
    ap.add_argument("--other", default="")
    ap.add_argument("--from", dest="t0", type=float)
    ap.add_argument("--to", dest="t1", type=float)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--ptrs", action="store_true")
    ap.add_argument("--track", default="")
    ap.add_argument("--pairs", action="store_true")
    ap.add_argument("--step", type=float, default=1.0)
    a = ap.parse_args()
    vals, times, ptrs, body_of = parse(a.log)
    print("objects: " + ", ".join("%s=0x%08x (%d samples)" % (k, body_of.get(k, 0), len(vals[k])) for k in sorted(vals)))
    if a.pairs:
        pairs(vals, times, a.obj, a.other or "body2", a.t0, a.t1, a.step)
        return 0
    if a.track:
        offs = [int(x, 0) for x in a.track.split(",")]
        track(vals, times, a.obj, offs, a.t0, a.t1, a.other or None, a.step)
        return 0
    for obj in [a.obj] if a.obj else sorted(vals):
        report(vals, times, obj, a.t0, a.t1, a.top)
        if a.ptrs and obj in ptrs:
            print("  pointers: " + " ".join("+0x%x->0x%08x" % kv for kv in sorted(ptrs[obj].items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
