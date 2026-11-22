# 06 — Delivery, packaging and the experiment plan

---

## 1. How AC4 loads its Lua (measured)

`common/event/include/inc.lua` on the disc contains:

```lua
function LoadLuaFile( strFileName)
    print( strFileName);
    dofile( strFileName);
end
LoadLuaFile( "game:\\common\\event\\include\\msgid.lua");
LoadLuaFile( "game:\\common\\event\\include\\msgmacro.lua");
LoadLuaFile( "game:\\common\\event\\include\\misc.lua");
LoadLuaFile( "game:\\common\\event\\include\\inlfunc.lua");
```

So scripts are **read from disc paths at runtime, as plain text**, and every mission calls
`LoadEventStdScriptFile(CURR_DLLUA_INSTANCE)` plus `LoadMissionConstValue("mXXX", …)`.

Consequence: modifying a mission is a **data edit**, not a code patch. There are three ways to deliver
that, in increasing order of integration.

---

## 2. Delivery mechanisms available

### 2.1 `--test-overlay DIR` — immediately usable (development)

`ac4-port/src/runtime/vfs.h:6-7`:

```
Test aid (--test-overlay DIR): a file present under DIR (same layout as the disc root) is read
instead of the disc's; the disc is never modified. Every overlaid file is logged.
```

Implementation: `vfs.cpp:141-150` — if the file exists under the overlay dir, it is served instead, and
each hit is logged with `TEST OVERLAY: <guest path> read from <host path>`.

- Overlay directory mirrors the disc root, e.g.
  `overlay/PS3_GAME/USRDIR/common/event/mission/m000.lua`.
- Read-only; the disc is untouched.
- This is the fastest possible iteration loop: edit a `.lua`, relaunch the mission.

### 2.2 `vfs::replace(discRelative, hostPath)` — already used in production

`ac4-port/src/runtime/game/system_menu.cpp` builds a patched `systemsetting.000` from the user's disc
and serves it through `vfs::replace` (`vfs.cpp:154`). This is the proven "modify game data without
touching the disc" pattern, and it is what a released mod should use: the launcher/game patches the
user's own files at install/startup.

### 2.3 `lua_load` hook — the ACFA-Port approach

`acfa-port/src/runtime/game/script_fixes.cpp:89` hooks Lua 5.0's `lua_load` and hands the parser a
**rewritten copy** of the script in guest memory, restoring the original pointer afterwards. That
lets a mod transform scripts in flight without any files at all.

For AC4 the equivalent anchor is `luaD_throw` at `0x002b12dc` (`hooks.txt`); `lua_load` can be found
from it by disassembly if this route is chosen.

**Recommendation:** use `--test-overlay` while developing, and 2.2 (patch the user's own files at
startup) for the release. 2.3 only if you need to avoid touching files.

---

## 3. Distribution and the game-data rule (important)

The port's own rule is *"Nothing from the game is in this repository."* Modified mission scripts are
**derived from game code**, so a mod must not redistribute full copies of them.

Correct approach — and it has precedent in the ACFA port (`script_fixes.cpp` ships *string
replacements*, not scripts):

- ship the mod as **patches**: a list of targeted edit rules plus any entirely new Lua files you
  authored;
- at install, apply them to the user's own extracted copies (the port can read them from the disc);
- never ship the disc's Lua verbatim.

This also makes the mod robust across disc versions, because a patch fails loudly on a mismatch
(compare `patches.txt`, which asserts a match count and refuses to build otherwise).

---

## 4. Payload layout

A working folder structure has been created at:

```
G:\Freespace\Armored Core 4 (USA)\coop-mod\
  payload\
    overlay\PS3_GAME\USRDIR\common\event\        <-- dev overlay root (mirrors disc)
      include\
      mission\
        chara\
    patches\                                     <-- release-time edit rules
    new\                                         <-- files fully authored by the mod
```

For development, point the port at `payload\overlay` with `--test-overlay`.

### Files you will touch first

| file | why |
|---|---|
| `common/event/include/msgid.lua` | add `ENTITY_ID__PLAYER_0/1`, `ENTITY_ID__FRIEND`, `GP_ID__PLAYERS`, `GP_ID__PC_PLAYERS`, `REGION_POSITION_STATE__OUT_MISSION_PLAYRE` |
| `common/event/include/msgmacro.lua` | add `OnPlayerAllDestroyed`, `IsExistFriend`, `IsCooperationMission` stub, per-player region helpers, AP-averaging helper |
| `common/event/mission/chara/mXXX_player.lua` | the player character script — P2 counterpart lives here |
| `common/event/mission/mXXX.lua` | death handler, region observers, AP ranking |
| `common/event/include/mXXX/entityid.lua` / `eventgroup.lua` | P2 entity id and `_2P_` groups |

---

## 5. Ordered experiment plan

Each step is small and has a clear pass/fail. Do not proceed past a failure without addressing it.

### E0 — Baseline (must pass first)
Build and run the unmodified port with `--test-overlay` pointing at an **empty** overlay, and confirm
the log prints no overlay hits and missions behave normally. This proves the mechanism is inert when
unused.

### E1 — Script edit takes effect (delivery proof)
Overlay a copy of `m000_player.lua` with a single `print("COOP-PROBE")` added to `Initial`. Launch
m000. **Pass:** the log shows `COOP-PROBE`, proving overlay + Lua loading work.

### E2 — Constants and helpers load (no behaviour change)
Add the new constants and helper functions (doc 02 §3) to the overlaid `msgid.lua` / `msgmacro.lua`.
**Pass:** no Lua parse errors anywhere; missions still run.

### E3 — **The decisive test**: a second human-controlled AC
Overlay `m000_joshua.lua` per doc 02 §4. **Pass:** Joshua responds to a pad.
Record: which pad, and whether he responds at all. This determines the whole architecture.

### E4 — Second pad routing
If E3 responded to pad 0 only, add a `pad_update` hook that serves port 1 for the second pad object
(port field at `+384` / `+736`). **Pass:** P1 and P2 move independently.

### E5 — Co-op fail semantics
Replace `ON_DEAD_ENTITY(PLAYER)` with `OnPlayerAllDestroyed` in m000 and m240. **Pass:** killing P1
does not end the mission while P2 lives; killing both does.

### E6 — Per-player region logic
Implement the two-player region-out variant. **Pass:** leaving the mission area only fails when both
are out.

### E7 — AP ranking average
Apply the averaging from doc 04 §3.3 to m000. **Pass:** result message matches the average AP, not
P1's AP.

### E8 — Second viewpoint
Prototype with `OpenSubWindowPlayerCameraFor` (present in AC4's executable) or defer to the two-process
model in doc 05. **Pass:** P2 can see where they are going.

### E9 — Transport spike (doc 05 Route A)
Implement `sys_net` UDP + `socketpoll` over WinSock, `cellNetCtl` link, and try `SystemLinkMatch`
between two instances. **Pass:** a versus match connects. This is the gate for online co-op.

---

## 6. Gotchas already discovered

1. **PowerShell + spaces.** `Start-Process -ArgumentList` splits paths containing spaces into separate
   argv entries. This silently broke the RPCS3 decrypt step until switched to
   `& $exe --flag "$path"`. Expect the same with any tool you script.
2. **Guest memory is one fixed 4 GiB region** at `PPU_MEM_BASE` (`ppu_recomp.h:17`). Two game states in
   one process requires making `mem()` instance-aware — the reason doc 05 prefers two processes.
3. **No deterministic lockstep.** Guest threads are host threads with real synchronisation
   (`ppu_thread.h:1,31`). Snapshot sync only.
4. **Draw-arena budget.** The port already patched the draw arenas from 3 MiB to 32 MiB
   (`patches.txt:6-11`, 138 `addis` rewrites) to survive m010's swarm at 60 fps. Two viewpoints and/or
   extra `_2P_` enemies add draw cost — watch the `frame` log channel's arena high-water marks.
5. **Version lock.** All addresses are BLUS30027 disc 01.01. `ac4-recomp fingerprint` exists to remap
   functions to other builds (`recomp/main.cpp:22-27`).
6. **`AttachEntityEventGroup` does not exist in AC4**, so extra enemies must be placed in mission data
   and gated, not attached dynamically (doc 04 §2).
7. **Lua errors are quiet.** The port hooks `luaD_throw` and logs to the `lua` channel — watch that
   channel; a script error silently disables that event layer for the frame and can look like a
   gameplay bug.

---

## 7. Minimum viable deliverable

If the project needs a first shippable milestone, it is:

> **E1–E5 on a single mission, two pads, one machine** — two ACs, independent movement, co-op death
> rules, no networking. That is a real "co-op works" demo and it validates every assumption the
> online routes rest on.
