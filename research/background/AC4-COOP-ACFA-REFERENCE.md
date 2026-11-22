# ACFA's native co-op — how it works, and what AC4 can reuse

Inputs analysed:
- `G:\Freespace\...\Armored Core - For Answer (USA).iso` (15,288,303,616 bytes, BLUS30187)
- <https://github.com/SulphurS16/ACFA-Port> v0.7.5 (`8e174e0`)
- The AC4 disc work from the previous passes (`./disc_utf8`)

Extracted artifacts: `./acfa_disc` (raw), `./acfa_all` + `./acfa_all_utf8` (1,649 Lua scripts
recovered from 105 `.bnd` archives), `./acfa_scripts`.

---

## 1. Premise check: confirmed, with three important caveats

**ACFA does have native co-op.** It is called **PARTNERSHIP** (spelled `Pertnership` in the
resources) and it is real: 93 of 1,649 recovered scripts reference a second player entity, and it has
its own lobby and mission-select scenes.

Three caveats that matter a lot for your plan:

| Claim | Reality |
|---|---|
| "native co-op" | True, but it is **online only** — `MultiPlay/Lobby/LobbyScene`, `Pertnership/LobbySelectScene`, and `MissionSortieInfo:Scr_IsNetworkSignedIn()` (`scene__025.lua:178`). No local/split-screen path exists. |
| "co-op campaign" | It is a **Free Play mission select**, not campaign progression: `CoOpMissionSelectScene__Resource = "FreePlay"` (`scene__026.lua:322`), which opens `FreeMissionSelectDialog`, sets `MissionID` + `Difficulty`, and launches. |
| "2 players" | Correct — **2 max**. `ENTITY_ID__PLAYER_0` and `ENTITY_ID__PLAYER_1`; no `PLAYER_2`/`PLAYER_3` anywhere. |

And the port does **not** implement it: `acfa-port/src/runtime/hle/sceNp.cpp:59` — *"PlayStation
Network is unavailable in this port (offline); online features fail."* There is no coop/netplay code
in ACFA-Port at all. So the *game* has co-op; the *port* does not expose it.

---

## 2. How ACFA implements it

### 2.1 Entity model — the elegant bit

`acfa_all_utf8/ingamescript__003.lua:69-73`:

```lua
ENTITY_ID__INVALID   = 0
ENTITY_ID__PLAYER    = 1000
ENTITY_ID__PLAYER_0  = ENTITY_ID__PLAYER
ENTITY_ID__PLAYER_1  = ENTITY_ID__PLAYER + 1     -- 1001
ENTITY_ID__BRIEFINGCARRIER = 10000
```

and `ingamescript__470.lua:22`, `sortie__059.lua:22`:

```lua
ENTITY_ID__FRIEND = 1001
```

**Player 2 occupies the same entity ID as the AI ally slot.** In single-player, 1001 is an AI
"friend" AC; in Partnership, a human takes over that slot. That is why the feature is cheap to add
at the script layer: the entity, its spawn, its HUD slot and its event wiring already exist for
allies.

Groups (`ingamescript__003.lua:397`): `GP_ID__PLAYERS = 100`, plus `GP_ID__PC_PLAYERS`.

### 2.2 Co-op-aware fail and region logic

`ingamescript__004.lua:1591-1596`:

```lua
-- プレイヤー側全滅(共同ミッション対応)  -- "player side annihilated (co-op mission support)"
function OnPlayerAllDestroyed( eventOperation , funcname )
  eventOperation:RegistHandle( WM_ENTITYFACTORY_NOTIFY, GP_ID__PC_PLAYERS,
                               ID_TO_SCRMSG__EVENTGROUP_ALLDELETE,
                               ID_TO_SCRMSG__EVENTGROUP_ALLDELETE, funcname, 1);
end
```

`ingamescript__004.lua:1581-1588`:

```lua
function ObservePlayerMissionRegionOutOrTimeout( eventOperation , funcCallback)
  eventOperation:ObservePlayerMissionRegionOutOrTimeout();
  ON_MISSION_REGION_NOTIFY( eventOperation , REGION_POSITION_STATE__OUT_MISSION_PLAYRE,
                            GP_ID__PLAYERS, funcCallback);
end
```

Note `REGION_POSITION_STATE__OUT_MISSION_PLAYRE = 3` (`ingamescript__003.lua:99`). **AC4's constant
file stops at `OUT_MISSION = 2` and `TIMEOUT_OUTSIDE_WARNING = 100`** — ACFA added a player-group
region state. So the engine, not just the scripts, learned about co-op.

`MissionFailure_PlayerIsDead` → `_2` → `_3` → `_4` → `_5` (`ingamescript__004.lua:887-917`) exist
because a mission can have several distinct player-death fail conditions.

### 2.3 Group-aware engine API

The engine grew a group dimension. Most-used methods across all scripts:

```
SetEventPropertyGroup           x404      SetEnableAttackGroup        x110
SetEventHandleGroup             x292      SetEnableMoveGroup           x88
SetEnableGravityGroup            x64      SetEnableSuperOverBoostGroup x10
KillEventGroup                   x56      SetEnableImmortalGroup        x4
GetEventGroupAlives             x114      ObserveRegionEventGroupInsideSomeone
IsEventGroupInRegionSomeone      x34      IsEventGroupInRegionAll      x15
SetRadarShapeIDGroup            x113      AttachEntityEventGroup       x30
```

Gating primitives: `IsCooperationMission()` (mode flag, 7 uses) and `IsExistFriend()` (runtime "is
P2 actually here" — 93 files). AI also knows about networking: `aicontext:IsNetworkMode()`
(`ingamescript__471.lua:630`).

§4 of `ingamescript__004.lua` also shows `SetEnableImmortalGroup(GP_ID__PLAYERS, true)` and
`SetMissionModeAllEntity(0)` used on mission failure — i.e. group semantics are first-class.

---

## 3. How ACFA balances two players — the direct answer

**There is no runtime difficulty scaling.** I searched all 1,649 scripts for
`PlayerNum`, `PlayerCount`, `GetPlayerNumber`, `NumberOfPlayer`, `NumOfPlayer`, `player_num`,
`NumPlayer` — **zero hits**. ACFA never asks "how many players are there?" to scale a number.

Instead it does three things, per mission, by hand:

### 3.1 Duplicate enemy groups per player, gated on P2 existing

`ingamescript__230.lua` (mission m070) is the clearest example:

```lua
eventOperation:AttachEntityEventGroup( ENTITY_ID__PLAYER_0, m070_GP_VOB_EXPLOSION_1P, "");
if( IsExistFriend( eventOperation)) then
    eventOperation:AttachEntityEventGroup( ENTITY_ID__PLAYER_1, m070_GP_VOB_EXPLOSION_2P, "");
end
...
eventOperation:SetEnableAttackGroup( m070_GP_DM_1P_1, false);
eventOperation:SetEnableAttackGroup( m070_GP_DM_1P_2, false);
eventOperation:SetEnableAttackGroup( m070_GP_DM_1P_3, false);
eventOperation:SetEnableAttackGroup( m070_GP_DM_2P,    false);
eventOperation:AttachEntityEventGroup( ENTITY_ID__PLAYER_0, m070_GP_DM_1P_1, "");
eventOperation:AttachEntityEventGroup( ENTITY_ID__PLAYER_0, m070_GP_DM_1P_2, "");
if( IsExistFriend( eventOperation)) then
    eventOperation:AttachEntityEventGroup( ENTITY_ID__PLAYER_1, m070_GP_DM_2P,   "");
    eventOperation:AttachEntityEventGroup( ENTITY_ID__PLAYER_1, m070_GP_DM_1P_3, "");
end
-- ダミー敵出現
ObserveRegionIn( eventOperation ,m070_PT_DM_1P_START, ENTITY_ID__PLAYER_0, "OnDummy1AttackOn");
if( IsExistFriend( eventOperation)) then
    ObserveRegionIn( eventOperation ,m070_PT_DM_2P_START, ENTITY_ID__PLAYER_1, "OnDummy2AttackOn");
end
```

So: enemy groups are authored in pairs (`_1P_` / `_2P_`), additional groups are attached **only when
P2 is present**, and there are separate trigger regions per player so the extra enemies activate on
P2's approach rather than all at once.

### 3.2 Rate results on the *average* AP of both players

`ingamescript__225.lua`:

```lua
local apRate = eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER_0);
if( IsExistFriend( eventOperation)) then
    apRate = ( eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER_1) + apRate) / 2;
end
```

Mission success messaging/rank thresholds are then chosen from that average. This stops co-op from
trivially earning the best result because one player stayed at full health.

### 3.3 Occasional co-op-specific content switches

`m240`: removes a dummy enemy group in co-op. `m280`: switches an arrival trigger to a shared-global
watch. Small, targeted, mission-specific.

**Summary:** ACFA's answer to "2 players makes it too easy" is *authorship*, not *scaling* — extra
enemy groups and trigger regions per player, gated on `IsExistFriend()`, plus AP averaging for the
result. That is exactly the approach to copy.

---

## 4. Can AC4 reuse it? Two layers, very different answers

### 4.1 Design/script layer — highly reusable

AC4 uses the same `eventOperation` API family and the same entity-ID architecture. AC4's equivalents:
`common/event/include/msgid.lua`, `msgmacro.lua`, `common/event/mission/mXXX.lua`,
`chara/mXXX_player.lua`, `include/mXXX/entityid.lua`.

What must be ported per mission (AC4 has 48 numbered mission scripts):

| ACFA pattern | AC4 today |
|---|---|
| `OnPlayerAllDestroyed(eo, fn)` | `ON_DEAD_ENTITY(eo, ENTITY_ID__PLAYER, "OnDeadPlayer")` → instant fail |
| `ObservePlayerMissionRegionOutOrTimeout(eo, fn)` | `ObserveMissionRegionOut(eo, ENTITY_ID__PLAYER, fn)` |
| `ENTITY_ID__PLAYER_0/1` | only `ENTITY_ID__PLAYER` |
| `GP_ID__PLAYERS` / `GP_ID__PC_PLAYERS` | none |
| `IsExistFriend()` gated extra enemy groups | none |

This is mechanical, and mission authoring is cheap to iterate (edit Lua → relaunch mission).

### 4.2 Engine layer — the real blocker

I grepped the whole AC4 disc tree for `GP_ID__`, `ENTITY_ID__FRIEND`, `IsExistFriend`, `PLAYER_0`,
`PLAYER_1`, `IsCooperation`, `OUT_MISSION_PLAYRE`, `PC_PLAYERS`: **no matches at all.**

Adding constants to Lua is trivial. What is *not* trivial is that ACFA's scripts call **engine
functions that AC4's build may not have**:

- `GP_ID__PC_PLAYERS` registration and the `WM_ENTITYFACTORY_NOTIFY` / `EVENTGROUP_ALLDELETE` notify
- `eventOperation:ObservePlayerMissionRegionOutOrTimeout()`
- `IsCooperationMission()` / `IsExistFriend()`
- the `*Group` setter family
- `REGION_POSITION_STATE__OUT_MISSION_PLAYRE` handling

AC4 is the earlier build. Whether its EBOOT contains any of these is **the decisive unknown**, and
it is answerable: build `ac4-recomp`, run `disasm`/`func` on the decrypted `eboot.elf`, and look at
AC4's event-system class to see whether the group dimension exists. If it does, the script layer
ports almost verbatim. If it does not, each missing primitive becomes a C++ hook in AC4-Port — the
port's hook framework and `hle::call_guest` make that tractable, but it is engineering, not editing.

One encouraging data point: ACFA and AC4 share the runtime (ACFA README: *"It shares this runtime
with the Armored Core 4 port. The two games share an engine family and middleware."*), and
ACFA-Port already rewrites game Lua at load time via a `lua_load` hook
(`acfa-port/src/runtime/game/script_fixes.cpp:89`) — so a script-level transplant has a proven
delivery mechanism.

### 4.3 Transport layer — not reusable, and it is the dominant cost

This is the part people underestimate. ACFA's Partnership is **client-server world sync running on
the game's own netcode over PSN**. That netcode lives in the PPC binary, not in Lua, and neither port
implements it (both stub `sceNp`/`sys_net` offline). So there is no working netcode to lift.

For AC4 online/LAN co-op you must build, from scratch:

1. **Transport** — WinSock in the mod's host code. (Do not bother with the `sys_net` HLE; a custom
   transport does not need the guest to see sockets.)
2. **Lobby/join handshake** — the easy part.
3. **World-state sync between two processes** — the hard part: unit transforms, animation state,
   weapon fire, projectiles, damage, AI decisions, spawn/despawn, and mission events. None of these
   structures are documented, and AC4-Port contains no game-logic code at all, so this is pure
   reverse engineering (`datawatch`, `--verify-native`, `guest_backtrace` are the tools).
4. **No lockstep available** — PPU guest threads are host threads with real mutexes/CVs
   (`ac4-port/src/runtime/ppu_thread.h:1,31`), so there is no cross-machine determinism. Snapshot
   sync plus per-AC authority is the only option.

Note the tempting shortcut — "just run the world on the host and send it" — is what ACFA's netcode
does, and we can't read it.

---

## 5. Honest verdict

**The reusable part is the design, not the code.** ACFA proves:

- the engine family can carry a second human in an entity slot that already exists (1001 = FRIEND);
- co-op is handled by **groups** + **"all players dead"** semantics, not by per-player special cases;
- balancing is done by **authoring extra per-player enemy groups**, gated on `IsExistFriend()`, and
  by **averaging AP** for results — not by a difficulty multiplier.

Those three ideas transfer to AC4 cleanly and should shape your mod's design.

**What does not transfer:** ACFA's netcode (in the binary, unimplemented by either port) and its
co-op UI plumbing (`Scr_*` scene/dialog layer, heavily PSN-coupled).

**And a scoping warning:** ACFA's co-op is *Free Play mission select*, not campaign progression.
Reproducing ACFA's approach faithfully gets you **per-mission 2-player co-op**, not "story start to
finish with shared progression". Campaign co-op additionally needs two-player save/progression,
hub/garage/briefing/mission-report handling, and mission-complete advancing both players — none of
which ACFA demonstrates.

---

## 6. Recommended next actions, in order

1. **Settle the engine question (decisive, cheap).** Build `ac4-recomp` with the mingw toolchain
   present here; analyse AC4's `eboot.elf`. Search for ACFA's group primitives — if AC4's event
   system has `SetEventPropertyGroup` / `WM_ENTITYFACTORY_NOTIFY` / a player-group concept, the
   whole script layer is a port job. If not, budget ~6 engine hooks.
2. **Check whether AC4's engine accepts entity 1001 as a controllable player.** ACFA aliases it to
   `FRIEND`; AC4's ally ACs use per-mission IDs (`m000_ET_JOSHUA`), so AC4 may have no "player 1"
   notion at all. This is the single highest-risk unknown.
3. **Prototype local 2P first, before any networking** — two pads, two ACs, one mission, one
   machine, using `--test-overlay` to ship the edited Lua. This validates the engine and the
   script layer with no netcode at all.
4. **Only then** build transport + world sync.
5. **Balance using ACFA's method** — per-player enemy groups gated on P2 presence + averaged AP for
   results. Do not look for a scaling multiplier; ACFA has none.
