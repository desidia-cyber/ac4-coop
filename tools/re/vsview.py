"""Research 20 helpers: gen index + thunk resolution, TOC cells, vtables, callers.

Builds its own function index in the session workspace so it never disturbs the
shared genidx.pkl used by other agents.

  python vsview.py build
  python vsview.py thunk 0x00737888 ...
  python vsview.py body 0x00737888 [N]
  python vsview.py cell 0x00eeaa0c -23476 ...
  python vsview.py vt 0x1013c010 [N]
  python vsview.py vtfind 0x006ca970 ...
  python vsview.py callers 0x006ca970 ...
  python vsview.py xref 0x1013c010 ...
"""
import os
import pickle
import re
import sys

RE_DIR = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re"
GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"
ELF_PATH = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf"
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "genidx20.pkl")

sys.path.insert(0, RE_DIR)
from ac4elf import ELF  # noqa: E402

CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78

FUNC_RE = re.compile(rb"^void f_([0-9a-f]{8})(?:_guest)?\(Ctx& __restrict ctx\)\{")
CALL_RE = re.compile(rb"ctx\.lr=0x([0-9a-f]{8})u;f_([0-9a-f]{8})(?:_guest)?\(ctx\)")

_E = None


def elf():
    global _E
    if _E is None:
        _E = ELF(ELF_PATH)
    return _E


def u32(va):
    return elf().u32(va)


def is_code(w):
    return w is not None and CODE_LO <= w < CODE_HI


def is_opd_ptr(p):
    c, t = u32(p), u32(p + 4)
    return (is_code(c) and t is not None and t != 0 and (t & 3) == 0
            and 0x00E00000 <= t < 0x00F0B4D4)


def build():
    funcs, calls = {}, {}
    for fn in sorted(os.listdir(GEN)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(GEN, fn), "rb") as f:
            lines = f.read().split(b"\n")
        cur, cur_start = None, 0
        for i, line in enumerate(lines, 1):
            m = FUNC_RE.match(line)
            if m:
                if cur is not None:
                    funcs[cur] = (fn, cur_start, i - 1)
                cur = int(m.group(1), 16)
                cur_start = i
            if cur is not None and b"ctx.lr=0x" in line:
                for mm in CALL_RE.finditer(line):
                    calls.setdefault(int(mm.group(2), 16), []).append(
                        (cur, int(mm.group(1), 16), fn, i))
        if cur is not None:
            funcs[cur] = (fn, cur_start, len(lines))
    with open(CACHE, "wb") as f:
        pickle.dump({"funcs": funcs, "calls": calls}, f)
    print("funcs %d callees %d edges %d"
          % (len(funcs), len(calls), sum(len(v) for v in calls.values())))


_D = None
_LC = {}


def idx():
    global _D
    if _D is None:
        if not os.path.exists(CACHE):
            build()
        with open(CACHE, "rb") as f:
            _D = pickle.load(f)
    return _D


def body(addr):
    d = idx()
    if addr not in d["funcs"]:
        return None, None, None
    fn, a, b = d["funcs"][addr]
    if fn not in _LC:
        with open(os.path.join(GEN, fn), "rb") as f:
            _LC[fn] = f.read().decode("latin1").split("\n")
    return fn, a, "\n".join(_LC[fn][a - 1:b])


_RE_W = re.compile(r"w64\(\(u32\(R\(1\)\)\+\d+u\),u64\(R\(2\)\)\);")
_RE_HI = re.compile(r"R\(2\)=R\(2\)\+u64\(s64\((-?\d+)\)<<16\);")
_RE_LO = re.compile(r"R\(2\)=R\(2\)\+u64\(s64\((-?\d+)\)\);")
_RE_TAIL = re.compile(r"\{f_([0-9a-f]{8})(?:_guest)?\(ctx\);return;\}")


def thunk_info(addr):
    fn, ln, txt = body(addr)
    if txt is None:
        return None
    m = re.search(r"void f_[0-9a-f]{8}_guest\(Ctx& __restrict ctx\)\{", txt)
    if m:
        txt = txt[m.start():]
        ln = ln + txt[:0].count("\n")
    if not _RE_W.search(txt):
        return None
    if "ctx.ctr" in txt or "icall" in txt:
        return None
    mt = _RE_TAIL.search(txt)
    if not mt:
        return None
    stmts = [s.strip() for s in txt.split("\n")[1:] if s.strip() and s.strip() != "}"]
    for s in stmts:
        if (_RE_W.fullmatch(s) or _RE_HI.fullmatch(s) or _RE_LO.fullmatch(s)
                or _RE_TAIL.fullmatch(s) or s.startswith("void f_")):
            continue
        return None
    delta = 0
    for m2 in _RE_HI.finditer(txt):
        delta += int(m2.group(1)) << 16
    m3 = _RE_LO.search(txt)
    if m3:
        delta += int(m3.group(1))
    return delta, int(mt.group(1), 16)


def resolve(addr, base_toc=0x00EEAA0C):
    chain, cur, toc = [addr], addr, base_toc
    for _ in range(8):
        ti = thunk_info(cur)
        if ti is None:
            return cur, toc, chain
        delta, tgt = ti
        if toc is not None:
            toc = (toc + delta) & 0xFFFFFFFF
        cur = tgt
        chain.append(cur)
    return cur, toc, chain


def dump_vtable(va, n=200):
    rows = []
    for i in range(n):
        p = u32(va + i * 4)
        if p is None or not is_opd_ptr(p):
            break
        rows.append((i, p, u32(p), u32(p + 4)))
    return rows


def vt_start(va):
    while True:
        p = u32(va - 4)
        if p is None or not is_opd_ptr(p):
            return va
        va -= 4


def vtables_with(code):
    out = []
    for p in elf().find_u32(code):
        if not is_opd_ptr(p):
            continue
        s = vt_start(p)
        out.append((s, (p - s) // 4, p))
    return out


def annot(w):
    if w is None:
        return "--------"
    if CODE_LO <= w < CODE_HI:
        return "CODE 0x%08x" % w
    if 0x10100000 <= w < 0x10152350:
        return "VT   0x%08x" % w
    if 0x10152350 <= w < 0x105E2DD8:
        return "BSS  0x%08x" % w
    if 0x10000000 <= w < 0x100F5480:
        c = elf().cstr(w, 80)
        if c and len(c) >= 3 and all(32 <= ord(ch) < 127 for ch in c):
            return 'RO   0x%08x "%s"' % (w, c[:60])
        return "RO   0x%08x" % w
    if 0x00E00000 <= w < 0x00F0B4D4:
        return "RW   0x%08x" % w
    return "0x%08x ?" % w


def main():
    cmd = sys.argv[1]
    args = sys.argv[2:]

    if cmd == "build":
        build()
        return

    if cmd == "thunk":
        for a in args:
            a = int(a, 0)
            ti = thunk_info(a)
            if ti is None:
                fn, ln, _ = body(a)
                print("f_%08x: NOT a pure thunk (%s:%s)" % (a, fn, ln))
                continue
            real, toc, chain = resolve(a)
            print("f_%08x delta=%+7d toc=0x%08x  %s"
                  % (a, ti[0], toc or 0,
                     " -> ".join("f_%08x" % c for c in chain)))

    elif cmd in ("cell", "cellsc"):
        toc = int(args[0], 0)
        base = toc if toc > 0x1000 else None
        for a in args[1:]:
            off = int(a, 0)
            va = (toc + off) & 0xFFFFFFFF
            w = u32(va)
            if cmd == "cellsc":
                print("cell 0x%08x (toc%+d) = 0x%08x  %s" % (va, off, w or 0, annot(w)))
            else:
                print("0x%08x = 0x%08x" % (va, w or 0))

    elif cmd == "vt":
        va = int(args[0], 0)
        n = int(args[1], 0) if len(args) > 1 else 200
        print("vtable 0x%08x:" % va)
        for i, p, c, t in dump_vtable(va, n):
            b = body(c)
            chain = resolve(c)
            tag = ("  %s:%d" % (b[0], b[1])) if b[0] else ""
            extra = ""
            if chain[2][1:] and chain[2][:6] == ["f_%08x" % 0]:
                pass
            if len(chain) > 1:
                extra = "  [thunk->0x%08x]" % chain[0]
            print("  slot %2d opd 0x%08x code 0x%08x toc 0x%08x%s%s"
                  % (i, p, c, t, extra, tag))

    elif cmd == "vtfind":
        for a in args:
            a = int(a, 0)
            print("vtables holding f_%08x:" % a)
            for s, slot, p in vtables_with(a):
                print("   vtable 0x%08x slot %d (opd 0x%08x)" % (s, slot, p))

    elif cmd == "callers":
        d = idx()
        for a in args:
            c = int(a, 0)
            print("== direct callers of f_%08x ==" % c)
            for caller, lr, fn, ln in d["calls"].get(c, []):
                print("  f_%08x ret 0x%08x  %s:%d" % (caller, lr, fn, ln))

    elif cmd == "body":
        a = int(args[0], 0)
        fn, ln, txt = body(a)
        if txt is None:
            print("f_%08x: no body" % a)
        else:
            n = int(args[1], 0) if len(args) > 1 else 400
            for i, line in enumerate(txt.split("\n")[:n]):
                print("%s:%d: %s" % (fn, ln + i, line))

    elif cmd == "xref":
        e = elf()
        for a in args:
            a = int(a, 0)
            print("words == 0x%08x:" % a)
            for va in e.find_u32(a):
                print("   0x%08x" % va)

    elif cmd == "fn":
        d = idx()
        n = sum(1 for a in d["funcs"] if thunk_info(a))
        print("pure thunks: %d of %d funcs" % (n, len(d["funcs"])))

    else:
        print(__doc__)


if __name__ == "__main__":
    main()
