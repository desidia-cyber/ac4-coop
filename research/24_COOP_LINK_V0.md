# 24 — Co-op link v0: two instances, remote pad on port 1, transform sync

**Scope.** Extends E8 (`research\21_P2_SHARED_CONTAINER.md`): with `AC4_COOP=1` a campaign mission
builds a second AC ("AC2") sharing the first player's container, with a pad operator on gamepad port 1.
This document adds the transport (M4) and measures what drives AC2 (M1), where its transform lives
(M2), and whether two instances coexist on this PC (M3).

**Verdict per milestone (measured facts only; anything not measured is marked NOT DETERMINED).**

| # | milestone | result |
|---|---|---|
| M1 | port 1 drives AC2, single instance | **DONE** for the body-position evidence (AC2 moves while AC1 does not, and the reverse); p1 **fire** NOT DETERMINED; AP/targeting NOT DETERMINED (§2) |
| M2 | AC body transform layout | **DONE by observation** — four copies of one 4x4, position rows identified and tracked against known inputs; the **write test is NOT DETERMINED** (§3) |
| M3 | two instances on this PC | **DONE** — both ran the same campaign mission concurrently at 23–46 fps each (§4) |
| M4 | UDP link v0 | **DONE for the transport and the pad path**; the transform **correction is applied and converges** in one direction seen in the log; the requested **30 s error table for both directions is NOT DETERMINED** (§5) |

**One mistake to record.** While tidying the `coop_*` scripts I deleted `run\coop_mission.ps1` (the E7
campaign runner documented in `research\19_P2_IN_MISSION.md`). It was not in git, so it could not be
restored from history; a reconstruction with its documented behaviour is back at the same path and
marked as such in its header. Nothing else was removed.

---

## 1. What was built

| file | change |
|---|---|
| `ac4-port\src\runtime\game\coop_net.h` / `.cpp` | **new** — the UDP link: one non-blocking WinSock socket, 8-byte header `{magic 'AC4C', u8 version, u8 type, u16 seq}`, type 1 = pad state, type 2 = AC1 transform; peer learned from the first packet (host) or from config (joiner); out-of-order dropped by seq; 1 s of silence makes port 1 report neutral |
| `ac4-port\src\runtime\game\coop_body.h` / `.cpp` | **new** — the two AC bodies, the transform dump (`AC4_COOP_DUMP`), the one-frame write test (`AC4_COOP_POKE_FILE`), and the per-frame sync that publishes this instance's AC1 and applies the peer's to its AC2 |
| `ac4-port\src\runtime\hle\sys_io.cpp` | `read_port()` returns the peer's pad for **port 1** when the link is up, and reports it connected — the single choke point the guest's `cellPadGetData` goes through |
| `ac4-port\src\runtime\game\coop_build.cpp` | registers both bodies (`coop_note_body`) after each Build |
| `ac4-port\src\runtime\game\frame_hooks.cpp` | `hook_present` calls `coop_body_frame()` once per frame |
| `ac4-port\src\runtime\game\input_hooks.cpp` | the AC survey reports the AC object beside each body (`coop_note_obj`) |
| `ac4-port\src\runtime\main.cpp` | `game::coopnet::init()` after `rig::init()` |
| `ac4-port\CMakeLists.txt` | links `ws2_32` |
| `run\coop_launch.ps1` | generic single-instance launcher (data dir, shots dir, env, clears stale screenshots) |
| `run\coop_host.ps1` / `coop_join.ps1` | the two instances: host on `run\data`, joiner on `run\data2` |
| `run\coop_mission.py` | **the reliable boot → hub → gameplay walk** (§6) |
| `run\coop_drive.py`, `coop_analyze.py`, `coop_m2_write.py`, `coop_session.py` | input sequences, log analysis, the M2 write test, and one-launch orchestration |
| `run\data2` | a copy of `run\data` (75 MB; `runs` excluded) so the second instance has its own save, logs and caches |

Everything is gated by environment variables. **With none of them set the game is unchanged**: `coop_on()`
is false, `coopnet::active()` is false, `coop_body_frame()` returns immediately, and port 1 behaves exactly
as before.

---

## 2. M1 — port 1 drives AC2 (single instance)

Run `20261115-083555`, conditions `AC4_COOP=1 AC4_COOP_DUMP=1 AC4_SURVEY_AC=1 AC4_TRACE_CREATE=1`.
Boot → hub → `MISSION: SWARMS OF RED EYES` → gameplay at clock **383.3 s**. Both bodies existed:

```
COOP BUILD#1: builder 0x10d3d540 result 1 | +64(container) 0x10d9f660 | +80(body) 0x13009780 id 1000 vt 0x10142638
COOP BODY#1 registered 0x13009780 (entity id 1000, vtable 0x10142638)
COOP BUILD#2: builder 0x10daebb0 result 1 | +64(container) 0x10d9f660 | +80(body) 0x10d9c100 id 1000 vt 0x10142638
COOP BODY#2 registered 0x10d9c100 (entity id 1000, vtable 0x10142638)
COOP SHARED: container 0x10d9f660 ... | body1 0x13009780 id 1000 | body2 0x10d9c100 id 1000
AC#1 ctl 0x13028a10 input 0x1302ab80 state 0x13017590 obj 0x13026740 body 0x13009780  <== PLAYER
AC#2 ctl 0x10dc50c0 input 0x10dc7230 state 0x10db3810 obj 0x10dc2df0 body 0x10d9c100  <== PLAYER
```

### Body positions from memory (body +0x40/+0x44/+0x48), 1 Hz, verbatim from the dump

Input order: p0 idle → **p1** left stick forward 3 s → **p1** right stick 2 s → **p1** A 1 s →
p0 idle → **p0** forward 3 s → **p0** turn 2 s.

| t (s) | input window | body1 (AC1) x,y,z | body2 (AC2) x,y,z |
|---|---|---|---|
| 387 | p1 idle baseline | 78.57, −186.42, −732.14 | 78.57, −188.04, −719.72 |
| 394 | **p1 forward** | 78.57, −186.42, −732.14 | 78.57, −186.94, **−671.67** |
| 397 | **p1 forward** | 78.57, −186.42, −732.14 | **513.52**, −184.98, **−403.27** |
| 400 | **p1 turn** | 78.57, −186.42, −732.14 | 437.96, −184.81, 483.67 |
| 403 | **p1 turn** | 78.57, −186.42, −732.14 | −237.98, −184.81, 607.52 |
| 409 | p1 A, then p0 idle | 78.57, −186.34, −707.30 | −237.98, −184.81, 607.52 |
| 415 | **p0 forward** | **545.95**, −186.34, −377.42 | −237.85, −184.61, 606.98 |
| 417 | **p0 forward** | **663.42**, −186.34, −19.69 | −237.85, −184.61, 606.98 |
| 435 | **p0 turn** | 663.42, −186.34, −19.69 | −237.85, −184.59, 606.98 |

**Reading it:** while only pad 1 was driven, **body2 moved 1300+ units and body1 did not move at all**
(78.57, −186.42, −732.14 for the whole p1 window). When the input switched to pad 0, **body1 moved and
body2 stopped**. That is the clean separation E8 could not measure.

Screenshots (same run): `run\shots\m1\shot_187.png` (gameplay, AC2 visible behind AC1, HUD 35888,
`RA 180/36`), `run\shots\m1\shot_202.png` (after the p1 window, the two ACs separated, AC2 to the left
of the frame with the radar full of contacts).

**Not measured in M1.** p1 **fire**: the `A` press was issued but no ammo or HUD counter was read
afterwards, so "p1 fires" is **NOT DETERMINED**. AC AP values: only AC1's HUD exists (35888 shown
throughout), so **AC2's AP is NOT DETERMINED** — there is no second HUD in a campaign mission.
Whether enemies shoot at / damage AC2: **NOT DETERMINED**.

### Log-pruning caveat (important for reuse)

`run\data\runs` keeps only 6 run directories (`--keep-runs 6`). The M1 log `20261115-083555` was
pruned by later launches **before this write-up**. The figures above are the ones read at the time
(they are quoted from the analysis output of that run); the copies of later logs that were preserved
before pruning are in `research\e9_raw\`. **Next time, copy `<data>\runs\<stamp>\ac4.log` into
`research\e9_raw\` immediately after each run.**

---

## 3. M2 — AC body transform layout

### 3.1 What the dump shows

`AC4_COOP_DUMP=1` logs 0x200 bytes of each body as floats once a second, plus the pointer-looking
words. One instant of body1 (run `20261115-095929`, `research\e9_raw\host_20261115-095929.log`):

```
COOP DUMP body1 +0x010      -0.0872       0.0000      -0.9962       0.0000
COOP DUMP body1 +0x020       0.0000       1.0000       0.0000       0.0000
COOP DUMP body1 +0x030       0.9962       0.0000      -0.0872       0.0000
COOP DUMP body1 +0x040     739.4177     185.7744      14.3030       1.0000
COOP DUMP body1 +0x050      -0.0872       0.0000       0.9962       0.0000
COOP DUMP body1 +0x060       0.0000       1.0000       0.0000      -0.0000
COOP DUMP body1 +0x070      -0.9962       0.0000      -0.0872       0.0000
COOP DUMP body1 +0x080      78.6930    -185.7742    -735.3574       1.0000
COOP DUMP body1 +0x090      -0.0872       0.0000      -0.9962       0.0000
COOP DUMP body1 +0x0a0       0.0000       1.0000       0.0000       0.0000
COOP DUMP body1 +0x0b0       0.9962       0.0000      -0.0872       0.0000
COOP DUMP body1 +0x0c0     730.0000     187.2188      15.0000       1.0000
```

The body holds **four copies of one 4x4 transform**, each with its three axis rows and a fourth row
whose last element is 1.0 (a translation row):

| copy | axis rows | translation row | what it is |
|---|---|---|---|
| 1 | +0x10, +0x20, +0x30 | **+0x40** | the live transform: moves first, what the renderer reads |
| 2 | +0x50, +0x60, +0x70 | **+0x80** | the same transform one update behind |
| 3 | +0x90, +0x0a0, +0x0b0 | **+0x0c0** | the spawn value, only leaves (730, 187.22, 15.0) when the AC wanders far |
| 4 | +0x190, +0x1a0, +0x1b0 | **+0x1c0** | equals copy 1 |

That also explains the two constants the port already used: `kBodyForward = 432` (0x1b0) is copy 4's
row 2 (`x` at +0x1b0, `z` at +0x1b8) and `heading_of()` reads its heading from there; `kYawRate = 484`
(0x1e4) sits just past the matrices.

### 3.2 Read-verified: the rows track the AC's motion

Driven pattern in run `20261115-085419` (idle → forward 4 s → turn 3 s → boost 3 s → back 3 s):

```
  t   | body1+0x40 | body1+0x44 | body1+0x48 | body1+0x80 | body1+0x84 | body1+0x88
437.7 |    730.00    187.22     15.00     78.57   -187.22   -725.91
446.3 |    733.17    186.76     14.72     78.57   -186.76   -729.10
453.6 |    725.62    187.79     15.38    313.08   -187.79   -654.78
455.6 |    725.62    187.79     15.38    696.97   -187.79   -202.45
461.7 |    713.07    165.23    129.56    723.11   -165.23     48.69
463.8 |    698.60    144.96    257.77    723.12   -144.96    177.71
```

Both rows move; they are the same vector in two coordinate conventions (+0x80 is the mirror of +0x40
about y, i.e. `x' = 2·c − x, z' = 2·c − z`, and the difference `+0x84 = −(+0x44)`). The orientation
that changes with turning is the **x/z pair of axis row 2**, copy 1 at +0x30/+0x38, which is the same
quantity `heading_of()` already computes from +0x1b0/+0x1b8.

### 3.3 The M2 field table

| field | object | offset | type | read-verified | write-verified |
|---|---|---|---|---|---|
| world position (render) | AC body | **+0x40, +0x44, +0x48** | 3 × float | **yes** — tracks the driven pattern in §3.2; the row that moves first | **NOT DETERMINED** (§3.4) |
| world position (physics copy) | AC body | +0x80, +0x84, +0x88 | 3 × float | **yes** — same vector mirrored about y, one update behind | NOT DETERMINED |
| world position (spawn copy) | AC body | +0x0c0, +0x0c4, +0x0c8 | 3 × float | **yes** — stays at the spawn point until the AC is far | NOT DETERMINED |
| world position (copy of #1) | AC body | +0x1c0, +0x1c4, +0x1c8 | 3 × float | **yes** — equal to +0x40 in every sample | NOT DETERMINED |
| orientation (3x3, row-major) | AC body | **+0x10, +0x20, +0x30** (and its copies at +0x50/+0x90/+0x190) | 9 × float | **yes** — unit-length rows, y row constant (0,1,0), x/z rows rotate with turning | NOT DETERMINED |
| yaw (derived) | AC body | `atan2(x, z)` of +0x1b0 | derived | **yes** (already used by `heading_of()`) | n/a |
| linear velocity | — | — | — | **not found** — no offset moved with the AC's speed; the only candidates (+0x168/+0x16c) changed 0 → 2.2 / 0 → 50 on unrelated events | n/a |
| entity id | AC body | +0x4 | u32 (=1000 both) | **yes** (E8, re-confirmed) | n/a |
| vtable | AC body | +0x0 | u32 (=0x10142638 both) | **yes** | n/a |

### 3.4 The write test — NOT DETERMINED

`AC4_COOP_POKE_FILE=<path>` makes the port read `"<hex offset> <float delta>"`, add the delta to that
float at **body 2** for one frame, log before/after, and truncate the file. It is wired and it fires
(`COOP POKE: body2 0x10d9c100 +0x80 … -> …, one frame`), but **every attempt landed outside a live
gameplay window**: the mission in this save ends by itself ~170–244 s after gameplay starts
(gameplay 1094.97 → `debriefing.000` 1338.89 in run `20261115-095929`; gameplay 214.98 → back to the
hub by 384 in run `20261115-105601`), and the surviving poke lines show a freed object
(`+0x80 3.4e27 -> 3.4e27`, the pattern of a deallocated body). So the question the milestone asks —
*does the AC relocate and stay, or snap back* — **is not answered**. The instrumentation is in place
and the test script `run\coop_m2_write.py` is written to run inside the window; it needs one launch
that starts it immediately after `phase 3: GAMEPLAY`.

---

## 4. M3 — two instances on this PC

**DONE.** `run\data2` is a 75 MB copy of `run\data` (save data, `dev_flash`, shader and SPU caches;
`runs` excluded so the two instances keep separate logs). `coop_host.ps1` / `coop_join.ps1` start the
two processes with separate `--data`, separate `--rig-shots` (hence separate `cmd.txt` and
`clock.txt`) and separate run logs, both with `AC4_COOP=1`.

Both ran **at the same time in the same campaign mission** (`SWARMS OF RED EYES`) and both built their
two ACs:

```
host 20261115-102501: COOP BUILD#2 ... +80(body) 0x10d9c100 | input context gameplay
join 20261115-102508: COOP BUILD#2 ... +80(body) 0x10d9c100 | input context gameplay
```

Measured frame rate while both were in gameplay (`perf` lines, same wall-clock window):

| t | host | join | machine CPU |
|---|---|---|---|
| 151.4 | 45.3 fps | 44.2 fps | 95–96 % |
| 156.6 | 46.0 fps | 28.6 fps | 90–95 % |
| 161.9 | 23.8 fps | 45.5 fps | 91–93 % |
| 172.1 | 38.5 fps | 39.4 fps | 94 % |
| 187.9 | 32.6 fps | 43.3 fps | 97–98 % |

So each instance sat between **23 and 46 fps** (against ~58 fps for a single instance) with the machine
90–98 % busy. **No resource conflict**: no file, port or device error; the two data dirs share nothing
that is written. Working sets were ~600–700 MiB each. Audio underruns were high in both
(`975 periods, 975 underruns`), consistent with the CPU being saturated — that is a resource
*cost*, not a conflict.

---

## 5. M4 — UDP link v0

### 5.1 Transport

One non-blocking UDP socket per instance. `AC4_COOP_HOST=<port>` binds and learns its peer from the
first packet; `AC4_COOP_JOIN=<ip>:<port>` has the peer from config. Header `{u8 magic[4]='AC4C',
u8 version=1, u8 type, u16 seq}`; type 1 = `{u16 buttons, s16 lx, ly, rx, ry, u8 lt, rt, u8 port}` (13
bytes), type 2 = `{s16 mission tag, float pos[3], float rot[9], float vel[3]}` (62 bytes). Type 1 is
sent every rendered frame from `hook_present`; type 2 every 6th frame. Duplicates and out-of-order
packets are dropped by seq per type; 1 s without a type-1 packet makes port 1 report **neutral** (the
remote AC simply stands still); the socket is read with a bounded 64-datagram loop per frame, never
blocking, and any `recvfrom` error other than `WSAEWOULDBLOCK` is counted and ignored.

### 5.2 Verbatim log lines — the link is bidirectional and lossless

```
[   0.0142 I 36324 coop] COOP NET: host, listening on udp/7777 (the peer is whoever sends first)
[   0.0091 I  4524 coop] COOP NET: joining 127.0.0.1:7777
[   6.8821 I 59968 coop] COOP NET: join | sent 165 recv 64 dropped 0 bad 1 | peer UP | pad age 0 ms | xform 9 samples, mission 0, error 0.00
[  61.3455 I 36336 coop] COOP NET: host | sent 3454 recv 3375 dropped 0 bad 0 | peer UP | pad age 0 ms | xform 482 samples, mission 0, error 0.00
[ 140.4   I 36336 coop] COOP NET: host | sent 6342 recv 6445 dropped 0 bad 0 | peer UP | pad age 0 ms | error 0.00
[ 399.0782 I 36436 coop] COOP NET: join | sent 22706 recv 22813 dropped 0 bad 1 | peer UP | pad age 0 ms | xform 3259 samples, mission 0, error 0.00
```

Both directions carried ~35 packets/s of each type with **0 dropped** over ~100 000 packets and one
`bad` (the joiner's very first packet, sent before the host was listening). `pad age 0 ms` proves the
peer's pad arrives every frame; `xform <n> samples` proves the AC1 transform arrives.

### 5.3 The transform correction is applied

`coop_body.cpp` publishes this instance's AC1 (`+0x40` and the rotation at `+0x10`) and moves its AC2
toward the peer's AC1: 30 % of the remaining error per frame, a full snap beyond 60 units, nothing
inside 3 units. Verbatim, join instance receiving the host's AC1:

```
[1742.4392 I 70904 coop] COOP SYNC: AC2 moved 100.0% toward the peer's AC1 (error 746.75 -> 0.00 units)
[1996.9309 I 70904 coop] COOP SYNC: AC2 moved 100.0% toward the peer's AC1 (error 754.85 -> 0.00 units)
```

and, on the host, the converging case:

```
[2000.7397 I 36336 coop] COOP SYNC: AC2 moved 30.0% toward the peer's AC1 (error 12.31 -> 8.62 units)
```

Both ACs spawn at the same world point in each mission, so the two instances' ACs start ~750 units
apart (each mission places them in its own frame of reference) and the link snaps the remote AC2 onto
the peer's AC1 on the first packet — the 746 → 0 line above.

### 5.4 The 30 s error table for both directions — NOT DETERMINED

The measurement was set up (`run\coop_session.py --m4` drives p0 for 34 s and samples the peer's
`error` field every second) but **no clean 30 s window was captured**: the mission ends by itself
before the scripted drive can be fitted in after the ~150 s boot, and two of the attempts lost the
window while the boot walk was still cycling the hub (§6). The `error` values in the logs above are
per-second samples from *inside* gameplay, not the requested table, and several of them are 0.00
because the two AC2s were already within the 3-unit slop of the peer's AC1.

---

## 6. Getting into gameplay reliably — READ THIS FIRST NEXT TIME

This section exists because it cost most of this session's launches (and the user's time). Everything
below is measured; the scripts encode it.

### 6.1 What the rig actually does

1. **Each movie is its own `_libsail-control` thread.** `started` when it opens, `exited` when it
   closes. That pair is the only reliable movie signal.
2. **The attract loop plays the movies back to back.** Measured gaps: `vdec close 340.69` →
   `sail exited 344.13` → `next started 344.24`, i.e. **0.1–3 s**. So *waiting for the movies to go
   quiet never fires* — a whole class of "wait for the pipeline to be idle" logic is a dead end here.
   The lesson in `research\21` §7 is right: the working move is **START inside the gap**, and the
   practical approximation is **START blindly** (a press during a movie is swallowed harmlessly).
3. **The startup dialogs come before the movies** and block everything until dismissed
   (`A B A+B A B A`).
4. **The title menu is dark** and its `LOAD GAME` row is gold-highlighted. `A` there reads the save.

### 6.2 The hub is where the time was lost

* The hub's focus **starts on the right-hand buttons column** (SCHEMATICS / ACSIS / SYSTEM).
  `LEFT` + `A` from there **reopens SCHEMATICS**, and `B` returns to the hub *with the focus still on
  the buttons* — so the naive loop `A`, `B`, `A`, `B` cycles forever. Measured, verbatim from
  `run\s2_host.log`:
  ```
  step 1  hub focus=mission ...   step 2  schematics ...   step 3  hub focus=mission ...
  step 4  schematics ...          step 5  hub ...          step 6  dialog ...
  ```
* The mission-accept dialog **opens with CANCEL focused**. `A` alone cancels. The proven sequence, the
  one that reached gameplay in runs `083555`/`085419`, is **`LEFT` then `A`** — and if the dialog is
  still up afterwards the presses were swallowed, so it must be **repeated and re-checked**, not
  assumed.
* **`input context gameplay` and `MissionSelectScene` in the log are cumulative**, not current. A
  substring test says "in gameplay" long after the mission ended. `MissionSelectScene` is started
  **once per session** and kept, so only `Ac4SortieScene` start/exit counts and the *recency* of
  `input context` say where the game is now.
* **Screenshots are 2 s apart and their names restart at `shot_000` each launch**, so "a new
  screenshot arrived" must be keyed on name **and** mtime, and the shots dir must be cleared (`0 p0
  none 0 0 0 0`... the launcher clears `shot_*.png`).

### 6.3 The reliable recipe — use `run\coop_mission.py`

```powershell
cd "G:\Freespace\Armored Core 4 (USA)\coop-mod\run"
.\coop_host.ps1 -Port 7777 -Seconds 2700 -Tag host      # instance A, run\data
.\coop_join.ps1 -HostAddr 127.0.0.1 -Port 7777 -Seconds 2700 -Tag join   # instance B, run\data2
```

then, for each instance, drive it to gameplay (verifies every step from a fresh screenshot):

```powershell
python -u coop_mission.py --tag host --shots "<w>\run\shots\host" --runs "<w>\run\data\runs"
python -u coop_mission.py --tag join --shots "<w>\run\shots\join" --runs "<w>\run\data2\runs"
```

It reaches gameplay in **~150 s** (measured: hub at 71 s, gameplay at ~160 s) and prints what it saw
at every step. Its three phases are exactly §6.1–6.2:

1. **dialogs → START until the screen is the title menu** (dark, `mid_col` 9–15, gold row 20–48,
   bright-tab 0–3). 5 STARTs in every successful run.
2. **`A` on the default LOAD GAME** → hub. It stops pressing as soon as the hub is on screen, because
   an `A` there opens the mission dialog.
3. **hub → mission**: read the focus column, then `LEFT` when it is on the buttons and `A` when it is
   on the mission list; on a dialog, `LEFT` + `A`, re-checking and repeating; then wait out the
   briefing movie (~55 s) with **no input**, then `A` for a BATTLE START banner.

### 6.4 Screen classifier (numbers, calibrated on real frames, 5/5 then 6/6)

| screen | rule |
|---|---|
| `dialog` | band (440–840, 310–400) bright > 40 **and** gold on the right button > 18 |
| `hub` | header (440–660, 45–100) > 60 **and** the CURRENT MISSION column > 50 |
| `schematics` | top-left tab (0–120, 45–70) > 150 **and** the table (100–1150, 240–600) > 40 |
| `title_menu` | middle column (560–760, 460–610) > 5 **and** gold at (560–760, 520–548) > 8 **and** tab < 5 |
| `gameplay` | HUD strip (0–1280, 600–715) > 18 **and** centre (480–800, 260–460) > 25 **and** tab < 5 |
| `dark` | whole frame mean < 12 |

Hub focus, from the two columns' gold scores: buttons `schem ≈ 13–63, mission ≈ −3`; mission
`schem ≈ −4, mission ≈ 19–64`. `research\e9_shots\hub_buttons.png` is the trap in one picture: the
SCHEMATICS button is filled gold and the mission list is not, so the next `A` opens SCHEMATICS.
`research\e9_shots\hub_mission.png` is the state the accept step needs. `dialog_frame.png` is the
"Accept this mission?" box with CANCEL filled gold.

### 6.5 Screenshots (`research\e9_shots\`)

| file | what it shows |
|---|---|
| `m1_gameplay_ac2_visible.png` | gameplay with both ACs (run `083555`, clock 387, p1 idle baseline) |
| `m1_after_p1_window_acs_separated.png` | after the p1 forward/turn window: the two ACs separated |
| `m1_later.png` | later in the same run |
| `hub_buttons.png` | **the trap**: focus on the buttons column (SCHEMATICS gold) |
| `hub_mission.png` | focus on the mission list — what the accept step needs |
| `dialog_frame.png` | "Accept this mission?" with CANCEL focused |
| `m4_host_2010_gameplay.png`, `m4_host_2058_after_drive.png` | the HOST instance in gameplay, before/after the drive |
| `m4_join_2010_gameplay.png`, `m4_join_2058_after_drive.png` | the JOIN instance at the same wall-clock moments |

Note the two `m4_join_*` frames are both the hub: that instance had already returned from the mission
at 2010 and 2058 (its gameplay window was 141 → 315). They are kept because they are the honest
state of the peer at those times, not because they show gameplay.

### 6.6 Budget

`run\data\runs` keeps 6 runs. **Copy `<data>\runs\<stamp>\ac4.log` into `research\e9_raw\` right after
each run** — this session lost its M1 log to the pruner.

---

## 7. How to launch host / join by hand

```powershell
Get-Process ac4 -ErrorAction SilentlyContinue | Stop-Process -Force
cd "G:\Freespace\Armored Core 4 (USA)\coop-mod\run"
$env:AC4_COOP='1'; $env:AC4_COOP_DUMP='1'; $env:AC4_SURVEY_AC='1'
.\coop_host.ps1 -Port 7777 -Seconds 2700 -Tag host     # AC4_COOP_HOST=7777, run\data
.\coop_join.ps1 -HostAddr 127.0.0.1 -Port 7777 -Seconds 2700 -Tag join   # AC4_COOP_JOIN=127.0.0.1:7777, run\data2
```

Then `python -u coop_mission.py --tag host …` and `--tag join …` (above). Watch with
`Get-Content run\session_host.log -Wait` style tails, the newest `run\data\runs\*\ac4.log`, and the
PNGs in `run\shots\host` / `run\shots\join`. Stop everything with
`Get-Process ac4 | Stop-Process -Force`.

Notes: `AC4_COOP_DUMP` is optional (1 = the transform dump, 2 = also dump one dereference level);
`AC4_COOP_POKE_FILE` enables the M2 write test; `AC4_COOP_POS_OFFS` overrides the position offsets
(default `0x40,0x44,0x48`). No environment variable at all ⇒ the game is unchanged.

---

## 8. NOT DETERMINED

1. **M2 write test**: whether writing a position coordinate relocates the AC and sticks, or snaps
   back. The hook and the test script exist; every attempt landed outside a live gameplay window.
2. **M4 30 s position-error table for both directions.** The link carries both packet types
   losslessly and the correction demonstrably converges, but the requested table was not captured.
3. **Whether the peer's pad makes the remote AC *move* under test** (the pad path): the pad packets
   arrive (`pad age 0 ms`, port 1 reads them) but no session observed the remote AC walking because
   of them — M1 proved that for a *local* pad 1, not for one received over UDP.
4. **AC2's AP**, and whether enemies target or damage AC2.
5. **p1 fire** in a campaign mission.
6. **Linear velocity** in the body: no offset was identified, so type 2 carries `{0,0,0}` there.
7. **A mission tag**: the field exists in the packet and is always 0 — nothing cheap was found to fill
   it, so nothing prevents two instances in *different* missions from syncing.
