# Rebuild an AC4 BND3 "world" bundle (bind/world/mXXX_mission.000, mXXX_event.000).
# Outer: BND3, one entry at 0x20 {flags, csize@0x24, doff@0x28, id, nameoff, usize@0x34}, zlib at doff.
# Inner container: u32 count, then 80-byte entries from 0x50
#   {name[64], zero u32 @+64, offset/16 @+68, rounded/16 @+72, size @+76}
import struct, zlib


def _entries(d):
    n = struct.unpack_from('>I', d, 0)[0]
    out = []
    for i in range(n):
        base = 0x50 + 80 * i
        name = d[base:base + 64].split(b'\x00')[0].decode('latin1')
        off = struct.unpack_from('>I', d, base + 68)[0] * 16
        size = struct.unpack_from('>I', d, base + 76)[0]
        out.append({'name': name, 'data': bytearray(d[off:off + size])})
    return out


def parse(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'BND3', 'not BND3'
    csize = struct.unpack_from('>I', b, 0x24)[0]
    doff = struct.unpack_from('>I', b, 0x28)[0]
    usize = struct.unpack_from('>I', b, 0x34)[0]
    d = zlib.decompress(b[doff:doff + csize])
    assert len(d) == usize, 'usize mismatch'
    return b, _entries(d)


def parse_bytes(b):
    csize = struct.unpack_from('>I', b, 0x24)[0]
    doff = struct.unpack_from('>I', b, 0x28)[0]
    d = zlib.decompress(b[doff:doff + csize])
    return _entries(d)


def build(orig, entries):
    csize = struct.unpack_from('>I', orig, 0x24)[0]
    doff = struct.unpack_from('>I', orig, 0x28)[0]
    old = zlib.decompress(orig[doff:doff + csize])
    hdr_len = 0x50 + 80 * len(entries)
    out = bytearray(old[:hdr_len])
    struct.pack_into('>I', out, 0, len(entries))
    for i, e in enumerate(entries):
        base = 0x50 + 80 * i
        out[base:base + 64] = e['name'].encode('latin1').ljust(64, b'\x00')
        struct.pack_into('>I', out, base + 64, 0)
        while len(out) % 16:
            out.append(0)
        off = len(out)
        struct.pack_into('>I', out, base + 68, off // 16)
        struct.pack_into('>I', out, base + 72, (len(e['data']) + 15) // 16)
        struct.pack_into('>I', out, base + 76, len(e['data']))
        out += e['data']
    comp = zlib.compress(bytes(out), 9)
    res = bytearray(orig[:doff])
    struct.pack_into('>I', res, 0x24, len(comp))
    struct.pack_into('>I', res, 0x34, len(out))
    res += comp
    return bytes(res)


def roundtrip(path):
    orig, entries = parse(path)
    again = parse_bytes(build(orig, entries))
    ok = len(again) == len(entries) and all(
        a['name'] == b['name'] and a['data'] == b['data'] for a, b in zip(entries, again))
    return ok, entries
