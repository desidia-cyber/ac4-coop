"""Index the gen tree: function bodies and direct calls.

funcs[addr] = (file, first_line, last_line)
calls[callee] = [(caller, lr, file, line)]
callers_of(callee) -> resolved call sites with the caller function.
"""
import os
import pickle
import re
import sys

GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"
OUT = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re\genidx.pkl"

FUNC_RE = re.compile(rb"^void f_([0-9a-f]{8})\(Ctx& __restrict ctx\)")
CALL_RE = re.compile(rb"ctx\.lr=0x([0-9a-f]{8})u;f_([0-9a-f]{8})\(ctx\)")


def build():
    funcs = {}
    calls = {}
    for fn in sorted(os.listdir(GEN)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(GEN, fn), "rb") as f:
            data = f.read()
        lines = data.split(b"\n")
        cur = None
        cur_start = 0
        for i, line in enumerate(lines, 1):
            m = FUNC_RE.match(line)
            if m:
                if cur is not None:
                    funcs[cur] = (fn, cur_start, i - 1)
                cur = int(m.group(1), 16)
                cur_start = i
            if cur is not None and b"ctx.lr=0x" in line:
                for mm in CALL_RE.finditer(line):
                    lr = int(mm.group(1), 16)
                    callee = int(mm.group(2), 16)
                    calls.setdefault(callee, []).append((cur, lr, fn, i))
        if cur is not None:
            funcs[cur] = (fn, cur_start, len(lines))
    with open(OUT, "wb") as f:
        pickle.dump({"funcs": funcs, "calls": calls}, f)
    print("funcs %d, callees %d, edges %d"
          % (len(funcs), len(calls), sum(len(v) for v in calls.values())),
          file=sys.stderr)


def load():
    with open(OUT, "rb") as f:
        return pickle.load(f)


if __name__ == "__main__":
    if sys.argv[1] == "build":
        build()
    else:
        d = load()
        for a in sys.argv[1:]:
            c = int(a, 0)
            print("\n== callers of f_%08x ==" % c)
            for caller, lr, fn, ln in d["calls"].get(c, []):
                print("  caller f_%08x  ret 0x%08x  %s:%d" % (caller, lr, fn, ln))
