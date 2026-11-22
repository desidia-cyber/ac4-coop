"""Annotated dump of a guest VA range: 32-bit words with string/code/vtable hints."""
import struct
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4elf import ELF  # noqa: E402

ELF_PATH = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf"

CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78
DATA_RW = (0x00E00000, 0x00F0B4D4)
RODATA = (0x10000000, 0x100F5480)
DATAVT = (0x10100000, 0x10152350)
BSS = (0x10152350, 0x105E2DD8)


def hint(e, w):
    if w == 0:
        return "0"
    if CODE_LO <= w < CODE_HI:
        return "CODE 0x%08x" % w
    for lo, hi, nm in ((RODATA[0], RODATA[1], "RO"),
                       (DATA_RW[0], DATA_RW[1], "RW"),
                       (DATAVT[0], DATAVT[1], "VT"),
                       (BSS[0], BSS[1], "BSS")):
        if lo <= w < hi:
            s = e.cstr(w, 64)
            if nm == "RO" and s and len(s) >= 2 and all(32 <= ord(c) < 127 for c in s):
                return "RO str %r" % s[:60]
            return "%s +0x%x" % (nm, w - lo)
    return "0x%08x?" % w


def main():
    e = ELF(ELF_PATH)
    start = int(sys.argv[1], 0)
    end = int(sys.argv[2], 0) if len(sys.argv) > 2 else start + 0x100
    for va in range(start, end, 4):
        w = e.u32(va)
        if w is None:
            print("%08x: --------" % va)
            continue
        s = e.cstr(va, 96)
        extra = ""
        if s and len(s) >= 3 and all(32 <= ord(c) < 127 for c in s):
            extra = "  str @this=%r" % s[:72]
        print("%08x: %08x  %-28s%s" % (va, w, hint(e, w), extra))


if __name__ == "__main__":
    main()
