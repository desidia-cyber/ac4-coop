# 10 — Gap status: what is closed and what is still open

## Closed (verified by the rig, with evidence)

| # | Gap | Evidence |
|---|---|---|
| 1 | Firmware version | `G:\...\PS3UPDAT.PUP` is **4.93**; all 10 decrypted modules match `kModules493` |
| 2 | Canonical build | prepared from 4.93; SPU stage `0 skipped, 0 rebuilt in part` (was 198 / 196 on 4.92) |
| 3 | Console save is readable | rig bypass accepts it: `save accepted (rig bypass; not a player build)` |
| 4 | Save content loads | `NOW LOADING …` → `MissionSelectScene` → hub |
| 5 | Mission can be started | `Ac4SortieScene` + `FcsManager` started; briefing file `lang/us/briefing/m160_b0.bin` |
| 6 | **E1 — Lua overlay delivery** | `TEST OVERLAY: /dev_bdvd/.../event/include/msgid.lua read from ...\payload\overlay\...` |
| 7 | **E2 — modified Lua loads cleanly** | the overlaid `msgid.lua` is read and **no `script error` lines appear** |
| 8 | Rig: hidden | window created invisible, process at Idle priority, ~58 fps |
| 9 | Rig: silent | `out_write` returns early when muted; no audio submitted |
| 10 | Rig: scripted input | START opens the menu, taps move the cursor, Cross confirms |
| 11 | Rig: live steering | script reload on change + `clock.txt`, so runs are driven in a closed loop |

## Hub UI, for the record

```
Chapter1 : Destination Unknown
  FIRST STRIKE   Area: Gryphon, The Independent City
  Objective: Defeat Main Enemy   Projection Damage: 8000 AP   Mission Time: 180 S
  [mission preview]   CURRENT MISSION: FIRST STRIKE (last mission of chapter 1)
  SCHEMATICS | ACSIS | SYSTEM
```

`START` = KEY GUIDE. `LEFT`/`RIGHT` move between the mission panel and the right-hand buttons;
`UP`/`DOWN` move within a panel; `A` confirms. Accepting the mission shows a confirm dialog whose
**CANCEL button is focused by default** — `LEFT` then `A` accepts it (this was the step that kept
failing).

`SYSTEM` opens: SAVE GAME / LOAD GAME / DELETE GAME / CONTROL OPTIONS / GAME OPTIONS /
REGULATIONS / QUIT GAME / **PC SETTINGS** — the last is the port's own injected row, which confirms
`system_menu.cpp`'s menu-archive patch works on this build. There is **no mission list** there.

Note: with the bypassed save the hub reports `CASH 0 / COMPLETE 0 / MISSION RANK E` — the mission
progression loaded but the profile fields did not, so this save is only partially honoured.

## ~~BLOCKER~~ RESOLVED (session 2): a real profile now exists

**Solved in-game, no port changes needed.** The user played NEW GAME through the tutorial and one
mission, then saved via SYSTEM → SAVE GAME. Verified afterwards with the rig:

- save loads with **status callback 0/1 (OK)** — the rig bypass is no longer involved;
- `GetApRateOfAC(ENTITY_ID__PLAYER)` / on-screen AP = **35888** (was `0`);
- `ACDES.DAT` holds a real build (`TYPE-HOGIRE` / `NE-L` / `Rosenthal`), entropy 0.96, plaintext;
- hub shows `Reward 50000 C` (was `-`);
- the AC renders, the radar shows live contacts, and it drives at **119 km/h** through a mission's
  tunnel interior with full geometry.

Two missions were then completed by the rig and the mission list advanced, so progression persists in
the save too.

Also fixed in the port: `rig::init()` muted audio unconditionally, so a normal visible run was silent.
Now `if (c.rig_script.empty() && c.rig_shots.empty()) g.muted = false;`.

**Rule from here on:** never let the old encrypted console save replace the new plaintext one at
`run\data\hdd0\home\00000001\savedata\BLUS30027GAMEDAT8MS4R1IYVB3D000`.

---

## Original diagnosis (kept): the player AC had no build — missions were unplayable

Entering any story mission shows only the HUD: no AC, no enemies, no world objects, stick does nothing,
`0 km/h`. **Not a regression** — it reproduces on a pristine overlay and was present from the start.

Measured with a Lua probe on `m160`'s `OnMissionStart`:

```
PROBE2 origOk=true err=nil | AP=0 | objN=2 | objA=2 | heriA=24 | self=nil
```

The world spawns fine (2 objectives, 24 heli parts, 699 draws/frame) but
`GetApRateOfAC(ENTITY_ID__PLAYER) == 0` — the player AC has **no assembly**, so there is nothing to
draw or drive.

Cause: the console save is **encrypted** and the port does not decrypt it (`save_data.cpp` stores saves
unencrypted). `rig::save_bypass()` only makes the corrupt-callback return `CB_OK_LAST` so the **load
list** proceeds; the data files stay ciphertext. Hence a working *menu* (progress comes from
`PARAM.SFO`) over an empty *profile*. The game also requests `BLUS30027ASSMBLY064` (the AC assembly
save) and gets `0 file(s)`/`-4`. NEW GAME works because it creates a valid AC.

Unblock by: playing NEW GAME to a save (tutorial is proven and does progress), adding save decryption
(RPCS3 is the reference), or importing an RPCS3-created profile. Until then, mission *scripts* are
still fully testable; piloting is not. See `HANDOFF.md` §0.7.

---

## E3 — ANSWERED (session 2): engine work required

**E3: does AC4's engine drive a non-player AC from a pad?** **No — not by scripting.**

What settles it:

- `TUTORIAL_MODE__USERINPUT` is **not** a pad binding. Reading `msgmacro.lua`, the tutorial operator's
  mode only decides whether *scripted tutorial operations* are pushed (`AUTO`) or the entity's own
  controller drives (`USERINPUT`). For the player that controller is the pad; for an AI AC it is the AI.
  AC4's own ally scripts rely on this: `Mission_EnableAutoPilot(eo,false)` = 手動運転モード is called on
  AI allies (m000/m050/m090/m170/m200/m380/m390/m490) and they keep behaving as AI.
- The port confirms a single player path: exactly one pad-reading operator (`0xb52584`), and "the
  player" is identified by **call site** (`kPlayerTurnSetReturn = 0xb52708`). `hook_input_turn_set`
  notes "the player's in the SIMULATOR (the operator's call), **AI inputs elsewhere**".

So a second AC needs a **runtime hook** that writes its input object — the port's E4 step. The seam is
already known: every AC's commands pass through `0x9fc210 ac_turn_input (r3 controller, r4 input)`,
which the port already hooks, and the port already injects into the *player's* input object for mouse
support.

### Corrections to the older "Still open" text below

- **The tutorial route cannot work.** `m717_enemy.lua` is the chara for `m717_ET_MISSILE` — a *missile*,
  not an AC. And m710–m719 each have only a `_player.lua` chara script: the tutorial has **no second
  AC**. `tutorialene.lua` is referenced by nothing (an orphan), which is why overlaying it appeared to
  do nothing.
- **Courtesy of session 2, the content problem is solvable:** story-mission Lua lives in BND3 bundles,
  but `--test-overlay` *does* intercept bundle paths and `tools\bnd3tool.py` repacks them byte-exactly.
  Injecting Lua into a bundle is proven to execute (`[lua] script error ... AC4RIG-BND3-EXEC-PROOF`).
- **Progression is scriptable:** `SetMissionResult_Success` + `OnMissionSuccess` in a mission's
  `OnMissionStart` completes it instantly (RANK S) and advances the story; that is how Chapter 2 was
  reached without playing. Progress is in-memory only while the console save is read-only.

See `HANDOFF.md` §0 and §7b for the full evidence and the E4-pre plan.

---

## Still open — as originally written (superseded above)

**E3: does AC4's engine drive a non-player AC from a pad?**

Blocked on content, not on tooling: the test needs a sortie that contains a second **AC character
script** (an ally/enemy AC with its own `eventOperation`, so `Mission_EnableAutoPilot(eo, false)` can
be called on it). Today the rig can reach:

- the loaded save's hub, which offers exactly one mission — `FIRST STRIKE` = **m160**. `m160` has
  only `m160_player.lua`; its enemy is an AI script (`airesource/script/m160/160_ga_ac.lua`), which
  has no event operation to switch.
- NEW GAME, which goes to `TutorialScene` (the tutorial), not a story mission.

Missions that *do* have a second AC character script: `m000`, `m050`, `m170` (Joshua), `m090`
(white AC), and the many missions with an `_ac.lua` (enemy AC event character).

### Three ways to reach one — tested results

1. **Tutorial route.** Tried. NEW GAME → `TutorialScene` (49 s) → `TutorialSortieScene` (69 s), and
   the tutorial's first sortie is **m710**, which loads only `m710_player.lua` — no second
   character. Overlaying `tutorialene.lua` (the tutorial's dummy enemy) had **no effect because m710
   never loads it**. Pressing A repeatedly does not advance past m710: the tutorial requires actually
   performing the lesson's inputs (move, boost, fire). So this route needs the tutorial *played*.
   `tutorialene.lua` is used by later steps; `m717` is the first tutorial step with a second
   character (`m717_enemy.lua`).
2. **Progress the story.** Not attempted beyond starting m160 — the bypassed save's profile is empty
   (`CASH 0 / COMPLETE 0 / MISSION RANK E`), so results would be unreliable.
3. **A start-of-game save** where `m000` is the current mission. Untested — no such save available.

### The observation to make once in such a mission

With the ally/enemy AC set to `TUTORIAL_MODE__USERINPUT`, feed the rig's pad and watch whether that
AC moves, and whether the player's AC moves at the same time:

| observation | conclusion |
|---|---|
| the second AC moves and the player does not | the engine binds user input per character — co-op is a scripting job |
| both ACs move together | one global pad binding; a runtime hook must route pads per character |
| the second AC ignores input | `USERINPUT` is AI-side only for non-player entities; engine work needed |

That single observation decides the whole architecture in `02_ENTITY_AND_GROUP_MODEL.md`.
