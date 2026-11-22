"""coop_req.py -- talk to the port's entity-table instrument (ac4-port/src/runtime/game/coop_entities.cpp).

    python coop_req.py --tag ent2 --wait 4 "scan 0x10142638 120"
    python coop_req.py --tag ent2 --wait 4 --grep "COOP ENT" "mem 0x13009d90 0x100"
    python coop_req.py --tag ent2 --tail 40            # just the newest log lines

The instrument reads <w>\\run\\shots\\<tag>\\req.txt at most once a second and empties it, so one command
per invocation. Log lines are printed with the '[lua] script error' noise stripped out.
"""
import argparse
import glob
import os
import sys
import time

B = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
RUNS = {"host": "run/data/runs", "join": "run/data2/runs"}


def newest_log(tag):
    runs = RUNS.get(tag, "run/data/runs")
    d = os.path.join(B, runs)
    dirs = sorted(glob.glob(os.path.join(d, "*")), key=os.path.getmtime)
    return os.path.join(dirs[-1], "ac4.log") if dirs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="ent2")
    ap.add_argument("--wait", type=float, default=0)
    ap.add_argument("--tail", type=int, default=0)
    ap.add_argument("--grep", action="append", default=None)
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args()

    log = newest_log(a.tag)
    if not log:
        print("no run log")
        return 1
    before = os.path.getsize(log)

    if a.grep is None:
        a.grep = ["COOP ENT", "COOP SYNC", "COOP NET", "AC4RIGPROBE", "input context", "lua]"]

    if a.cmd:
        req = os.path.join(B, "run", "shots", a.tag, "req.txt")
        with open(req, "w") as f:
            f.write(" ".join(a.cmd) + "\n")
        if a.wait <= 0:
            a.wait = 3.0
    if a.wait:
        time.sleep(a.wait)

    with open(log, errors="ignore") as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        if a.tail:
            f.seek(max(0, size - 400 * a.tail))
            data = f.read()
        else:
            f.seek(before)
            data = f.read()
    out = [ln for ln in data.splitlines() if any(g in ln for g in a.grep)]
    if a.tail and not out:
        out = data.splitlines()[-a.tail:]
    print("\n".join(out[-400:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
