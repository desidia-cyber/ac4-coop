"""Map every recompiled import stub to its (library, NID, name) and count call sites.

An import stub in the generated code is a short function of the shape
(see ppu_gen_148.cpp:435-444):

    R(12)=u64(s64(0));
    R(12)=R(12)|0xe10000ull;                 <- slot-page base
    R(12)=r32((u32(R(12))+-9664u));          <- + (slot - base)
    w64((u32(R(1))+40u),u64(R(2)));
    R(0)=r32(u32(R(12))); R(2)=r32((u32(R(12))+4u));
    ctx.ctr=R(0); {ctx.cia=...;icall(ctx,u32(ctx.ctr));return;}

so slot_va = base + delta.  The slot VA is a PLT-style entry that the port
rewrites with an HLE OPD (loader.cpp:89), and `icall` dispatches it to the HLE
handler (ppu_runtime.cpp icall).

  python netstub.py                  # all imports of the interesting libs
  python netstub.py --libs sceNp
  python netstub.py --all
"""
import argparse
import os
import pickle
import re
import sys
import tempfile

sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
import genidx  # noqa: E402
import netimp  # noqa: E402
from ac4elf import ELF  # noqa: E402

GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"
OUT = os.path.join(tempfile.gettempdir(), "ac4_netstub.pkl")

FUNC_RE = re.compile(rb"^void f_([0-9a-f]{8})\(Ctx&")
BASE_RE = re.compile(rb"R\((\d+)\)=R\(\1\)\|0x([0-9a-f]+)ull;")
DELTA_RE = re.compile(rb"R\((\d+)\)=r32\(\(u32\(R\(\1\)\)\+(-?\d+)u\)\);")
ICALL_TXT = b"icall(ctx,u32(ctx.ctr))"


def build():
    names = netimp.hle_names()
    tbl = netimp.imports(ELF(netimp.ELF_PATH))
    slot_of = {}
    for ent in tbl:
        for fid, sl in zip(ent["fnids"], ent["slot_addrs"]):
            slot_of[sl] = (ent["lib"], fid, names.get(ent["lib"], {}).get(fid))

    stubs = {}   # slot -> [stub addrs]
    for fn in sorted(os.listdir(GEN)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(GEN, fn), "rb") as f:
            lines = f.read().split(b"\n")
        cur = None
        base = None
        for lineno, line in enumerate(lines, 1):
            m = FUNC_RE.match(line)
            if m:
                cur, base = int(m.group(1), 16), None
                continue
            if cur is None:
                continue
            mb = BASE_RE.search(line)
            if mb:
                base = (int(mb.group(1)), int(mb.group(2), 16), fn, lineno)
                continue
            md = DELTA_RE.search(line)
            if md and base and int(md.group(1)) == base[0]:
                slot = (base[1] + int(md.group(2))) & 0xFFFFFFFF
                tail = b"\n".join(lines[lineno - 1:lineno + 6])
                if ICALL_TXT in tail:
                    stubs.setdefault(slot, []).append((cur, fn, lineno))
                base = None
    d = genidx.load()
    rows = []
    for slot, ss in sorted(stubs.items()):
        lib, fid, nm = slot_of.get(slot, ("<unknown lib>", 0, None))
        for stub, fn, ln in ss:
            sites = d["calls"].get(stub, [])
            rows.append(dict(slot=slot, lib=lib, fnid=fid, name=nm, stub=stub,
                             file=fn, line=ln,
                             callers=sorted(set(c[0] for c in sites)),
                             nsites=len(sites)))
    with open(OUT, "wb") as f:
        pickle.dump(rows, f)
    print("stubs %d (slots %d), unresolved slots: %d"
          % (len(rows), len(stubs), sum(1 for s in stubs if s not in slot_of)), file=sys.stderr)
    return rows


def load():
    with open(OUT, "rb") as f:
        return pickle.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--libs", default="sys_net,sceNp,cellNetCtl")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--rebuild", action="store_true")
    a = ap.parse_args()
    rows = build() if a.rebuild or not os.path.exists(OUT) else load()
    want = set(x for x in a.libs.split(",") if x)
    for r in sorted(rows, key=lambda r: (r["lib"], r["name"] or "", r["slot"])):
        if not a.all and r["lib"] not in want:
            continue
        print("%-14s %-34s slot %08x stub f_%08x %s:%d sites %d %s"
              % (r["lib"], r["name"] or ("fn_%08x" % r["fnid"]), r["slot"], r["stub"],
                 os.path.basename(r["file"]), r["line"], r["nsites"],
                 " ".join("f_%08x" % c for c in r["callers"][:6])))


if __name__ == "__main__":
    main()
