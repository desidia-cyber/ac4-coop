"""OPD / TOC / vtable helpers for BLUS30027.

PS3 ELF: a function pointer is a 32-bit pointer to an 8-byte OPD entry
{code_address, toc_base}.  Vtables are arrays of such 32-bit OPD pointers.
Code reaches data as r2-relative, i.e. u32(R(2)) + offset, where r2 is the
toc_base out of the OPD entry used for the call.
"""
import struct
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4elf import ELF  # noqa: E402

ELF_PATH = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf"

CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78


def load():
    return ELF(ELF_PATH)


def is_code(w):
    return CODE_LO <= w < CODE_HI


def scan_opds(e):
    """Find {code, toc} pairs anywhere in data; return (opd_va, code, toc)."""
    out = []
    for seg in e.segs:
        if seg.flags & 4 and seg is e.segs[0]:
            pass
        if seg.filesz < 8:
            continue
        base = seg.vaddr
        blob = e.data[seg.offset:seg.offset + seg.filesz]
        for i in range(0, len(blob) - 8, 4):
            a, b = struct.unpack_from(">II", blob, i)
            if is_code(a) and 0x10000000 <= b < 0x11000000 and (b & 3) == 0:
                out.append((base + i, a, b))
    return out


def opd(e, ptr):
    """(code, toc) for an OPD pointer, or None."""
    a = e.u32(ptr)
    b = e.u32(ptr + 4)
    return (a, b) if a is not None else None


def vtable(e, va, n=40, stop_non_code=True):
    """[(slot_index, opd_ptr, code)] for n slots."""
    out = []
    for i in range(n):
        p = e.u32(va + i * 4)
        if p is None:
            break
        if not (0x00E00000 <= p < 0x105E2DD8):
            if stop_non_code:
                break
            out.append((i, p, None))
            continue
        c = opd(e, p)
        if c is None or not is_code(c[0]):
            if stop_non_code:
                break
            out.append((i, p, None))
            continue
        out.append((i, p, c[0]))
    return out


def toc_slots(e, value):
    """VAs of 32-bit words equal to `value` that look like TOC slots
    (inside the data segment, 4-byte aligned)."""
    return e.find_u32(value)


if __name__ == "__main__":
    e = load()
    ops = scan_opds(e)
    print("OPD-like pairs: %d" % len(ops))
    from collections import Counter
    c = Counter(t for _, _, t in ops)
    for toc, n in c.most_common(8):
        print("  toc 0x%08x used by %d opds" % (toc, n))
    print("first 10:")
    for va, code, toc in ops[:10]:
        print("  opd 0x%08x -> code 0x%08x toc 0x%08x" % (va, code, toc))
