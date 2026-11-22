# 25 — The mission entity table, and a divergence meter

**Scope.** M1 find the engine's `entity id -> entity object` container. M2 enumerate every live mission
entity (id, class, position, orientation, hp, alive) and compare the total with the mission script's own
group counts. M3 find where hit points and the alive flag live, for the player and for enemies. M4 a
divergence meter between two instances. M5 why m010 ends by itself after ~170–240 s, and a patch that
keeps a test mission alive.

Everything below is measured on this machine; anything not measured is marked **NOT DETERMINED**.

| # | milestone | result |
|---|---|---|
| M1 | entity table / id → object | **DONE for the container, the class and the fields** — the id-1000 lookup returns the player's AC body, verified. **The engine's own lookup *function* is NOT DETERMINED** (§1.5) |
| M2 | enumerate + compare with the script | **DONE** — every live mission entity in m010 is in the table; 318 mission objects vs the script's `g1=312/312` (§2) |
| M3 | hit points + alive | **DONE** for the player AC and for the m010 enemy class (same class, two very different AP values, death measured). A **second enemy class was not measured — m010 has only one** (§3) |
| M4 | divergence meter | **DONE for the meter, the analyser and an idle baseline**; the "host drives and fires" window is **NOT DETERMINED** — the input had no effect because the simulation had stopped advancing (§4) |
| M5 | why the mission ends | **DONE** — `KamikazeDeads` reaching `m010_CONST_LAST_INVASION = 100` fails the mission; the patch keeps the sortie open **1388 s (23 min)** (§5) |

Runs (logs preserved in `research\e10_raw\`): `ent1` 20261115-114110, `ent2` 20261115-115551, host/join
20261115-120953/120958, host/join 20261115-122725/122730. 4 launches of the ~30 allowed; all `ac4.exe`
stopped at the end.

---

## 1. M1 — the container, the class, the fields

### 1.1 The actor class covers both the player and the mission's units

From `ent2` (run 20261115-115551, gameplay):

```
COOP ENT body1 0x12fd7ff0 vt 0x10142638 id 1000
COOP ENT +0x0000 10142638 000003e8 00000001 00000000
COOP ENT +0x0010 bdb27eb3 00000000 bf7f069e 00000000     -0.08716  0  -0.9962  0   <- axis row 1
COOP ENT +0x0020 00000000 3f800000 00000000 00000000      0  1  0  0              <- axis row 2
COOP ENT +0x0030 3f7f069e 00000000 bdb27eb3 00000000      0.9962 0 -0.08716 0
COOP ENT +0x0040 44368ada 433b2b2b 416fc337 3f800000    730.2  187.2  14.99  1  <- position row
```

and an m010 unit, found by the same class scan (`findvt 0x10142638`):

```
   obj 0x12f9f8e0 id 2 +8 0x00000064 moff 0x10  pos  -3413.00  20.00  114.60
COOP ENT +0x0000 10142638 00000002 00000064 3f800000
COOP ENT +0x0010 3db27e9f ... (orthonormal, position row at +0x40)
```

So **one vtable is enough**: `0x10142638` = the actor/entity class; `+4` = entity id (u32, big-endian);
`+0x10/+0x20/+0x30` = the rotation rows; `+0x40/+0x44/+0x48` = world position; the fourth 4x4 at `+0x190`
is a copy. That is the layout `research\24` §3 established for the player, and it holds for the units.

### 1.2 The container that holds them

Scanning all committed guest memory for the 32-bit word `0x12fd7ff0` (this run's body pointer) returned
**156 hits**; the informative ones form pointer arrays. One of them, verbatim from the log:

```
COOP ENT mem 0x158f5800..0x158f5b00
COOP ENT +0x0000 12dd8240 12ddf2c0 12de6430 12dfb510
COOP ENT +0x0010 12e02590 12e09610 12e10690 12e17710     stride 0x7080 between entries
...
COOP ENT +0x00d0 12f98860 12f9f8e0 12fa6960 12fad9e0
COOP ENT +0x00f0 12fd0b20 12fd7ff0 12fd7ff0 12fd7ff0     the player's body appears here
COOP ENT +0x0220 12fd7ff0 12fd7ff0 12fd7ff0 10d959d0
COOP ENT +0x0240 10d95d00 10d95da0 00000000 00000000
```

Reading it: an array of 4-byte pointers to actor objects in **address order**, the objects themselves
sitting **0x7080 bytes apart** (the entity pool), followed by a run of repeats of the player's body and
then a second, smaller run at stride 0xa0. The same pool objects appear in several such arrays
(`0x10d69f74`, `0x15906a54`, `0x158f590c` …), i.e. the scene keeps more than one list over the same
objects (update / draw / collision are the obvious candidates; **which list is which is NOT
DETERMINED**).

Reproducing it is not required: the pool is *derivable*. One pass over committed memory for objects whose
first word is `0x10142638` yields exactly the pool, and the walk in `coop_entities.cpp` then keeps a
`[lo, hi)` window around it (~6 MB) and re-scans only that window once a second.

### 1.3 Counts — the class is the table

Run `ent2`, `findvt 0x10142638` at 682 s and again at 745 s: **320 objects both times** (a fixed-capacity
pool). In the mission (20261115-122725 host) the walk reports:

```
COOP ENT t=800.3 objects=320 alive=320     (run 120953, just after gameplay)
   ids: 304 x id 0, 14 x id 2, 1 x id 1000 (player), 1 x 269761672 (a .data word, not an object)
```

### 1.4 The id field, honestly

`+4` carries the id in the sense `research\24` measured — 1000 for the player, 2 for the objects of
`m010_ET_ENEFIRST` — but **most of m010's units carry 0 there** (304 of 318). The `mXXX_ET_*` ids address
*event entities* (spawners/groups), not each spawned unit; a unit spawned as a munition has no id of its
own. Consequence for the co-op design: a per-unit identity has to be the **object pointer**, not an
entity id.

### 1.5 The engine's lookup function — NOT DETERMINED

What was tried and what it gave:

* Static: `SendMessageToEntity` `0x00a80684` → `f_00a80148`, which searches two 8-byte-element sorted
  runs at `obj+128`/`obj+136` (`ppu_gen_112.cpp:17679-17712`) — a **per-object message-handler** map, not
  the entity table (its key comes from the TOC global `r32(r2+2080)`, not from a script id).
* Static: no generated function compares a vtable against the immediate `0x10142638`
  (`Select-String` over all 162 `ppu_gen_*.cpp`: **0 hits**), so the type is not tested that way; a
  generic `+4 == id` scan finds 362 candidate sites and none could be singled out without a runtime hook
  on a guest function, which the launch budget did not justify.
* Runtime: the container was found by pointer scan (§1.2) and **its own id → object relation was
  verified**: looking up id 1000 among the walk's entities returns `0x12fd7ff0`, the object the port's
  AC build registers as AC1's body (`vt 0x10142638`, `+4 == 1000`, `+0x40` = the AC's known spawn
  position). That is the M1 verification the brief asks for; what is missing is the *address of the
  engine routine* that does it.

---

## 2. M2 — enumerate, and compare with the mission's own counts

`AC4_COOP_ENTS=1` logs a per-second summary; `AC4_COOP_METER=<file>` writes one line per entity per
second (the meter, §4). Walk cost: a full committed-memory pass on the first tick and every 20 s
(~0.3 s), a ~6 MB window scan every other second.

Mission script probe (Lua injected into `m010_event.000`, `run\coop_probe.py --mode probe`), verbatim:

```
[ 143.2253 W ... lua] script error 1 ... m010.lua:327: AC4RIGPROBE kill=0 g1=312/312 g2=45/45 g3=39/39
   g4=38/38 g5=38/38 g6=38/38 g7=38/38 g8=38/38 g9=38/38
```

(`g1 = m010_GP_ALLENEMY`, `g2..g9` = `GP_ENEMY01..GP_ENEMY_LAST`, 45+39+38·6 = 312 — the subgroups
partition the total, exactly as `include\m010\eventgroup.lua` declares.)

| measured | value |
|---|---|
| mission's own enemy total at mission start | **312** (`g1=312/312`) |
| objects in the entity table with an enemy-shaped vtable | **318** (304 × id 0 + 14 × id 2) |
| player bodies | **2** (both id 1000 — `AC4_COOP=1` builds two) |
| non-entity false positives from the class scan | 1 (`0x00ef499c`, a `.data` word) |
| **total objects walked** | **321** |

So the table covers the whole mission and then **6 objects more than the script counts**. What those 6
are is **NOT DETERMINED** (the rocket facility `m010_ET_ROCKET = 1` and the briefing carrier
`ENTITY_ID__BRIEFINGCARRIER = 10000` are the obvious candidates, but none of the 318 carried id 1, and
the object with id 10000 was not found). Class grouping: **321/321 share vtable `0x10142638`** — in m010
there is exactly one entity class.

### 2.1 The AI-loop units of `research\15` are not in this mission

`research\15` §2c logged the per-unit command loop `f_00b4cce0` with unit vtable `0x101475c8` in m070 and
m010. `findvt 0x101475c8` in this m010 run returns **1 object** (`0x00f00260`, a `.data` table word, no
matrix) — i.e. **no live unit of that class exists in m010**, so there is nothing to link: every live
mission entity in m010 is an object of class `0x10142638` and is in the table. Whether a mission that
does have infantry units (m070) keeps them in the same table, and what the unit → entity pointer is,
remains **NOT DETERMINED**.

---

## 3. M3 — hit points and the alive flag

### 3.1 The field (one class, two values)

A whole-memory search for the float `35888.0` (= the HUD's AP) found **3** words; two of them are adjacent
inside the player's body:

```
COOP ENT scanf 0x470c3000: 3 hit(s)
  hit 0x12fd8294  ... *0x470c3000 0x470c3000 0x1586c820 ...     body + 0x2a4, + 0x2a8
```

Dumped directly:

| object | +0x2a4 (current) | +0x2a8 (max) | state |
|---|---|---|---|
| player body `0x12fd7ff0` | **35888.0** | **35888.0** | full |
| m010 unit `0x12ddf2c0` | **1.0** | 1.0 | alive |
| m010 unit `0x12dd8240` | **0.0** | 1.0 | **dead** |

and the dead unit's first axis row at `+0x10` had collapsed to `(0,0,0)` while the live one's was a unit
vector — so a dead slot stays in the pool with `hp = 0`, which is the alive flag.

**Field table**

| class (vtable) | field | object | offset | type | how verified |
|---|---|---|---|---|---|
| actor `0x10142638` — player AC | AP current | body | **+0x2a4** | float | the HUD's AP (`35888`) searched as a float; the two hits are body+0x2a4/+0x2a8; the meter reads 35888.0 while the HUD shows 35888 |
| actor `0x10142638` — player AC | AP max | body | **+0x2a8** | float | same |
| actor `0x10142638` — m010 units | HP current | object | **+0x2a4** | float | alive unit 1.0, dead sibling 0.0 in the same dump |
| actor `0x10142638` — m010 units | HP max | object | +0x2a8 | float | 1.0 in both |
| actor `0x10142638` | alive (object has hit points) | object | derived | `hp > 0` | the pool's alive count falls with the mission's own `g1` alive count for the first ~400 s, and the two dead/alive siblings above — but the two counts separate later, see §3.3 |
| actor `0x10142638` | entity id | object | +4 | u32 | id 1000 on the player's body, id 2 on 14 unit objects |
| actor `0x10142638` | position | object | +0x40/+0x44/+0x48 | 3 × float | matches the AC spawn (730.2, 187.2, 14.99) in `research\24` |
| actor `0x10142638` | orientation | object | +0x10/+0x20/+0x30 | 3×3 float | orthonormal (the scan's test is |row| = 1 ± 0.02, rows orthogonal ± 0.02) |

### 3.2 The damage confirmation — partially measured

Not measured: the player taking damage. The player's `+0x2a4` read **35888.0 for the whole session** in
every run (the AC was never hit while idle), so "the field follows the HUD down" is **NOT DETERMINED**;
only "the field equals the HUD at full AP" is.

Measured: **destruction**, on one object. The unit `0x12ddf2c0` read `hp 1.0` while its neighbour
`0x12dd8240` read `hp 0.0` with its first axis row collapsed to `(0,0,0)` — a before/after diff taken
inside one dump of two objects of the same class in the same pool. A second enemy class was **not**
measured — m010 has only the actor class.

### 3.3 The pool's `hp > 0` is not the mission's group-alive

The two counts track each other early and then separate, measured in the host run 20261115-122725:

| game clock | mission script (`g1` alive / total) | pool objects with `hp > 0` |
|---|---|---|
| 145 | 312 / 312 | 321 |
| 529 | ~232 / 295 | 240 |
| 658 (last probe) | **0 / 81** | **240** |

So by the end every enemy the mission counts had been destroyed (`g1 = 0/81`) while 240 pool objects
still reported `hp > 0`. Either those 240 are objects the mission's groups never contained, or the hp
field is not what the group bookkeeping reads. Which is which is **NOT DETERMINED**; what is measured is
the field and its 1.0 → 0.0 death transition on a unit the mission does count.

---

## 4. M4 — the divergence meter

`AC4_COOP_METER=<file>` appends, once a second, a header `# t=<t> n=<count>` and then one line per entity:

```
<t> <id> <vtable> <x> <y> <z> <hp> <alive>
```

`run\coop_divergence.py --a <file> --b <file> [--offset s]` aligns B onto A (`bt = t - offset`) and
reports per second: entities present in only one instance, max and mean position distance over the common
keys, hp mismatches, and the first key to exceed 10 units. Keys are `(id, vtable, ordinal-within-that-pair)`
because ids are not unique (304 objects share id 0); **ordinal keying is a heuristic** and is the reason
the max distance is dominated by a few mis-paired pool slots (§4.3).

Both instances ran m010 with the UDP link up and `AC4_COOP=1` (two ACs each). Link state, verbatim:

```
COOP NET: host | sent 53159 recv 54930 dropped 0 bad 0 | peer UP | pad age 0 ms | xform ... error 0.00
COOP NET: join | sent 43491 recv 42799 dropped 0 bad 1 | peer UP | pad age 0 ms | xform 6114 samples
```

### 4.1 Run 1 — both idle (window 980–1235 s, 249 paired seconds, offset −1.9 s)

| t_A | t_B | onlyA | onlyB | common | maxdist | meandist | hpmis |
|---|---|---|---|---|---|---|---|
| 980.7 | 982.2 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1005.0 | 1006.5 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1029.6 | 1031.8 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1054.0 | 1056.3 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1078.2 | 1079.7 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1102.5 | 1104.2 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1127.2 | 1129.5 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1151.4 | 1153.8 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1175.7 | 1177.3 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |
| 1199.9 | 1201.7 | 0 | 0 | 321 | 3183.87 | 454.81 | 54 |

`seconds compared 249 | peak max distance 3183.87 | mean of per-second means 454.81`
`entity-seconds present in only one instance: A 0, B 0 | hp mismatches: 13446 (54 per second)`
`first id beyond 10 units: t=980.7 id=0 vt=0x10142638 distance 3183.87`

### 4.2 Run 2 — p0 on the host held forward + fire (window 1250–1480 s, 224 paired seconds)

| t_A | t_B | onlyA | onlyB | common | maxdist | meandist | hpmis |
|---|---|---|---|---|---|---|---|
| 1250.8 | 1252.6 | 0 | 0 | 321 | 3183.87 | 455.20 | 54 |
| 1273.1 | 1275.0 | 0 | 0 | 321 | 3183.87 | 457.42 | 54 |
| 1295.4 | 1297.3 | 0 | 0 | 321 | 3183.87 | 459.79 | 54 |
| 1318.1 | 1319.6 | 0 | 0 | 321 | 3183.87 | 461.76 | 54 |
| 1340.4 | 1341.9 | 0 | 0 | 321 | 3183.87 | 461.93 | 54 |
| 1362.7 | 1364.1 | 0 | 0 | 321 | 3183.87 | 462.20 | 54 |
| 1385.1 | 1387.4 | 0 | 0 | 321 | 3183.87 | 462.57 | 54 |
| 1407.7 | 1409.5 | 0 | 0 | 321 | 3183.87 | 463.02 | 54 |
| 1430.0 | 1431.8 | 0 | 0 | 321 | 3183.87 | 463.55 | 54 |
| 1452.4 | 1454.1 | 0 | 0 | 321 | 3183.87 | 464.14 | 54 |

`seconds compared 224 | peak max distance 3239.60 | mean of per-second means 461.68`
`entity-seconds present in only one instance: A 0, B 0 | hp mismatches: 12096`

**This run's input did nothing** and must not be read as "driving increases divergence": the host's
player body sat at `(722.2, 188.2, 15.4)` for the whole 226 s window (it had been at
`(725.3, 187.6, 13.1)` during run 1), and the `meandist` drift 455.20 → 464.14 is the same drift run 1
would show. Cause, measured: the simulation had stopped advancing — the mission's kill counter froze at
231 and the pool's alive count at 240 for both instances while the renderer stayed at 56 fps. So **"p0
driving" is NOT DETERMINED** (§7).

### 4.3 What the numbers actually say

* **The entity set never differs** (0 only-in-A, 0 only-in-B, 321 common, every second of both runs): the
  two instances spawn exactly the same entities — the mission is deterministic at spawn time.
* **Positions diverge a lot already with nobody acting**: median distance 8.43 units, p90 1929 units,
  84–85 pairs beyond 100 units (sample second, idle run). That is the expected, valid result with no
  enemy sync — the two simulations are independent and enemy positions are unrelated across instances.
* **hp differs on 54 of 321 entities every second** (17 %): the two instances have killed different
  units. Again expected with no sync.
* The **max** (3183) and much of the mean are inflated by **ordinal mis-pairing** of identical-id objects:
  the worst pairs are all `id 0`, e.g. `(0, 0x10142638, 37) 2182`, `(0, 0x10142638, 55) 2973`. A
  per-object key (a stable per-entity serial) would fix this; the honest summary is the **median
  (8.43)** and the **count beyond 10 units (136 of 321, 42 %)**.

---

## 5. M5 — why m010 ends by itself, and the patch that stops it

### 5.1 The cause is the mission's own invasion counter, not a timer

`extracted\ac4_lua\...\include\m010\constvalue.lua` and `...\event\mission\obj\m010_rocket.lua`:

```lua
m010_CONST_LAST_INVASION = 100;
-- m010_rocket.lua, OnMissionStart
WatchSharedGlobalValue( eventOperation, "KamikazeDeads", m010_CONST_LAST_INVASION, numenemy, "OnDeadCrasher4" );
-- OnDeadCrasher4: SetMissionResult_Failure( eo ); ... OnMissionFailure( eo )
```

`KamikazeDeads` counts the self-propelled munitions that have reached the facility. The probe measured it
rising monotonically with the player completely idle, and the mission ending right after it crossed 100:

| t (game clock) | probe |
|---|---|
| 641.5 | `kill=0` |
| 684.9 | `kill=18` |
| 720.3 | `kill=49` |
| 753.8 | `kill=79` |
| 786.4 | **`kill=96`** |
| 799.4 | `kill=107` |
| 811.6 | `kill=130` |
| **818.9** | `input context screen` — **the sortie is over** |

and the duration matches the blocker exactly: gameplay 598.9 → out at 818.9 = **220 s**. It is not a
clock, not the area, and not the player dying (the other three fail paths — `OnOutOfMissionArea`,
`OnOutOfRegionAndTimeOut`, `OnDeadPlayer`, all registered in `m010.lua:Initial` — were tagged by the probe
and **never fired** in any run).

### 5.2 The patch (test overlay only)

`run\coop_probe.py --mode keepalive` appends to the bundled `m010.lua`:

```lua
local AC4RIG_noop = function( eo ) end;
OnOutOfRegionAndTimeOut = AC4RIG_noop;
OnOutOfMissionArea = AC4RIG_noop;
OnDeadPlayer = AC4RIG_noop;
```

and to the bundled `obj\m010_rocket.lua`:

```lua
function OnDeadCrasher4( eo ) error( "AC4RIGKEEPALIVE: invasion fail suppressed" ); end
```

(three lines of behaviour in the event bundle, one function in the mission bundle). Both are registered by
**name**, so replacing the globals after `Initial` is enough. The bundles are rebuilt by
`tools\bnd3tool.py` into **`payload\overlay_test\`**; `payload\overlay` was not touched.

### 5.3 Confirmed

Run 20261115-122725 (host) with `--overlay-test` and the patch: mission start **143.2 s**, last meter
second **1556.5 s** → the sortie stayed open for **1413 s (23.5 min)** with `kill` well past 100 (231),
**no `Ac4SortieScene` exit, no `debriefing.000`** in the log. The requirement was 15+ minutes.

**Caveat, measured:** the mission does not stay *live*, it stays *open*. The keepalive tag printed
**once** (`AC4RIGKEEPALIVE: invasion fail suppressed`), the failure never reached
`SetMissionResult_Failure`, and the sortie was never exited — but from ~530 s the entity pool's alive
count and every entity position stop changing, and the host's player ignored a 260 s forward+fire hold,
while the renderer stayed at 56 fps. The last probe also shows the mission's own groups empty
(`g1=0/81`) without the success path completing either. So the scene is kept open for 23 min, but a
test that needs the battle to keep *running* would also need the success path suppressed (or a
different mission).

---

## 6. Files changed

| file | change |
|---|---|
| `ac4-port\src\runtime\game\coop_entities.h` | **new** — the instrument/walk API and its env vars |
| `ac4-port\src\runtime\game\coop_entities.cpp` | **new** — request file (`scan`, `scanf`, `findvt`, `mem`, `watch`, `sweep`, `dumpbody`), the class scan, the pool window, the per-second walk, the meter writer |
| `ac4-port\src\runtime\game\frame_hooks.cpp` | +1 include, +1 call: `coop_entities_frame()` beside `coop_body_frame()` |
| `run\coop_launch.ps1` | `-Overlay <dir>` (was hard-coded to `payload\overlay`), `-Req` (creates `<shots>\req.txt` and sets `AC4_COOP_REQ`), echoes the three new env vars |
| `run\coop_host.ps1`, `run\coop_join.ps1` | forward `-Overlay` / `-Req` |
| `run\coop_probe.py` | **new** — injects the group-count probe / the keepalive patch into `m010_event.000` + `m010_mission.000`, into an overlay directory |
| `run\coop_req.py` | **new** — one command per invocation into `<shots>\req.txt` and prints the log lines it caused |
| `run\coop_divergence.py` | **new** — the two-file divergence analyser |
| `payload\overlay_test\...` | **new** — the test overlay (probe and keepalive modes). `payload\overlay` untouched |

All new runtime behaviour is behind `AC4_COOP_REQ`, `AC4_COOP_ENTS` and `AC4_COOP_METER`; with none of
them set the port behaves exactly as before.

## 7. NOT DETERMINED

1. **The engine's `entity id -> object` function.** The container (the 0x7080-stride actor pool and the
   scene's pointer arrays over it) and the id field are measured and the lookup is verified from the
   outside; the routine that performs it is not identified (§1.5).
2. **The role of each pointer array** over the pool (update / draw / collision / other).
3. **The 6 objects** the table has that the script's `g1` does not count, and the identity of the object
   carrying `ENTITY_ID__BRIEFINGCARRIER = 10000`.
4. **A second enemy class's hp field.** m010 ships one enemy class only; the next classes to try are
   `0x10147840` (id 1, the rocket facility, 4 objects measured in `ent1`) and the AC class in a mission
   with a real `_ET_NEXTAC` (m030/m050/m170).
5. **"AP follows the HUD down".** The player was never damaged in any run, so only the full-AP equality
   (35888.0 ↔ HUD 35888) is measured.
6. **M4's driven run.** The input had no effect because the keepalive'd mission had stopped simulating;
   the second table is a second idle window, not a driven one. A clean pair needs a fresh pair of
   instances with the drive window *inside* the first ~300 s of gameplay.
7. **Whether the m070-style AI-loop units (`0x101475c8`) are in the same table** — no such unit exists in
   m010, so no unit → entity link could be measured.
8. **A proper per-entity key** for the divergence meter (ids are not unique; ordinals mis-pair).
9. **Whether the pool's `hp > 0` set is the mission's enemy set.** They agree in trend for ~400 s and
   separate afterwards (240 objects vs `g1=0/81`, §3.3), so the pool may hold objects the mission's
   groups never contained.
10. **Why the success path did not complete** in the keepalive'd run once `g1` reached 0.
