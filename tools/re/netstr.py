"""string VA -> referencing recompiled functions (for __FILE__ / class-name anchors).

  python netstr.py "SystemLinkMatch" "SessionSystemLink" ...
  python netstr.py --all-src Network/        # substring match, all hits
"""
import argparse
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
import refindex  # noqa: E402
from ac4elf import ELF  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("strings", nargs="*")
    ap.add_argument("--substr", action="store_true", help="match as substring in .rodata")
    ap.add_argument("--max", type=int, default=8)
    a = ap.parse_args()
    e = ELF(r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf")
    d = refindex.load_index()
    for s in a.strings:
        if a.substr:
            hits = []
            needle = s.encode("latin1")
            start = 0
            while True:
                i = e.data.find(needle, start)
                if i < 0:
                    break
                va = e.offset_to_va(i)
                if va is not None:
                    hits.append((va, e.cstr(va, 200)))
                start = i + 1
        else:
            hits = [(va, s) for va in e.find_cstr(s)]
        print("\n### %s -> %d string VA(s)" % (s, len(hits)))
        for va, text in hits[:a.max]:
            rs = refindex.refs(va, d)
            funcs = {}
            for fn, ln, cur, toc, ver in rs:
                funcs.setdefault(cur, []).append((fn, ln))
            print("  0x%08x %r  refs=%d funcs=%d" % (va, (text or "")[:70], len(rs), len(funcs)))
            for cur, items in sorted(funcs.items(), key=lambda kv: -len(kv[1]))[:12]:
                fn, ln = items[0]
                print("      f_%08x  %s:%d (%d refs)" % (cur, fn, ln, len(items)))


if __name__ == "__main__":
    main()
