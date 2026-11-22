"""Fast literal/regex scanner over the recompiled gen_*.cpp files.

~110 MB across ~160 files, so read each file once and use bytes.find.
Output is "file.cpp:line: <line text>", ready to cite.
"""
import argparse
import os
import re
import sys

DEFAULT_DIR = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"


def iter_files(root, names=None):
    for fn in sorted(os.listdir(root)):
        if not fn.endswith(".cpp"):
            continue
        if names and fn not in names:
            continue
        yield os.path.join(root, fn)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pattern")
    ap.add_argument("--re", dest="regex", action="store_true")
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--max", type=int, default=120)
    ap.add_argument("--maxper", type=int, default=0)
    ap.add_argument("--count", action="store_true")
    ap.add_argument("--ctx", type=int, default=0, help="lines of context before/after")
    ap.add_argument("--files", default="")
    a = ap.parse_args()

    names = [x for x in a.files.split(",") if x] or None
    pat = a.pattern.encode()
    rx = re.compile(pat) if a.regex else None
    total = 0
    shown_total = 0
    per_file = {}
    for path in iter_files(a.dir, names):
        with open(path, "rb") as f:
            data = f.read()
        if not data:
            continue
        if rx is not None:
            positions = [m.start() for m in rx.finditer(data)]
        else:
            positions = []
            start = 0
            while True:
                i = data.find(pat, start)
                if i < 0:
                    break
                positions.append(i)
                start = i + 1
        if not positions:
            continue
        per_file[os.path.basename(path)] = len(positions)
        total += len(positions)
        # line starts for context extraction
        line_starts = None
        if positions and not a.count and shown_total < a.max:
            line_starts = [0]
            for i, ch in enumerate(data):
                if ch == 10:
                    line_starts.append(i + 1)
        shown = 0
        for pos in positions:
            if a.count or shown_total >= a.max or (a.maxper and shown >= a.maxper):
                continue
            ln = bisect(line_starts, pos) - 1
            lo = max(0, ln - a.ctx)
            hi = min(len(line_starts) - 1, ln + a.ctx)
            for k in range(lo, hi + 1):
                s = line_starts[k]
                e = data.find(b"\n", s)
                if e < 0:
                    e = len(data)
                txt = data[s:e].decode("latin1")
                if len(txt) > 200:
                    txt = txt[:200] + "..."
                print("%s:%d: %s" % (os.path.basename(path), k + 1, txt))
            if a.ctx:
                print("  --")
            shown += 1
            shown_total += 1
    if a.count:
        for k, v in sorted(per_file.items(), key=lambda kv: -kv[1]):
            print("%6d  %s" % (v, k))
    print("TOTAL %d hits in %d files" % (total, len(per_file)), file=sys.stderr)


if __name__ == "__main__":
    from bisect import bisect
    main()
