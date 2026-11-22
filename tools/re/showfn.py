"""Print the body of one or more recompiled guest functions.

  python showfn.py 0x00812238 [0x...]          # whole bodies
  python showfn.py 0x00812238 --max 120
  python showfn.py --range ppu_gen_086.cpp 12200 12320

Body text is cited as <file>:<line>, matching the gen sources.
"""
import argparse
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
import genidx  # noqa: E402

GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"


def print_range(fname, lo, hi):
    with open(GEN + "\\" + fname, "rb") as f:
        lines = f.read().split(b"\n")
    for i in range(max(1, lo), min(len(lines), hi) + 1):
        print("%s:%d: %s" % (fname, i, lines[i - 1].decode("latin1")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addrs", nargs="*")
    ap.add_argument("--max", type=int, default=0)
    ap.add_argument("--range", nargs=3, action="append", default=[])
    a = ap.parse_args()
    d = genidx.load()
    for r in a.range:
        print_range(r[0], int(r[1]), int(r[2]))
    for s in a.addrs:
        v = int(s, 0)
        info = d["funcs"].get(v)
        if not info:
            print("f_%08x : NOT FOUND" % v)
            continue
        fname, lo, hi = info
        n = hi - lo + 1
        if a.max:
            hi2 = min(hi, lo + a.max - 1)
        else:
            hi2 = hi
        print("\n=== f_%08x  %s:%d-%d (%d lines) ===" % (v, fname, lo, hi, n))
        print_range(fname, lo, hi2)
        if hi2 < hi:
            print("   ... (%d more lines)" % (hi - hi2))


if __name__ == "__main__":
    main()
