"""Resolve each guest function's r2 (TOC base).

A direct call does not change r2, so functions joined by direct-call edges share
one TOC.  Seed the union-find with the TOC found in each function's own OPD
entry, then label every component.  Conflicts are reported and, in practice,
should be zero.
"""
import os
import pickle
import re
import sys
from collections import Counter

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4opd import load  # noqa: E402

GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"
OUT = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re\tocmap.pkl"
CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78

CALL_RE = re.compile(rb"ctx\.lr=0x([0-9a-f]{8})u;f_([0-9a-f]{8})\(ctx\)")
FUNC_RE = re.compile(rb"^void f_([0-9a-f]{8})\(Ctx")


class DSU:
    def __init__(self):
        self.p = {}

    def find(self, x):
        self.p.setdefault(x, x)
        r = x
        while self.p[r] != r:
            r = self.p[r]
        while self.p[x] != r:
            self.p[x], x = r, self.p[x]
        return r

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[rb] = ra


def opd_tocs(e):
    mapped = [(s.vaddr, s.vaddr + s.memsz) for s in e.segs if s.memsz]

    def ok(va):
        return any(lo <= va < hi for lo, hi in mapped)

    out = {}
    for seg in e.segs:
        if not seg.filesz:
            continue
        blob = e.data[seg.offset:seg.offset + seg.filesz]
        for i in range(0, len(blob) - 4, 4):
            p = int.from_bytes(blob[i:i + 4], "big")
            if not ok(p) or (p & 3):
                continue
            w = e.u32(p)
            if w is None or not (CODE_LO <= w < CODE_HI):
                continue
            t = e.u32(p + 4)
            if t is None or not ok(t) or (t & 3) or not ok(t - 0x8000):
                continue
            if w not in out:
                out[w] = t
    return out


def main():
    e = load()
    seeds = opd_tocs(e)
    print("functions with an OPD toc: %d" % len(seeds), file=sys.stderr)
    dsu = DSU()
    edges = 0
    for fn in sorted(os.listdir(GEN)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(GEN, fn), "rb") as f:
            data = f.read()
        cur = None
        for line in data.split(b"\n"):
            m = FUNC_RE.match(line)
            if m:
                cur = int(m.group(1), 16)
                dsu.find(cur)
            if cur is None or b"ctx.lr=0x" not in line:
                continue
            for mm in CALL_RE.finditer(line):
                callee = int(mm.group(2), 16)
                dsu.union(cur, callee)
                edges += 1
    print("direct call edges: %d" % edges, file=sys.stderr)

    # label components
    comp_votes = {}
    for f, t in seeds.items():
        comp_votes.setdefault(dsu.find(f), Counter())[t] += 1
    fmap = {}
    conflicts = 0
    for f in list(dsu.p):
        r = dsu.find(f)
        votes = comp_votes.get(r)
        if votes:
            if len(votes) > 1:
                conflicts += 1
            fmap[f] = votes.most_common(1)[0][0]
    print("labelled %d functions, %d conflicting components"
          % (len(fmap), conflicts), file=sys.stderr)
    tc = Counter(fmap.values())
    for t, n in tc.most_common():
        print("   toc 0x%08x : %d funcs" % (t, n), file=sys.stderr)
    with open(OUT, "wb") as f:
        pickle.dump({"func_toc": fmap, "seeds": seeds}, f)


if __name__ == "__main__":
    main()
