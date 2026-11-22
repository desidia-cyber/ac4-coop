# 21 — E8: the second player AC reuses the FIRST container and the engine drives it

**Question.** E7 built a second AC (second builder, second body, port-1 operator) but the scene never
touched it, because the Build's first step creates a *container* and E7's second build made a second
one. In versus both players' bodies are registered in ONE container. Does the same trick make a
campaign mission drive a second AC?

**Answer (measured).** Yes. With the second build reusing builder1's container, the engine **registers,
renders and updates** the second AC: the AC survey prints `AC#2`, the second pad operator is updated
every frame (3600+ updates logged), and **two ACs are visible on screen**. The E7 mission-end crash
**does not happen any more** (0 `fatal` lines; the mission ran 240 s, ended, and returned to the hub
through `debriefing.000`). Whether pad 1 actually *steers* the second AC is **not** cleanly measured —
see the table and §6.

Run `20261115-064824` (`run\shots\e8k`), conditions `AC4_COOP=1 AC4_TRACE_CREATE=1 AC4_SURVEY_AC=1`,
binary `ac4-port\local\build\release\ac4.exe` built 15.11.2026 06:40 with the E8 sources. Full log kept
at `research\e8_raw\e8k_success.log` (4,006,336 bytes).

---

## 1. What was implemented

| file | change |
|---|---|
| `ac4-port\src\runtime\game\coop_build.cpp` | **rewritten** — adds `hook_ac_container` (the shared container), `hook_ac_container_final`, `hook_ac_build_slot10`, `hook_ac_build_slot9`, `hook_ac_attach_a/b/c` (skippable calls), `coop_note_p2_operator`/`coop_p2_operator`, `coop_operator_tick`, `coop_skip`; the Build log now prints the builder's slot-+32 chain, the container, both bodies with their entity ids and vtables, and the second operator |
| `ac4-port\src\runtime\game\coop_build.h` | **rewritten** — declares the above and `enum CoopSkipId` |
| `ac4-port\src\runtime\game\input_hooks.cpp` | `operator_probe()` now calls `coop_operator_tick(r3)` first (counts the second operator's updates whatever the trace setting); `hook_player_ctor_e` records the operator it gave port 1 |
| `ac4-port\src\runtime\game\hooks.txt` | 7 new lines: `0x00b0b77c ac_container`, `0x00b12770 ac_container_final`, `0x00b0bf04 ac_build_slot10`, `0x00b0b36c ac_build_slot9`, `0x00af6d60 ac_attach_a`, `0x00af6d40 ac_attach_b`, `0x00b02718 ac_attach_c` |
| `ac4-port\local\gen\ppu\ppu_gen_118.cpp` | wrappers for `f_00b0b77c`, `f_00b0b36c`, `f_00b0bf04`, `f_00b12770`, `f_00b02718` (emitter form, inserted by script with an "exactly one anchor" check) |
| `ac4-port\local\gen\ppu\ppu_gen_117.cpp` | wrappers for `f_00af6d40`, `f_00af6d60` |
| `run\e8_launch.ps1`, `run\e8_wait.py`, `run\e8_patient.py`, `run\e8_last.py`, `run\e8_start.py`, `run\e8_gap.py`, `run\e8_auto*.py`, `run\e8_final.py`, `run\e8_mission.py`, `run\e8_boot.ps1`, `run\e8_go.py`, `run\e8_run.py` | **new** — the launch/drive experiments of §7 (`e8_run.py` is the one that produced the good boot) |

### The hook that matters

`f_00b0d354` (the Build) resolves its first step as `r32(r32(builder)+32) -> opd -> code`. Statically
(builder vtable `0x10146340`, TOC cell `0x00efde98` = scene TOC `0x00eeaa0c` + 78988) that is
**opd `0x00ea0318` -> code `0x00b0b77c`**, and the runtime prints the same thing:

```
COOP: object 0x10d3d540 vtable 0x10146340 -> +32 opd 0x00ea0318 -> code 0x00b0b77c
```

`hook_ac_container` returns early while the second build runs:

```cpp
if (rt::game::coop_p2_building() && lr == 0x00b0d610u) {   // the Build's slot-+32 call site
  ctx.r[3] = g_container1;                                   // = r32(builder1 + 64)
  return;                                                    // the original is NOT called
}
```

Everything else in the second Build runs unchanged, so the second body is still created by
`f_00b0bb80`, still registered by `f_00b12634` (which appends to the container's list at `+200`), and
still gets its own operator with pad port forced to 1 (`input_hooks.cpp`).

The post-build calls can be skipped one at a time for diagnosis via
`AC4_COOP_SKIP=0x00b12770,...` (each hook checks `building_p2` **and** its exact `lr`). **No skip was
needed**: the second Build returned `result 1` with the shared container and nothing crashed.

---

## 2. Verbatim log lines (`research\e8_raw\e8k_success.log`)

```
[ 970.0966 I 62180 coop] COOP SCENE_INIT scene 0x1080e9b0 toc 0x00eeaa0c lr 0x007ce638
[ 970.0966 W 62180 coop] COOP mission scene 0x1080e9b0 toc 0x00eeaa0c allocator owner 0x10047050
[ 972.4358 I 62180 coop] COOP: container created 0x10d9f660 for builder 0x10d9f660 (lr 0x00b0d610)
[ 972.5178 I 62180 coop] COOP BUILD#1: builder 0x10d3d540 result 1 | +64(container) 0x10d9f660 | +80(body) 0x13009780 id 1000 vt 0x10142638 | +84 0x13034b60 +112 0x10d3d640 | +176.. colours 000102030607040511100d090f0b0a080e0c13e0
[ 972.5179 I 62180 coop] COOP: builder1 0x10d3d540 container 0x10d9f660 -> slot+32 resolves to:
[ 972.5179 I 62180 coop] COOP: object 0x10d3d540 vtable 0x10146340 -> +32 opd 0x00ea0318 -> code 0x00b0b77c
[ 972.5179 I 62180 coop] COOP: builder2 allocator(0x008fe96c) size 240 owner 0x10047050 tag 103 -> 0x10daebb0
[ 972.5179 I 62180 coop] COOP: builder2 0x10daebb0 constructed with scene+8000, 20 colour byte(s) copied from builder1
[ 972.5194 W 62180 coop] COOP: slot8 CREATOR SKIPPED during the second build: builder 0x10daebb0 gets the SHARED container 0x10d9f660 (lr 0x00b0d610)
[ 972.5348 I 62180 coop] COOP: operator ctor 0x00b5129c this 0x10dc7e10 port 0 -> 1 (lr 0x00b51964)
[ 972.5677 I 62180 coop] COOP BUILD#2: builder 0x10daebb0 result 1 | +64(container) 0x10d9f660 | +80(body) 0x10d9c100 id 1000 vt 0x10142638 | +84 0x10dd1210 +112 0x10daecb0 | +176.. colours 000102030607040511100d090f0b0a080e0c13e0
[ 972.5678 I 62180 coop] COOP SHARED: container 0x10d9f660 (+196) 0x11a19450 (+200 list) 0x11a19470 | body1 0x13009780 id 1000 vt 0x10142638 | body2 0x10d9c100 id 1000 vt 0x10142638 | builders 0x10d3d540/0x10daebb0 (+64 of #2) 0x10d9f660
[ 972.5678 I 62180 coop] COOP: object 0x10d9f660 vtable 0x10146710 -> +8 opd 0x00e6e1f8 -> code 0x004dd108
[ 972.5678 I 62180 coop] COOP: second operator 0x10dc7e10 vt 0x10147a50 +4(enabled) 1 +8(input) 0x10db3810 +12(port) 1
[ 972.5751 I 62180 create] OPERATOR(0xb54e94) 0x1302b760 vt 0x10147a50 +4(enabled) 1 +8(input) 0x13017590 +12(port) 0
[ 972.5787 I 62180 create] OPERATOR(0xb54e94) 0x10dc7e10 vt 0x10147a50 +4(enabled) 1 +8(input) 0x10db3810 +12(port) 1
[ 972.6037 I 62180 input] input context gameplay
[ 972.6331 I 62180 input] AC#1 ctl 0x13028a10 input 0x1302ab80 state 0x13017590 obj 0x13026740 body 0x13009780  <== PLAYER
[ 972.6350 I 62180 input] AC#2 ctl 0x10dc50c0 input 0x10dc7230 state 0x10db3810 obj 0x10dc2df0 body 0x10d9c100  <== PLAYER
[ 972.5787 I 62180 coop] COOP: second operator 0x10dc7e10 UPDATED, call #1 (this frame included)
[ 972.5791 I 62180 coop] COOP: second operator 0x10dc7e10 UPDATED, call #2 (this frame included)
[ 973.3911 I 62180 coop] COOP: second operator 0x10dc7e10 UPDATED, call #60 (this frame included)
[ 981.3936 I 62180 coop] COOP: second operator 0x10dc7e10 UPDATED, call #600 (this frame included)
[1041.7476 I 62180 coop] COOP: second operator 0x10dc7e10 UPDATED, call #3600 (this frame included)
```

Reading it: the second build ran **into the first container** (`+64(container) 0x10d9f660` on both
builders), registered a second body (`0x10d9c100`) in the same container whose list object is
`0x11a19470`, and got its own port-1 operator (`0x10dc7e10`). The scene then drove it: the survey
prints `AC#2` with its own controller/input/state/obj/body, and the operator update counter reached
**3600 calls in 69 s (≈52/s, i.e. every rendered frame)** and kept going.

---

## 3. YES / NO table

| question | answer | evidence |
|---|---|---|
| second AC body created | **YES** | `COOP BUILD#2 … result 1 … +80(body) 0x10d9c100 id 1000 vt 0x10142638` |
| registered in the shared container | **YES** | `+64(container) 0x10d9f660` on both builders; `COOP SHARED: … (+200 list) 0x11a19470 | body1 0x13009780 | body2 0x10d9c100`; the scene then updates it (below) |
| operator port 1 updated every frame | **YES** | `OPERATOR(0xb54e94) 0x10dc7e10 … +12(port) 1` and `COOP: second operator 0x10dc7e10 UPDATED, call #1/#2/#60/#600/#3600` — none of this existed in E7, where the second operator was never updated once |
| `AC#2` survey line | **YES** | `AC#2 ctl 0x10dc50c0 input 0x10dc7230 state 0x10db3810 obj 0x10dc2df0 body 0x10d9c100` |
| P2 AC visible | **YES** | `research\e8_shots\00_gameplay_arrival.png` — two ACs in the third-person view: the player's in front, the second directly behind/above it at the shared spawn point |
| p1 moves the P2 AC | **PARTIAL / NOT DETERMINED** | p1 right stick 4 s: frame diff **8.33 mean / 6.47 %** vs the scene's own idle baseline **3.42 / 4.36 %** (mean 2.4× the noise); the mission's own *"Please head back."* caption appeared immediately after that input. No clean separation test was measured (§6) |
| p0 still moves P1 | **YES (indirect)** | p0 left stick 2.6 s: **14.43 %** changed, and the km/h readout moves (P1 HUD); the camera never left P1 in any frame |
| camera / HUD still follow P1 | **YES** | every gameplay frame is a third-person view behind P1 with P1's HUD (AP 35888, `RA 180/36`, `LA`, radar) |
| p1 fire (A) does anything | **NOT DETERMINED** | the fire test landed after the mission had already ended (the snapshot is the hub, see §6) |
| enemies react / target | **NOT DETERMINED** | radar showed hostile contacts (green) after the mission restarted, but no per-AC targeting evidence was captured |
| stability 120 s | **YES** | gameplay 972.6 → 1213.4 = **240 s** live, no crash, mission then ended normally |

---

## 4. Screenshots (`research\e8_shots\`)

| file | what it shows |
|---|---|
| `00_gameplay_arrival.png` | **first gameplay frame of the E8 run: two ACs** (P1 in front, the second AC behind/above it at the spawn point), P1's HUD, radar |
| `10_idle_a.png`, `12_idle_b.png`, `20_idle_c.png`, `21_idle_d.png` | idle control frames |
| `11_p1_fwd.png`, `22_p1_turn.png` | **pad 1** forward 2.6 s / right stick 4 s |
| `13_p0_fwd.png` | **pad 0** forward 2.6 s (P1 moves, view translates) |
| `live_now.png`, `title_now.png`, `hub_state.png`, `dlg1..3.png`, `state_now.png` | the rig/menu walk of §7 (attract movie, title card, hub, mission-accept dialog, SCHEMATICS) |

Frame-diff metric (same as `research\16`): mean \|ΔRGB\| per sampled pixel and % of sampled pixels
changing by >30, box (60,120)-(1220,620), 2-pixel stride.

| step | mean / changed |
|---|---|
| idle → idle (control) | 3.42 / **4.36 %** |
| idle → **p1** left stick forward 2.6 s | 10.60 / **13.56 %** |
| idle → **p0** left stick forward 2.6 s | 15.58 / **14.43 %** |
| idle → **p1** right stick 4 s | 8.33 / **6.47 %** |

The scene is full of sparks and fire, so the whole-frame metric is noisy (E7's darker mission gave a
5.9 % baseline, this one 4.4 %). It separates p1 (6.47 %) from idle (4.36 %) but does not isolate
*which* AC moved.

---

## 5. Follow-ups (only the ones the run called for)

**(a) Skip a post-build step — NOT NEEDED.** The second Build returned `result 1` with the shared
container and no call crashed, so none of `0x00b12770`, `0x00b0bf04`, `0x00b0b36c`, `0x00af6d60`,
`0x00af6d40`, `0x00b02718` was skipped (`AC4_COOP_SKIP` was unset in this run). The hooks and the env
switch are in place for the next session.

**(b) Same entity id — CONDITION MET, EXPERIMENT NOT RUN.** Both bodies report **id 1000**
(`body1 0x13009780 id 1000`, `body2 0x10d9c100 id 1000`), which is exactly the condition in the
brief for trying the second Build with the second 64-bit stack argument (`r1+112`/`r1+120`, the second
is 0 for P1) set to 1. That variant was **not run**: the launch budget was consumed by the menu/movie
problem of §7 (13 of ~15 launches), and the game clock expired before a second mission could be
entered. Candidate code path is in place (`w64(sp + 112, arg112); w64(sp + 120, arg120);` in
`hook_ac_build`, `arg120` is the value to change).

**(c) Both ACs spawn at the same position — CONFIRMED, NOT FIXED.** The two bodies are at the same
place: in `00_gameplay_arrival.png` the second AC is drawn inside/behind the first. Fixed nor
attempted, as instructed.

---

## 6. What could not be measured in the 240 s of gameplay

The mission ended **by itself** 240 s after gameplay began: the caption *"Please head back."* appeared
and the mission returned to the hub (`debriefing.000` 1213.4 → `Ac4SortieScene` exit 1219.4 →
`m700_world.000..003` → `input context menu` 1221.7). That ended the only successful gameplay window:

* the **p1 fire** test was issued after that point, so `31_p1_fire.png` is the hub, not ammo — the
  "p1 fire" and "enemies react" rows are **NOT DETERMINED**;
* the clean **P2-movement** test (walk P1 forward so the two ACs separate, then turn with pad 1 and
  compare) was never reached: a re-entry attempt ended in SCHEMATICS (`state_now.png`) and the
  `--seconds 1800` watchdog expired (game clock 1800).

---

## 7. The rig: movies and menus (13 launches, more than half an hour) — READ THIS FIRST NEXT TIME

This is the part that went wrong, and the user's verdict on it is on the record: *"you are stupid
because you couldn't figure how to get past freaking videos and menus for more than half an hour."*
That is a fair verdict — the recipe was in `HANDOFF.md` §5.1 all along (two STARTs), and I burned
**14 of the 15 launches** rediscovering it (`e8k` was the 15th). Measured facts, so the next session
does not repeat it:

1. **A movie decodes exactly 8 pictures and then sits.** `opening.pam` 22.1 → 189.8 s,
   `title.pam` 189 → 257 s, 8 `GetPicture` lines each. This is **normal**, not a stall. Every "movie
   stalled" abort in this session (`e8f`, `e8g`, `e8h`) was me aborting healthy runs. The only real
   stall signature is `timed wait expired after 3000.0 ms: '_libsail-control'`, which follows ~50 s later.
2. **The startup "HDD access indicator" dialog blocks everything** until A is pressed: run `e8e` sat on
   it for 450 s with no movie at all.
3. **The load list opens early and then waits behind the movies.** Six taps `A B A+B A B A` at ~11-27 s
   produce `cellSaveDataListLoad2: prefix 'BLUS30027GAMEDAT', 2 save(s) found`, but the save is only
   read when the movies are done.
4. **Pressing during a movie is swallowed; pressing in the ~1.5-11 s gap between movies is what the
   menu needs.** Spread-out tapping restarts the attract loop instead (run `e8i`: `opening/title` pairs
   at 189/256/326/399 s, hub never arrives).
5. **The working recipe (run `e8k`, after 13 failures):** dismiss the dialogs → let the movies play
   untouched → when a movie **closes**, press **START** (title card `PRESS START BUTTON`) → press
   **START** again (menu) → press **A** on the default `LOAD GAME` → save reads → `MissionSelectScene`.
   That is exactly `HANDOFF.md` §5.1 ("the first START only stops the movie; a second START opens the
   menu"), and it reproduces: hub at game clock 784.4.
6. **Drive gameplay through `cmd.txt`, not the rig script.** `cmd.txt` is a queue appended line by
   line (`rig.cpp load_commands`); writing the script instead reloads it and clears the owned ports
   (`research\16` §(e)1).
7. **Mission-accept dialog**: `A` opens it with **CANCEL** focused, `LEFT` then `A` accepts — and the
   two presses must be ~2 s apart or the dialog closes with CANCEL (`dlg1.png` → `dlg2.png` →
   `dlg3.png`).

Launch log of this session: `e8a` (briefing stalled, `research\e8_raw\e8a_stalled.log`), `e8b`, `e8c`,
`e8d` (all three: movie/menu, logs in `research\e8_raw\*_stalled.log`), `e8e` (HDD dialog, no movie),
`e8f`×3, `e8g`×2, `e8h`×2 (aborted as "stalled" — they were healthy), `e8i` (attract loop), `e8j`
(no hub), **`e8k` (success)**.

---

## 8. The E7 mission-end crash — GONE

E7 crashed 2/2 at the debrief transition in `f_0066e228` (`guest read at 0x000067ba`, backtrace
`0067cfec 007cd59c …`). With the shared container the same transition is **clean**:

```
[1213.3954 I 62180 fs] cellFsOpen('/dev_bdvd/PS3_GAME/USRDIR/bind/lang/us/menu/debriefing.000', 0x0) -> 0x0 fd 249
[1219.3948 I 18264 thread] thread 0x1000065 'Ac4SortieScene' exited (status 0x0)
[1219.3954 I 62572 fs] cellFsOpen('/dev_hdd1/bind/world/m700_world.000', 0x0) -> 0x0 fd 251
[1221.7065 I 62180 input] input context menu
```

`grep -c 'fatal\]'` over the whole run: **0**. So the crash was a consequence of the orphan container
(the second build's own container, which the scene never updated), not of the mission or the rig —
consistent with E7's control run, and now with the fix in place the symptom is gone. What exactly
`f_0066e228` dereferenced in E7 remains **NOT DETERMINED** (no dump was taken then, and it no longer
faults).

---

## 9. NOT DETERMINED

1. **Whether pad 1 actually steers the second AC.** The operator runs with port 1 every frame and the
   AC is driven by the scene, but the decisive test (separate the two ACs, then turn with pad 1 and
   watch only the second one move) was not reached (§6). The p1 right-stick frame diff is 2.4× the
   idle noise and the mission's "Please head back." caption followed it — suggestive, not proof.
2. **p1 fire / enemies targeting the second AC** (§6).
3. **Follow-up (b)**, the second stack argument = 1 (needed because both bodies are id 1000) — not run.
4. **What the mission's per-frame code does with the container**: the bodies are in its list at
   `container+200` (`0x11a19470`) and both are updated, but the walk itself was not traced.
5. **Whether the shared container changes anything else** (AP/FCS/radar ownership): only P1's HUD is
   drawn, so P2 has no HUD of its own.
