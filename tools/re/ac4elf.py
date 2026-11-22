"""Minimal big-endian ELF64 reader for the AC4 PS3 EBOOT.

Guest VAs are 32-bit even though the file is ELF64.  Everything here works in
guest VA space so addresses match the recompiled C (f_XXXXXXXX) and the Ghidra
listing.
"""
import struct

PT_LOAD = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_DYNSYM = 11


class Seg:
    __slots__ = ("name", "vaddr", "offset", "filesz", "memsz", "flags", "shatype")

    def __init__(self, name, vaddr, offset, filesz, memsz, flags, shatype):
        self.name = name
        self.vaddr = vaddr
        self.offset = offset
        self.filesz = filesz
        self.memsz = memsz
        self.flags = flags
        self.shatype = shatype

    def __repr__(self):
        return "<%s va=%08x off=%x sz=%x>" % (self.name, self.vaddr, self.offset, self.filesz)


class ELF:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise ValueError("not an ELF")
        self.is64 = d[4] == 2
        # PS3 ELF is big-endian
        if d[5] != 2:
            raise ValueError("expected big-endian ELF")
        if self.is64:
            (self.e_type, self.e_machine, _ver, self.e_entry, self.e_phoff,
             self.e_shoff, self.e_flags, _ehsize, self.e_phentsize,
             self.e_phnum, self.e_shentsize, self.e_shnum, self.e_shstrndx) = \
                struct.unpack_from(">HHIQQQIHHHHHH", d, 16)
        else:
            (self.e_type, self.e_machine, _ver, self.e_entry, self.e_phoff,
             self.e_shoff, self.e_flags, _ehsize, self.e_phentsize,
             self.e_phnum, self.e_shentsize, self.e_shnum, self.e_shstrndx) = \
                struct.unpack_from(">HHIIIIIHHHHHH", d, 16)
        self.segs = []
        self.sections = []
        self._parse_sections()
        self._parse_segments()

    def _parse_sections(self):
        d, off = self.data, self.e_shoff
        raw = []
        for i in range(self.e_shnum):
            b = off + i * self.e_shentsize
            if self.is64:
                (name, typ, flags, addr, offset, size, link, info, align,
                 entsize) = struct.unpack_from(">IIQQQQIIQQ", d, b)
            else:
                (name, typ, flags, addr, offset, size, link, info, align,
                 entsize) = struct.unpack_from(">IIIIIIIIII", d, b)
            raw.append((name, typ, flags, addr, offset, size, link, info,
                        align, entsize))
        shstr = b""
        if self.e_shstrndx < len(raw):
            s = raw[self.e_shstrndx]
            shstr = d[s[4]:s[4] + s[5]]
        for (name, typ, flags, addr, offset, size, link, info, align, entsize) in raw:
            nm = shstr[name:shstr.find(b"\0", name)].decode("latin1") if name < len(shstr) else ""
            self.sections.append(Seg(nm, addr, offset, size, size, flags, typ))

    def _parse_segments(self):
        d, off = self.data, self.e_phoff
        for i in range(self.e_phnum):
            b = off + i * self.e_phentsize
            if self.is64:
                p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
                    struct.unpack_from(">IIQQQQQQ", d, b)
            else:
                p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
                    struct.unpack_from(">IIIIIIII", d, b)
            if p_type == PT_LOAD:
                self.segs.append(Seg("LOAD%d" % i, p_vaddr, p_offset, p_filesz,
                                     p_memsz, p_flags, p_type))
        self.segs.sort(key=lambda s: s.vaddr)

    # ---- addressing ----------------------------------------------------
    def seg_of(self, va, memsz=False):
        for s in self.segs:
            end = s.vaddr + (s.memsz if memsz else s.filesz)
            if s.vaddr <= va < end:
                return s
        return None

    def read(self, va, n):
        s = self.seg_of(va)
        if s is None:
            return None
        start = va - s.vaddr
        if start >= s.filesz:
            return None
        return self.data[s.offset + start: s.offset + min(start + n, s.filesz)]

    def u32(self, va):
        b = self.read(va, 4)
        return None if b is None or len(b) < 4 else struct.unpack(">I", b)[0]

    def cstr(self, va, limit=256):
        b = self.read(va, limit)
        if b is None:
            return None
        z = b.find(b"\0")
        if z >= 0:
            b = b[:z]
        try:
            return b.decode("latin1")
        except Exception:
            return None

    def va_to_offset(self, va):
        s = self.seg_of(va)
        if s is None or va - s.vaddr >= s.filesz:
            return None
        return s.offset + (va - s.vaddr)

    def offset_to_va(self, off):
        for s in self.segs:
            if s.offset <= off < s.offset + s.filesz:
                return s.vaddr + (off - s.offset)
        return None

    # ---- searching -----------------------------------------------------
    def find_bytes(self, needle, seg_flags=None):
        """Return guest VAs whose backing bytes equal needle."""
        out = []
        start = 0
        while True:
            i = self.data.find(needle, start)
            if i < 0:
                break
            va = self.offset_to_va(i)
            if va is not None:
                if seg_flags is None:
                    out.append(va)
                else:
                    s = self.seg_of(va)
                    if s and (s.flags & seg_flags):
                        out.append(va)
            start = i + 1
            if start > len(self.data) - len(needle):
                break
        return out

    def find_cstr(self, text, minlen=None):
        """VAs of NUL-terminated occurrences of text (exact, with NUL after)."""
        needle = text.encode("latin1") + b"\0"
        return self.find_bytes(needle)

    def find_u32(self, value, exclude_zero_page=False):
        """VAs of big-endian 32-bit words equal to value."""
        return self.find_bytes(struct.pack(">I", value))

    # ---- symbols -------------------------------------------------------
    def symbols(self):
        """(name, value, size, info) for .symtab plus .dynsym."""
        out = []
        for sec in self.sections:
            if sec.shatype not in (SHT_SYMTAB, SHT_DYNSYM):
                continue
            if sec.link >= len(self.sections):
                continue
            strtab = self.sections[sec.link]
            entsize = 24 if self.is64 else 16
            n = sec.filesz // entsize if entsize else 0
            for i in range(n):
                b = sec.offset + i * entsize
                if self.is64:
                    st_name, st_info, st_other, st_shndx, st_value, st_size = \
                        struct.unpack_from(">IBBHQQ", self.data, b)
                else:
                    st_name, st_value, st_size, st_info, st_other, st_shndx = \
                        struct.unpack_from(">IIIBBH", self.data, b)
                if not st_name:
                    continue
                raw = self.data[strtab.offset + st_name:]
                z = raw.find(b"\0")
                nm = raw[:z if z >= 0 else 128].decode("latin1", "replace")
                out.append((nm, st_value, st_size, st_info, st_shndx))
        return out

    def section_report(self):
        print("%-28s %-10s %-10s %-10s %s" % ("name", "vaddr", "offset", "size", "flags"))
        for s in self.sections:
            print("%-28s %08x   %08x   %08x   %x/%d" % (s.name, s.vaddr, s.offset,
                                                        s.filesz, s.flags, s.shatype))
        print("--- segments ---")
        for s in self.segs:
            print("%-28s %08x   %08x   filesz=%08x memsz=%08x flags=%x" % (
                s.name, s.vaddr, s.offset, s.filesz, s.memsz, s.flags))


if __name__ == "__main__":
    import sys
    e = ELF(sys.argv[1])
    print("entry=%08x machine=%d phnum=%d shnum=%d" % (e.e_entry, e.e_machine,
                                                       e.e_phnum, e.e_shnum))
    e.section_report()
