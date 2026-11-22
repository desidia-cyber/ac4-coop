# 19 — E7: a campaign mission builds a SECOND player AC (pad port 1)

**Question.** Can a campaign mission be made to build a second player AC whose pad operator reads
gamepad **port 1**, from a hook — and does the engine then drive it?

**Answer (measured).** The second AC **is built completely** — a second builder, a second AC body and
a second pad operator whose `+12` is **port 1** — but the game **never registers or updates it**:
no second operator update, no second entry in the AC survey, nothing on screen, and pad 1 changes
nothing. The build is therefore the easy half; the missing half is registration with the scene.
Both co-op runs also **crash at the mission-end transition** (2/2), while a control run without
`AC4_COOP` goes through the same transition cleanly.

Everything below was measured in these runs (all under `run\data\runs\`, logs kept by `--keep-runs 4`):

| run dir | scenario | outcome |
|---|---|---|
| `20261115-043857` | mission, `AC4_COOP=1` | gameplay, then **crash** at the debrief → hub transition (948.0 s) |
| `20261115-045604` | mission, `AC4_COOP=1` | gameplay, then **crash** at the same transition (1072.9 s) |
| `20261115-051454` | mission, **`AC4_COOP=0`** (control) | same transition **clean**, reached the hub, ran a second mission, no fatal (log ends 1084 s) |

`AC4_TRACE_CREATE=1 AC4_SURVEY_AC=1` were set in all three runs.

---

## 1. What was implemented

All new behaviour is gated by `AC4_COOP=1`; with the variable unset the game behaves as before (the
control run above is the evidence). The binary used for all three runs is
`ac4-port\local\build\release\ac4.exe`, built 15.11.2026 05:07 (99,116,032 bytes) — that is the
`AC4_COOP` build; rebuild with `-DAC4_COOP` absent only if the experiment is removed.

| file | change |
|---|---|
| `ac4-port\src\runtime\game\coop_build.h` | **new** — `coop_on()`, `coop_p2_building()` |
| `ac4-port\src\runtime\game\coop_build.cpp` | **new** — `hook_ac_scene_init`, `hook_ac_build` (the second build) |
| `ac4-port\src\runtime\game\input_hooks.cpp` | `hook_player_ctor_e` (0xb5129c) now forces `R(4) = 1` while the second build runs, and logs the port it replaced |
| `ac4-port\src\runtime\game\hooks.txt` | two new lines: `0x00740838 ac_scene_init`, `0x00b0d354 ac_build` |
| `ac4-port\local\gen\ppu\ppu_gen_077.cpp` | hook wrapper for `f_00740838` (emitter form, see note) |
| `ac4-port\local\gen\ppu\ppu_gen_118.cpp` | hook wrapper for `f_00b0d354` (emitter form) |
| `run\coop_mission.ps1` | **new** — the campaign runner (both pads owned from the first rig line) |
| `run\rigpress.py`, `run\wait_menu.py`, `run\wait_title.py`, `run\drive_mission.py` | **new** — rig drivers (future-dated presses; screenshot-driven menu detection) |

**Note on the generated files.** `hooks.txt` is the canonical input (`ac4-recomp emit … --hooks`),
but a re-emit rewrites all 163 `ppu_gen_*.cpp` (109 MB) and forces a full recompile. The two wrappers
were therefore inserted by hand in the emitter's exact form, so a re-emit reproduces them:

```cpp
namespace ppu { void hook_ac_build(Ctx&, void (*)(Ctx&)); }
void f_00b0d354_guest(Ctx& __restrict ctx);
void f_00b0d354(Ctx& __restrict ctx){ ppu::hook_ac_build(ctx, f_00b0d354_guest); }
void f_00b0d354_guest(Ctx& __restrict ctx){
```

**How it works.** `hook_ac_scene_init` (0x00740838, `Ac4SortieScene::Init`, called from
`MissionSortieScene::Init` with `lr 0x007ce638`) remembers the scene and **the scene's TOC** — the
TOC is not obtainable inside the Build, because the TOC thunk `0x00737d48` rewrites `r2`
(`scene_toc + 0x1a44c`). `hook_ac_build` (0x00b0d354, `lr 0x00740a88`) saves `R(2)`…`R(10)` and the
two 64-bit stack arguments at `r1+112`/`r1+120`, lets the game's own build run, and — only if it
returned non-zero — repeats the whole creation with a second builder: allocator `f_008fe96c(240,
r32(scene_toc-32112), 103)`, constructor `f_00737b68(builder2, scene+8000)`, the 20 colour bytes
copied from `builder1+176`, and the Build again with the caller's arguments and `building_p2` set.
`scene+8280` is deliberately **not** written for builder2 (it stays in a C++ global). The first
build's result is what the caller gets in `R(3)`.

---

## 2. Verbatim log lines (run `20261115-045604`, all with `Ctx.lr` valid and the caller's TOC)

```
[ 813.3721 I 82388 coop] COOP SCENE_INIT scene 0x1080e9b0 toc 0x00eeaa0c lr 0x007ce638
[ 813.3721 W 82388 coop] COOP mission scene 0x1080e9b0 toc 0x00eeaa0c allocator owner 0x10047050
[ 817.2450 I 82388 coop] COOP BUILD#1: builder 0x10d3d540 result 1 | +64(body) 0x10d9f660 id 1162697008 vt 0x10146710 | +80 0x13009780 +84 0x13034b60 +112 0x10d3d640 | +176.. colours 000102030607040511100d090f0b0a080e0c13e0
[ 817.2461 I 82388 coop] COOP: builder2 allocator(0x008fe96c) size 240 owner 0x10047050 tag 103 -> 0x10daebb0
[ 817.2462 I 82388 coop] COOP: builder2 0x10daebb0 constructed with scene+8000, 20 colour byte(s) copied from builder1
[ 821.2321 I 82388 coop] COOP: operator ctor 0x00b5129c this 0x1485fee0 port 0 -> 1 (lr 0x00b51964)
[ 821.2761 I 82388 coop] COOP BUILD#2: builder 0x10daebb0 result 1 | +64(body) 0x10d9c100 id 4054429373 vt 0x10146710 | +80 0x1483e620 +84 0x148692e0 +112 0x10daecb0 | +176.. colours 000102030607040511100d090f0b0a080e0c13e0
[ 821.2858 I 82388 create] OPERATOR 0x1302b760 vt 0x10147a50 +4(enabled) 1 +8(input) 0x13017590 +12(port) 0
[ 821.8664 I 82388 input] AC#1 ctl 0x13028a10 input 0x1302ab80 state 0x13017590 obj 0x13026740 body 0x13009780  <== PLAYER
```

Reading it: the second build **succeeded** (`result 1`) and produced a full set of parts
(`+80` 0x1483e620 body, `+84` 0x148692e0, `+112` 0x10daecb0) with the same shape as the first
(`+80` 0x13009780 = the player's AC body, the one the survey prints as `AC#1 … body 0x13009780`).
The second operator was created with **port 1**. Then nothing: the only `OPERATOR` line in the whole
run is the original `0x1302b760 … +12(port) 0`, and the only `AC#` line is `AC#1`.

Grep for the second operator (`0x1485fee0`) over the whole run: **7 lines, all from its construction**
(`CTOR 0xb5193c`, `CTOR 0xb5129c`, the two arg dumps, and the `COOP:` line). It is never updated.

The mission then ran live for **251 s** before the transition described in §5.

---

## 3. YES / NO table

| question | answer | evidence |
|---|---|---|
| second AC body created | **YES** | `COOP BUILD#2 … result 1`, `+80 0x1483e620` (and `+64 0x10d9c100`, `+84`, `+112` non-null) |
| second operator created with port 1 | **YES (created), NO (driven)** | `COOP: operator ctor 0x00b5129c this 0x1485fee0 port 0 -> 1`; no `OPERATOR` line for 0x1485fee0, no `AC#2` line |
| game reaches gameplay | **YES** | `input context gameplay` in both co-op runs; AP 35888, ammo 180/36, radar live |
| P2 AC visible | **NO** | one AC on screen in every co-op frame; the dark object to its left is also present in the **control** run (no `AC4_COOP`), so it is not P2 — see `20_…`/`21_…` |
| p1 moves P2 AC | **NO** | idle→p1-forward frame diff **6.21 / 5.66 %** vs the scene's own idle noise **5.87 / 5.88 %** (mean \|ΔRGB\| / % pixels changed >30) |
| p0 still moves P1 AC | **YES** | same metric p1→p0 **18.79 / 15.73 %**; speed readout 114–118 km/h; radar position moves |
| camera / HUD still follow P1 | **YES** | third-person view stays behind P1 through every input; HUD/radar unchanged by pad 1 |
| p1 fire (A) does anything | **NO** | `RA 180 / 36` before and after `800 p1 A` |
| stability over 60 s | **YES in gameplay**, then **crash at the mission end** | 251 s and 220 s of live gameplay, then §5 |

---

## 4. Screenshots (`research\e7_shots\`)

| file | what it shows |
|---|---|
| `00_gameplay_arrival.png` | first gameplay frame of a co-op run: one player AC, HUD, radar |
| `10_idle_a.png`, `15_idle_c.png`, `16_idle_d.png` | idle control frames (the scene's own noise: fire, particles, subtitles) |
| `11_p1_fwd.png`, `17_p1_fwd_b.png` | **pad 1 forward** — indistinguishable from idle |
| `12_p0_fwd.png` | **pad 0 forward** — 118 km/h, the view has translated |
| `13_idle_b.png`, `14_p1_fire.png` | before / after **pad 1 fire**: `RA 180 / 36` unchanged |
| `20_control_no_coop_arrival.png` | control run (no `AC4_COOP`): hub after the mission **without a crash** |
| `21_control_no_coop_gameplay.png` | control run in the same mission: the same dark object left of the AC, **no second AC** |

---

## 5. The crash

Both `AC4_COOP=1` runs crash at the **mission-end transition**, at the same instruction, after the
debriefing assets load and `Ac4SortieScene` exits (run `20261115-045604`):

```
[1063.8515 I 20500 thread] thread 0x1000036 'CollisionSystem' exited (status 0x0)
[1064.1205 I 82388 fs] cellFsOpen('/dev_bdvd/PS3_GAME/USRDIR/bind/lang/us/menu/debriefing.000', 0x0) -> 0x0 fd 249
[1071.5352 I 61792 thread] thread 0x100002c 'Ac4SortieScene' exited (status 0x0)
[1071.5365 I 52836 fs] cellFsOpen('/dev_hdd1/bind/world/m700_world.000', 0x0) -> 0x0 fd 251
[1071.5433 I 82388 input] input context screen
[1072.8531 F 82388 fatal] host exception 0xc0000005 at rip 00007FF6CCFC959F in thread 0x1000000 'main_thread' (ac4.exe+0x45959f)
  guest read at 0x000067ba (outside all areas)
  in recompiled f_0066e228 (+0x7f host bytes)
  r0  000000000067cfec r1  00000000d00ffd90 r2  0000000000ed7990 r3  000000000000000a
  r4  00000000d00fff00 r5  00000000d00ffea0 r6  0000000000000000 r7  0000000000000000
  r8  0000000000000000 r9  000000000000000a r10 0000000013001a90 r11 0000000000000000
  r12 000000000000004d r13 0000000000e04030 r14 0000000000000000 r15 0000000000000000
  r23 0000000000000180 r24 00000000d0100bc0 r27 00000000d00ffea4 r30 00000000d00fff00 r31 0000000010daf260
  lr 0067cfec ctr 00000000006bc920 cr 48000084 xer so=0 ov=0 ca=0 cia 00a73f38
  guest backtrace: 0067cfec 007cd59c 0074e8c8 0074eb40 0074eae8 0074eae8 005edb10 0091b0e4 0091b188 00024484 00011518 000113cc
```

Run `20261115-043857` crashed identically at 948.0273 (same `rip`, same `guest read at 0x000067ba`,
same backtrace), 220 s after gameplay began.

**The control settles whose bug it is.** `20261115-051454` (`AC4_COOP=0`, same mission, same path)
reaches the same transition and continues: `debriefing.000` at 554.9, `Ac4SortieScene` exits at
563.09, `m700_world.000` at 563.10, then the hub — **zero `fatal` lines** in the log (it goes on to a
second mission and the log ends at 1084 s). So the crash is introduced by the second AC, not by the
mission or the rig.

The crashing function `f_0066e228` is reached from `0x007cd59c` (in the mission-sortie scene's
per-frame code) and dereferences `0xa` (`r3 = r9 = 0xa`) as a pointer. **Which object `f_0066e228`
expects — and whether the second AC/operator is what makes it see `0xa` — is NOT DETERMINED.**

---

## 6. Rig notes for reuse (cost me three runs)

1. **Do not enter a mission while another movie is playing.** Runs `coop3`/`coop5` pressed START
   through the attract movie and then accepted the mission; `brf_m010.pam` opened, produced **no**
   `GetPicture`, showed a black screen and the game waited on it **forever** (no timeout, no input
   response). With the attract movie left to finish (`title.pam` plays ~68 s and closes; the next
   movie opens ~10 s later — that gap is the menu window), the briefing played **51 s** and the
   sortie scene followed immediately, exactly as in `research\18_raw\B2_mission_creators.log`
   (53 s).
2. **Press times must be dated in the future.** The rig timeline only takes effect from an entry's
   timestamp, so a press appended for "now" is never seen. `run\rigpress.py` dates each press
   `clock + lead`.
3. **A lost `none` line coalesces two presses** into one long hold, which the menus swallow. Every
   append is verified by reading the file back (`run\coop_mission.ps1`). One PowerShell run dropped
   every `START` line this way; python appends never did.
4. Console-command (`cmd.txt`) presses **replace** the timeline for that port once the queue is
   non-empty (`rig.cpp` `pad()`), so menus were driven through the timeline and gameplay through
   `cmd.txt`.

---

## 7. NOT DETERMINED

1. **Whether the second AC is ever registered anywhere.** It exists (builder, body, controller,
   operator) but the scene's per-frame code never touches it. What "registration" means concretely
   (the scene's operator list, its entity registry, `scene+8280`, the FCS) is unmeasured — the
   second builder is deliberately kept out of `scene+8280`, and that is the first thing to vary.
2. **Why `f_0066e228` reads `0xa`.** No dump of the object it was called for was taken before the
   crash.
3. **Whether a registered second AC would then be visible and pad-driven.** The port-1 operator
   never ran even once, so the claim "port 1 reaches a second AC" is still unproven; only "the
   operator object is constructed with port 1" is measured.
4. **Variant B (the brief's fallback: dump `r32(scene+7992)` and 64 bytes at `scene+7980..8044`) was
   not needed** — the second Build did not crash, so it was not run. `coop_build.cpp` contains the
   dump behind `AC4_COOP_DUMP=1` for the next session.
