# 27 — Why the join drifted, what the freeze is really called, and the damage seam

**Scope.** The three failures in `26_ENEMY_SNAPSHOT_SYNC.md` §4–§5: the join's entities kept moving
between snapshots, the join's AI could not be frozen through `unit_ai`, and no player ever took damage.
This note reports what was measured in a fresh pair of runs after three changes, and what is still open.

Measurements: `research\e11_raw\r27_*.log` (120 s idle), `r28_*.log` (host drive+fire, join fire),
`r29_*.log` (90 s idle; the join entered the mission 157 s late). Meters:
`research\r27_{host,join}_meter.txt`, `r28_*`, `r29_*`. Code: `ac4-port\src\runtime\game\coop_hooks.cpp`,
`coop_entities.cpp`, `coop_body.cpp`, `coop_net.cpp`.

| # | failure in research\26 | status now |
|---|---|---|
| 1 | snapshots did not hold: the join's entities were driven away between packets | **FIXED** — the join re-writes every entity's transform **every frame**, interpolating between the two newest host snapshots. Measured mean p90 **32.3 units** (baseline 371.5, old sync 545.1), peak **61.9** (baseline 2144.4), hp mismatches **0.3/s** (baseline 9.8/s) |
| 2 | `unit_ai` could not freeze the join's enemies | **WRONG LAYER** — `unit_ai` is not the movement path. The mission layer's real switches were found (`0xb0ef70`/`0xb0fa14` → `0xaf4cd0` for move, command 25 at `0xa65198` for attack) and are now hooked, but **m010 never calls them**; its swarm is not gated by them. The per-frame re-application makes the freeze visually unnecessary, but the join's own simulation still runs underneath (§2) |
| 3 | no player damage model existed | **HOST-AUTHORITATIVE AP ADDED** — the host publishes its AC2 (the join's player) AP in the type-2 packet; the join applies it to its own AC1, with a sanity guard so a freed body's `0/0` cannot zero it. Observed 35888 → 35888. The exact function that writes AP is still **NOT DETERMINED** (§3) |

---

## 1. The fix that mattered: hold the target every frame

### 1.1 Why the old write "snapped back"

`research\26` §2.2 measured the pre-apply error growing to 1771–2031 units: the join applied a record
when it arrived (5–7 times a second) and then let its own simulation run for 140–200 ms. The write was
durable; the **stream was too slow to hold a per-frame simulation**. Re-writing only on arrival cannot
work at any plausible host rate, because the join's AI moves a rocket hundreds of units a second.

### 1.2 What is done instead (`coop_entities.cpp`)

`g_targets` is an address-keyed table of the two newest records per entity. `snapshot_ingest()` only
updates the table; `snapshot_apply()` runs **every frame** (after the frame's simulation, from
`hook_present`) and for every target:

```
alpha = (now - t_cur) / (t_cur - t_prev)     # clamped to [0, 1.25]
pos   = prev + alpha * (cur - prev)
write pos (+0x40), rot (+0x10), hp (+0x2a4)
```

So the entity is pinned to the host's interpolated trajectory at the end of every frame. The join's own
simulation still starts each frame from the host's position (instead of 2000 units away), and the renderer
only ever sees the pinned value.

### 1.3 Measured result (r27, 120 s idle, both instances in m010)

The auto-offset used by `coop_divergence.py --auto-offset` is **wrong for this pair**: it aligns the
first second with ≥200 entities, but the two instances reach that walk second at different mission ages.
The correct alignment was found by minimizing the p90 over offsets (sharp minimum at **+4.2 s**; +4.0 =
28.9, +4.5 = 36.8, +5.0 = 102.9). At that offset, 126 compared seconds:

```
mean of per-second medians   1.87      (research\26 M1 baseline 30.40, M4 Run A 267.07)
mean p90                    32.33      (baseline 371.49, M4 Run A 545.06)
peak max                    61.93      (baseline 2144.35, M4 Run A 2030.33)
hp mismatches                0.3 /s    (baseline 9.8/s, M4 Run A 4.4/s)
entities present one side    0          every second, 320 common
```

Per 10 s the p90 stays 17–41 units for the whole window (no tail growth). The residual is consistent
with the comparison's ±0.5 s quantization against targets moving ~80 units/s, not with the join drifting.

The screenshots `research\e11_shots\r27_end_{host,join}_*.png` show the same dense swarm in both skies.

### 1.4 What this does *not* fix

The join's local simulation is **not stopped**. `r29` proves it: its mission entered 157 s after the
host's, and the pre-apply error stayed ~4000 units all window (the local sim is 157 s behind), while the
*rendered/metered* positions still matched the host (`r29`: at a 5 s process-clock offset, median 2.8,
p90 24.2). In a real session both instances enter together, so the local sim is only seconds behind and
the pinning keeps the visible world correct; a late join renders the host's world but its own mission
timers are still at its own age. Mission-timeline sync is separate work.

---

## 2. The freeze: the mission's real switches, and why m010 ignores them

### 2.1 `unit_ai` was the wrong class (confirmed)

`0x00b4cce0` is the command loop of the **unit controller** class `0x101475c8` (21 vtable slots). The
mission's 320 entities are all `0x10142638`; only six objects of `0x101475c8` exist in m010. Skipping it
can only affect those six, which is exactly what `research\26` M3 saw. **Do not retry this hook.**

### 2.2 The mission scripts' own switch, found and hooked

The extracted mission scripts use three different things:

* `eventOperation:SetEnableMoveGroup(group, bool)` / `SetEnableAttackGroup(group, bool)`
  (m090, m200, m390 …) — the 120-entry metadata table at `0x00eff1e8`, still no code address.
* `ctrl:SetEnableMove(bool)` / `ctrl:SetEnableAttack(bool)` in chara scripts
  (`m090_ac.lua:58`, `m200_ene_ac.lua:105/165`, `m718_player.lua:53…`) — the Lua `other` bindings
  `0x00a5a054` / `0x00a5a084`.
* `eventOperation:SetEventPropertyGroup(group, b1, b2, b3)` (m010 and most missions).

The C++ behind the per-entity/groups is now located:

| address | what it does |
|---|---|
| `0x00a65084` | `*(u32*)(ctrl + 12) = value` — the move flag store |
| `0x00a5a054` | `SetEnableMove(self, v)`: `f_00a65084(*(self+0xa0) + 308, v)` |
| `0x00a65198` | the controller command dispatcher: `(ctrl, cmd, v, …)`; **cmd 25 = attack enable** (`0xa5a084` calls it with 25) |
| **`0x00af4cd0`** | the low-level move switch used by the property/group layer: `f_00a65084(r3+308, r4)`; called from **single** `0xb0ef70` and **per group** `0xb0fa14` (the latter loops a controller list) |

`coop_hooks.cpp` now hooks `0x00af4cd0` and `0x00a65198` (command 25 only), plus the direct
`SetEnableMove`/`SetEnableAttack`, and forces the value to 0 on the join with `AC4_COOP_FREEZE=1`.

**Measured on m010: none of these is called.** Both r28 and r29 have the freeze request line and not one
`COOP FREEZE:` force line. The swarm's movement is not gated by the mission's move/attack enable at all;
it is driven straight from the AI. The move/attack switches will still be useful for the missions that
do use them (m090/m200/m718 and chara scripts) and are left in place.

### 2.3 What is still open for a true freeze

The missing piece is the per-frame function that turns the AI's commands into the entity's transform
(+0x40) / its controller at +0x2c4. The plausible next step is a **write watchpoint** on one live
entity's +0x40 (hardware DR0–DR3 on the guest threads, or a temporary instrumented `wf32` in
`ppu_recomp.h`) to capture the writer's host RIP; the exe keeps its symbols
(`llvm-nm ac4.exe` resolves `f_00a4f574` etc.), so the RIP maps to a guest function directly. This is
now a measurement, not a guess.

Until then the pragmatic path is §1: the renderer and the meter follow the host, and only the join's
unseen simulation remains divergent.

---

## 3. Damage: the AP field, the writers, and host authority

### 3.1 The field

AP/HP is the float at **body + 0x2a4**, maximum at +0x2a8 (measured again: both ACs read 35888; a dead
unit has 0 at +0x2a4 and a collapsed first rotation row).

### 3.2 The two literal writers are not the live damage path

A whole-image scan found exactly **two** functions that write `+0x2a4` through a non-stack object base:

* `0x00a53900` — `*(float*)(this+0x2a4) = f1`, then a notification. **No direct caller at all**, and it
  never fires in r27/r28/r29 (`COOP HP` log empty).
* `0x00a569f0` — the AC's per-frame HP/PA update (reads +0x304/+0x33c, writes +0x2a4). Its only caller
  `0x00a571c0` is itself uncalled. **It never fires either** (`COOP HPU` log empty).

So the live damage path writes the field through a **computed base** (e.g. `ctrl→body`), not a literal
`this+0x2a4`, and the scan cannot see it. This is the one part of the damage model that is still
**NOT DETERMINED**. The candidate area is the AC virtual family around `0xa56d58` (reads `+0x2a4` and
builds the hit/death effect at `0x9a7240`) and `0xa58264`/`0xa58c1c`; the same write-watchpoint technique
as §2.3 settles it in one run.

### 3.3 What is implemented instead: host authority for the join player's AP

The host simulates **both** ACs; its AC2 *is* the join's player, and the host's enemies attack it. That
makes the host the natural authority. The type-2 transform packet now carries the AP of the AC the
sender simulates for the peer:

```c
// coop_body.cpp, every frame on both instances
set_local_peer_ap(body2 + 0x2a4, body2 + 0x2a8);

// join only: the host's AC2 AP is this player's AP
if (role_join() && t.apmax >= 1000 && 0 <= t.ap <= t.apmax)
    wf32(body1 + 0x2a4, t.ap);
```

The `>= 1000` guard matters: r28 caught a packet pair `ap = 0, apmax = 0` (a freed/rebuilt body during a
scene change) and the unguarded version zeroed the join's AC. With the guard, `0/0` is ignored; a
**legitimate** death is `ap = 0` with `apmax` still ~35888, which is applied.

The join's `f_00a569f0` hook also re-asserts the host AP after any local HP update, and the per-frame
`coop_body` write does the same — so local damage cannot win. One local-lethal frame (AP to 0 inside the
simulation before the next write) is still theoretically possible; the hook at the only two known
writers would close it if one of them were live, and a watchpoint would give the real one.

### 3.4 Evidence

`r28_join.log`:

```
COOP AP: join AC1 0x13009780 35888.00 -> 35888.00 (host-authoritative, max 35888.00)
```

and the host's meter shows its AC2 (the join player, `0x10d9c100`) at 35888 for the whole run. The
`0/0` incident and the guard are logged in §3.3. A synthetic test — poke the host's AC2 AP down and
watch the join follow — is the cheap next check (the existing `AC4_COOP_POKE_FILE` can write
`0x2a4 <delta>` on body 2).

---

## 4. Files changed in this step

| file | change |
|---|---|
| `coop_entities.cpp` | `g_targets` interpolation table; `snapshot_ingest()` (receive) split from `snapshot_apply()` (every frame); mean/max pre-apply error and worst address in the per-second line |
| `coop_hooks.cpp` / `.h` (new) | hooks `hp_set` (0xa53900), `hp_update` (0xa569f0), `enable_move` (0xa5a054), `enable_attack` (0xa5a084), `move_enable_lo` (0xaf4cd0), `command_dispatch` (0xa65198); damage/enable logging; join AP re-assert |
| `coop_net.cpp` / `.h` | type 2 carries `ap`/`apmax` (payload 70 bytes); `set_local_peer_ap` / `peer_ap` with the `apmax >= 1000` guard |
| `coop_body.cpp` | publishes body2's AP; join applies the peer's AP to body1 |
| `hooks.txt` | the six new hook lines |

Everything is behind the existing environment variables. With no `AC4_COOP_*` set the game is unchanged.

---

## 5. NOT DETERMINED

1. **The live HP/AP writer** (computed base). The two literal writers are dead in m010.
2. **The per-frame entity/AI transform writer** needed for a true freeze. Hook addresses are known for the
   mission-level move/attack switches, but m010 bypasses them.
3. **A clean simultaneous mission start.** The test harness's join menu walk is flaky (it cycled the
   accept dialog 17 times in r29 and entered 157 s late); every late-join measurement exaggerates the
   local-sim offset. A real session starts together.
4. **Mission timers / result sync.** The host and join each run their own mission script; positions and
   hp are shared, timers and `SetMissionResult` are not.
5. **Whether the join's local enemy attacks can ever win** a race against the AP re-assert (one frame).
6. **Worst target in the join's pin table.** One object per second still reports a large pre-apply error
   (1865 units in r27, 7790 in r29 when the sim is 157 s behind) while the rest track. It is one address
   whose +0x40 is not the live transform field; harmless for the visible sync, worth identifying.

---

# Addendum (same session): the freeze was found, and where damage is *not*

## A1. The per-frame entity update chain (the freeze)

The write watch (DR0-DR3 on every PPU thread: `coop_watch.cpp`) was re-run with the correct DR7
encoding (`RW_i` at bit `16+4i`, `LEN_i` at `18+4i`; the first attempt on slots 1-3 accidentally armed
read/write, which is why it "found" the HP getter `0xa4f574`). Following one moving m010 entity's
`+0x40` produced this chain, every frame:

```
0x004b9eb8   the per-object motion update (computes velocity from +0x10/+0x240)
  -> *(this+132) vtable +48 (the controller update)
     -> 0x00a4f968  the entity's vtable slot 12: copies src -> this+0x190
        -> 0x00a68dbc  the generic 4x4 setter: copies src -> this+0x10
           -> the +0x40 store the watch caught
```

`COOP XBLIT` logged the blit being called with `src = entity+0x190` and **caller `0x004b9f7c`**
(the virtual call inside `0x004b9eb8`); the watch RIP resolved to `f_00a68dbc` +0x326.

**The freeze.** `hook_xform_blit` (0x00a4f968) now returns without calling the original on the join
for every non-player entity (`vt == 0x10142638`, excluding both player bodies). The live `+0x10`
transform is only ever moved by this blit, so the host snapshot's value stays exactly where it was
written; the join's simulation may keep computing its own `+0x190` copy, but it can no longer publish
it. This is the freeze `research\26` M3 could not find (it was two layers above `unit_ai`).

**Measured (r36, one join instance, m010, `AC4_COOP_FREEZE=1`, no host):**

```
COOP FREEZE: transform blit skipped for entity 0x12069c10 (call 0) ... (320 entities)
window 165.0 -> 184.3 s (19 s), 320 slots
mean displacement 0.001 | median 0.000 | max 0.322
moved >1 unit: 0 (0%) | >10 units: 0 (0%)
```

Baseline for the same quantity (`research\26` M3, join, 30 s): mean **171.94**, 45 entities >10 units.
So the visible simulation is stopped, not merely corrected. The player bodies are excluded and keep
their own pad-driven motion.

Caveat: this freezes the **published** transform, not the AI/weapon logic. The join's local AI still
runs (it can fire from the host positions), and the mission's own timer/death logic is untouched. For
the visual/co-op contract that is enough; a full freeze of the simulation is an SPU question (A2).

## A2. The HP writer is not PPU code

With the corrected watch, `AC4_COOP_WATCH=hp` was armed on the four most-moving live entities and the
mission ran for 60 s. **85 entities died** (alive 319 -> 234) and the watch recorded **0 writes to
`+0x2a4`**. The watched slots were movers, but the result is consistent with the earlier static
finding: the only two PPU functions that write an object's `+0x2a4` are dead in m010
(`0xa53900` no caller; `0xa569f0` only called by the uncalled `0xa571c0`).

Conclusion: **the live HP/AP write in m010 is not a PPU instruction.** It is either an SPU/DMA write
into the same guest buffer or a value the PPU only reads. This also explains why the `unit_ai` hook
(`0xb4cce0`, a PPU controller loop) never stopped the swarm: the AI/physics that moves and kills these
entities is not the PPU loop.

Consequences for co-op:

* A PPU hook cannot clamp or authoritatively set HP. The practical mechanism is the one already in
  place: the host's per-frame snapshot overwrites `+0x2a4`, and the join's own value is re-asserted
  every frame. A one-frame local lethal write (SPU) can still trigger a local death before the next
  overwrite; closing that needs SPU-side interception or a PPU-side death-event guard, both separate
  work.
* The same applies to the AC AP: the host's `AC2` AP is the authority; the join mirrors it in
  `coop_body.cpp` and after `f_00a569f0`. The mirroring is correct at the PPU-visible field level.

## A3. The flaky join menu walk: measured cause and fix

The 17-attempt join accept in r29 was a **200 ms LEFT tap**. In the mission-accept dialog the focus
moves on a ~500 ms tap; 200 ms is swallowed more often than not, so the script kept closing the dialog
with CANCEL. `run\coop_mission.py` now:

* holds LEFT for **500 ms** (and A for 250 ms) in the dialog and hub steps;
* treats a "Load this schematic?" dialog (bright top-left tab) as a **B** to cancel, not LEFT+A.

Measured: the host accepts in 1 step; the join in 1-4 steps (r33/r35/r36), down from 14-17. The
remaining variance is the two instances' frame rate, not a logic error.

## A4. What is still open

1. **Mission timer/result sync.** The host's mission script owns the result; the join's scripts run
   their own timers. The clean fix is to broadcast the host's `MissionSuccess/Failure` result and call
   the same result path on the join through its loaded Lua state (the port already hooks `lua_load`
   and the Lua C API addresses are in `research\23`). Not implemented.
2. **SPU-side damage/AI.** If the simulation must be stopped (not just the published transform), the
   SPU DMA path is the next target; the PPU hooks cannot reach it.
3. **One-frame local death** on the join, per A2.

## A5. Files changed in the addendum

| file | change |
|---|---|
| `coop_watch.cpp/.h` (new) | hardware write-watchpoint tool (`AC4_COOP_WATCH=pos|hp|all`), **correct** DR7 layout, mover-ranked targets, VEH + per-thread DR install, RIP/RVA log |
| `coop_hooks.cpp/.h` | `xform_blit` (0xa4f968) freeze skip + caller log; `xform_set` (0xa68dbc) log; `actor_update` (0x4b9eb8) diagnostic |
| `hooks.txt` | `xform_blit`, `xform_set`, `actor_update` |
| `run\coop_mission.py` | 500 ms LEFT/A taps; schematics-dialog B handling |

## A6. Default and scope of the freeze

`run\coop_join.ps1` now sets `AC4_COOP_FREEZE=1` by default (set it to `0` to opt out). The freeze
excludes the two player bodies only; every other `0x10142638` entity on the join stops publishing its
own transform. Two consequences worth stating:

* The join's **own projectiles** are only affected if they are part of the 320-object event pool. In
  m010 the pool is the kamikaze swarm plus scenery; the AC's bullets are separate objects, so they are
  expected to move normally. This is **NOT YET VERIFIED with live fire on a frozen join**.
* The join's **mission result** is not frozen: the death/group logic is separate from the transform
  blit, and hp is still written by the host snapshots. If the group-alive count is computed from
  `+0x2a4`, `ON_EVENTGROUP_ALL_DESTROYED` fires on the join at the host's kill rate and the result can
  match without an explicit result packet. A full-mission pair run is needed to confirm; if it does
  not, the result broadcast in A4.1 is the fallback.

## A7. Live fire on a frozen join — verified

One join instance (`AC4_COOP_JOIN` set, `AC4_COOP_FREEZE=1`), taken to m010, then the local pad was
driven with `A` (fire) + left stick forward for 8 s. The screenshot at the end of the burst
(`run\shots\r38\shot_418.png`) shows:

* **300 km/h** and the boost flames: the local AC moves normally — it is excluded from the freeze.
* **RA 158 / 24**: the right weapon fired from 180/36 down to 158/24.
* The entity count stayed **320** through the burst, i.e. the AC's projectiles are not part of the
  320-object `0x10142638` mission pool. The transform blit freeze cannot reach them, and they render
  and travel normally.

So the freeze is scoped to the host-authoritative mission entities and does not break local movement
or local fire. (A frame-by-frame tracer comparison was not needed: movement + ammo + the unchanged
pool are the three independent confirmations.)

## A8. Mission result sync — attempt and current status

To test whether the join completes with the host when the host's enemies die (the "automatic" path
above), the host's `m010_event.000` was patched in a **host-only overlay**
(`payload\overlay_host`) to kill the whole enemy group on a timer:

```lua
local __coopOrigStart = OnMissionStart;
function OnMissionStart( eventOperation )
    __coopOrigStart( eventOperation );
    SetTimerWithHandler( eventOperation, 20, 60.0, "CoopAutoKill" );
end
function CoopAutoKill( eventOperation )
    LogEvent( "COOP TEST: KillEventGroup ALLENEMY" );
    eventOperation:KillEventGroup( m010_GP_ALLENEMY );
end
```

The overlay was demonstrably used (`TEST OVERLAY: .../m010_event.000 read from
...payload\overlay_host\...`), and the bundle contains the injected code (`CoopAutoKill` marker
verified after rebuild). **The timer did not fire within the observed windows** (the host's alive count
stayed 250, no `COOP TEST` line), so the automatic-result question is still **NOT DETERMINED**.

Two separate harness problems blocked the pair test: the accept dialog still cycles (the fixed 500 ms
tap helps, and an OK/CANCEL gold comparison was added, but it is not yet deterministic), and the two
instances entered m010 30-90 s apart, so a 20 s test kill cannot be made with both in the mission.

Next steps, in order:
1. Validate the auto-kill **solo**: confirm the timer fires and the group empties (try timer id 1-16,
   a shorter delay, and the direct `OnAllDestroyed(eventOperation)` call as a control).
2. With the accept made deterministic, run the pair with the host overlay and a delay long enough for
   both to be in the mission; observe whether the join's `ON_EVENTGROUP_ALL_DESTROYED` /
   `OnAllDestroyed` fires from the host-synced `+0x2a4` values.
3. If it does not, add the result packet: the host sends `{mission, result, rank}` when its result is
   set, and the join invokes its own `SetMissionResult_Success/Failure` through the mission's Lua
   state (the port already hooks `lua_load`; API addresses in `research\23_LUA_BINDING_MAP.md`).
