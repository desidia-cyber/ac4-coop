"""Insert hook wrappers into the generated ppu_gen_*.cpp in the emitter's exact form.

A full `ac4-recomp emit --hooks` rewrites all 163 files and forces a full recompile, so new hooks
are inserted by hand (research/19 "Note on the generated files"); a later re-emit reproduces them.

  python addhook.py 0x002a6bdc lua_print 0x007cc0ec flow_ctor_a ...

Idempotent: a function that already has a wrapper is reported and left alone.
"""
import os
import re
import sys

GEN = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\ac4-port\local\gen\ppu"


def main(pairs):
    want = {int(a, 16): n for a, n in pairs}
    left = dict(want)
    for fn in sorted(os.listdir(GEN)):
        if not (fn.startswith("ppu_gen_") and fn.endswith(".cpp")):
            continue
        path = os.path.join(GEN, fn)
        with open(path, "rb") as f:
            data = f.read()
        changed = False
        for addr, name in list(left.items()):
            tag = b"void f_%08x(Ctx& __restrict ctx){" % addr
            i = data.find(tag)
            if i < 0:
                continue
            if data.find(b"void f_%08x_guest(Ctx& __restrict ctx){" % addr) >= 0:
                print("0x%08x %s: already hooked in %s" % (addr, name, fn))
                del left[addr]
                continue
            if i > 0 and data[i - 1:i] != b"\n":
                continue
            wrapper = (b"namespace ppu { void hook_%s(Ctx&, void (*)(Ctx&)); }\n"
                       b"void f_%08x_guest(Ctx& __restrict ctx);\n"
                       b"void f_%08x(Ctx& __restrict ctx){ ppu::hook_%s(ctx, f_%08x_guest); }\n"
                       b"void f_%08x_guest(Ctx& __restrict ctx){"
                       % (name.encode(), addr, addr, name.encode(), addr, addr))
            data = data[:i] + wrapper + data[i + len(tag):]
            changed = True
            print("0x%08x %s: wrapped in %s" % (addr, name, fn))
            del left[addr]
        if changed:
            with open(path, "wb") as f:
                f.write(data)
    for addr, name in left.items():
        print("0x%08x %s: NOT FOUND" % (addr, name))
    return 0 if not left else 1


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) % 2:
        sys.exit("pairs of <address> <name>")
    sys.exit(main(list(zip(args[0::2], args[1::2]))))
