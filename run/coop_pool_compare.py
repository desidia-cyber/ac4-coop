"""coop_pool_compare.py -- compare the two instances' entity table lines (M1).

    python coop_pool_compare.py --a host.log --b join.log

Reads the one-time `<COOP POOL t=... n=... lowest 0x...>` line and every `<COOP POOL ord N addr A id
I pos X Y Z hp H>` line from both logs and reports: the two lowest addresses, whether the entity sets
are identical, and for the common entries whether the address, the id and the spawn position agree.
"""
import argparse
import re
import sys

TABLE_RE = re.compile(r"COOP POOL t=\S+ n=(\d+) lowest 0x([0-9a-f]+)")
ROW_RE = re.compile(r"COOP POOL ord (\d+) addr 0x([0-9a-f]+) id (\d+) pos (\S+) (\S+) (\S+) hp (\S+)")


def load(path):
    lowest, n, rows = None, 0, {}
    for line in open(path, errors="ignore"):
        m = TABLE_RE.search(line)
        if m:
            n, lowest = int(m.group(1)), int(m.group(2), 16)
            continue
        m = ROW_RE.search(line)
        if m:
            rows[int(m.group(1))] = (int(m.group(2), 16), int(m.group(3)),
                                     float(m.group(4)), float(m.group(5)), float(m.group(6)))
    return lowest, n, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    a = ap.parse_args()
    la, na, A = load(a.a)
    lb, nb, B = load(a.b)
    print("A lowest %s n %d rows %d" % (hex(la) if la else "?", na, len(A)))
    print("B lowest %s n %d rows %d" % (hex(lb) if lb else "?", nb, len(B)))
    if la and lb:
        print("lowest addresses identical: %s" % (la == lb))
    if not A or not B:
        print("one side has no entity table")
        return 1
    ka, kb = set(A), set(B)
    print("ordinals only in A: %s" % sorted(ka - kb)[:20])
    print("ordinals only in B: %s" % sorted(kb - ka)[:20])
    addr_bad = id_bad = pos_bad = 0
    worst = 0.0
    for k in sorted(ka & kb):
        aa, ai, ax, ay, az = A[k]
        ab, bi, bx, by, bz = B[k]
        if aa != ab:
            addr_bad += 1
        if ai != bi:
            id_bad += 1
        d = ((ax - bx) ** 2 + (ay - by) ** 2 + (az - bz) ** 2) ** 0.5
        if d > 1.0:
            pos_bad += 1
        worst = max(worst, d)
    print("of %d common ordinals: address differs %d, id differs %d, spawn position differs (>1 unit) %d,"
          " worst position delta %.3f" % (len(ka & kb), addr_bad, id_bad, pos_bad, worst))
    return 0 if not (ka - kb) and not (kb - ka) and addr_bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
