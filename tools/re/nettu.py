"""path-string -> descriptor record -> functions in that translation unit.

Chain: `n:/Ac4/...` literal  --(u32 pointer)-->  per-TU descriptor record
--(r2-relative refs)-->  the functions compiled from that source file.

  python nettu.py "NexusRevolution/Source/Session" "Fsdp" "PS3UDPSocket"
"""
import argparse
import os
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
import netrefs  # noqa: E402
import refindex  # noqa: E402
from ac4elf import ELF  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("substrs", nargs="+")
    ap.add_argument("--min", type=int, default=2, help="min refs to report a function")
    a = ap.parse_args()
    e = ELF(r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf")
    data = e.data
    descmap = {}   # descriptor VA -> path
    for s in a.substrs:
        needle = s.encode("latin1")
        start = 0
        while True:
            i = data.find(needle, start)
            if i < 0:
                break
            va = e.offset_to_va(i)
            start = i + 1
            if va is None:
                continue
            # rewind to the start of the literal: after the previous NUL, or after the
            # 'n:/Ac4/' prefix for paths that are packed without a leading NUL
            cands = []
            z = data.rfind(b"\x00", max(0, i - 300), i + 1)
            if z >= 0:
                cands.append(e.offset_to_va(z + 1))
            j = data.rfind(b"n:/Ac4/", max(0, i - 300), i + 1)
            if j >= 0:
                cands.append(e.offset_to_va(j))
            sva = None
            for c in cands:
                if c is None:
                    continue
                t = e.cstr(c, 300) or ""
                if needle.decode("latin1") in t and all(32 <= ord(ch) < 127 for ch in t):
                    sva = c
                    break
            if sva is None or sva in descmap:
                continue
            text = e.cstr(sva, 200) or ""
            if not all(32 <= ord(c) < 127 for c in text):
                continue
            ptrs = e.find_u32(sva)
            if not ptrs:
                continue
            descmap[ptrs[0]] = text
    print("descriptors: %d" % len(descmap), file=sys.stderr)
    hits = netrefs.scan(sorted(descmap))
    for desc, path in sorted(descmap.items(), key=lambda kv: kv[1]):
        rows = hits.get(desc, [])
        funcs = {}
        for fn, ln, cur in rows:
            funcs.setdefault(cur, []).append((fn, ln))
        big = {k: v for k, v in funcs.items() if len(v) >= a.min}
        print("\n== desc 0x%08x  %s" % (desc, path))
        print("   %d refs in %d funcs (%d with >=%d refs)" % (len(rows), len(funcs), len(big), a.min))
        for cur, items in sorted(big.items(), key=lambda kv: -len(kv[1]))[:24]:
            fn, ln = items[0]
            print("      f_%08x  %s:%d  (%d)" % (cur, os.path.basename(fn), ln, len(items)))


if __name__ == "__main__":
    main()
