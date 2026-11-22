"""Resolve data references to specific guest VAs, using each function's own TOC.

refindex.pkl only indexes the 8 most common TOCs; a function's real TOC is the
one recorded in its OPD, so resolving per enclosing function is exact.  This
matters for .rodata strings whose TOC is not in the top 8 (which is why
`netstr.py` reports 0 refs for the Sw/Source/Network/__FILE__ literals).

  python netrefs.py 0x1007bd50 0x1007bc20 ...
  python netrefs.py --cstr n:/Ac4/data/source/Sw/Source/Network/Scene/NetVsSortieScene.cpp
"""
import argparse
import os
import pickle
import re
import sys
import tempfile

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
import refindex  # noqa: E402
from ac4elf import ELF  # noqa: E402

GEN = refindex.GEN
CACHE = os.path.join(tempfile.gettempdir(), "ac4_perfunc_refs.pkl")
FUNC_RE = re.compile(rb"^void f_([0-9a-f]{8})\(Ctx&")
R2_RE = re.compile(rb"u32\(R\(2\)\)\+(-?\d+)u")
HEX_RE = re.compile(rb"0x([0-9a-f]{8})u")


def scan(targets):
    """{va: [(file, line, func)]} for every requested VA."""
    d = refindex.load_index()
    func_toc = d["func_toc"]
    cand = sorted(set(func_toc.values()) | set(d["tocs"]))
    want = set(targets)
    out = {t: [] for t in want}
    for fn in sorted(os.listdir(GEN)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(GEN, fn), "rb") as f:
            lines = f.read().split(b"\n")
        cur, toc = None, None
        for lineno, line in enumerate(lines, 1):
            m = FUNC_RE.match(line)
            if m:
                cur = int(m.group(1), 16)
                toc = func_toc.get(cur)
                continue
            if b"(R(2))" in line and cur is not None:
                tocs = [toc] if toc else cand
                for mm in R2_RE.finditer(line):
                    dl = int(mm.group(1))
                    for t in tocs:
                        va = (t + dl) & 0xFFFFFFFF
                        if va in want:
                            out[va].append((fn, lineno, cur))
            if b"0x" in line:
                for mm in HEX_RE.finditer(line):
                    va = int(mm.group(1), 16)
                    if va in want:
                        out[va].append((fn, lineno, cur))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addrs", nargs="*")
    ap.add_argument("--cstr", action="append", default=[])
    a = ap.parse_args()
    e = ELF(r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf")
    targets = [int(x, 0) for x in a.addrs]
    labels = {int(x, 0): x for x in a.addrs}
    for s in a.cstr:
        for va in e.find_cstr(s):
            targets.append(va)
            labels[va] = s
    if os.path.exists(CACHE):
        with open(CACHE, "rb") as f:
            cached = pickle.load(f)
    else:
        cached = {}
    todo = [t for t in targets if t not in cached]
    if todo:
        cached.update(scan(todo))
        try:
            with open(CACHE, "wb") as f:
                pickle.dump(cached, f)
        except OSError:
            pass
    for t in targets:
        hits = cached.get(t, [])
        funcs = {}
        for fn, ln, cur in hits:
            funcs.setdefault(cur, []).append((fn, ln))
        print("\n== 0x%08x %s : %d refs in %d functions" % (t, labels.get(t, ""), len(hits), len(funcs)))
        for cur, items in sorted(funcs.items(), key=lambda kv: -len(kv[1])):
            fn, ln = items[0]
            print("   f_%08x  %s:%d  (%d refs)" % (cur, os.path.basename(fn), ln, len(items)))


if __name__ == "__main__":
    main()
