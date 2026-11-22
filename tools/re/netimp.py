"""Enumerate the executable's import stub tables (library, NID, slot VA).

The port's loader binds imports exactly this way (loader.cpp:52-103,238-240):
PRX process-param (phdr 0x60000002) +24 = import-stub table begin, +28 = end;
each entry walked by its size byte holds, at +0x10, the library-name VA, and at
+0x14/+0x18 the NID array and the slot array (one 4-byte slot per NID).

  python netimp.py                     # table of imports in the given libs
  python netimp.py --all
  python netimp.py --slot 0x00dfc440   # what is at this slot
"""
import argparse
import hashlib
import os
import re
import struct
import sys

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
from ac4elf import ELF  # noqa: E402

ELF_PATH = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\work\files\EBOOT.elf"
HLE_DIR = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\src\runtime\hle"
NID_SUFFIX = bytes([0x67, 0x59, 0x65, 0x99, 0x04, 0x25, 0x04, 0x90,
                    0x56, 0x64, 0x27, 0x49, 0x94, 0x89, 0x74, 0x1A])


def nid_of(name):
    h = hashlib.sha1(name.encode() + NID_SUFFIX).digest()
    return h[0] | (h[1] << 8) | (h[2] << 16) | (h[3] << 24)


def hle_names():
    """{'module': {fnid: name}} from the port's HLE registration tables."""
    out = {}
    fn_re = re.compile(r"s_module\(\"(\w+)\"")
    hle_fn = re.compile(r"HLE_FN\((\w+)\)|HLE_RAW\((\w+)\)")
    explicit = re.compile(r"hle::Func\{\"(\w+)\",\s*&[^,]+,\s*0x([0-9a-fA-F]+)u\}")
    for fn in sorted(os.listdir(HLE_DIR)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(HLE_DIR, fn), "r", encoding="utf-8", errors="replace") as f:
            txt = f.read()
        mods = list(fn_re.finditer(txt))
        for i, m in enumerate(mods):
            mod = m.group(1)
            body = txt[m.end(): mods[i + 1].start() if i + 1 < len(mods) else len(txt)]
            tbl = out.setdefault(mod, {})
            for mm in hle_fn.finditer(body):
                nm = mm.group(1) or mm.group(2)
                tbl[nid_of(nm)] = nm
            for mm in explicit.finditer(body):
                nm, fid = mm.group(1), int(mm.group(2), 16)
                tbl[fid] = nm
                tbl.setdefault(nid_of(nm), nm)
    return out


def imports(e):
    phoff, phnum, phentsize = e.e_phoff, e.e_phnum, e.e_phentsize
    prx = None
    for i in range(phnum):
        p = phoff + i * phentsize
        if struct.unpack_from(">I", e.data, p)[0] == 0x60000002:
            prx = struct.unpack_from(">Q", e.data, p + 16)[0]
    if prx is None:
        raise SystemExit("no PRX param segment")
    begin, end = e.u32(prx + 24), e.u32(prx + 28)
    out = []
    a = begin
    while a < end:
        sz = struct.unpack(">B", e.read(a, 1))[0]
        nfunc = struct.unpack(">H", e.read(a + 6, 2))[0]
        nvar = struct.unpack(">H", e.read(a + 8, 2))[0]
        name_p = e.u32(a + 0x10)
        nids, slots = e.u32(a + 0x14), e.u32(a + 0x18)
        vnids, vrefs = e.u32(a + 0x1c), e.u32(a + 0x20)
        lib = e.cstr(name_p) if name_p else None
        out.append(dict(lib=lib, at=a, size=sz, nfunc=nfunc, nvar=nvar,
                        nids=nids, slots=slots, vnids=vnids, vrefs=vrefs,
                        fnids=[e.u32(nids + 4 * k) for k in range(nfunc)],
                        slot_addrs=[slots + 4 * k for k in range(nfunc)]))
        a += sz
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--libs", default="sys_net,sceNp,cellNetCtl,cellSysutil,cellHttp,cellHttpUtil")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--slot", default="")
    a = ap.parse_args()
    e = ELF(ELF_PATH)
    names = hle_names()
    tbl = imports(e)
    want = set(x for x in a.libs.split(",") if x)
    if a.slot:
        sv = int(a.slot, 0)
        for ent in tbl:
            if ent["slots"] <= sv < ent["slots"] + 4 * ent["nfunc"]:
                k = (sv - ent["slots"]) // 4
                fid = ent["fnids"][k]
                print("%s::%08x slot 0x%08x name=%s" % (ent["lib"], fid, sv,
                                                        names.get(ent["lib"], {}).get(fid, "?")))
        return
    for ent in tbl:
        if not a.all and ent["lib"] not in want:
            continue
        print("== lib %-24s entry 0x%08x size 0x%02x nfunc %d nvar %d slots 0x%08x"
              % (ent["lib"], ent["at"], ent["size"], ent["nfunc"], ent["nvar"], ent["slots"]))
        for fid, sl in zip(ent["fnids"], ent["slot_addrs"]):
            print("   fnid %08x  slot %08x  %s" % (fid, sl,
                                                   names.get(ent["lib"], {}).get(fid, "<not in HLE table>")))


if __name__ == "__main__":
    main()
