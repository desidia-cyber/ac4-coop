"""Address plumbing for BLUS30027 statics.

opds(CODE)      OPD entries {code, toc} for a guest function address
ptrto(ADDR)     data words equal to ADDR (32-bit big-endian pointers)
vtables(CODE)   vtables that hold a slot pointing at one of CODE's OPDs
slotinfo(P)     classify a data word: is it a vtable slot? which vtable/slot
"""
import struct
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4opd import load  # noqa: E402
import genidx  # noqa: E402

CODE_LO, CODE_HI = 0x00010000, 0x00DFCD78
_E = None


def elf():
    global _E
    if _E is None:
        _E = load()
    return _E


def mapped(va):
    e = elf()
    return any(s.vaddr <= va < s.vaddr + s.memsz for s in e.segs if s.memsz)


def is_code(w):
    return w is not None and CODE_LO <= w < CODE_HI


def opd_code(ptr):
    """(code, toc) at a candidate OPD pointer."""
    e = elf()
    return e.u32(ptr), e.u32(ptr + 4)


def is_opd_ptr(ptr):
    c, t = opd_code(ptr)
    return is_code(c) and mapped(t) and (t & 3) == 0


def opds(code):
    """Data words W such that {W: code-ptr, W+4: toc} and *(W)==code."""
    e = elf()
    out = []
    for va in e.find_u32(code):
        if va % 4:
            continue
        t = e.u32(va + 4)
        if t is not None and mapped(t) and (t & 3) == 0:
            out.append((va, t))
    return out


def ptrto(addr):
    e = elf()
    return [va for va in e.find_u32(addr) if mapped(va)]


def _run_start(p):
    """Walk back over consecutive valid OPD-slot words."""
    while True:
        w = elf().u32(p - 4)
        if w is None or not is_opd_ptr(w):
            return p
        p -= 4


def vtable_of_slot(p):
    start = _run_start(p)
    return start, (p - start) // 4


def vtables(code, limit=8):
    out = []
    for opd, t in opds(code):
        for p in ptrto(opd):
            start, slot = vtable_of_slot(p)
            out.append((start, slot, opd, t))
    return out


def dump_vtable(va, n=40):
    e = elf()
    rows = []
    for i in range(n):
        p = e.u32(va + i * 4)
        if p is None or not is_opd_ptr(p):
            break
        c, t = opd_code(p)
        rows.append((i, p, c, t))
    return rows


def callers(code):
    d = genidx.load()
    return d["calls"].get(code, [])


if __name__ == "__main__":
    cmd = sys.argv[1]
    for a in sys.argv[2:]:
        v = int(a, 0)
        if cmd == "opds":
            print("OPDs for f_%08x:" % v)
            for o, t in opds(v):
                print("   opd 0x%08x toc 0x%08x" % (o, t))
        elif cmd == "ptrto":
            for p in ptrto(v):
                print("   ptr @ 0x%08x" % p)
        elif cmd == "vt":
            print("vtables holding f_%08x:" % v)
            for start, slot, opd, t in vtables(v):
                print("   vtable 0x%08x slot %d (opd 0x%08x toc 0x%08x)"
                      % (start, slot, opd, t))
        elif cmd == "callers":
            print("callers of f_%08x:" % v)
            for caller, lr, fn, ln in callers(v):
                print("   f_%08x ret 0x%08x  %s:%d" % (caller, lr, fn, ln))
        elif cmd == "vtbody":
            print("vtable 0x%08x:" % v)
            for i, p, c, t in dump_vtable(v):
                print("   slot %2d opd 0x%08x code 0x%08x" % (i, p, c))
