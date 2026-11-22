# Patch a mission's event bundle so the mission script auto-succeeds at start.
# Usage: autosucceed.py <mission-id>   e.g. autosucceed.py m010
#
# Mission scripts are not uniform: some define OnMissionSuccess, others OnMissionSuccess1..4.
# The hook therefore resolves the handler at run time and calls whichever exists.
import sys, os
sys.path.insert(0, r"G:\dsh-tmp\dsh-1PuwLP")
import bnd3tool as T

DISC = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\disc\PS3_GAME\USRDIR\bind\world"
OV = r"G:\Freespace\Armored Core 4 (USA)\coop-mod\payload\overlay\PS3_GAME\USRDIR\bind\world"

HOOK = (b'\r\n\r\n-- RIG: instant mission success (research only)\r\n'
        b'local __rigOrigOnMissionStart = OnMissionStart;\r\n'
        b'function OnMissionStart( eventOperation )\r\n'
        b'\t__rigOrigOnMissionStart( eventOperation );\r\n'
        b'\tlocal r = "RIGSUCC";\r\n'
        b'\tlocal ok, err = pcall( function() SetMissionResult_Success( eventOperation, 0 ) end );\r\n'
        b'\tr = r .. " result=" .. tostring(ok) .. (ok and "" or (":" .. tostring(err)));\r\n'
        b'\tlocal names = { "OnMissionSuccess", "OnMissionSuccess1", "OnMissionSuccess2", "OnMissionSuccess3", "OnMissionSuccess4" };\r\n'
        b'\tlocal fired = "none";\r\n'
        b'\tfor _, n in ipairs( names ) do\r\n'
        b'\t\tlocal f = _G[ n ];\r\n'
        b'\t\tif type( f ) == "function" then\r\n'
        b'\t\t\tlocal o2, e2 = pcall( f, eventOperation );\r\n'
        b'\t\t\tfired = n .. "=" .. tostring( o2 ) .. (o2 and "" or (":" .. tostring(e2)));\r\n'
        b'\t\t\tbreak;\r\n'
        b'\t\tend\r\n'
        b'\tend\r\n'
        b'\terror( r .. " handler=" .. fired );\r\n'
        b'end\r\n')


def patch(mid):
    src = os.path.join(DISC, mid + '_event.000')
    if not os.path.isfile(src):
        print(mid, 'MISSING'); return False
    orig, entries = T.parse(src)
    done = False
    for e in entries:
        if e['name'].replace('\\', '/').endswith('event/mission/%s.lua' % mid):
            d = bytes(e['data'])
            if b'function OnMissionStart' not in d:
                print(mid, 'no OnMissionStart in', e['name']); return False
            e['data'] = bytearray(d + HOOK)
            done = True
            break
    if not done:
        print(mid, 'mission lua entry not found'); return False
    os.makedirs(OV, exist_ok=True)
    out = os.path.join(OV, mid + '_event.000')
    open(out, 'wb').write(T.build(orig, entries))
    print('patched', mid, '->', os.path.getsize(out), 'bytes')
    return True


if __name__ == '__main__':
    for mid in sys.argv[1:]:
        patch(mid)
