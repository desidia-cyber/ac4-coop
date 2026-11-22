"""coop_probe.py -- inject an ASCII-only Lua probe into a mission's BND3 bundles (test overlay only).

    python coop_probe.py --mode probe                 # group counts + a tag on every fail path
    python coop_probe.py --mode keepalive             # neutralise the fail paths (M5, 15 min runs)
    python coop_probe.py --mode both

Writes <w>\\payload\\overlay_test\\PS3_GAME\\USRDIR\\bind\\world\\m010_{event,mission}.000. It never
touches <w>\\payload\\overlay (the shipping overlay) and never writes into <w>\\disc.

Why this shape (research\\24 s6, research\\15 s6): mission Lua is a zlib entry inside those bundles, the
port's --test-overlay intercepts the bundle path, and a probe's output reaches the log only through
error(...) -> "[lua] script error ... <text>". Scripts are Shift-JIS with CRLF; the appended text is
pure ASCII so it is valid Shift-JIS.

Measured m010 facts this relies on (extracted\\ac4_lua ...\\m010\\eventgroup.lua, entityid.lua,
obj\\m010_rocket.lua):
  groups 1 = m010_GP_ALLENEMY, 2..9 = ENEMY01..ENEMY_LAST, 10..13 = GP_AC_ALL/AC01..AC03
  entities 1 = m010_ET_ROCKET, 2 = m010_ET_ENEFIRST, 3 = m010_ET_ENELAST
  fail paths: OnOutOfMissionArea / OnOutOfRegionAndTimeOut / OnDeadPlayer (m010.lua) and the
  prompt's own OnDeadCrasher4 -> MissionFailure (m010_rocket.lua), which fires when the shared
  counter "KamikazeDeads" reaches m010_CONST_LAST_INVASION = 100.
"""
import argparse
import os
import sys

B = r"G:\Freespace\Armored Core 4 (USA)\coop-mod"
sys.path.insert(0, os.path.join(B, "tools"))
import bnd3tool  # noqa: E402

PROBE_EVENT = b"""
-- AC4RIG PROBE (coop_probe.py). Appended: it must stay ASCII-only.
function AC4RIG_probe( eo )
  local t = "";
  for i = 1, 13 do
    local n = eo:GetEventGroupNum( i );
    if n ~= nil and n > 0 then
      t = t .. string.format( " g%d=%d/%d", i, eo:GetEventGroupAlives( i ), n );
    end
  end
  error( string.format( "AC4RIGPROBE kill=%s%s", tostring( GetSharedGlobalValue( eo, "KamikazeDeads" ) ), t ) );
end
function AC4RIG_tick( eo )
  SetTimerWithHandler( eo, 97, 10, "AC4RIG_tick" );
  AC4RIG_probe( eo );
end
local AC4RIG_orig_start = OnMissionStart;
function OnMissionStart( eo )
  AC4RIG_orig_start( eo );
  SetTimerWithHandler( eo, 97, 10, "AC4RIG_tick" );
  AC4RIG_probe( eo );
end
local AC4RIG_orig_region = OnOutOfRegionAndTimeOut;
function OnOutOfRegionAndTimeOut( eo )
  error( "AC4RIGPROBE FAIL region-timeout" );
  AC4RIG_orig_region( eo );
end
local AC4RIG_orig_area = OnOutOfMissionArea;
function OnOutOfMissionArea( eo )
  error( "AC4RIGPROBE FAIL mission-area-out" );
  AC4RIG_orig_area( eo );
end
local AC4RIG_orig_dead = OnDeadPlayer;
function OnDeadPlayer( eo )
  error( "AC4RIGPROBE FAIL player-dead" );
  AC4RIG_orig_dead( eo );
end
"""

KEEPALIVE_EVENT = b"""
-- AC4RIG KEEPALIVE (coop_probe.py): every mission-fail path of m010 becomes a no-op, so a test run
-- stays in gameplay. m010.lua registers all three handlers by NAME in Initial, so replacing the
-- globals after Initial is enough (and the engine looks them up by name when they fire).
local AC4RIG_noop = function( eo ) end;
OnOutOfRegionAndTimeOut = AC4RIG_noop;
OnOutOfMissionArea = AC4RIG_noop;
OnDeadPlayer = AC4RIG_noop;
"""

KEEPALIVE_ROCKET = b"""
-- AC4RIG KEEPALIVE (coop_probe.py): the prompt's own fail path (KamikazeDeads reaching 100) is the
-- fourth fail condition of m010; suppressing it keeps the sortie alive.
function OnDeadCrasher4( eo )
  error( "AC4RIGKEEPALIVE: invasion fail suppressed" );
end
"""

TARGETS = {
    "m010_event.000": ("common\\event\\mission\\m010.lua", PROBE_EVENT, KEEPALIVE_EVENT),
    "m010_mission.000": ("common\\event\\mission\\obj\\m010_rocket.lua", None, KEEPALIVE_ROCKET),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="probe", choices=("probe", "keepalive", "both"))
    ap.add_argument("--mission", default="m010")
    ap.add_argument("--out", default=os.path.join(B, "payload", "overlay_test"))
    a = ap.parse_args()

    dst_dir = os.path.join(a.out, "PS3_GAME", "USRDIR", "bind", "world")
    os.makedirs(dst_dir, exist_ok=True)
    want_probe = a.mode in ("probe", "both")
    want_keep = a.mode in ("keepalive", "both")

    for bundle, (entry_name, probe, keep) in TARGETS.items():
        src = os.path.join(B, "disc", "PS3_GAME", "USRDIR", "bind", "world", bundle)
        orig, entries = bnd3tool.parse(src)
        hit = 0
        for e in entries:
            if e["name"] != entry_name:
                continue
            add = b""
            if want_probe and probe:
                add += probe
            if want_keep and keep:
                add += keep
            if not add:
                continue
            e["data"] = bytearray(bytes(e["data"]) + b"\r\n" + add)
            hit += 1
        if not hit:
            if (want_probe and probe) or (want_keep and keep):
                print("FAIL %s: no entry %s" % (bundle, entry_name))
                return 1
            print("skip %s (nothing to add)" % bundle)
            continue
        out = os.path.join(dst_dir, bundle)
        with open(out, "wb") as f:
            f.write(bnd3tool.build(orig, entries))
        print("wrote %s (%d entry patched, %d bytes)" % (out, hit, os.path.getsize(out)))
    print("overlay_test = %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
