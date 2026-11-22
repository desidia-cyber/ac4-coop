"""T1: locate scene-name strings in the ELF, list pointers to them, and dump
the surrounding words (name tables / factories / RTTI-ish records).
"""
import struct
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4elf import ELF  # noqa: E402

ELF_PATH = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf"

NAMES = [
    "Ac4SortieScene",
    "MissionSortieScene",
    "VsSortieScene",
    "ArenaSortieScene",
    "TutorialSortieScene",
    "MissionSortieScene::Init",
    "Ac4SortieScene::Init:",
    "Ac4SortieScene::LoadThread",
    "NetVsSortieScene",
    "MissionDebugSortieScene",
    "SplitTimer",
    "TwinTimer",
    "LEAVING AREA P1",
    "LEAVING AREA P2",
]


def seg_name(e, va):
    s = e.seg_of(va)
    return s.name if s else "?"


def words(e, va, before=8, after=8):
    """Dump 32-bit words around va, marking the word that points at va."""
    out = []
    for i in range(-before, after + 1):
        a = va + i * 4
        w = e.u32(a)
        if w is None:
            out.append("%08x: --------" % a)
        else:
            mark = "  <== target" if i == 0 else ""
            out.append("%08x: %08x%s" % (a, w, mark))
    return out


def main():
    e = ELF(ELF_PATH)
    print("segments: " + " ".join("%08x+%x" % (s.vaddr, s.filesz) for s in e.segs))
    for target in NAMES:
        print("\n===== %s =====" % target)
        exact = e.find_cstr(target)
        if exact:
            for va in exact:
                print("  EXACT cstr @ %08x (%s) len=%d" % (va, seg_name(e, va), len(target)))
        else:
            print("  EXACT cstr: none")
        # substring occurrences (e.g. inside a source path string)
        hits = e.find_bytes(target.encode())
        subs = []
        for va in hits:
            s = e.cstr(va - 200 if False else va)  # not meaningful; report position only
            subs.append(va)
        # report substring strings by walking back to the previous NUL
        reported = []
        for va in hits:
            back = e.read(max(0, va - 220), 220)
            if back is None:
                continue
            z = back.rfind(b"\0")
            start = va - (len(back) - (z + 1)) if z >= 0 else va
            txt = e.cstr(start, 260)
            if txt and (start, txt) not in reported:
                reported.append((start, txt))
                print("  IN-STRING @ %08x (%s): %r" % (start, seg_name(e, start), txt[:160]))
        for va in exact:
            ptrlocs = e.find_u32(va)
            print("  pointers to %08x: %d" % (va, len(ptrlocs)))
            for p in ptrlocs[:24]:
                print("    ptr @ %08x (%s)" % (p, seg_name(e, p)))
                for line in words(e, p, 4, 6):
                    print("       " + line)


if __name__ == "__main__":
    main()
