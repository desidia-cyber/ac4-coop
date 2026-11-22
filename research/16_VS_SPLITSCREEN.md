# 16 — Offline 2-player SPLIT-SCREEN versus: reached and proven pad1-driven

**Status: CONFIRMED.** AC4's offline 2-player split-screen versus mode is present, reachable
from the title menu without loading story progress, runs a full match, and **pad port 1
independently drives the second AC**. Evidence below is from screenshots I opened and log
lines copied verbatim from `run\data\runs\20261115-022935\ac4.log`.

Method: the rig (`research\07_RIG.md`) driving a **closed loop** — every input is followed by a
fresh screenshot that is read numerically before the next input is chosen. No step advances on
a blind fixed delay. Run script: `run\vs_split.ps1`.

---

## (a) Exact menu path to offline versus

From boot, the whole route is **title menu → MULTIPLAYER → SPLIT-SCREEN**. Loading story
progress is *not* required to reach the mode, but the two players' ACs come from save data
(`ACDES.DAT`), so answering YES to the load prompts gives both sides a real `TYPE-HOGIRE`.

`run\vs_split.ps1` reproduces this unattended. The rig command lines it emits, in order
(port `p0` = pad 0; the numbers are ms durations, not absolute times — see §Rig notes):

```
# 1  startup: clear "HDD access indicator" / update dialogs
160 p0 A       2200 p0 none
160 p0 B       2200 p0 none
160 p0 A+B     2200 p0 none
160 p0 A       2200 p0 none
160 p0 B       2200 p0 none
160 p0 A       2200 p0 none

# 2  attract movie -> title menu -> MULTIPLAYER   (retried until the panel's gold cursor appears)
160 p0 START   3000 p0 none
200 p0 DOWN    1400 p0 none
240 p0 A       3000 p0 none

# 3  "Load game progress data?" -> YES
240 p0 A       1800 p0 none

# 4  walk the MULTIPLAYER panel to SPLIT-SCREEN, verified from the screenshot each step
200 p0 DOWN    1400 p0 none      # repeat until the gold band is on SPLIT-SCREEN

# 5  enter SPLIT-SCREEN setup
260 p0 A       3000 p0 none

# 6  "Load 1P data?" YES, then "Load 2P data?" YES
260 p0 A       2600 p0 none
260 p0 A       2600 p0 none

# 7  START (match), then "Commencing multiplayer match." -> YES
280 p0 A       2600 p0 none
280 p0 A       6000 p0 none

# 8  clear the BATTLE START banner
220 p0 A       2500 p0 none
```

The menu tree as seen on screen:

```
Title menu                     NEW GAME / LOAD GAME / MULTIPLAYER   (LOAD GAME is the default)
└─ MULTIPLAYER                ONLINE / SYSTEM LINK / SPLIT-SCREEN / SYSTEM
   │                          (each entry shows a one-line description on hover)
   ├─ ONLINE                  requires PSN  (descriptions: "Online multiplayer match using
   ├─ SYSTEM LINK               the PLAYSTATION®Network"; SYSTEM LINK "Connect to another
   │                          PLAYSTATION®3 system wirelessly, or with a network cable")
   └─ SPLIT-SCREEN            "Multiplayer match using a divided screen on one
       │                       PLAYSTATION®3 system."
       ├─ START               → "Commencing multiplayer match." YES/NO
       ├─ SCHEMATICS          pick each player's AC
       ├─ MAP                 BATTLE FIELD (DESERT here)
       ├─ TIME                5 MIN
       └─ SYSTEM              save/load/options
```

On entry the game itself asks **"Load 1P data?"** and then **"Load 2P data?"** — two separate
prompts for two players' AC data, which is the game's own confirmation the mode is 2-player.
Both default to YES.

Asset evidence from the log that this is the versus mode (not a story mission):

```
cellFsOpen('/dev_bdvd/PS3_GAME/USRDIR/bind/lang/us/menu/vssortie.000', 0x0) -> 0x0 fd 134
cellFsOpen('/dev_hdd1/bind/lang/us/menu/vssortie.000', 0x41) -> 0x0 fd 135
```

and at match end `.../menu/vsresult.000`. Scene thread names: `MatchSelectManager`,
`VsSortieScene` (two instances).

---

## (b) Answers with evidence

| question | answer | evidence |
|---|---|---|
| versus menu reachable | **YES** | Screenshot `10_menu_split_screen_highlighted.png` shows the MULTIPLAYER panel with SPLIT-SCREEN gold-selected and its description *"Multiplayer match using a divided screen on one PLAYSTATION®3 system."* |
| match starts | **YES** | "Commencing multiplayer match." dialog → YES (`14_…png`); then `vssortie.000` opened and `VsSortieScene` threads run; in-match HUD at 04:46 counting down (`01_versus_match_split_screen.png`) |
| screen is split | **YES** | Viewport is 1280×720; the 3D view occupies x≈32–1248 split into two halves by a seam at x≈640. **Each half has its own complete HUD** — LA / LB / RA / LB ammo, the 35888 AP bar, its own radar with its own ECM readout. Left HUD x≈80–590, right HUD x≈710–1200. A single shared match timer (`04:46`) sits at bottom centre. |
| p1 moves AC2 independently | **YES** | See the movement matrix below: with p1's right stick held, the **right** half changes 71.8 % while the left stays at 0.7 % (idle baseline 0.6 %). |
| p0 moves AC1 independently | **YES** | Same test with p0: **left** half 65.9 %, right 0.9 %. And the fire test: p0's A drained the **left** HUD only (180/36 → 180/32). |

### Split-to-player mapping

Each half carries a small opponent marker over the enemy AC: the **left** view shows `2P 35888`
and the **right** view shows `1P 35888`. So the left viewport is **player 1's camera** and the
right is **player 2's**. Pad 0 drives the left view, pad 1 the right — i.e. **pad 0 = P1,
pad 1 = P2**, which is the natural mapping.

### Movement matrix (pixel diff of the two view halves)

Metric: mean |ΔRGB| per sampled pixel and % of sampled pixels changing by >30, comparing two
screenshots straddling one 3 s input. Both halves sampled identically (x 32–632 and x 648–1248,
y 200–552). Order matters: every row is its own before/after, and `IDLE` rows are the control.

| step | LEFT mean / chg | RIGHT mean / chg |
|---|---|---|
| IDLE1 (no input) | 7.8 / **0.6 %** | 7.8 / **0.6 %** |
| **p1** left-stick forward 3 s | 8.1 / 0.8 % | 12.3 / 5.8 % |
| IDLE2 | 8.1 / 0.7 % | 7.7 / 0.6 % |
| **p0** left-stick forward 3 s | 12.8 / 5.8 % | 8.3 / 0.9 % |
| IDLE3 | 8.6 / 1.0 % | 8.5 / 0.9 % |
| **p1** right-stick right 3 s | 7.9 / **0.7 %** | 110.9 / **71.8 %** |

Forward motion gives a modest change (the camera translates, the AC barely rotates); the
right-stick turn is unambiguous. In every case the **other** half stays at the idle baseline.
The earlier independent run showed the same with a combined forward+turn input: p1 → right
71.5 % / left 8.3 %, p0 → left 65.5 % / right 2.6 %.

### Fire test (ammo in the HUD, tight crops of each half)

Weapon fire is **A (CROSS)** on this build; `RT`/`RB` produced no ammo change for *either* pad,
so they are not the fire button. Crops of both HUDs taken from the same frame each time:

```
p1 fire test (pad 0 idle):
  before                LEFT LA 180/36  |  RIGHT LA 180/36
  during p1 A held      LEFT LA 180/36  |  RIGHT LA 180/31     <- only the RIGHT drains

p0 fire test (pad 1 idle):
  before                LEFT LA 180/36  |  RIGHT LA  72/20
  during p0 A held      LEFT LA 180/32  |  RIGHT LA  72/20     <- only the LEFT drains
```

(The right half already read `72 / 20` entering the second test, having been fired down by the
p1 test; what matters is that it is unchanged while the left drains.)

Cleanest single-shot instance (`02_ammo_before_p1_fire.png` → `03_p1_fire_right_hud_drains.png`):
`180 / 36` → `180 / 31` on the right half while the left half stayed `180 / 36`.

Both ACs are genuinely in one arena: after the exchanges the two AP values are **both below
full** and differ — `35671` (left) and `35526` (right) in `05_both_acs_damaged_in_arena.png`,
i.e. they damaged each other. A full 5-minute match ran to completion and produced
`MATCH RESULTS` — `1 VS 1 / DESERT / MATCH TIME 5:00`, both sides `DRAW`, both at
`35888 / 35888` AP.

---

## (c) Log lines, verbatim

Rig ownership of both pads (this is what makes pad 1 usable at all):

```
[   0.2122 I 42712 rig] script G:\Freespace\Armored Core 4 (USA)\coop-mod\run\rig\vs_live.txt: 2 state(s) on 2 port(s)
[   0.2124 I 42712 rig] rig active: script 'G:\Freespace\Armored Core 4 (USA)\coop-mod\run\rig\vs_live.txt', screenshots 'G:\Freespace\Armored Core 4 (USA)\coop-mod\run\shots\vsfinal' every 2.00 s, audio silent
```

The AC input survey (`AC4_SURVEY_AC=1`) — **two ACs, each with its own controller and state**:

```
[ 202.5428 I 69316 input] AC#1 ctl 0x10fcba20 input 0x10fcdb90 state 0x10fba150 obj 0x10fc9730 body 0x10fae660
[ 202.5439 I 69316 input] AC#2 ctl 0x10fce550 input 0x10ff42f0 state 0x10fe08c0 obj 0x10fefea0 body 0x10fd4dd0
```

Turn-command survey — both ACs go through the same player call site, with **different states**:

```
[ 207.9826 I 69316 input] TURNSET#1 lr 0x00b52708 input 0xd00fb9c0 vt 0x101485f8 state 0x10fba150  <== PLAYER
[ 207.9834 I 69316 input] TURNSET#2 lr 0x00b52708 input 0xd00fb9c0 vt 0x101485f8 state 0x10fe08c0  <== PLAYER
```

Later, a **third controller** is seen reaching AC#2's state — i.e. a second pad path converging
on the same AC object:

```
[ 608.7886 I 69316 input] AC#3 ctl 0x10ff21e0 input 0x10ff42f0 state 0x10fe08c0 obj 0x10fefea0 body 0x10fd4dd0  <== PLAYER
```

Scene threads:

```
[  75.4424 I 69316 thread] thread 0x1000026 'MatchSelectManager' started (entry opd 0xe565d8, prio 1001, stack 0xd0263000+0x10000, tls 0x115b000)
[  75.4506 I 69316 thread] thread 0x1000027 'VsSortieScene' started (entry opd 0xe565d8, prio 1001, stack 0xd0274000+0x10000, tls 0x1158000)
[  75.4591 I 69316 thread] thread 0x1000028 'VsSortieScene' started (entry opd 0xe565d8, prio 1001, stack 0xd0285000+0x10000, tls 0x115a000)
```

Menu/asset loads:

```
[ 201.1498 I 68680 fs] cellFsOpen('/dev_bdvd/PS3_GAME/USRDIR/bind/lang/us/menu/vssortie.000', 0x0) -> 0x0 fd 134
[ 201.1512 I 68680 fs] cellFsOpen('/dev_hdd1/bind/lang/us/menu/vssortie.000', 0x41) -> 0x0 fd 135
[ 629.8632 I 69316 fs] cellFsOpen('/dev_bdvd/PS3_GAME/USRDIR/bind/lang/us/menu/vsresult.000', 0x0) -> 0x0 fd 147
[ 629.8653 I 69316 fs] cellFsOpen('/dev_hdd1/bind/lang/us/menu/vsresult.000', 0x41) -> 0x0 fd 148
```

Pad and network (the port has no network, which does **not** block split-screen):

```
[   5.0898 I 69316 pad] cellPadInit(4)
[  17.1432 I 69316 netctl] cellNetCtlInit: no network interface in this port; state is Disconnected
[  17.1432 W 69316 netctl] network start dialog (type 0): no network available, finishing immediately
```

No error lines appeared on entering versus. The only warning is the network one above, which
also appears on ordinary boots.

---

## (d) Screenshots

In `research\e5_shots\` (G: paths):

| file | what it shows |
|---|---|
| `10_menu_split_screen_highlighted.png` | MULTIPLAYER panel, SPLIT-SCREEN gold-selected, with the game's own description of the mode |
| `11_setup_load_1p_data.png` | SPLIT-SCREEN setup, "Load 1P data?" dialog, PLAYER 01/02 panels |
| `12_setup_load_2p_data.png` | "Load 2P data?" — the second-player prompt |
| `13_setup_both_players_loaded.png` | both players loaded with a real AC (TYPE-HOGIRE / Rosenthal), MAP DESERT, TIME 5 MIN |
| `01_versus_match_split_screen.png` | **the match**: split 3D view, two independent HUDs, shared 04:46 timer |
| `02_ammo_before_p1_fire.png` / `03_p1_fire_right_hud_drains.png` | p1 fires: right HUD 180/36 → 180/31, left untouched |
| `04_p0_fire_left_hud_drains.png` | p0 fires: left HUD drains, right untouched |
| `05_both_acs_damaged_in_arena.png` | both APs below full (35671 / 35526) — the two ACs hit each other in one arena |
| `06…09_*_turn_test_*` | before/after pair proving pad 1 turns only the right view and pad 0 only the left |

---

## (e) What blocked me, and corrections to the doc

1. **`--rig` must not be the command queue.** `rig::pad()` returns false for any port that never
   appears in the `--rig` timeline, and every rewrite of that file triggers a reload which
   **clears the owned-port list**. My first attempt pointed `--rig` at `cmd.txt` itself, so the
   rig owned no ports and *every* input was silently dropped — the game sat on the HDD dialog
   for 50 s. `run\vs_split.ps1` therefore keeps a static `run\rig\vs_live.txt` containing
   `0 0 none 0 0 0 0` and `0 1 none 0 0 0 0`, and never touches it while the game runs.

2. **The ms field in `cmd.txt` is a DURATION, not an absolute time.** `load_commands()` does
   `g.q_accum += ms/1000.0; e.end = g.q_accum;`. My first driver wrote absolute clock values
   there (e.g. `64030`), which is why a press appeared to last 64 s.

3. **Repeated identical commands get coalesced.** Two `DOWN` lines in a row with no neutral
   between them move the cursor once. Every press must be preceded by a `none` line.

4. **One tap sometimes moves more than one menu row** (e.g. one `DOWN` jumped SPLIT-SCREEN →
   SYSTEM, another wrapped ONLINE→ONLINE). Fixed by never counting presses: the script reads the
   highlighted row from the screenshot and repeats until the target row is gold. Detection is
   numeric — the selected button is the only row with a gold fill, measured as (R−B) over the
   row band: selected ≈ +27, unselected = 0.

5. **All input is swallowed during the "Leaving network…" caption** after answering "Load game
   progress data?". Screenshots taken during it show a panel that looks ready but ignores the
   pad; that is what made the earlier fixed-delay scripts descend into the SYSTEM submenu. The
   script waits for the save-read log line and a settle delay first.

6. **`SAVE GAME` hazard hit and avoided.** While probing the panel I drifted into
   MULTIPLAYER → SYSTEM, whose submenu contains `SAVE GAME` (highlighted `LOAD GAME` in my
   case). I backed out with B without pressing A. The save was not modified; the backup taken
   before starting is at `run\save_backup_e5\` and both slots still hold the same data.

7. **`RT`/`RB` are not the fire button.** They produced no ammo change on either pad, so an
   early "p1 fire doesn't work" reading was a wrong-button artefact, not a finding. `A` (CROSS)
   fires.

8. **Screenshot staleness.** With `--rig-shot-every 2`, reading the newest PNG immediately after
   an input can compare a frame to itself and show "no change". Every measurement here waits for
   a screenshot file that did not exist before the input.

---

## Rig notes for reuse

```
run\vs_split.ps1            full closed-loop boot -> versus match (leaves the game running)
run\vsloop.ps1              screen readers + Press/FreshShot helpers (dot-source, -Tag <run>)
run\vsdrv.ps1               simple send/log helpers for an already-running rig
run\vs_run.ps1              bare launcher (hidden, screenshots, both pads owned)
```

Drive a running match by appending to `run\shots\<tag>\cmd.txt`:

```
3000 p1 none 0 26000 26000 0     # pad 1: forward + turn right
1500 p1 A 0 0 0 0                # pad 1: fire (CROSS)
   0 p0 none 0 0 0 0             # neutral line so the next press is not coalesced
```

Kill with `Stop-Process -Name ac4 -Force`.

## Consequence for the co-op project

This materially softens `HANDOFF.md` §0.6 / §11 ("E3 = engine work required"). The engine
already routes **two pads to two ACs** in versus: `AC#1` and `AC#2` hold separate controllers
and separate input states, both reach the same player turn-set call site, and both respond to
their own pad with no port hook (`AC4_P2_AUTO` was deliberately **unset** for these tests, so the
P2 unit-claiming hook played no part). What versus still does not give us is a **campaign**
mission with an allied second AC: versus builds both ACs as players and loads them from
`ACDES.DAT`, whereas story missions ship one `_player.lua` and have no second AC
(`HANDOFF.md` §0.1). So the remaining work is narrower than "make a second AC pad-drivable":
the pad→AC2 path is already in the engine and worth reusing instead of re-implementing.
