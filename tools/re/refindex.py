"""Index of guest data references in the recompiled C.

The binary has several per-translation-unit TOC bases (r2), all of the form
OPD+4.  Code reaches data as  r32((u32(R(2)) + <signed decimal>u)), and a few
addresses as 0x........ hex immediates.  So:

  index[(toc, delta)] -> [(file, line, enclosing_function)]
  imm_index[addr]     -> [(file, line, enclosing_function)]

Query(addr) tries every TOC, keeps the delta that exists, and checks the
enclosing function's own OPD TOC when it has one.
"""
import os
import pickle
import re
import struct
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4opd import load  # noqa: E402

GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"
INDEX = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re\refindex.pkl"
CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78

R2_RE = re.compile(rb"u32\(R\(2\)\)\+(-?\d+)u")
HEX_RE = re.compile(rb"0x([0-9a-f]{8})u")
FUNC_RE = re.compile(rb"^void f_([0-9a-f]{8})\(Ctx")


def is_code(w):
    return CODE_LO <= w < CODE_HI


def build():
    e = load()
    mapped = [(s.vaddr, s.vaddr + s.memsz) for s in e.segs if s.memsz]

    def ok(va):
        return any(lo <= va < hi for lo, hi in mapped)

    # ---- OPD table: function -> toc, and referenced OPDs ---------------
    from collections import Counter
    toc_count = Counter()
    opd_of_func = {}
    for seg in e.segs:
        if not seg.filesz:
            continue
        blob = e.data[seg.offset:seg.offset + seg.filesz]
        for i in range(0, len(blob) - 4, 4):
            p = struct.unpack_from(">I", blob, i)[0]
            if not ok(p) or (p & 3):
                continue
            w = e.u32(p)
            if w is None or not is_code(w):
                continue
            t = e.u32(p + 4)
            if t is None or not ok(t) or (t & 3) or not ok(t - 0x8000):
                continue
            toc_count[t] += 1
            opd_of_func.setdefault(w, t)
    tocs = [t for t, n in toc_count.most_common(8)]
    print("TOCs: " + ", ".join("0x%08x(%d)" % (t, toc_count[t]) for t in tocs),
          file=sys.stderr)

    index = {}
    imm = {}
    n = 0
    for fn in sorted(os.listdir(GEN)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(GEN, fn), "rb") as f:
            data = f.read()
        cur = None
        for lineno, line in enumerate(data.split(b"\n"), 1):
            m = FUNC_RE.match(line)
            if m:
                cur = int(m.group(1), 16)
            if b"(R(2))" in line:
                for mm in R2_RE.finditer(line):
                    d = int(mm.group(1))
                    for t in tocs:
                        tg = t + d
                        if ok(tg):
                            index.setdefault((t, d), []).append((fn, lineno, cur))
                            n += 1
            if b"0x" in line:
                for mm in HEX_RE.finditer(line):
                    v = int(mm.group(1), 16)
                    if not is_code(v) and ok(v):
                        imm.setdefault(v, []).append((fn, lineno, cur))
    print("r2 entries %d, imm addrs %d, toc-delay keys %d"
          % (n, len(imm), len(index)), file=sys.stderr)
    with open(INDEX, "wb") as f:
        pickle.dump({"index": index, "imm": imm, "tocs": tocs,
                     "func_toc": opd_of_func}, f)


def load_index():
    with open(INDEX, "rb") as f:
        return pickle.load(f)


def refs(addr, d=None):
    """All (file, line, func, toc, verified) referencing addr."""
    d = d or load_index()
    out = []
    for t in d["tocs"]:
        for (fn, ln, cur) in d["index"].get((t, addr - t), []):
            expected = d["func_toc"].get(cur)
            ver = (expected == t) if expected else None
            out.append((fn, ln, cur, t, ver))
    for (fn, ln, cur) in d["imm"].get(addr, []):
        out.append((fn, ln, cur, None, True))
    return out


if __name__ == "__main__":
    if sys.argv[1] == "build":
        build()
        sys.exit(0)
    d = load_index()
    for a in sys.argv[1:]:
        addr = int(a, 0)
        rs = refs(addr, d)
        funcs = {}
        for fn, ln, cur, t, ver in rs:
            funcs.setdefault(cur, []).append((fn, ln, t, ver))
        print("\n== 0x%08x : %d refs in %d functions" % (addr, len(rs), len(funcs)))
        for cur, items in sorted(funcs.items(), key=lambda kv: -len(kv[1])):
            print("  f_%08x  (%d)" % (cur, len(items)))
            for fn, ln, t, ver in items[:6]:
                print("     %s:%d toc=%s verified=%s"
                      % (fn, ln, "0x%08x" % t if t else "-", ver))
