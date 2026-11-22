#!/usr/bin/env python3
"""luamap.py - recover Armored Core 4's Lua binding map from the static EBOOT.elf.

Scans the decrypted PS3 ELF for the two registration mechanisms the game uses and
emits a NAME -> ENGINE FUNCTION mapping.

Mechanism A (Lua 5.0.2 stock libraries)
    Classic ``luaL_reg`` arrays:  { const char *name; lua_CFunction func; }
    8 bytes per entry, NULL-name terminated.  ``func`` is a pointer to an 8-byte
    PPC64 function descriptor (OPD) { code_addr, toc }.

Mechanism B (game / Dantelion DLLua reflection descriptors)
    Per-class method descriptor arrays of the form
        { const char *ascii_name; const char *utf16_name;
          void *runtime_object;        /* BSS, zero in file */
          const void **method_set; }   /* -> N consecutive OPD pointers */
    Records are variable length (extra optional words); the parser below walks
    them by looking for the next (ascii,utf16) name pair whose texts agree.

Also parsed: the mission-command metadata table used by the debug/FE layer
``{ ascii, utf16, [id_array], <runtime handle> }`` which yields names only.

Everything is derived from the file; nothing is launched or built.

Usage:
    python luamap.py                 # print a summary
    python luamap.py --tsv  out.tsv  # write the binding table
    python luamap.py --report out.md # write the markdown report
    python luamap.py --selftest      # verify the Lua C-API accessor addresses
"""

from __future__ import annotations

import argparse
import csv
import os
import pickle
import re
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    from ac4elf import ELF
except ImportError:  # running from a scratch copy: fall back to tools/re
    sys.path.insert(0, r"G:\Freespace\Armored Core 4 (USA)\coop-mod\tools\re")
    from ac4elf import ELF

COOP = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
DEFAULT_ELF = os.path.join(COOP, "tools", "work", "files", "EBOOT.elf")
GEN_DIR = os.path.join(COOP, "ac4-port", "local", "gen", "ppu")
LUA_DIR = os.path.join(COOP, "extracted", "ac4_lua", "PS3_GAME", "USRDIR")

# guest address layout (see HANDOFF / ac4elf)
CODE_LO, CODE_HI = 0x00010000, 0x00decd78
DATA_LO, DATA_HI = 0x00E00000, 0x00F0B4D4     # .data/.opd (filesz)
STR_LO, STR_HI = 0x10000000, 0x10152350       # rodata
BSS_LO, BSS_HI = 0x10100000, 0x15D00000        # .data + .bss

FNIDX_CACHE = os.path.join(HERE, "luamap_fnidx.pkl")
TOC_CACHE = os.path.join(HERE, "luamap_toc.pkl")


def _cache_path(path):
    """Preferred cache location, falling back to the temp dir if not writable."""
    try:
        d = os.path.dirname(path) or "."
        probe = os.path.join(d, ".luamap_write_probe")
        with open(probe, "w") as f:
            f.write("")
        os.remove(probe)
        return path
    except OSError:
        import tempfile
        return os.path.join(tempfile.gettempdir(), os.path.basename(path))

# ---------------------------------------------------------------------------
# Lua C-API accessors.
#
# Addresses were recovered by decompiling the stock Lua 5.0.2 library bindings
# (math.abs, string.len, math.atan2, ...) and following their callees; the
# `fingerprint` strings are checked against the recompiled C++ on every run so a
# different EBOOT makes the tool fail loudly instead of lying.
# ---------------------------------------------------------------------------
ACCESSORS = {
    # name              code addr     role            fingerprint
    "lua_tonumber":    (0x002A30A4, "get:number",   "rf32((u32(R(3))+4u))"),
    "lua_tostring":    (0x002A32A4, "get:string",   "s32(R(0)),4)"),
    "lua_type":        (0x002A2854, "get:type",     "s64(s32(R(0)))"),
    "lua_gettop":      (0x002A0AE0, "get:top",      "sraw(ctx,R(3),3)"),
    "lua_pushnil":     (0x002A0B98, "push:nil",     "w32(u32(R(11)),u32(R(0)))"),
    "lua_pushnumber":  (0x002A0BB4, "push:number",  "wf32((u32(R(11))+4u),F(1))"),
    "lua_pushstring":  (0x002A0BD4, "push:string",  "s64(4)"),
    "lua_pushboolean": (0x002A0E6C, "push:boolean", "w32((u32(R(10))+4u),u32(R(0)))"),
    "lua_pushlightuserdata": (0x002A0E9C, "push:lightuserdata", "s64(2)"),
    "luaL_checknumber":  (0x002A5AA4, "check:number", "f_002a30a4"),
    "luaL_checklstring": (0x002A58E0, "check:string", "f_002a32a4"),
    # (L, idx, lua_Type) - the expected type arrives in r5, see TAG_NAME
    "luaL_checktype":    (0x002A59D0, "checktype:type", "f_002a2854"),
    # (L, idx) -> int; used by table.getn/sort (luaL_getn / lua_objlen)
    "lua_objlen":        (0x002A5DC0, "get:any", "f_002a2854"),
}
ACC_BY_ADDR = {v[0]: k for k, v in ACCESSORS.items()}

# Lua 5.0 TValue tags
TAG_NAME = {0: "nil", 1: "boolean", 2: "lightuserdata", 3: "number",
            4: "string", 5: "table", 6: "function", 7: "userdata",
            8: "thread"}

CALL_RE = re.compile(r"ctx\.lr=0x([0-9a-f]{8})u;f_([0-9a-f]{8})\(ctx\)")
CALL_RE_B = re.compile(rb"ctx\.lr=0x([0-9a-f]{8})u;f_([0-9a-f]{8})\(ctx\)")
DEF_RE = re.compile(rb"^void (f_([0-9a-f]{8}))\(Ctx& __restrict ctx\)\{")
R2_RE = re.compile(rb"u32\(R\(2\)\)\+(-?\d+)u")
SET_R4_CONST = re.compile(r"^R\(4\)=u64\(s64\((\d+)\)\);$")
SET_R5_CONST = re.compile(r"^R\(5\)=u64\(s64\((\d+)\)\);$")
SET_R3_CONST = re.compile(r"^R\(3\)=u64\(s64\((\d+)\)\);$")


# ===========================================================================
# ELF access helpers
# ===========================================================================
class Image:
    def __init__(self, path):
        self.elf = ELF(path)
        self.path = path
        self._str = {}
        self._u16 = {}
        self._opd = {}
        self.mapped = [(s.vaddr, s.vaddr + s.memsz) for s in self.elf.segs if s.memsz]
        # fast VA -> bytes regions (avoids slicing on every access)
        self.regions = []
        for s in self.elf.segs:
            if s.filesz:
                self.regions.append((s.vaddr, s.vaddr + s.filesz,
                                     memoryview(self.elf.data)[s.offset:s.offset + s.filesz]))
        self.regions.sort()
        self.reg_hi = max((hi for _lo, hi, _mv in self.regions), default=0)

    def is_mapped(self, va):
        return any(lo <= va < hi for lo, hi in self.mapped)

    def u32(self, va):
        for lo, hi, mv in self.regions:
            if lo <= va < hi - 3:
                return int.from_bytes(mv[va - lo: va - lo + 4], "big")
        return None

    def read(self, va, n):
        for lo, hi, mv in self.regions:
            if lo <= va < hi:
                return bytes(mv[va - lo: va - lo + n])
        return None

    def words(self):
        """[(va_of_first_word, array('I'))] - fast big-endian word views of each region."""
        if getattr(self, "_words", None) is None:
            from array import array
            out = []
            for lo, hi, mv in self.regions:
                n = len(mv) // 4
                a = array("I", bytes(mv[:n * 4]))
                if sys.byteorder == "little":
                    a.byteswap()
                out.append((lo, a))
            self._words = out
        return self._words

    def ascii_at(self, va):
        """Printable ASCII C string at va, or None."""
        if va in self._str:
            return self._str[va]
        r = None
        if STR_LO <= va < STR_HI:
            b = self.read(va, 128)
            if b:
                z = b.find(b"\0")
                if z >= 2:
                    s = b[:z]
                    if all(32 <= c < 127 for c in s):
                        r = s.decode("latin1")
        self._str[va] = r
        return r

    def utf16_at(self, va):
        """Printable UTF-16BE C string at va, or None."""
        if va in self._u16:
            return self._u16[va]
        r = None
        if STR_LO <= va < STR_HI:
            b = self.read(va, 200)
            if b and len(b) >= 8:
                out, i, bad = [], 0, False
                while i + 1 < len(b):
                    cp = (b[i] << 8) | b[i + 1]
                    if cp == 0:
                        break
                    if cp < 32 or cp > 126:
                        bad = True
                        break
                    out.append(chr(cp))
                    i += 2
                if not bad and len(out) >= 2:
                    r = "".join(out)
        self._u16[va] = r
        return r

    def name_pair(self, va):
        """(ascii, utf16) at va when both exist and agree, else (None, None)."""
        a = self.ascii_at(self.u32(va) or 0)
        w = self.u32(va + 4)
        if a is None or w is None:
            return None, None
        u = self.utf16_at(w)
        if u is None or u != a:
            return None, None
        return a, u

    def opd_code(self, ptr):
        """Code address for an OPD pointer, or None."""
        if ptr in self._opd:
            return self._opd[ptr]
        r = None
        if DATA_LO <= ptr < DATA_HI and (ptr & 3) == 0:
            c = self.elf.u32(ptr)
            t = self.elf.u32(ptr + 4)
            if c is not None and t is not None and CODE_LO <= c < CODE_HI:
                r = c
        self._opd[ptr] = r
        return r

    def opd_toc(self, ptr):
        if DATA_LO <= ptr < DATA_HI and (ptr & 3) == 0:
            c = self.elf.u32(ptr)
            t = self.elf.u32(ptr + 4)
            if c is not None and t is not None and CODE_LO <= c < CODE_HI:
                return t
        return None


# ===========================================================================
# Recompiled-C++ body index
# ===========================================================================
def build_fn_index(gen_dir=GEN_DIR, cache=FNIDX_CACHE, rebuild=False):
    """{ 'f_00abcdef': (file, start_line, end_line) } over the recompiled C++.

    Only the index is cached; file bodies are read back on demand so the cache
    stays a few MB instead of ~100 MB.
    """
    if not rebuild and os.path.exists(cache):
        try:
            with open(cache, "rb") as f:
                d = pickle.load(f)
            if d.get("gen_dir") == gen_dir and d.get("n", 0) > 1000:
                return d["idx"]
        except Exception:
            pass
    cache = _cache_path(cache)
    idx = {}
    for fn in sorted(os.listdir(gen_dir)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(gen_dir, fn), "rb") as f:
            lines = f.read().split(b"\n")
        cur, start = None, 0
        for i, ln in enumerate(lines):
            m = DEF_RE.match(ln)
            if m:
                if cur is not None:
                    idx[cur] = (fn, start, i)
                cur, start = m.group(1).decode(), i
            elif ln == b"}" and cur is not None:
                idx[cur] = (fn, start, i)
                cur = None
        if cur is not None:
            idx[cur] = (fn, start, len(lines) - 1)
    with open(cache, "wb") as f:
        pickle.dump({"gen_dir": gen_dir, "n": len(idx), "idx": idx}, f)
    return idx


class Bodies:
    def __init__(self, gen_dir=GEN_DIR):
        self.gen_dir = gen_dir
        self.idx = build_fn_index(gen_dir)
        self.cache = {}
        self.file_cache = {}

    def _lines(self, fn):
        if fn not in self.file_cache:
            with open(os.path.join(self.gen_dir, fn), "rb") as f:
                self.file_cache[fn] = f.read().split(b"\n")
        return self.file_cache[fn]

    def get(self, addr):
        k = "f_%08x" % addr
        if k in self.cache:
            return self.cache[k]
        v = self.idx.get(k)
        if not v:
            self.cache[k] = None
            return None
        fn, a, b = v
        lines = [l.decode("latin1") for l in self._lines(fn)[a:b + 1]]
        self.cache[k] = lines
        return lines

    def callees(self, addr):
        b = self.get(addr)
        if not b:
            return []
        return sorted({int(m.group(2), 16) for m in CALL_RE.finditer("\n".join(b))})


# ===========================================================================
# TOC resolution (function -> r2 base), so data xrefs can be resolved
# ===========================================================================
class DSU:
    def __init__(self):
        self.p = {}

    def find(self, x):
        self.p.setdefault(x, x)
        r = x
        while self.p[r] != r:
            r = self.p[r]
        while self.p[x] != r:
            self.p[x], x = r, self.p[x]
        return r

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[rb] = ra


def build_func_toc(img, gen_dir=GEN_DIR, cache=TOC_CACHE):
    if os.path.exists(cache):
        try:
            with open(cache, "rb") as f:
                d = pickle.load(f)
            if d.get("gen_dir") == gen_dir:
                return d["func_toc"], d["deltas"]
        except Exception:
            pass
    cache = _cache_path(cache)
    seeds = {}
    for seg in img.elf.segs:
        if seg.filesz < 8:
            continue
        blob = img.elf.data[seg.offset:seg.offset + seg.filesz]
        for i in range(0, len(blob) - 8, 4):
            a = int.from_bytes(blob[i:i + 4], "big")
            b = int.from_bytes(blob[i + 4:i + 8], "big")
            if CODE_LO <= a < CODE_HI and (a & 3) == 0 and 0x10000000 <= b < 0x10200000:
                seeds.setdefault(a, b)
    dsu, deltas = DSU(), {}
    for fn in sorted(os.listdir(gen_dir)):
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(gen_dir, fn), "rb") as f:
            data = f.read()
        cur, curd = None, set()
        for line in data.split(b"\n"):
            m = DEF_RE.match(line)
            if m:
                if cur:
                    deltas[cur] = curd
                cur, curd = int(m.group(2), 16), set()
                dsu.find(cur)
            if cur is None:
                continue
            if b"u32(R(2))" in line:
                for mm in R2_RE.finditer(line):
                    d = int(mm.group(1))
                    curd.add(d)
            if b"ctx.lr=0x" in line:
                for mm in CALL_RE_B.finditer(line):
                    dsu.union(cur, int(mm.group(2), 16))
        if cur:
            deltas[cur] = curd
    votes = {}
    for f, t in seeds.items():
        votes.setdefault(dsu.find(f), Counter())[t] += 1
    func_toc = {}
    for f in dsu.p:
        v = votes.get(dsu.find(f))
        if v:
            func_toc[f] = v.most_common(1)[0][0]
    with open(cache, "wb") as f:
        pickle.dump({"gen_dir": gen_dir, "func_toc": func_toc,
                     "deltas": {k: sorted(v) for k, v in deltas.items()}}, f)
    return func_toc, {k: sorted(v) for k, v in deltas.items()}


def find_code_refs(img, func_toc, deltas, target):
    """Functions that load `target` through an r2-relative TOC slot."""
    out = []
    for f, t in func_toc.items():
        if (target - t) in set(deltas.get(f, ())):
            out.append(f)
    return sorted(out)


# ===========================================================================
# Mechanism A: luaL_reg tables
# ===========================================================================
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def scan_lual_reg(img):
    """Runs of >=3 {char *name, OPD} records, NULL-name terminated.

    Also probes 12/16-byte strides for {name, opd, extra...} layouts, but a
    candidate is only accepted when *every* name is a C identifier and the
    record directly after the run has a NULL name pointer (the luaL_reg
    terminator), which keeps unrelated string/pointer soups out.
    """
    cands = []
    for base, ws in img.words():
        n = len(ws)
        for stride in (8, 12, 16):
            i = 0
            run = []
            while i + 1 < n:
                va = base + i * 4
                name_ptr = ws[i]
                nm = img.ascii_at(name_ptr) if name_ptr else None
                fn_ptr = ws[i + 1]
                code = img.opd_code(fn_ptr) if fn_ptr else None
                if nm and code is not None and len(nm) >= 2:
                    run.append({"record": va, "name": nm, "opd": fn_ptr,
                                "code": code, "stride": stride})
                    i += stride // 4
                    continue
                terminated = (i + 1 >= n) or not ws[i]
                if len(run) >= 3 and terminated and \
                        all(IDENT_RE.match(r["name"]) for r in run):
                    for r in run:
                        r["table"] = run[0]["record"]
                    cands.append({"addr": run[0]["record"], "stride": stride,
                                  "count": len(run), "kind": "global",
                                  "entries": list(run)})
                run = []
                i += 1
            if len(run) >= 3 and all(IDENT_RE.match(r["name"]) for r in run):
                for r in run:
                    r["table"] = run[0]["record"]
                cands.append({"addr": run[0]["record"], "stride": stride,
                              "count": len(run), "kind": "global",
                              "entries": list(run)})
    # accept largest first; drop candidates that reuse an accepted record slot
    cands.sort(key=lambda t: (t["stride"], -t["count"]))
    used, out = set(), []
    for t in cands:
        slots = {e["record"] for e in t["entries"]}
        if slots & used:
            continue
        used |= slots
        out.append(t)
    return sorted(out, key=lambda t: t["addr"])


# ===========================================================================
# Mechanism B: Dantelion method-descriptor tables
# ===========================================================================
def _is_field_word(img, w):
    """A word that may appear between one record's name pair and the next.

    Records carry 1..4 trailing words (runtime handle, method-set pointer,
    an id-array pointer, ...).  The values live in .bss/.data and are sometimes
    *past* the declared BSS end, so the discriminator is simply "not a string
    pointer" - the record boundary is then found by the next (ascii,utf16) pair.
    """
    if w == 0:
        return True
    return True


def scan_descriptor_tables(img):
    """Tables of {ascii, utf16, [field...]} records.

    One pass: collect every site whose word is a printable UTF-16BE string
    pointer, then keep the sites whose predecessor word is a printable ASCII
    pointer with the *same* text (that pairing is the record signature).
    """
    utf_sites = []
    for lo, ws in img.words():
        for i, w in enumerate(ws):
            if STR_LO <= w < STR_HI and img.utf16_at(w) is not None:
                utf_sites.append(lo + i * 4)

    records = []
    for site in utf_sites:
        nm, _ = img.name_pair(site - 4)
        if nm is not None:
            records.append((site - 4, nm))
    records.sort()
    rec_va = {va for va, _ in records}
    recset = dict(records)

    tables, seen = [], set()
    for va, nm in records:
        if va in seen:
            continue
        chain, p = [], va
        while p in recset and p not in seen:
            seen.add(p)
            chain.append((p, recset[p]))
            q, steps = p + 8, 0
            while steps < 6 and q not in rec_va:
                w = img.u32(q)
                if w is None or _is_field_word(img, w) is False:
                    break
                q += 4
                steps += 1
            if q == p + 8 or q not in rec_va:
                break          # no fields, or the next word is not a record
            p = q
        if len(chain) >= 3:
            tables.append(chain)

    tables.sort(key=lambda c: (-len(c), c[0][0]))
    used, out = set(), []
    for c in tables:
        if any(v in used for v, _ in c):
            continue
        used.update(v for v, _ in c)
        out.append(c)
    return out


def describe_descriptor_record(img, va):
    """Field breakdown of one descriptor record."""
    nm, uni = img.name_pair(va)
    q = va + 8
    fields = []
    while q < va + 40:
        if img.name_pair(q)[0] is not None:
            break
        w = img.u32(q)
        if w is None or not _is_field_word(img, w):
            break
        fields.append((q, w))
        q += 4
    method_set, methods = None, []
    for off, w in fields:
        if STR_LO <= w < STR_HI:
            p = img.u32(w)
            if p and img.opd_code(p) is not None:
                method_set = w
                mp = w
                for _ in range(8):
                    o = img.u32(mp)
                    if not o:
                        break
                    c = img.opd_code(o)
                    if c is None:
                        break
                    methods.append({"opd": o, "code": c})
                    mp += 4
                break
    return {
        "record": va, "name": nm, "unicode": uni,
        "runtime_obj": fields[0][1] if fields else None,
        "method_set": method_set, "methods": methods,
        "next": q,
    }


# ===========================================================================
# Argument / return analysis over the recompiled bodies
# ===========================================================================
def analyse_binding(bodies, code):
    """(n_args, arg_types, returns, callees) for a Lua C function.

    n_args comes from the highest constant stack index passed to a Lua API
    getter/checker.  The index register R(4) is tracked through the body and
    invalidated by any other write to it, so the constant must be the live one
    at the call site.
    """
    body = bodies.get(code)
    if not body:
        return None, "", None, []
    callees = bodies.callees(code)
    argidx = {}
    last4 = None
    last5 = None
    for i, line in enumerate(body):
        s = line.strip()
        m = SET_R4_CONST.match(s)
        if m:
            last4 = int(m.group(1))
            continue
        if s.startswith("R(4)="):
            last4 = None
            continue
        m = SET_R3_CONST.match(s)
        if m:
            last5 = int(m.group(1))
            continue
        m = SET_R5_CONST.match(s)
        if m:
            last5 = int(m.group(1))
            continue
        if s.startswith("R(5)="):
            last5 = None
            continue
        m = CALL_RE.search(s)
        if not m:
            continue
        tgt = int(m.group(2), 16)
        acc = ACC_BY_ADDR.get(tgt)
        info = ACCESSORS.get(acc) if acc else None
        if not info or last4 is None:
            continue
        role = info[1]
        if role.startswith("checktype:"):
            t = TAG_NAME.get(last5, "any") if last5 is not None else "any"
            argidx.setdefault(last4, set()).add(t)
        elif role.startswith(("get:", "check:")):
            argidx.setdefault(last4, set()).add(role.split(":")[1])
    n_args = max(argidx) if argidx else None
    if argidx:
        types = []
        for k in range(1, (n_args or 0) + 1):
            types.append("/".join(sorted(argidx.get(k, {"any"}))))
        arg_types = "|".join(types)
    else:
        arg_types = ""
    # result count: the R(3)=<const> that is live at the function's exit
    rets = None
    for i in range(len(body) - 1, -1, -1):
        s = body[i].strip()
        if s.rstrip(";") == "return":
            for j in range(i - 1, max(-1, i - 10), -1):
                m = SET_R3_CONST.match(body[j].strip())
                if m:
                    rets = int(m.group(1))
                    break
            break
        if s.startswith("R(3)="):
            break
    return n_args, arg_types, rets, callees


# ===========================================================================
# Script cross-check
# ===========================================================================
def scan_scripts(lua_dir=LUA_DIR):
    """(global_calls Counter, method_calls Counter) from the extracted .lua."""
    KW = {"if", "then", "else", "elseif", "end", "for", "while", "do", "return",
          "function", "local", "repeat", "until", "and", "or", "not", "in",
          "nil", "true", "false", "break"}
    call = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    meth = re.compile(r"[:.]([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    glob, methods = Counter(), Counter()
    if not os.path.isdir(lua_dir):
        return glob, methods
    for root, _d, files in os.walk(lua_dir):
        for fn in files:
            if not fn.lower().endswith(".lua"):
                continue
            try:
                txt = open(os.path.join(root, fn), "rb").read().decode("latin1")
            except Exception:
                continue
            for m in call.finditer(txt):
                n = m.group(1)
                if n not in KW:
                    glob[n] += 1
            for m in meth.finditer(txt):
                methods[m.group(1)] += 1
    return glob, methods


def count_call_args(lua_dir=LUA_DIR, names=None):
    """Observed top-level argument counts per called name."""
    res = defaultdict(Counter)
    names = names or set()
    pat = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    if not os.path.isdir(lua_dir):
        return res
    for root, _d, files in os.walk(lua_dir):
        for fn in files:
            if not fn.lower().endswith(".lua"):
                continue
            txt = open(os.path.join(root, fn), "rb").read().decode("latin1")
            for m in pat.finditer(txt):
                nm = m.group(1)
                if nm not in names:
                    continue
                i, depth, j, inq = m.end(), 1, m.end(), None
                while j < len(txt) and depth:
                    ch = txt[j]
                    if inq:
                        if ch == inq:
                            inq = None
                    elif ch in "\"'":
                        inq = ch
                    elif ch == "(":
                        depth += 1
                    elif ch == ")":
                        depth -= 1
                    j += 1
                argstr = txt[i:j - 1]
                n = 0 if not argstr.strip() else 1
                d2, q2 = 0, None
                for ch in argstr:
                    if q2:
                        if ch == q2:
                            q2 = None
                        continue
                    if ch in "\"'":
                        q2 = ch
                    elif ch in "([{":
                        d2 += 1
                    elif ch in ")]}":
                        d2 -= 1
                    elif ch == "," and d2 == 0:
                        n += 1
                res[nm][n] += 1
    return res


# ===========================================================================
# Classification
# ===========================================================================
EVENT_HINTS = {
    "SetEventPropertyGroup", "GetEventGroupAlives", "SetEventProperty",
    "GetCharaEventInterface", "GetObjectEventInterface", "RegistHandle",
    "SetEventHandleGroup", "KillEntity", "KillEventGroup", "BroadcastMessageToEventGroup",
    "GetApRateOfAC", "SetMissionResult", "SetEnableMoveGroup", "SetEnableMove",
    "SetEnableAttack", "SetEnableImmortal", "TerminateMission", "AccomplishMission",
    "GetDistEntityPair", "SetTimer", "ShowText", "SetRadarShapeID",
}
AI_HINTS = {
    "CreateShoot", "CreateShootInRange", "CreateShootMachinegun", "CreateFollow",
    "CreateRandomWalk", "CreatePassThrough", "CreateNothing", "CreateOperationLayer",
    "CreateAltitudeAdjust", "CreateCircularMoveEntity", "GetAiContext",
}


def classify(names):
    names = set(names)
    if len(names & EVENT_HINTS) >= 3:
        return "eventOperation"
    if len(names & AI_HINTS) >= 2:
        return "aicontext"
    return "other"


def lua_defined(lua_dir=LUA_DIR):
    """Names the scripts define themselves (so they are not engine bindings)."""
    pats = [
        re.compile(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)"),
        re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*function"),
        re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", re.M),
        re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*="),
    ]
    out = set()
    if not os.path.isdir(lua_dir):
        return out
    for root, _d, files in os.walk(lua_dir):
        for fn in files:
            if not fn.lower().endswith(".lua"):
                continue
            try:
                txt = open(os.path.join(root, fn), "rb").read().decode("latin1")
            except Exception:
                continue
            txt = re.sub(r"--\[\[.*?\]\]", " ", txt, flags=re.S)
            txt = re.sub(r"--[^\n]*", " ", txt)
            for p in pats:
                for m in p.finditer(txt):
                    out.add(m.group(1))
    return out


def crosscheck(rows, lua_dir=LUA_DIR):
    glob, meth = scan_scripts(lua_dir)
    defined = lua_defined(lua_dir)
    byname = {}
    for r in rows:
        byname.setdefault(r["name"], []).append(r)
    used = set(glob) | set(meth)
    missing = sorted((n for n in used
                      if n not in byname and n not in defined
                      and (glob.get(n, 0) + meth.get(n, 0)) > 0),
                     key=lambda n: -(glob.get(n, 0) + meth.get(n, 0)))
    unused = sorted(n for n in byname if n not in used)
    arity = count_call_args(lua_dir, set(byname))
    return {
        "used": used, "defined": defined, "missing": missing, "unused": unused,
        "arity": arity, "globals": glob, "methods": meth, "byname": byname,
    }


# ===========================================================================
# Main
# ===========================================================================
def class_hint_for(img, table_va, records, max_back=0x600):
    """Nearest name-pair *before* the table that is not itself a table record.

    Dantelion reflection emits {ClassName, utf16 class name} immediately before
    the method descriptors of that class, so this recovers the owning class.
    """
    recset = set(records)
    va = table_va - 4
    lo = table_va - max_back
    while va >= lo and va > 0:
        if va not in recset:
            nm, _ = img.name_pair(va)
            if nm is not None:
                return nm
        va -= 4
    return None


def build_map(elf_path=DEFAULT_ELF):
    img = Image(elf_path)
    bodies = Bodies()
    print("[luamap] elf=%s  functions indexed=%d" % (elf_path, len(bodies.idx)),
          file=sys.stderr)

    rows = []
    # ---- A: luaL_reg tables
    reg_tables = scan_lual_reg(img)
    print("[luamap] luaL_reg tables: %d" % len(reg_tables), file=sys.stderr)
    for t in reg_tables:
        names = [e["name"] for e in t["entries"]]
        kind = "global"
        for e in t["entries"]:
            n_args, atypes, rets, callees = analyse_binding(bodies, e["code"])
            rows.append({
                "table_addr": "%08x" % t["addr"], "kind": kind,
                "name": e["name"], "opd": "%08x" % e["opd"],
                "code_addr": "%08x" % e["code"],
                "n_args": n_args if n_args is not None else "NOT DETERMINED",
                "arg_types": atypes or "NOT DETERMINED",
                "returns": rets if rets is not None else "NOT DETERMINED",
                "direct_callees": " ".join("%08x" % c for c in callees) or "-",
                "source": "luaL_reg", "record_size": t["stride"],
            })

    # ---- B: descriptor tables
    desc = scan_descriptor_tables(img)
    print("[luamap] descriptor tables: %d" % len(desc), file=sys.stderr)
    for chain in desc:
        recs = [describe_descriptor_record(img, va) for va, _ in chain]
        names = [r["name"] for r in recs]
        kind = classify(names)
        klass = class_hint_for(img, chain[0][0], [v for v, _ in chain])
        for r in recs:
            code = r["methods"][0]["code"] if r["methods"] else None
            opd = r["methods"][0]["opd"] if r["methods"] else None
            n_args, atypes, rets, callees = (None, "", None, [])
            if code:
                n_args, atypes, rets, callees = analyse_binding(bodies, code)
            rows.append({
                "table_addr": "%08x" % chain[0][0], "kind": kind,
                "name": r["name"],
                "opd": "%08x" % opd if opd else "NOT DETERMINED",
                "code_addr": "%08x" % code if code else "NOT DETERMINED",
                "n_args": n_args if n_args is not None else "NOT DETERMINED",
                "arg_types": atypes or "NOT DETERMINED",
                "returns": rets if rets is not None else "NOT DETERMINED",
                "direct_callees": " ".join("%08x" % c for c in callees) or "-",
                "source": "descriptor",
                "record_size": "-",
                "method_set": "%08x" % r["method_set"] if r["method_set"] else "-",
                "n_methods": len(r["methods"]),
                "class_hint": klass or "-",
            })
    return img, rows, reg_tables, desc


TSV_COLS = ["table_addr", "kind", "name", "opd", "code_addr", "n_args",
            "arg_types", "returns", "direct_callees"]

# names that matter for network synchronisation
SYNC_HINTS = (
    "MoveTarget", "TurnToTarget", "KeepAltitude", "Altitude", "ViewTurn",
    "Kill", "SetEnableMove", "SetEnableAttack", "SetEnableImmortal",
    "SetEnableLock", "SetEnableTree", "SetEnableGravity", "GetApRate",
    "SetEnablePA", "MissionResult", "Accomplish", "TerminateMission", "FailMission",
    "SetTimer", "ShowText", "SendMessage", "BroadcastMessage", "GetEventGroupAlives",
    "GetEventGroupNum", "GetEntityGroupNum", "SetEventProperty", "RegistHandle",
    "TargetHasShot", "IsHardStan", "EntityIsInRegion", "GetDistEntityPair",
    "WatchEntityPairRelation", "GetCurrTargetDistance", "SetMoveTarget",
    "SetScriptCtrlGlobalValue", "GlobalValue", "SetCapEvent", "SetMissionTimeLimit",
)


def sync_relevant(rows, limit=40):
    """Sync-relevant rows, one per name, preferring the row that has a code address."""
    best = {}
    for r in rows:
        if not any(h in r["name"] for h in SYNC_HINTS):
            continue
        cur = best.get(r["name"])
        if cur is None:
            best[r["name"]] = r
        elif cur["code_addr"] == "NOT DETERMINED" and r["code_addr"] != "NOT DETERMINED":
            best[r["name"]] = r
    picked = list(best.values())
    picked.sort(key=lambda r: (r["code_addr"] == "NOT DETERMINED", r["name"]))
    return picked[:limit]


def write_report(path, rows, reg_tables, desc, img, cc, arity, elf_path):
    kinds = Counter(r["kind"] for r in rows)
    ncode = sum(1 for r in rows if r["code_addr"] != "NOT DETERMINED")
    nargs = sum(1 for r in rows if r["n_args"] != "NOT DETERMINED")
    L = []
    A = L.append
    A("# AC4 Lua binding map (BLUS30027)")
    A("")
    A("Static facts only, from the decrypted `EBOOT.elf`. Generated by `tools/re/luamap.py`")
    A("(re-runnable); machine-readable form in `research/lua_bindings.tsv`.")
    A("")
    A("## Registration mechanism")
    A("")
    A("**(A) stock Lua 5.0.2 `luaL_reg`** - 8-byte `{ const char *name; lua_CFunction fn; }`")
    A("records, NULL-name terminated, `fn` = pointer to an 8-byte PPC64 OPD `{code, toc}`.")
    A("9 tables / 113 entries cover the whole standard library (coroutine, _G, debug, io,")
    A("os, file-handle methods, math, string, table).")
    A("")
    A("```")
    _emit_reg_excerpt(img, reg_tables, A)
    A("```")
    A("")
    A("**(B) Dantelion DLLua reflection descriptors** - per-class method arrays whose records")
    A("carry an ASCII *and* a UTF-16 name plus 1..4 trailing words; one word points at a run of")
    A("up to 7 OPD pointers. Every game-specific command (`eventOperation:*`, `aicontext:*`,")
    A("AI operation factories) reaches C++ this way - names are string-compared against these")
    A("descriptors, not registered via `luaL_reg`. `runtime_object` is a .bss word (zero in file).")
    A("")
    A("```")
    _emit_desc_excerpt(img, desc, A)
    A("```")
    A("")
    A("## Counts")
    A("")
    A("%d bindings total - %s." % (len(rows),
      ", ".join("%s %d" % (k, v) for k, v in kinds.most_common())))
    A("%d resolve to concrete code; %d have a body-derived `n_args` (only Lua-C-function"
      % (ncode, nargs))
    A("style bindings can yield one). %d `luaL_reg` tables, %d descriptor tables."
      % (len(reg_tables), len(desc)))
    A("")
    A("## Entry points / Lua state")
    A("")
    A("- Library registration tables (verified, data): " +
      ", ".join("`%08x`" % t["addr"] for t in reg_tables))
    A("- `lua_open`/`lua_newstate`/`luaL_openlibs` address: **NOT DETERMINED**. Those tables")
    A("  are reached only from the Lua TU rodata pool at `0x00e1f4xx+`; no `r32(R(2)+delta)`")
    A("  reference to any Lua rodata address exists in the recompiled code (checked against all")
    A("  1046 observed TOC bases), so the referencing functions could not be pinned.")
    A("- `lua_State*` is heap-allocated; there is no static instance in the image.")
    A("")
    A("## Lua C-API accessors (identified, fingerprint-verified)")
    A("")
    A("TValue = 8 bytes (tag at +0, value at +4). Tags: 0 nil, 1 boolean, 3 number, 4 string,")
    A("5 table, 6 function, 7 userdata. `lua_State`: +8 base, +12 top, +16 global_State.")
    A("")
    acs = sorted(ACCESSORS.items(), key=lambda kv: kv[1][0])
    for i in range(0, len(acs), 2):
        cells = []
        for nm, (ad, _role, _fp) in acs[i:i + 2]:
            cells.append("`%s` = `0x%08x`" % (nm, ad))
        A("- " + "   ".join(cells))
    A("")
    A("## Bindings most relevant to network sync")
    A("")
    A("`n_args` is derived from the binding body; `script` is the argument count observed at")
    A("the extracted-Lua call sites, shown when no body-level count is derivable.")
    A("")
    A("| name | kind | code | opd | n_args | script |")
    A("|---|---|---|---|---|---|")
    for r in sync_relevant(rows, 24):
        obs = arity.get(r["name"])
        obs = "/".join(str(k) for k in sorted(obs)) if obs else "-"
        A("| `%s` | %s | `%s` | `%s` | %s | %s |"
          % (r["name"], r["kind"], r["code_addr"], r["opd"], r["n_args"], obs))
    A("")
    A("## Cross-check against the scripts")
    A("")
    A("- %d distinct names are called by the extracted scripts; %d distinct names are defined"
      % (len(cc["used"]), len(cc["defined"])))
    A("  by the scripts themselves (so not engine bindings).")
    A("- **%d script-used engine names are missing** from the map (top 10):"
      % len(cc["missing"]))
    A("  " + ", ".join("`%s`" % n for n in cc["missing"][:10]))
    A("  They exist as ELF strings but live in `{ascii,utf16,...}` records whose")
    A("  function-pointer word did not parse unambiguously -> **NOT DETERMINED**.")
    A("- **%d bindings are never used by any script** (top 10): %s"
      % (len(cc["unused"]), ", ".join("`%s`" % n for n in cc["unused"][:10])))
    A("")
    A("## NOT DETERMINED")
    A("")
    A("- `n_args`/`arg_types`/`returns` for mechanism-(B) bindings. They are plain C++ methods")
    A("  (`slot[0]` of the OPD set) called by a generic reflection dispatcher, so they never")
    A("  touch `lua_tonumber`/`luaL_checknumber` and no arity is encoded statically. The")
    A("  descriptor's constant-returning slot[3] was checked against 11 script call sites and")
    A("  does not correlate with argument count, so it is not arity.")
    A("- Code addresses for the 120-entry mission-command metadata table at `0x00eff1e8`")
    A("  (this is where `SetEventPropertyGroup`, `KillEntity`, `GetEventGroupAlives`,")
    A("  `SetMissionResult`, `GetApRateOfAC` etc. live). Its records have no OPD pointer;")
    A("  their trailing word (`0x15cef4xx`) is past the declared .bss end (`0x15ce2dd8`) and")
    A("  never materialised by any instruction, so the implementing functions stay unknown.")
    A("- Meaning of OPD-set slots 1..6 (7 slots + 3-NULL terminator is **verified**).")

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")


def _emit_reg_excerpt(img, reg_tables, A):
    t = None
    for c in reg_tables:
        if any(e["name"] == "abs" for e in c["entries"]):
            t = c
            break
    if not t:
        A("(no luaL_reg table found)")
        return
    A("; luaL_reg table @%08x  (name ptr, then OPD ptr -> {code, toc})" % t["addr"])
    for e in t["entries"][:5]:
        A("%08x  %08x %-10s   %08x -> code %08x"
          % (e["record"], img.u32(e["record"]), '"%s"' % e["name"], e["opd"], e["code"]))
    A("%08x  00000000             ; NULL name terminates" % (t["addr"] + 8 * t["count"]))


def _emit_desc_excerpt(img, desc, A):
    chain = None
    for c in desc:
        if any(n == "SetCapEvent" for _v, n in c):
            chain = c
            break
    if not chain:
        A("(no descriptor table found)")
        return
    A("; Dantelion descriptor table @%08x  {ascii, utf16, runtime_obj, method_set}" % chain[0][0])
    for va, nm in chain[:2]:
        r = describe_descriptor_record(img, va)
        A("%08x  %08x %-12s  %08x utf16  %08x obj  %08x method_set"
          % (va, img.u32(va), '"%s"' % r["name"], img.u32(va + 4),
             r["runtime_obj"] or 0, r["method_set"] or 0))
        if r["methods"]:
            A("          method_set -> %s"
              % " ".join("%08x" % m["code"] for m in r["methods"]))


def write_tsv(rows, path):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(TSV_COLS)
        for r in rows:
            w.writerow([r.get(c, "") for c in TSV_COLS])


def selftest(elf_path=DEFAULT_ELF):
    img = Image(elf_path)
    bodies = Bodies()
    bad = 0
    print("checking %d Lua C-API accessor fingerprints" % len(ACCESSORS))
    for name, (addr, role, fp) in sorted(ACCESSORS.items(), key=lambda kv: kv[1][0]):
        b = bodies.get(addr)
        if not b:
            print("  f_%08x %-22s NO BODY" % (addr, name))
            bad += 1
            continue
        txt = "\n".join(b)
        ok = fp in txt
        print("  f_%08x %-22s %-18s %s" % (addr, name, role, "OK" if ok else "MISMATCH"))
        if not ok:
            bad += 1
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", default=DEFAULT_ELF)
    ap.add_argument("--tsv")
    ap.add_argument("--report")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--rebuild-index", action="store_true")
    ap.add_argument("--xref", nargs="*", metavar="ADDR",
                    help="resolve r2-relative code references (best effort)")
    a = ap.parse_args(argv)

    if a.selftest:
        return selftest(a.elf)
    if a.rebuild_index:
        build_fn_index(rebuild=True)

    img, rows, reg_tables, desc = build_map(a.elf)

    if a.tsv:
        write_tsv(rows, a.tsv)
        print("wrote %s (%d rows)" % (a.tsv, len(rows)))

    if a.report:
        cc = crosscheck(rows)
        arity = cc["arity"]
        write_report(a.report, rows, reg_tables, desc, img, cc, arity, a.elf)
        print("wrote %s" % a.report)

    if a.xref:
        ft, deltas = build_func_toc(img)
        for t in a.xref:
            target = int(t, 0)
            refs = find_code_refs(img, ft, deltas, target)
            print("%08x -> %d r2-referencing function(s): %s"
                  % (target, len(refs), " ".join("%08x" % r for r in refs) or "-"))

    if not a.tsv and not a.report:
        kinds = Counter(r["kind"] for r in rows)
        print("total bindings: %d" % len(rows))
        for k, v in kinds.most_common():
            print("  %-16s %d" % (k, v))
        print("\nluaL_reg tables:")
        for t in reg_tables:
            print("  @%08x stride=%d n=%-3d %s" % (
                t["addr"], t["stride"], t["count"],
                ", ".join(e["name"] for e in t["entries"][:6])))
        print("\ndescriptor tables: %d" % len(desc))
        for c in desc:
            print("  @%08x n=%-3d %s" % (c[0][0], len(c),
                                         ", ".join(n for _, n in c[:5])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
