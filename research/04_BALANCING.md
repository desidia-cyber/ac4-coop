# 04 — Balancing two players

Your concern is correct: AC4's 48 missions are authored for one AC. This document records exactly how
the successor game solves the same problem, and the concrete plan for AC4.

---

## 1. What ACFA actually does (measured, not assumed)

I searched all 1,649 recovered ACFA scripts for any player-count scaling:
`PlayerNum`, `PlayerCount`, `GetPlayerNumber`, `NumberOfPlayer`, `NumOfPlayer`, `player_num`,
`NumPlayer` — **zero hits.**

**ACFA does not scale difficulty at runtime.** It has three other mechanisms.

### 1.1 Per-player enemy groups, gated on a second player existing

`ingamescript__230.lua` (mission m070) is the clearest case:

```lua
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
ObserveRegionIn( eventOperation ,m070_PT_DM_1P_START, ENTITY_ID__PLAYER_0, "OnDummy1AttackOn");
if( IsExistFriend( eventOperation)) then
    ObserveRegionIn( eventOperation ,m070_PT_DM_2P_START, ENTITY_ID__PLAYER_1, "OnDummy2AttackOn");
end
```

The pattern: enemy groups are authored in pairs (`_1P_` / `_2P_`), the `_2P_` groups are attached
only when P2 exists, and **separate trigger regions** exist per player so the extra enemies engage
P2 rather than all swarming P1.

Also note `m070_GP_VOB_EXPLOSION_1P` / `_2P` — even scripted hazards are duplicated per player.

### 1.2 Results are ranked on the average AP of both players

`ingamescript__225.lua`:

```lua
local apRate = eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER_0);
if( IsExistFriend( eventOperation)) then
    apRate = ( eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER_1) + apRate) / 2;
end
```

Mission-result messages/ranks then key off `apRate`. Without this, two players trivially earn the best
result because one of them can stay untouched.

### 1.3 Occasional per-mission content switch

ACFA flips a few mission behaviours behind `IsCooperationMission()`. Confirmed cases:

- `m240`: in co-op, **removes** a dummy enemy group (`KillEventGroup(m240_GP_ENE_DUMMY)`), because the
  extra group existed to pressure a lone player.
- `m280`: swaps an arrival trigger to a shared-global watch instead of the player-group region check.

Both are one-line, mission-specific judgment calls — there is no general rule.

**Summary: balancing in ACFA is authored content, not a multiplier.**

---

## 2. What AC4 gives us to do the same

Verified present in AC4's executable and already used by AC4's scripts:

| primitive | use for balancing |
|---|---|
| `SetEnableAttackGroup` | turn an extra enemy group's attacks on/off per phase |
| `SetEnableMoveGroup` | keep extra enemies parked until P2 engages |
| `SetEnableImmortalGroup` | protect a scripted unit of one player's group |
| `SetEnableGravityGroup`, `SetEnableLockGroup`, `SetEnableEntityCollideGroup`, `SetEnableMapCollideGroup`, `SetEnableTreeGroup` | per-group behaviour tuning |
| `KillEventGroup`, `KillGroupInRegion` | remove extra enemies when P2 is absent |
| `GetEventGroupAlives`, `GetEventGroupNum`, `GetEntityGroupNum` | count survivors for phase logic |
| `ObserveRegionEventGroupInsideSomeone` | "any player entered this region" |
| `BroadcastMessageToEventGroup`, `SetEventHandleGroup`, `DeleteHandlGroup` | script plumbing |
| `SetRadarShapeIDGroup`, `ShowRadarAllowGroup` | markers for both players |
| `IsEntityExist(ENTITY_ID__PLAYER_1)` | the "is P2 here" test (same as ACFA's `IsExistFriend`) |

**Important constraint:** ACFA's `AttachEntityEventGroup` is **absent in AC4**. AC4 associates
entities with groups at spawn time from mission data, not from Lua. This changes the implementation
route:

- You cannot cheaply attach a *newly spawned* enemy to a group at runtime the way ACFA does.
- The practical AC4 approach is to **place the extra enemies in the mission data** (the per-mission
  `cap` placement records) as part of a `_2P_` group, then gate them at runtime with
  `SetEnableAttackGroup` / `SetEnableMoveGroup` and `KillEventGroup` when P2 is absent.

That is more work per mission than ACFA's one-liner, but it is the same design.

---

## 3. Concrete AC4 balancing plan

### 3.1 Baseline rule for every mission

1. On mission start, test `IsEntityExist(ENTITY_ID__PLAYER_1)`.
2. If P2 is absent, `KillEventGroup` (or never enable) the `_2P_` groups — so single-player behaviour
   is bit-identical to retail.
3. If P2 is present, enable the `_2P_` groups on their per-player trigger regions.

This preserves single-player balance exactly, which matters because the same scripts ship to both modes.

### 3.2 Per-mission-type recipe

| mission type | 2-player fix |
|---|---|
| **Duel vs a single enemy AC** (`_ac` / `_ene_ac` scripts) | Adding a second player halves the time-to-kill and splits aggro. Compensate by adding a `_2P_` escort group (2–4 Normals/MTs) that activates when P2 engages, and/or an extra damage phase. Do **not** just raise the duel AC's HP — that only lengthens the fight and does not restore threat. |
| **Fortress / objective assault** (`_fortress` scripts: `m020`, `m040`, `m110`, `m140`, `m190`, `m240`, `m260`, `m380`) | These have destructible objectives and waves. Add a second wave group on the reverse flank, triggered by P2's approach region. Cheapest high-value change. |
| **Escort / defend** (`m180` submarine+tanker, `m290` carrier/cruiser/head_ship) | Two players make these much easier. Increase attacker pressure (extra `_2P_` attackers) rather than reducing the timer. |
| **Timed / survival** | Keep the timer; add pressure via `_2P_` groups. Do not shorten the timer — it punishes the slower player unfairly. |
| **Boss / finale** (`m350` boss, `m490` boss+genius+ene) | Longest fights in the game; they will feel trivial with two. Add a second boss-side group and phase-gate on `GetEventGroupAlives`. |
| **Tutorial / simulator** (`m710`–`m719`) | Leave alone; these are instructional and should stay single-AC. |

### 3.3 Result ranking

Adopt ACFA's averaging verbatim. 13 AC4 missions use `GetApRateOfAC(ENTITY_ID__PLAYER)`:

`m000, m030, m050, m080, m090, m170, m190, m200, m210, m280, m310, m380, m490`

In each, replace the single lookup with:

```lua
local apRate = eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER_0 );
if ( IsEntityExist( ENTITY_ID__PLAYER_1 ) ) then
    apRate = ( eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER_1 ) + apRate ) / 2;
end
```

`m050` (11 calls) and `m200`/`m380` (6 each) need care — some of those calls are per-entity
thresholds, not the result rank.

### 3.4 Economy / progression (a design gap ACFA does not solve)

ACFA's co-op is Free Play, so it never had to answer this. For AC4 campaign co-op you must choose:

- **Host-authoritative (recommended):** host's credits/missions advance; P2 plays a borrowed AC and
  does not persist progress. Simple, no save reconciliation.
- **Split rewards:** half credits to each. Needs two saves and a reward rule.
- **Mirrored:** both saves advance identically. Most player-friendly, most engine-facing work.

Recommendation: start host-authoritative, and only revisit if the mod gains traction.

### 3.5 What *not* to do

- Do not add a global damage/HP multiplier. Nothing in ACFA does, and AC4's difficulty comes from
  encounter design (positioning, waves, aggro), not from enemy HP.
- Do not scale by player count at mission load without also handling the case where a player
  disconnects mid-mission.
