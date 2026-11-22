# HANDOFF — AC4 two-player co-op feasibility research

You are taking over a research task. Everything lives on **G:** (nothing on C:, which is nearly full).
Read this file first, then `10_GAP_STATUS.md`.

---

## 0. CORRECTIONS FROM SESSION 2 — read this before §7

Session 2 re-checked §7 and found **three errors**, then proved **two new capabilities**. §7's plan is
dead; use §7b instead.

### 0.1 §7 is wrong: `m717_enemy` is a MISSILE, not an AC

`common/event/include/m717/entityid.lua` declares exactly one entity:

```lua
m717_ET_MISSILE = 1;   -- the missile target
```

and m717's AI scripts are `717_mech-walk.lua` + `717_missile.lua`. `m717_enemy.lua` is a 658-byte
stub whose `Initial` only calls `LogEvent`. There is no `eventOperation:AutoPilot()` in it to replace.
**Driving the tutorial to m717 would have proved nothing.** The tutorial was checked lesson by lesson:
`m710`–`m719` each ship exactly ONE chara script (`_player.lua`), plus that one missile stub. **The
tutorial contains no second AC at all** — so the "grind the tutorial" route cannot work, at any lesson.

### 0.2 `tutorialene.lua` is an orphan (and the overlay file was inert)

Nothing references it: no bundle contains the string, and no script `dofile`s it. That is why the
previous session saw "no effect" when overlaying it — not because the overlay failed.

### 0.3 Story-mission chara scripts do NOT load from loose disc files

They are **inside the BND3 bundles**. Evidence (m160 run): the game opened
`bind/world/m160_event.000` and `m160_mission.000` and **never** opened
`common/event/mission/m160.lua` or `common/event/mission/chara/m160_player.lua`; both scripts are
zlib entries inside those bundles. (The tutorial is different: `m710_player.lua` is loose and IS
opened loose.) **Consequence: `payload\overlay\...\chara\m000_joshua.lua` currently does nothing.**

### 0.4 NEW — story-mission Lua IS moddable (BND3 repack + overlay)

The overlay *does* intercept bundle paths. Proven end-to-end:

1. Put a **repacked** `m160_event.000` in the overlay → log shows
   `TEST OVERLAY: .../bind/world/m160_event.000 read from ...payload\overlay\...`.
2. Inject `error("AC4RIG-BND3-EXEC-PROOF");` into `Initial` of the bundled `m160.lua` → the log shows
   `[lua] script error 1 ... [string "$(MissionEventScript)\m160.lua"]:12: AC4RIG-BND3-EXEC-PROOF`
   → **the modified script executed.**

Tooling: `tools\bnd3tool.py` (parse/build/roundtrip) and `tools\autosucceed.py`.
Container format: `u32 count` at 0, then 80-byte entries **from 0x50**
`{name[64], 0, offset/16 @+68, rounded/16 @+72, size @+76}`; outer BND3 entry at 0x20 with
`csize@0x24`, `data offset@0x28`, `usize@0x34`, zlib payload at the offset.

### 0.5 NEW — progression is scriptable (chapter 2 reached without playing)

Appending this to the bundled `m160.lua` made the mission complete instantly (**MISSION REPORT,
RANK S**) and advanced the story to **Chapter 2** with 4 selectable missions:

```lua
local __orig = OnMissionStart;
function OnMissionStart( eventOperation )
    __orig( eventOperation );
    SetMissionResult_Success( eventOperation, 0 );
    OnMissionSuccess( eventOperation );
end
```

Caveat: the save is the **read-only console save**, so progress is **in-memory only** — it is lost on
restart. Re-apply the patch and play forward within one run.

### 0.6 E3 answered from static evidence (no second-AC mission needed)

`TUTORIAL_MODE__USERINPUT` is **not a pad binding**. It means "stop applying scripted tutorial
operations and let this entity's own controller drive". For the player that controller is the pad; for
an AI AC it is the AI. Hence `Mission_EnableAutoPilot(eo,false)` = 手動運転モード in the ally scripts is
the AI taking over, not a human.

Corroboration in the port: there is exactly **one** pad-reading operator (`0xb52584`), and the port
identifies "the player" purely by **call site** (`kPlayerTurnSetReturn = 0xb52708`,
`input_hooks.cpp:42`). `hook_input_turn_set` notes "the player's in the SIMULATOR (the operator's call),
**AI inputs elsewhere**" — i.e. AI ACs reach the same input setters from their own call sites.

**Conclusion: E3 = the third row of §7's table — engine work required.** A second AC cannot be
pad-driven by Lua alone; a runtime hook must write a second AC's input object (or a second operator
instance must be created). This is the E4 hook the port is already well shaped for.

### 0.7 WHY MISSIONS LOOKED EMPTY — SOLVED (a valid profile was made in-game)

> **STATUS: FIXED.** The user played NEW GAME through the tutorial and one mission, and saved with
> **SYSTEM → SAVE GAME**. The save is now genuine plaintext and everything works: the AC renders, AP
> reads 35888, the radar shows contacts, and the AC drives at 119 km/h through a full tunnel interior.
> The save loads with **status callback 0/1 (OK)** — no rig bypass needed any more.
> `ACDES.DAT` now contains a real build (`TYPE-HOGIRE` / `NE-L` / `Rosenthal`), and the hub shows
> `Reward 50000 C` instead of `-`.
>
> **The rest of this section is kept as the diagnosis.** It still explains why the supplied console
> save is unusable and must not be reintroduced: put the new plaintext save back if anything overwrites
> it (backup: `run\save_backup_blank` was the *old* blank one; the working one is
> `run\data\hdd0\home\00000001\savedata\BLUS30027GAMEDAT8MS4R1IYVB3D000`).

**Also fixed in the port (one line):** `rig::init()` used to mute audio unconditionally, so even an
ordinary visible run was silent. `src\runtime\rig.cpp` now does
`if (c.rig_script.empty() && c.rig_shots.empty()) g.muted = false;` — a plain run has sound, rig runs
stay silent. The binary at `ac4-port\local\build\release\ac4.exe` includes this.

**How to play it visibly (this is also how to make another profile if needed):**

```
ac4.exe --data "<w>\run\data" --disc "<w>\disc" --flash "<w>\run\data\dev_flash" \
        --test-overlay "<w>\payload\overlay" --fps 60
```

No `--hidden`, no `--rig`, no `--isolate-input` ⇒ a normal window with keyboard/mouse/pad and sound.
Note `--rig` is what enables `save_bypass()`; without it a corrupt save is simply rejected (correct
behaviour) — so LOAD GAME of the old console save will fail, while the new plaintext save loads fine.

#### Original diagnosis (kept for reference)

**Symptom:** entering any story mission (m160, m010, …) shows only the HUD — sky, ground, `0 km/h`,
no AC, no enemies, no world objects. The player is inert: the stick does nothing, speed stays 0.

**This is NOT caused by the overlay/BND3 work of §0.4–0.5.** It reproduces with a completely pristine
overlay, and it is present in the very first mission entry of the whole project.

**Measured, not inferred** (Lua probe injected into the bundled `m160.lua`, called from
`OnMissionStart`; `HANDOFF.md` §10 has the recipe):

```
PROBE2 origOk=true err=nil | AP=0 | objN=2 | objA=2 | heriA=24 | self=nil
```

Reading: the mission script runs fine and the **world is fully spawned** (2 objectives, 24 heli parts
alive; 699 draws/frame in the render log). But **`GetApRateOfAC(ENTITY_ID__PLAYER) == 0`** — the player
AC has **no build**, so there is no AC to draw and nothing to drive. That is the whole symptom.

**Why:** the provided console save is **encrypted**, and the port does not decrypt it
(`save_data.cpp`: "Files are stored unencrypted, so saves are not interchangeable with a real console's").
`rig::save_bypass()` only rewrites the *corrupt* callback result so the **load list** proceeds; the
individual data files stay ciphertext. The game therefore reads a valid *menu* (progress fields come
from `PARAM.SFO`, cf. §0.5 and `CASH 0 / COMPLETE 0`) but cannot populate the profile behind it —
no AC assembly, no parts, no cash. The tutorial works because NEW GAME *creates* a valid AC.

Corroborating: at load the game also asks for `BLUS30027ASSMBLY064` — the **AC assembly save** — and
gets `0 file(s)` / status `-4` (no data). Only `…GAMEDAT…` exists on this machine.

**Consequences**

- Do **not** chase this as a bug in the mod work; it is a fixture problem.
- Mission *startup* can still be studied (scripts, entity ids, groups, cameras — all live), which is why
  the §0.4/§0.5 results stand. But **anything involving actually piloting an AC is unblocked only by a
  real save.**
- The E4 input hook therefore cannot be validated in-game until an AC exists. Order of work is
  now: **obtain/create a valid profile → then E4.**

**Ways forward (untested, in cost order)**

1. Play NEW GAME far enough to save a profile (tutorial route is proven to work and to progress), then
   use that save for all mission tests.
2. Add save *decryption* to the port (RPCS3 has the reference: SDAT/secure-file handling with the
   title's keys). This makes the console save fully usable and is arguably the single highest-value
   piece of infrastructure for the whole project.
3. Obtain a profile created by RPCS3 with this title (it decrypts on import).

> **Session 2 addendum — see `research\14_SAVE_ENCRYPTION_VS_RPCS3.md`.** Option 2 above was
> investigated and is **wrong as written**: RPCS3's `cellSaveData` contains **no decryption at all**
> (save files are read/written as opaque bytes; secure-file IDs are only logged). So there is nothing
> to port, and "RPCS3 can read console saves so the port can too" is unsupported. The gap is a
> missing *firmware* secure layer, not a missing function. Option 1 is the recommended route; the
> exact cipher is AES-CBC/CTR (all files are 16-byte multiples with no header, and no ECB repetition
> even in the 62-byte mostly-zeros `OSET.DAT`), but the key lives in firmware the port does not run.

Note for (1): the port's own save is plaintext and writable, so once *any* valid profile exists it
persists and can be reused/edited freely.

---

## 1. The objective

Decide whether a **2-player online/LAN co-op mod that plays the full AC4 campaign** is feasible, and
prove the one unknown that decides the architecture: **does AC4's engine let a non-player AC be driven
by a gamepad?**

Everything short of implementation is done and documented. The single open question is that test
(called **E3** in the docs).

---

## 2. Environment

| item | value |
|---|---|
| work root | `G:\Freespace\Armored Core 4 (USA)\coop-mod` (~17 GB) |
| game | Armored Core 4 (USA), **BLUS30027**, disc 01.01 |
| eboot sha256 | `0fd1e2ee4dbc599874fabccd596f922904955d8f5c1dbd92a34101ac930daaa1` |
| port | AC4-Port static recompilation, C++/D3D11 |
| binary | `ac4-port\local\build\release\ac4.exe` |
| toolchain | `ac4-port\local\tools` (pinned llvm-mingw) |
| firmware | **4.93**, installed in `run\data\dev_flash`; modules in `tools\work\modules93` |
| disc image | fully extracted at `disc\` (9,900 files, 14.4 GB) |

**Build:** `cd ac4-port; . .\tools\env.ps1; .\tools\build.ps1 -Config Release -Target ac4`
(do not use raw CMake — this project's script sets up the pinned toolchain).
Kill any running `ac4.exe` first or the link fails with `Permission denied`.

**Sandbox:** writes under `G:` need the escalated permission (`danger-full-access` in this harness).
Python is available and handy for binary work.

---

## 3. The rig (this is the important part)

The game is driven headlessly by a test rig built into the port. Sources:
`ac4-port\src\runtime\rig.cpp` and `rig.h`.

What it does:
- **hidden** window, **muted** audio, process at Idle priority (~58 fps)
- **scripted gamepad** — it synthesises the XINPUT state the port reads
- **screenshots** every N seconds into a shots dir
- **save bypass** (`rig::save_bypass()`) so the game accepts a console-signed save it calls corrupt
  — research only, never in a player build
- **live command queue** (see below)

### Live control — use this, do not restart the game

With `--rig-shots <dir>` the rig writes `<dir>\clock.txt` (elapsed seconds) and reads
`<dir>\cmd.txt`. **Append lines to `cmd.txt` while the game runs**; the rig consumes them in order,
each held for its own duration, and then holds the last state.

Format:

```
<milliseconds> <buttons|none> [lx ly rx ry]
```

- buttons: `A B X Y START BACK LB RB L3 R3 UP DOWN LEFT RIGHT LT RT`, joined with `+` (`A+B`)
- `LT` / `RT` are analog triggers (added for this work; boost is `L2` = `LT`)
- sticks: −32768..32767. **Left stick forward = ly +26000**, backward −26000. Right stick rx = turn,
  ry = look up/down.

Example — open the title menu:

```
300 START 0 0 0 0
2500 none 0 0 0 0
300 START 0 0 0 0
```

Watch progress with `clock.txt`, the latest PNG in the shots dir, and the game log at
`run\data\runs\<newest>\ac4.log`.

### Ready-made scripts in `run\`

| script | purpose |
|---|---|
| `boot_tut.ps1 -Tag X` | boots, clears dialogs, picks NEW GAME, waits for the tutorial, **leaves the game running** for interactive control |
| `drive.ps1 -Explore \| -NewGame \| -Hub` | closed-loop driver (log-feedback) to the hub / tutorial |
| `hubcheck.ps1` | load the save and stop at the hub |
| `savegame.ps1` | reaches the hub and uses SYSTEM → SAVE GAME |
| `tutdrive.ps1` | cycles a repertoire of inputs and reports lesson progression |
| `run_rig.ps1` | generic launcher (`-Script -Seconds -ShotEvery -Tag`) |
| `tutorial.ps1` | reach the tutorial lesson list |

---

## 4. Where the session stopped — EXACTLY

**Session 2 state:** a rig run from `hubcheck.ps1 -Seconds 3600 -Tag live7` is alive (it survived
session 2's end). Shots dir `run\shots\live7`, command file `run\shots\live7\cmd.txt`, latest log under
`run\data\runs\`. At handoff it was in the **Chapter 2** mission SWARMS OF RED EYES (`m010`), reached
by auto-succeeding `m160` with the patched bundle — that progression is **in-memory only** and is lost
when this run ends.

**Resume:** either keep using `live7`, or relaunch cleanly and load the save:

```
G:\...\coop-mod\run\hubcheck.ps1 -Seconds 3600 -Tag live8
```
then append to `run\shots\live8\cmd.txt`. `hubcheck` gets to the hub by itself in ~50 s. If it ends on
the attract movie instead, press `START`, wait, `START` again (see §5.1), then `A` on `LOAD GAME`.

Note: the title/attract loop and the LOAD GAME default are still exactly as §5 describes.

---

## 4b. Session-1 state (historical)

A game was running from `boot_tut.ps1 -Seconds 2400 -Tag live2`. State at that handoff:
clock ~218 s, **title menu with `LOAD GAME` highlighted**, shots dir `run\shots\live2`.
**Immediate next action was** `300 A 0 0 0 0` → load list → save accepted (bypass) → ~15 s → hub
(`Chapter 1 : Destination Unknown`, `FIRST STRIKE`, `m160`). Session 2 confirmed this works, and also
confirmed the handoff's claim was already stale: the game was in fact sitting at the **hub**, not the
title menu.

---

## 5. Gotchas that cost the previous session a lot of time

1. **The title is an attract-movie loop.** The first `START` only stops the movie; a **second `START`**
   opens the menu. Pressing `A` does nothing there. This is the single biggest time sink.
2. **Menu default is `LOAD GAME`** when a save exists — not NEW GAME.
3. **Menus need taps.** A held direction (or 3 s stick deflection) is swallowed. Use ~120–150 ms taps.
   One tap usually moves one item, but occasionally two — verify from a screenshot.
4. **The mission-accept dialog focuses `CANCEL`.** You must press `LEFT` then `A`.
5. **Do not spam `A` at the menu** — A confirms the highlighted item (i.e. starts NEW GAME).
6. The game's own **"Cannot check update file"** dialog blocks the title until acknowledged; clear the
   startup dialogs with a burst of `A B A+B A B A`.
7. `SAVE GAME` in SYSTEM **overwrites the save** with the port's own blank save. It has been restored
   to the console save — do not press it again unless intended.

---

## 6. What is already proven (with evidence)

- Firmware **4.93** correct; all 10 decrypted module hashes match; SPU stage now `0 skipped, 0 partial`
  (was 198/196 on 4.92) — the build is canonical.
- **Lua overlay delivery works**: `TEST OVERLAY: /dev_bdvd/.../event/include/msgid.lua read from
  ...payload\overlay\...`; the modified script loads with **no `script error`**.
- **Save bypass works**; the console save loads; hub reachable; a mission starts
  (`Ac4SortieScene`, `FcsManager`, briefing `lang/us/briefing/m160_b0.bin`).
- **Tutorial is reachable and lessons advance with inputs**: `TutorialScene` →
  `TutorialSortieScene`, and `m710 → m711` was observed after performing the lesson actions.

---

## 7. The open task (E3) — SUPERSEDED BY §0.6; the route below is DEAD

**Question:** does a non-player AC accept pad input? This decides whether co-op is a scripting job
(two characters, one pad each) or needs engine work.

> **Session 2 answer: engine work required (the third row of the table below).** `USERINPUT` is not a
> pad binding; there is one pad-reading operator and it is identified by call site. See §0.6, and see
> §7b for what to do next. The material below is kept only as a record of the dead route.

**Requirements:** a sortie containing a **second AC character script** (so
`Mission_EnableAutoPilot(eo,false)` / `eventOperation:SetTutorialInputMode(TUTORIAL_MODE__USERINPUT)`
can be called on it).

Facts already established:
- `m160` (what the save's hub offers) has **only** `m160_player.lua` — no second AC.
- Tutorial `m710` has only `m710_player.lua`.
- **`m717` is the first tutorial step with a second character (`m717_enemy.lua`).** — **FALSE.** That
  entity is `m717_ET_MISSILE`, a missile (`717_missile.lua` is its AI). No tutorial lesson has a second
  AC: each of m710–m719 ships exactly one `_player.lua` chara script. See §0.1.

**Recommended path:** ~~drive the tutorial lessons until `m717` loads~~ — **dead, see §0.1/§0.2.**
`tutorialene.lua` is an orphan referenced by nothing, so the prepared overlay never fires either.

**Lesson notes gathered so far** (AC4 tutorial, in order):
- "Move your AC to the target destination" — left stick forward (`ly +26000`)
- "Turn your AC (3 times)" — right stick `rx` ±26000, alternating
- "Adjust your view (3 times)" — right stick `ry` ±26000
- "Reset your view" — stick click (`R3` and/or `L3`)
- "Use Boost to move to the target destination" — **`LT` + left stick** (triggers were unsupported in
  the rig until this session; they now work)

**The observation that settles it:**
| observed | conclusion |
|---|---|
| second AC moves, player does not | engine binds input per character → co-op is scripting |
| both move together | one global pad binding → need a runtime hook routing pads per character |
| second AC ignores input | USERINPUT is AI-side only for non-players → engine work required |

---

## 7b. Where session 2 left it, and what to do next

E3 is answered (§0.6). Remaining work is the hook, and the tooling to test it now exists.

### What is proven

| # | Capability | Evidence |
|---|---|---|
| 1 | Overlay intercepts **BND3 world bundles** | `TEST OVERLAY: .../bind/world/m160_event.000 read from ...` |
| 2 | Repacked bundle scripts **execute** | injected `error(...)` → `[lua] script error ... AC4RIG-BND3-EXEC-PROOF` |
| 3 | BND3 parse/build is **byte-exact** | `tools\bnd3tool.py` round-trips m000/m160/m170/m710 |
| 4 | Missions can be **completed from Lua** | `SetMissionResult_Success`+`OnMissionSuccess` → RANK S → Chapter 2 |
| 5 | Story is **scriptable** | m160 auto-succeed unlocked Chapter 2 (SWARMS OF RED EYES + 3 more) |

### Next experiment (E4-pre): prove a hook can drive a second AC

No reachable mission yet has an ally AC: m010 (first Chapter 2 mission) ships only `m010_player.lua`.
Ally-AC missions are m050/m090/m170/m200 (and m000/m380/m390/m490). To reach one:

1. `tools\autosucceed.py m160` — patch m160 to auto-complete (this unlocks Chapter 2).
2. Also pre-patch the missions you must pass (`autosucceed.py m010 m020 …`). Progress is **in-memory
   only** (read-only console save), so clear them all within a single run.
3. When an ally-AC mission is selectable, patch **its** `_event.000`/`_mission.000` so the ally script
   calls `Mission_EnableAutoPilot(eo,false)` at mission start, and add a probe.
4. The real E4 test: a hook that writes a second AC's input object. The port already writes into the
   *player's* input object (`input_hooks.cpp`: `hook_view_turn_set` → `player_turn_call`; the mouse
   camera injects turn/pitch there), so the mechanism is known — it needs generalising to a chosen AC.
   The generic seam is `0x9fc210 ac_turn_input` (`r3` controller, `r4` input), which the port already
   hooks and which is reached for every AC.

### Overlay state at handoff

`payload\overlay` is **clean** — only `include\msgid.lua`, `chara\m000_joshua.lua`,
`chara\tutorialene.lua`. All research bundles were removed after the experiments. Per §0.3 the two
chara files there are **inert**.

---

## 8. Dead ends already explored (do not repeat)

**Save editing** (`11_SAVE_EDITING.md`): the console save is **encrypted** (entropy 7.96, no zlib
stream) so it cannot be edited. The port's own save is plaintext but **all zeros**; flooding it with
`0xFF` crashes the game; and an all-zero `GPROG.DAT` still yields the first mission, which proves the
**current mission is not stored in the save** (it is derived from progress). So a save edit cannot
point the game at an arbitrary mission.

**Save serialiser disassembly** (`12_SAVE_SERIALIZER_DISASM.md`): save request comes from
`0x00d41b64` → `cellSaveDataListSave2` (`0x00ddaa78`); callbacks are file `OPD 0x00EAD880` /
code `0x00d44590` (a pure dispatcher, 173 insns, **no calls**) and status `OPD 0x00EAD8B8` /
code `0x00d467b4`. Per-file guest buffers are logged by the port (GPROG at `0xe0269f10`, 3624 bytes).
The save-init function has **no `bl` callers** and its OPD `0x00EAD6D0` has **no static reference** —
it is dispatched through a runtime table, so static xref walking stops there.

**Level swap** (`13_LEVEL_SWAP.md`): mission bundles are **BND3 archives** with plaintext entry names
and a map tag (`m160` = `M31`, `m000` = `M25`). Serving m000's files under m160's names works at the
VFS level, and rewriting the internal ids (`m000`→`m160`, `M25`→`M31`) still **crashes at the same
instruction** (`ac4.exe+0x1e9773c`) right after `m160_mission.000`. A real conversion needs BND3
repacking plus retargeting a mission→map table outside the bundle.

> **Session 2 update:** the *repacking half is now solved* — `tools\bnd3tool.py` parses and rebuilds
> these bundles byte-exactly, and a rebuilt bundle in the overlay is used by the game (proven, §0.4).
> Only the mission→map retarget outside the bundle is still open. So this is a smaller dead end than
> written: do not redo the repacker, only the map-table half.

---

## 9. Key engine facts (for the write-up)

- AC4 guest memory is a single fixed **4 GiB** at `0x0000200000000000` (`ppu_recomp.h:17,58`) — cannot
  be duplicated in-process. PPU guest threads are real host threads → **no cross-machine determinism**,
  so no lockstep netcode.
- AC4 has **one** player entity id: `ENTITY_ID__PLAYER = 1000`.
- ACFA (Armored Core For Answer) co-op = "PARTNERSHIP": `PLAYER_0 = 1000`, `PLAYER_1 = 1001 =
  ENTITY_ID__FRIEND`, `GP_ID__PLAYERS = 100`, plus `IsCooperationMission()`, `IsExistFriend()`,
  `OnPlayerAllDestroyed`. **Online-only, 2 players max, Free Play — not campaign progression.**
- AC4's engine **already has** `SetEventPropertyGroup`, `SetEventHandleGroup`, `SetRadarShapeIDGroup`,
  `KillEventGroup`, `SetEnableImmortalGroup`, `SetEnableMoveGroup`, `SetEnableAttackGroup`,
  `SetTutorialInputMode`, `GetDistEntityPair`, `OpenSubWindowPlayerCameraFor`, …
  and **lacks** `IsCooperationMission`, `IsExistFriend`, `AttachEntityEventGroup`,
  `GetEventGroupDestroys`, `SendMessageToGroup`.
- AC4 ships its own UDP netcode (`Sw/Source/Network/Ac/*.cpp`, Dantelion2 `FSDP` + `UDPP2PSocket`,
  and **SystemLinkMatch** for LAN), but both ports stub `sceNp`/`sys_net` offline — so ACFA's netcode
  is **not** reusable through the port.
- **ACFA does not scale difficulty at runtime** (zero hits for `PlayerNum`/`GetPlayerNumber`). It uses
  per-player enemy group pairs (`_1P_`/`_2P_`) gated on `IsExistFriend()`, separate trigger regions per
  player, and averages AP across players (`apRate = (GetApRateOfAC(PLAYER_1) + apRate) / 2`).
- Recommended online route: **Route A** — implement real `sys_net` UDP + `socketpoll` over WinSock,
  `cellNetCtl` link, then bring up `SystemLinkMatch`. Fallback **Route B**: custom transport +
  snapshot sync with per-AC authority.

---

## 10. Files worth reading (in this order)

`research\` —
`00_README.md` (index + verdict), `02_ENTITY_AND_GROUP_MODEL.md` (entity/group delta, why E3 matters),
`03_MISSION_CONVERSION_PLAN.md` (48 missions, tiers A/B/C), `04_BALANCING.md`,
`05_ONLINE_LAN_ARCHITECTURE.md`, `06_DELIVERY_AND_EXPERIMENTS.md`, `07_RIG.md`,
`08_FIRMWARE_AND_SAVE.md`, `09_SAVE_LOADING_WORKS.md`, `10_GAP_STATUS.md`,
`11_SAVE_EDITING.md`, `12_SAVE_SERIALIZER_DISASM.md`, `13_LEVEL_SWAP.md`,
`A1_ac4_lua_api_reference.md`, `A2_mission_matrix.md`, `A3_engine_capability_matrix.md`,
`A4_engine_module_map.md`.

Data: `extracted\ac4_lua\` (UTF-8), `ac4_lua_raw\` (Shift-JIS), `acfa_lua_all\`.

**New in session 2 — `tools\`:**
- `bnd3tool.py` — `parse(path)` / `build(orig, entries)` / `parse_bytes(b)` / `roundtrip(path)` for AC4
  world bundles (`bind\world\mXXX_event.000`, `mXXX_mission.000`). Verified byte-exact on
  m000/m160/m170/m710.
- `autosucceed.py` — `python autosucceed.py m160 [m010 …]` patches a mission's event bundle so it
  completes the moment it starts; drop the result in
  `payload\overlay\PS3_GAME\USRDIR\bind\world\` and the game will use it.

Quick recipe for an arbitrary mission edit:

```python
import sys; sys.path.insert(0, r"...\coop-mod\tools")
import bnd3tool as T
orig, entries = T.parse(disc_bundle)          # e.g. disc\...\bind\world\m170_event.000
for e in entries:
    if e['name'].endswith('m170.lua'):
        e['data'] = bytearray(bytes(e['data']).replace(b'ANCHOR', b'ANCHOR + edit', 1))
open(overlay_bundle, 'wb').write(T.build(orig, entries))
```

Scripts inside bundles are **Shift-JIS with CRLF** — edit bytes, keep ASCII-only where possible.
Overlay: `payload\overlay\PS3_GAME\USRDIR\...` (currently `include\msgid.lua`,
`chara\m000_joshua.lua`, `chara\tutorialene.lua`).

**Delivery rule for the eventual mod:** ship **patches**, not game Lua (the port's rule is "nothing
from the game is in this repository"; precedent: `src/runtime/script_fixes.cpp` hooks `lua_load`).

---

## 11. Verdict on file

**Revised by session 2.** Co-op is still feasible, but the architecture question is now answered and
it is *not* the "cheap scripting" answer: **E3 = engine work required** (§0.6). `USERINPUT` selects an
entity's own controller, not a pad; the pad reaches exactly one AC through one operator.

The good news is that the engine work is narrowly scoped and the port is already shaped for it: every
AC's commands pass through `0x9fc210 ac_turn_input (r3 controller, r4 input)`, which the port already
hooks, and the port already injects into the *player's* input object for mouse support. Generalising
that to a chosen second AC is the E4 hook, not a rewrite.

Two blockers from the original assessment are also **retired**:

- **Mission scripting is no longer blocked.** Story-mission Lua is inside BND3 bundles, but the overlay
  intercepts bundle paths and `tools\bnd3tool.py` round-trips them exactly — proven by injecting Lua
  into `m160` and watching it execute. So delivering a mod as *patches* to the user's own bundles is
  viable (§0.4), which is exactly the licensing-safe shape the project wants.
- **Progression can be driven from Lua.** `SetMissionResult_Success` + `OnMissionSuccess` completes a
  mission instantly (RANK S) and advances the story (§0.5), so any mission is reachable in one run
  without playing it. Progression is in-memory only while the console save is read-only.

Remaining work: the E4 input hook, then mission scripting (ally groups), balancing (copy ACFA's `_2P_`
pattern since AC4 lacks the helper), and a transport.
