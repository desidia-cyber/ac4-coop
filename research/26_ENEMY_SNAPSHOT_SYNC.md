# 26 — Enemy snapshot sync: host authority over the mission entity pool

**Scope.** M1 a stable entity key, M2 host→join entity snapshots (a new packet type), M3 freezing the
join's own AI, M4 measuring what the sync buys, M5 whether the join player's fire reaches the host.

Everything below is measured on this machine. Anything not measured is marked **NOT DETERMINED**.

| # | milestone | result |
|---|---|---|
| M1 | stable entity key | **DONE, and the design's premise was wrong**: the pool is NOT a 0x7080 stride (200 of 319 consecutive gaps are 0x7080, 119 are not). Both instances nevertheless build a **bit-identical entity set**: 320 objects, the same addresses in the same order, the same ids, the same spawn positions. The key is therefore the **entity address**, verified: 320/320 common, 0 differences (§1) |
| M2 | snapshots host → join | **DONE** — packet type 3, 60-byte records, 19 per ≤1200-byte datagram, host ~5–7 snapshots/s (measured 1590–2226 records/s in 85–119 datagrams/s), join applied **105 529 records and skipped none once it had entities** (the only skipped records are the ones that arrive before the join itself is in the mission — `local 0x00000000`) (§2) |
| M3 | freeze the join AI | **DONE for the implementation, FAILED to stop movement** — the `unit_ai` skip fires (6 units of class 0x101475c8 at mission start) but entity displacement is not reduced (mean 439.7 vs 171.9/235.3 units over 30 s in two unfrozen samples). The one allowed alternative (`SetEnableMove`/`SetEnableAttack`) was **not attempted**: their receiver class and value semantics are not evident (§3) |
| M4 | measure | **DONE** — Run A and Run B tables next to the M1 baseline; the sync is real but **stops at the snapshot boundary**: mean mover distance 271.83 → 245.90 units (−10 %) while the p90 gets *worse* (381.91 → 545.06). Screenshot pairs in `research\e11_shots\` (§4) |
| M5 | damage direction | **DONE (measure only)** — the host's entities do lose hp while only the join player fires, and the host's own AC1 never fired; the two AC APs stay equal at 35888 (§5) |

Runs: 21 launches (`m1` ×4 pairs including 3 aborted boots, `m3` ×1, `m4` ×3 pairs, `m5`/`m6` ×3
pairs). Logs and meters in `research\e11_raw\`, screenshots in `research\e11_shots\`. All `ac4.exe`
stopped at the end.

---

## 1. M1 — the key is the address, not the stride

### 1.1 What research\25 assumed, and what the pool actually is

research\25 §1.2 described the pool as objects "0x7080 bytes apart". That is only the average. Measured
on one live second of the m010 pool (320 objects, `m1_host.txt` t=285.8):

```
consecutive address gaps: most common [('0x7080', 200), ('0x70f0', 8), ('0x7010', 8), ('0x7040', 7), ('0x7170', 7), ('0x7120', 6)]
gaps == 0x7080: 200  gaps != 0x7080: 119
min gap 0x15b0 max gap 0x12cdb10
```

So `(addr - base) / 0x7080` is not a slot index for 119 of the 319 gaps: the objects are separate heap
allocations whose sizes differ a little, and the two AC bodies sit far outside the run. A stride key
would collide.

### 1.2 What both instances DO agree on — everything

The two instances run the same mission with the same allocation sequence, so they build the **same
object graph**. `AC4_COOP_ENTS=1` logs a one-time table when the pool is first populated; compared with
`run\coop_pool_compare.py`:

```
A lowest 0x10d9c100 n 320 rows 320
B lowest 0x10d9c100 n 320 rows 320
lowest addresses identical: True
ordinals only in A: []
ordinals only in B: []
of 320 common ordinals: address differs 0, id differs 0, spawn position differs (>1 unit) 0, worst position delta 0.000
```

Verbatim, first rows of **both** logs (identical, including the AC body at ord 0):

```
[ 285.9271 I 77892 coop] COOP POOL t=285.8 n=320 lowest 0x10d9c100 (address order)
[ 285.9271 I 77892 coop] COOP POOL ord 0 addr 0x10d9c100 id 1000 pos 730.000 187.219 15.000 hp 35888.0000
[ 285.9271 I 77892 coop] COOP POOL ord 1 addr 0x12069c10 id 0 pos 1434.304 163.951 -171.833 hp 100.0000
[ 285.9271 I 77892 coop] COOP POOL ord 2 addr 0x12070d50 id 0 pos 1439.022 164.753 -188.451 hp 100.0000
```

The same holds for the two AC bodies in every session (`COOP BODY#1 registered 0x13009780`,
`COOP BODY#2 registered 0x10d9c100` on **both** sides), which is the same observation research\24 M3
made. Consequence: the key is the **entity address**; the rank in the address-ordered list is carried in
the snapshot record as a fallback. The meter, `run\coop_divergence.py`, `run\coop_motion.py` and
`run\coop_movers.py` all key on it.

An entity is only counted above `0x10000000`: the ELF image holds one data word equal to the actor
vtable (`0x00ef499c`, the false positive research\25 §2 counted) and it pushed the very first build of
this code into latching the AC2 body as "the pool base". Excluded, the walk reports exactly 320 objects
for m010.

### 1.3 The idle baseline to beat (120 s, link on, `AC4_COOP_NOSNAP=1` on the join)

Alignment is by **mission age**, not process clock: the two instances reach the mission tens of seconds
apart (measured 93 s in this pair), so `coop_divergence.py --auto-offset` aligns the first populated
second of each meter. All 320 entities are common on every second.

```
t_A      t_B    onlyA onlyB common  median      p90      max   >10u hpmis
  285.8   195.9     0     0    320    0.00     0.00     0.00     0     0
  303.1   213.1     0     0    320    0.00    82.15    85.91    45     0
  322.5   232.5     0     0    320    0.00   427.12   427.30    84     0
  339.1   249.8     0     0    320    0.00   486.80   486.80   122    12
  356.5   266.8     0     0    320    0.00   575.97  1234.89   121     8
  375.7   286.2     0     0    320    0.00   323.21  1911.73   121    11
  392.0   302.2     0     0    320    1.26   472.18  2144.35   148    17
  408.8   318.8     0     0    320    2.97   384.41  2144.35   146    15
  426.9   337.3     0     0    320  191.67   327.87  2144.35   183    15
  444.5   354.9     0     0    320   10.35   295.64  2144.35   163    15

seconds compared 150 | peak max distance 2144.35 | mean of per-second medians 30.40 | mean p90 371.49
entity-seconds in only one instance: A 0, B 0 | hp mismatches 1468 (9.8/s)
entities beyond 10 units: 126.3 per second of 320 common (39%)
```

**Mission age 0 is bit-identical** (median, p90 and max all 0.00, 0 hp mismatches): the mission is
deterministic at spawn and only then diverges.

The median of 0.00 is not a bug: half the pool never moves. Splitting the entities by whether the host
moved them (>10 units across the window) gives the number the sync must move:

```
window 285.8..405.5 s | 320 entities, 160 movers (>10 units on the host)
movers  n=160  mean 271.83 | median 332.72 | p90 381.91 | peak 2144.35
static  n=160  mean 0.01 | median 0.00 | p90 0.00 | peak 3.72
```

---

## 2. M2 — snapshots host → join

### 2.1 The packet

Type 3 on the existing `coop_net` socket (little-endian, the existing 8-byte `{magic 'AC4C', version,
type, u16 seq}` header), payload:

| field | type | meaning |
|---|---|---|
| `counter` | u32 | whole-snapshot counter |
| `tag` | u32 | the host's lowest entity address, so the join can log both beside each other |
| `count` | u16 | records in this datagram |
| `reserved` | u16 | 0 |
| records | 19 × 60 B | `{u16 ord, u16 0, u32 addr, float pos[3], float rot[9], float hp}` |

8 + 12 + 19·60 = **1160 bytes ≤ 1200**. The host sends every occupied entity except the two player
bodies (the type-2 path owns those) at ~10 Hz; the join applies every received record each frame:
rotation rows `+0x10/+0x20/+0x30`, position `+0x40/+0x44/+0x48`, hp `+0x2a4`, **no smoothing**. A record
is resolved by its address; if that address does not hold an entity locally the record's `ord` indexes
the local address list instead. `AC4_COOP_NOSNAP=1` receives but does not apply.

### 2.2 Measured rate and integrity

Host, per second (verbatim, `m4_host.log`):

```
[ 268.5903 I 54988 coop] COOP SNAP host t=268.6 sent 2226 records in 119 datagrams (318 entity(s) excluding the 2 players, lowest 0x10d9c100)
[ 269.9141 I 54988 coop] COOP SNAP host t=269.9 sent 954 records in 51 datagrams (318 entity(s) excluding the 2 players, lowest 0x10d9c100)
```

Join, per second (verbatim, `m6_join.log`) — this is the M2 "records applied + max position error seen
before applying" line:

```
[ 260.6326 I 47004 coop] COOP SNAP join t=260.6 applied 1590 rec (total 9336), skipped 0, counter 160, host lowest 0x10d9c100 vs local 0x10d9c100, max pre-apply error 1595.17
[ 343.5017 I 47004 coop] COOP SNAP join t=343.5 applied 1590 rec (total 104575), skipped 0, counter 463, host lowest 0x10d9c100 vs local 0x10d9c100, max pre-apply error 1967.01
```

Totals for the session, both directions (verbatim):

```
[ 328.3051 I 54988 coop] COOP NET: host | sent 19634 recv 9790 dropped 0 bad 0 | peer UP | pad age 0 ms | xform 1398 samples, mission 0, error 0.00 | snap sent 186030 rec in 9945 pkt, recv 0 rec
[ 327.5877 I 42232 coop] COOP NET: join | sent 9788 recv 19481 dropped 0 bad 1 | peer UP | pad age 0 ms | xform 1382 samples, mission 0, error 0.00 | snap sent 0 rec in 0 pkt, recv 183686 rec
```

* **No record is ever skipped once the join has entities.** Every `skipped != 0` line in every session
  is a pre-mission line: `m4_join` 10 such seconds (t 192.4–203.6), `m5_join` 30 (t 212.9–248.8),
  `m6_join` 19 (t 123.7–142.6), all with `local 0x00000000` — the join was still in the menus and those
  records had no target. From the first second the join is in the mission, `skipped 0` and every record
  resolves by address.
* Datagram count = records / 19 exactly, i.e. the ≤1200-byte split works.
* The cycle is **5–7 snapshots/s**, not 10: the publish gate is 0.1 s but each instance runs at 23–40 fps
  with both instances up (research\24 §4 measured the same frame rate).
* The `max pre-apply error` is the interesting number: it grows from ~435 to **1771–2031 units** through
  a mission. That is how far the join's own simulation has moved an entity away from the host's last
  value by the time the next snapshot lands — the local AI out-runs the 5–7 Hz stream.

---

## 3. M3 — freezing the join's AI

### 3.1 What was built

`hook_unit_ai` (`0x00b4cce0`) now returns **without calling the original** for every unit that is not one
of the two player bodies, when `AC4_COOP_FREEZE=1` **and** this process is the joiner
(`coopnet::role_join()`), so the host and any run without the link are unchanged. Verbatim at mission
start on the join:

```
[ 128.7226 I 28684 input] P2: AI freeze requested; it applies only on the join instance
[ 128.7226 I 28684 input] AI FREEZE: unit 0x12070a30 vt 0x101475c8 skipped (call 1, 1 unit(s) so far)
[ 128.7228 I 28684 input] AI FREEZE: unit 0x120792e0 vt 0x101475c8 skipped (call 2, 2 unit(s) so far)
[ 128.7228 I 28684 input] AI FREEZE: unit 0x12080680 vt 0x101475c8 skipped (call 3, 3 unit(s) so far)
[ 128.7229 I 28684 input] AI FREEZE: unit 0x12087a10 vt 0x101475c8 skipped (call 4, 4 unit(s) so far)
```

The skip is safe: `f_00b4cce0_guest` returns nothing (`ppu_gen_121.cpp:9468`, no value in R(3) at
return), no crash in any run (`fatal` count 0), and the mission kept running (alive 320 → 286 by age 40).

**But only 6 units of class 0x101475c8 exist in m010.** The 320 mission entities are class 0x10142638
and are not driven through this loop at all — consistent with research\25 §2.1 ("no live unit of that
class exists in m010").

### 3.2 Displacement with and without the freeze (`AC4_COOP_NOSNAP=1` in both)

30 s windows at the same mission age, mean displacement of all 320 entities:

| run | conditions | mean | median | max | moved >10 units |
|---|---|---|---|---|---|
| m3 join | `FREEZE=1 NOSNAP=1` | **439.74** | 0.00 | 2254.98 | 84 (26 %) |
| m1 join | `NOSNAP=1` | 171.94 | 0.00 | 1224.04 | 45 (14 %) |
| m1 host | (control sample) `NOSNAP=1` | 235.34 | 0.00 | 1480.54 | 84 (26 %) |
| m3 join, age 30–60 | `FREEZE=1` | 617.70 | 0.00 | 2263.06 | 112 (35 %) |
| m1 join, age 30–60 | `NOSNAP=1` | 312.44 | 0.00 | 1205.49 | 84 (26 %) |

**Skipping the loop does not stop movement.** The frozen run moves *more* than either unfrozen sample
(run-to-run spread is real — 171.9 vs 235.3 for the two unfrozen samples — but 439.7 is outside it, and
the 30–60 s window is 617.7 vs 312.4). The freeze also does not crash and does not stop the mission from
progressing.

### 3.3 The one allowed alternative — not attempted

`SetEnableMove` `0x00a5a054` / `SetEnableAttack` `0x00a5a084` were read from the recompiled code
(`ppu_gen_111.cpp:1216/1229`):

```c
void f_00a5a054(Ctx& ctx){ ... R(3)=r32((u32(R(3))+160u)); R(3)=R(3)+308; f_00a65084(ctx); ... }   // (self->[160]+308, r4)
void f_00a5a084(Ctx& ctx){ ... R(5)=R(4); R(4)=25; R(3)=r32((u32(R(3))+160u)); f_00a65198(ctx); }    // (self->[160], 25, r4)
```

The register usage is evident; what is **not** evident is (a) which object class the wrapper expects as
`self` — it dereferences `self+0xa0`, and nothing in this port has measured what an actor entity holds
there — and (b) what `r4` means (enable/disable polarity) for the two leaf functions. Calling it with a
wrong receiver would dereference a vtable word as a pointer. Per the brief's condition ("only if their
calling convention is evident … otherwise stop at this milestone"), **M3 stops here**; a guest-call
facility does exist (`hle::call_guest`) so the experiment is possible once `entity+0xa0` is measured.

---

## 4. M4 — what the sync buys

Both instances in m010 with `AC4_COOP=1`, the UDP link up, snapshots applied (`AC4_COOP_NOSNAP` unset)
and the join's AI freeze on. Mission-age alignment throughout.

### 4.1 The comparison table (movers = entities the host moved >10 units in the window)

| window | seconds | movers | mean | median | p90 | peak | hp mismatches/s |
|---|---|---|---|---|---|---|---|
| **M1 baseline** (idle, no sync) | 120 | 160 | 271.83 | 332.72 | 381.91 | 2144.35 | 9.8 |
| **M4 Run A** (both idle, sync) | 120 | 160 | **245.90** | **267.07** | 545.06 | 2030.33 | **4.4** |
| **M4 Run B** (host drives 60 s, sync) | 50 | 86 | 570.58 | 563.89 | 957.12 | 1646.72 | 31.0 |
| **M5** (join fires 40 s, sync) | 26 | 81 | 707.97 | 872.63 | 1088.79 | 1541.35 | — |

Run B has no matched baseline (the M1 baseline is idle); it is reported for completeness.

### 4.2 Run A, per 10 s (host idle, join applying snapshots)

| bucket (s) | common | mean of medians | mean of p90 | max | >10 units/s | hp mismatches/s |
|---|---|---|---|---|---|---|
| 0–10 | 320 | 0.00 | 236.13 | 498.09 | 40.0 | 0.0 |
| 10–20 | 320 | 0.00 | 387.60 | 568.42 | 45.0 | 0.0 |
| 20–30 | 320 | 0.00 | 497.10 | 742.87 | 72.9 | 0.0 |
| 30–40 | 320 | 0.00 | 472.02 | 868.89 | 84.0 | 1.3 |
| 40–50 | 320 | 0.00 | 617.36 | 825.72 | 110.6 | 16.1 |
| 50–60 | 320 | 0.00 | 326.60 | 682.13 | 115.0 | 9.1 |
| 60–70 | 320 | 0.00 | 159.12 | 723.41 | 104.2 | 0.0 |
| 70–80 | 320 | 0.00 | 117.25 | 1254.45 | 119.5 | 0.0 |
| 80–90 | 320 | 0.00 | 313.53 | 1808.08 | 119.1 | 0.0 |
| 90–100 | 320 | 0.00 | 483.19 | 2030.33 | 121.3 | 3.6 |
| 100–110 | 320 | 0.00 | 555.53 | 835.36 | 125.1 | 14.0 |
| 110–120 | 320 | 0.00 | 696.52 | 845.30 | 117.9 | 2.4 |

Baseline for the same framing (idle, no sync): medians 0.00, mean p90 **371.49** overall, 126.3/s beyond
10 units, 9.8 hp mismatches/s.

**Reading it honestly:** the sync is applied and it does help on the mean/median (271.83 → 245.90,
332.72 → 267.07; hp mismatches halved) — but **the p90 gets worse (381.91 → 545.06) and the peak is
unchanged**, and the 160 entities that never move were already identical without any sync (static mean
0.01) so the whole change comes from the movers. The reason for the tail is in §2.2: the join applies
host positions 5–7 times a second and its own simulation moves the entities several hundred units away
again before the next snapshot (max pre-apply error 435 → 2031 units). With the AI not actually frozen
(M3), the snapshot is a correction that is immediately undone.

### 4.3 Screenshot pairs (`research\e11_shots\`)

| pair | files | what it shows |
|---|---|---|
| Run A end (`m4a_end_*`) | `m4a_end_host_shot_153.png` / `m4a_end_join_shot_151.png` | host radar: 2 faint edge contacts, flat horizon; join radar: ~20 contacts plus ~15 falling munition blips. Both HUDs `35888`, `RA 180/36`, `0 km/h` |
| Run B end (`m4b_end_*`) | `m4b_end_host_shot_135.png` / `m4b_end_join_shot_134.png` | host: `125 km/h`, `RA 144/21` (it fired), ~12 radar contacts; join: `0 km/h`, `RA 180/36` (never fired), ~40 radar contacts and a sky full of munitions |
| M5 end (`m6_end_*`) | `m6_end_host_shot_088.png` / `m6_end_join_shot_088.png` | host: `RA 180/36`, `0 km/h` (its own AC1 idle), ~12 contacts; join: `RA 144/0` — it emptied both weapons — radar almost empty |

**Answering the three visual questions plainly**

* *Does the join view show enemies where the host has them?* **Only partly.** The entity **set** is
  identical (320/320 common every second) and the entity positions are written correctly, but the two
  views' radar pictures differ substantially at the same wall second — and the pair is not the same
  mission age either (the two instances enter the mission 13–37 s apart), so the pictures cannot be
  compared one-to-one anyway.
* *Do written positions stick, or snap back/jitter?* **They snap back.** Measured, not inferred: the
  pre-apply error on the join grows to 1771–2031 units, i.e. by the time the next snapshot lands the
  entity has been driven hundreds of units away from where the last snapshot put it. The write is
  durable (0 skipped) but the local simulation immediately re-drives the entity.
* *Do dead enemies (hp 0 on host) look dead/absent on join?* **They are dead on the join too**: hp is
  replicated, and at the end of the M5 session both instances have **exactly 25 of 320** entities at
  hp 0, with only 4.2 % of individual hp values differing at a matched second. What they *render* as was
  not inspected frame by frame — **NOT DETERMINED** (the host's dead units have a collapsed rotation
  row and the join receives that row, so they should render collapsed).

---

## 5. M5 — damage direction (measure only)

Clean run (`m6`): both instances in m010, sync on, join AI freeze on, the **join's** p0 held `A` with a
slow right-stick sweep for 40 s and **the host completely idle** (nothing written to the host's cmd
file).

| question | measurement | answer |
|---|---|---|
| does the host's copy of the enemies lose hp? | host hp drops: **0** before t=150, then **12** (150–160) and **13** (160–170); 25 of 320 host entities reach hp 0 by the end | **YES** — the host's entities die during the window, starting ~3 s after the join's fire begins |
| does the host's AC2 fire at all? | the host's own AC1 **never fired**: its HUD reads `RA 180/36` (full) at the end of the window while the join's reads `RA 144/0`; the host had no local pad input at all, so its only pad-driven actor is AC2 (port 1 = the replicated pad) | **Indirect YES** — damage on the host correlates exactly with the join's fire and the host's own AC1 fired nothing; AC2's own ammo is **NOT DETERMINED** (no ammo instrument) |
| does the join AC1's AP differ from the host AC2's AP? | host AC2 body `0x10d9c100`: 61 samples, min = max = **35888.0**; join AC1 body `0x13009780`: 40 samples, min = max = **35888.0** (and the same in the earlier `m5` session: both ACs 35888 for every sample) | **NO** — they stay equal at full AP; neither AC took damage in any run of this session |

---

## 6. Files changed

| file | change |
|---|---|
| `ac4-port\src\runtime\game\coop_net.h` / `.cpp` | **packet type 3**: `SnapshotRecord {u32 addr, u16 ord, pos[3], rot[9], hp}`, 60-byte wire record, ≤1200-byte datagrams (19 records), `send_snapshot` / `take_snapshot` / `snapshot_totals` / `snapshot_tag`, `role_host()` / `role_join()`, send/receive buffers enlarged to 1200 bytes, snapshot counters appended to the per-second `COOP NET` line |
| `ac4-port\src\runtime\game\coop_entities.cpp` / `.h` | the **address** key (the 0x7080-stride slot removed), the `0x10000000` entity floor, the one-time `COOP POOL` table, the new meter format (`<t> <addr> <id> <vt> <x> <y> <z> <hp> <alive> <ord>`, header `lowest=`), the 10 Hz host publisher, the per-frame join applier, the per-second `COOP SNAP` lines, `AC4_COOP_NOSNAP`; the walk is now also enabled by the link itself |
| `ac4-port\src\runtime\game\input_hooks.cpp` | M3: `ai_freeze_on()` + the `unit_ai` skip with its `AI FREEZE` evidence lines; `AC4_COOP_FREEZE` |
| `run\coop_launch.ps1` | echoes `AC4_COOP_FREEZE` and `AC4_COOP_NOSNAP` |
| `run\coop_divergence.py` | address keys, median/p90/max, `>10 units` count, `--buckets`, `--auto-offset` (mission-age alignment), `--from` / `--to` |
| `run\coop_pool_compare.py` | **new** — compares the two `COOP POOL` tables |
| `run\coop_motion.py` | **new** — displacement over a window (M3) |
| `run\coop_movers.py` | **new** — divergence restricted to entities that actually move |
| `run\coop_window.py` | **new** — parallel boot of both instances, mission detection from the entity walk, the timed windows (idle / drive / fire / m4m5 / drive_fire), screenshot pairs into `research\e11_shots` |

Everything new is behind the existing environment variables. With none of them set the game is
unchanged; with the link set, `AC4_COOP_NOSNAP=1` disables applying and `AC4_COOP_FREEZE=1` (join only)
enables the AI skip.

## 7. NOT DETERMINED

1. **Why the freeze does not stop movement.** The `unit_ai` loop covers only 6 objects of class
   `0x101475c8` in m010; how the 320 `0x10142638` entities are driven was not found.
2. **`SetEnableMove` / `SetEnableAttack`.** The wrappers' register usage is read but the receiver class
   (`self+0xa0`) and the value polarity are not; not attempted (§3.3).
3. **Whether the join player's pad actually makes the host's AC2 fire**, as opposed to the damage coming
   from the mission's own munitions: the temporal correlation is exact and the host's AC1 fired nothing,
   but AC2's ammo/projectiles are not instrumented.
4. **What a dead entity looks like on the join** — hp is replicated and the dead sets match exactly, but
   no frame-by-frame visual check of a specific corpse was done.
5. **A "host drives" baseline without sync.** Run B is compared only against the idle baseline.
6. **10 Hz snapshots.** The measured cycle is 5–7 snapshots/s because both instances run at 23–40 fps;
   whether a single-instance-rate 10 Hz helps was not measured.
7. **Interpolation / prediction** — not implemented, by design ("no smoothing in v0").
8. **The mission tag.** Type 2's tag is still always 0; type 3's `tag` carries the host's lowest entity
   address (a layout fingerprint), not a mission id — nothing cheap was found to fill a real one.
