"""Real vtable index: maximal runs of 32-bit OPD pointers.

A guest vtable is an array of 32-bit pointers, each pointing at an OPD
{code, TOC} (brief: "vtables = arrays of 32-bit OPD pointers").  A single
function OPD also looks like that, so only runs of >= --min entries are kept
(default 4), which is what distinguishes a vtable from a lone OPD.

  python vtidx.py --build
  python vtidx.py --find 0x0029ade8        # vtables whose slots contain this code
  python vtidx.py --dump 0x1013c010        # slots -> code
"""
import argparse
import os
import pickle
import struct
import sys
import tempfile

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4elf import ELF  # noqa: E402

ELF_PATH = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf"
CACHE = os.path.join(tempfile.gettempdir(), "ac4_vtindex.pkl")
CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78


def is_code(w):
    return w is not None and CODE_LO <= w < CODE_HI


def build(minlen=4):
    e = ELF(ELF_PATH)
    words = {}      # va -> u32 for every aligned, file-backed word
    for seg in e.segs:
        if not seg.filesz:
            continue
        blob = e.data[seg.offset:seg.offset + seg.filesz]
        for i in range(0, len(blob) - 3, 4):
            words[seg.vaddr + i] = struct.unpack_from(">I", blob, i)[0]

    def is_opd(p):
        if p is None or p & 3:
            return False
        c, t = words.get(p), words.get(p + 4)
        return is_code(c) and t is not None and 0x00E00000 <= t < 0x00F0B4D4 and (t & 3) == 0

    out = []        # (vtable_va, [code addresses])
    for va in sorted(words):
        if va % 4:
            continue
        if not is_opd(words[va]):
            continue
        prev = words.get(va - 4)
        if prev is not None and is_opd(prev) and va - 4 in words:
            continue          # not the start of a run
        run = []
        v = va
        while True:
            p = words.get(v)
            if not is_opd(p):
                break
            run.append((v, p, words[p]))
            v += 4
        if len(run) >= minlen:
            out.append((va, [(p, c) for _, p, c in run]))
    with open(CACHE, "wb") as f:
        pickle.dump({"vt": out, "words": words}, f)
    print("vtables %d (min %d entries)" % (len(out), minlen), file=sys.stderr)


def load():
    if not os.path.exists(CACHE):
        build()
    with open(CACHE, "rb") as f:
        return pickle.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--min", type=int, default=4)
    ap.add_argument("--find", nargs="*", default=[])
    ap.add_argument("--dump", default="")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.build:
        build(a.min)
        return
    d = load()
    if a.list:
        for va, slots in d["vt"]:
            print("vtable 0x%08x  %d slots  first code 0x%08x" % (va, len(slots), slots[0][1]))
        return
    if a.dump:
        va = int(a.dump, 0)
        hit = [s for s in d["vt"] if s[0] == va]
        if not hit:
            print("no vtable at 0x%08x" % va)
            return
        for i, (opd, code) in enumerate(hit[0][1]):
            print("  slot %2d opd 0x%08x code 0x%08x" % (i, opd, code))
        return
    for s in a.find:
        want = int(s, 0)
        print("== vtables with slot code 0x%08x" % want)
        found = False
        for va, slots in d["vt"]:
            for i, (opd, code) in enumerate(slots):
                if code == want:
                    found = True
                    print("   vtable 0x%08x slot %d (%d slots)" % (va, i, len(slots)))
        if not found:
            print("   none")


if __name__ == "__main__":
    main()
