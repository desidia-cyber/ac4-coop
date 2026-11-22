# 02 — Entity & group model: AC4 vs ACFA, and the exact Lua delta

This is the core design document. It states what AC4 has, what ACFA added, and the minimum set of
additions needed for 2-player campaign co-op — plus the one question that cannot be answered from
data alone and how to answer it.

---

## 1. AC4's entity model (measured)

`common/event/include/msgid.lua` (shared by every mission):

```lua
ENTITY_ID__INVALID          = 0
ENTITY_ID__PLAYER           = 1000
ENTITY_ID__BRIEFINGCARRIER  = 10000
```

There is no `ENTITY_ID__FRIEND`, no `PLAYER_0/1`, no `GP_ID__*` anywhere in AC4's 874 scripts.

Entities a mission spawns get **mission-local small ids** from
`common/event/include/mXXX/entityid.lua`, e.g. mission `m000`:

```lua
m000_ET_NEXTAC  = 1;   -- enemy AC
m000_ET_VEHICLE = 2;
m000_ET_DUMMYOBJ= 3;
m000_ET_JOSHUA  = 4;   -- ally AC
```

So AC4 already spawns and script-controls **ally ACs as ordinary entities**. 27 of the 48 mission
scripts have one or more such character scripts.

Every `eventOperation:` call takes an explicit entity id, so per-entity logic is native to the
scripting layer. The player is simply the one id nobody parameterises.

## 2. ACFA's entity model (measured)

```lua
ENTITY_ID__INVALID  = 0
ENTITY_ID__PLAYER   = 1000
ENTITY_ID__PLAYER_0 = ENTITY_ID__PLAYER
ENTITY_ID__PLAYER_1 = ENTITY_ID__PLAYER + 1        -- 1001
ENTITY_ID__BRIEFINGCARRIER = 10000

ENTITY_ID__FRIEND   = 1001                          -- same slot
GP_ID__PLAYERS      = 100
```

**Player 2 and the AI ally share entity id 1001.** That is the whole trick: the ally slot already
has spawn, event wiring and HUD support, so making it human-controlled is additive.

## 3. The delta, symbol by symbol (verified against both discs and AC4's executable)

| symbol | AC4 | ACFA | port cost |
|---|---|---|---|
| `ENTITY_ID__PLAYER` | yes | yes | — |
| `ENTITY_ID__PLAYER_0/1` | **no** | yes | add 2 Lua constants |
| `ENTITY_ID__FRIEND` | **no** | yes | add 1 Lua constant |
| `GP_ID__PLAYERS` | **no** | yes | add 1 Lua constant (any free group id) |
| `WM_ENTITYFACTORY_NOTIFY` | **yes** (`msgid.lua`, 7 uses in `msgmacro.lua`) | yes | none |
| `ID_TO_SCRMSG__EVENTGROUP_ALLDELETE` | **yes** (`msgid.lua` + `msgmacro.lua`) | yes | none |
| `ON_MISSION_REGION_NOTIFY` | **yes** (`msgmacro.lua`, 3 uses) | yes | none |
| `RegistHandle` | **yes** (`msgmacro.lua`, 71 uses) | yes | none |
| `IsEntityExist` | **yes** (engine + used in 8 scripts) | yes | none |
| `SetTutorialInputMode` | **yes** (`msgmacro.lua`, 6 uses) | yes | none |
| `TUTORIAL_MODE__USERINPUT/AUTO/BLEND` | **yes** (`msgid.lua`) | yes | none |
| `IsCooperationMission` | no | engine method | **stub in Lua** (`return true`) |
| `IsExistFriend` | no | **Lua function** | port verbatim |
| `OnPlayerAllDestroyed` | no | **Lua function** | port verbatim |
| `ObservePlayerMissionRegionOutOrTimeout` | no | Lua wrapper over an engine call AC4 lacks | reimplement |
| `REGION_POSITION_STATE__OUT_MISSION_PLAYRE = 3` | **no** | yes | needs engine support for state 3 — **verify** |

**Headline: three of the four co-op behaviours that matter are Lua and depend only on primitives AC4
already has.** Only the group region-out state needs thought.

### 3.1 ACFA's helpers, verbatim (for porting)

`OnPlayerAllDestroyed` (`ingamescript__004.lua:1594`) — every symbol it uses exists in AC4:

```lua
function OnPlayerAllDestroyed( eventOperation , funcname )
    eventOperation:RegistHandle( WM_ENTITYFACTORY_NOTIFY, GP_ID__PC_PLAYERS,
                                 ID_TO_SCRMSG__EVENTGROUP_ALLDELETE,
                                 ID_TO_SCRMSG__EVENTGROUP_ALLDELETE, funcname, 1);
end
```

(`GP_ID__PC_PLAYERS` is another Lua constant; pick a free group id and make the player entities
members of it.)

`IsExistFriend` (`ingamescript__004.lua`, body uses only `IsEntityExist`):

```lua
function IsExistFriend( eo )
    return eo:IsEntityExist( ENTITY_ID__PLAYER_1 );
end
```

`ObservePlayerMissionRegionOutOrTimeout` (`ingamescript__004.lua:1581`):

```lua
function ObservePlayerMissionRegionOutOrTimeout( eventOperation , funcCallback)
    eventOperation:ObservePlayerMissionRegionOutOrTimeout();   -- engine call AC4 lacks
    ON_MISSION_REGION_NOTIFY( eventOperation, REGION_POSITION_STATE__OUT_MISSION_PLAYRE,
                              GP_ID__PLAYERS, funcCallback);
end
```

The `ON_MISSION_REGION_NOTIFY` half is available in AC4. The engine half is not. Replacement that
keeps the same semantics without engine support: register `ObserveRegionOut` for **each** player and
fire the callback only when both are out (a two-bit counter); for the warning-timeout variant, reset
the timer whenever any player is back inside.

`Mission_EnableAutoPilot` / `Mission_EnableBlendAutoPilot` (`msgmacro.lua:1631` / `:1617`) — **already
present in AC4**, both just call `SetTutorialInputMode`.

## 4. The one open question, and its test

**Does AC4's engine accept a second AC, driven by a second pad, as a player?**

What is known:

- AC4 has only one player entity id in its scripts, and the port's input hooks identify "the player"
  by call site (`input_hooks.cpp:40-43`), implying a single operator.
- But the engine *does* have `SetTutorialInputMode` (a per-character "user input" switch) and AC4's
  own scripts already switch **ally ACs** into `TUTORIAL_MODE__USERINPUT` (`Mission_EnableAutoPilot(…,
  false)` in `m000_joshua.lua:126`, `m050_joshua.lua:105`, `m090_ac.lua:60`, `m170_joshua.lua:108`,
  `m200_ac.lua:86`, `m380_ac.lua:86`).
- ACFA shows the engine family supports a human at 1001.

What is unknown: whether "USERINPUT" on a non-1000 entity resolves to a pad, and whether that pad can
be port 1.

### Test (pure Lua, minutes, no reverse engineering)

Use the port's `--test-overlay` (see `06_DELIVERY_AND_EXPERIMENTS.md`) to shadow one file, and run
mission `m000`:

1. In `common/event/include/msgid.lua`, add the constants from §3.
2. In `common/event/mission/chara/m000_joshua.lua`, at mission start:
   - call `Mission_EnableAutoPilot(eventOperation, false)` and **remove** the arrival handler that
     calls `OnSetPropertyArrive` (which currently hands Joshua back to autopilot),
   - remove `Mission_EnableAutoPilot(eventOperation, true)` from `OnMissionStart`.
3. Launch `m000` with two controllers connected.

Read the result:

| observation | meaning |
|---|---|
| Joshua moves with pad 1 (or pad 0) | **engine supports a second human-controlled AC** — proceed |
| Joshua ignores pads and does nothing | engine has no player binding for non-1000 entities — needs runtime hook |
| Joshua follows an AI path | `USERINPUT` was overridden by the behaviour list — check `SetEventPropertyMySelf` |
| Both players move together | single pad binding; pad routing must be added |

Record which pad moved him — that alone tells you the port-binding question.

## 5. Proposed AC4 co-op entity design

### 5.1 Player identity

Add to the shared constant file (overlay):

```lua
ENTITY_ID__PLAYER_0 = ENTITY_ID__PLAYER          -- 1000
ENTITY_ID__PLAYER_1 = ENTITY_ID__PLAYER + 1      -- 1001
ENTITY_ID__FRIEND   = 1001
GP_ID__PLAYERS      = 100
GP_ID__PC_PLAYERS   = 101                        -- ACFA uses a separate id; AC4 has none
```

Two options for P2's entity:

- **Reuse an existing ally slot** (27 missions have one, e.g. `m000_ET_JOSHUA = 4`). Cheapest and most
  likely to work, because the AC, its spawn point and its event wiring already exist. Downside: the
  ally may be scripted to appear/leave at fixed moments.
- **Spawn a new AC at 1001.** Matches ACFA's model and gives consistency across all 48 missions, but
  requires the mission to spawn an AC, which means an engine call or reuse of the ally spawn path.

Recommendation: prototype both on `m000` (which has an ally) and then decide. Consistency across 48
missions favours 1001 if the engine accepts it.

### 5.2 Input

Per-character switch already available:

```lua
eventOperation:SetTutorialInputMode( TUTORIAL_MODE__USERINPUT )   -- P2 human
eventOperation:SetTutorialInputMode( TUTORIAL_MODE__AUTO )         -- AI drives
```

Pad routing, if the engine binds port 0 only: a runtime hook on `pad_update` (`0x00951f94`) that
substitutes the port field (`+384`) per pad object — the object is fully parameterised by that field
(`01_ENGINE_ANALYSIS.md` §3). Port ≥ 1 already takes the clean `cellPadGetData` path with no
keyboard/mouse merge.

### 5.3 Death semantics

Today all 48 missions do `ON_DEAD_ENTITY(eventOperation, ENTITY_ID__PLAYER, "OnDeadPlayer")` →
immediate failure. Co-op needs "fail when the whole player group is gone":

- Replace that registration with `OnPlayerAllDestroyed(eventOperation, "OnDeadPlayer")`.
- Keep `MissionFailure_PlayerIsDead` as the fail action (already in AC4's `msgmacro.lua`).
- Decide a downed player's rule: respawn after N seconds, or spectator until mission end. ACFA has
  no explicit revive logic in its scripts, so this is a design choice (recommended: respawn at
  mission start point after ~10 s, which keeps a 2-player run from ending on one mistake).

### 5.4 Region / timeout

Replace `ObserveMissionRegionOut(eo, ENTITY_ID__PLAYER, fn)` with the per-player variant described in
§3.1: warn while any player is out of the warning region, fail only when **all** players are out (or
when the timeout expires with all out).

### 5.5 Camera and HUD

- Each player needs their own view. Options in order of cost:
  1. two game processes (each renders its own view) — natural for online/LAN;
  2. reuse the mission sub-window camera (`OpenSubwindowFixedPos`) for a small P2 viewport;
  3. true split-screen by rendering the frame twice with two cameras — largest renderer change.
  For the online/LAN goal in `05_ONLINE_LAN_ARCHITECTURE.md`, option 1 removes this problem entirely.
- HUD: AC4 draws one player HUD per frame from the player's data; the second player's HUD is the
  larger UI question. ACFA's Partnership had its own HUD logic in the binary (not in Lua), so there
  is no script-level reference to copy. Expect this to be the fiddliest UI work.

### 5.6 Progression

ACFA's co-op is Free Play mission select, so it has **no** shared-campaign reference. For AC4
campaign co-op the choices are:

- **Host-authoritative progression** (recommended): host's save advances; P2's garage is a copy of
  host's parts, or a fixed loadout. Simplest and avoids writing two-save reconciliation.
- **Mirrored progression**: both saves advance identically. Requires writing both save slots from one
  mission result — more engine-facing work, no precedent in either game.
