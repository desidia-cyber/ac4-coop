# 09 — Save loading now works (rig bypass) — and the exact driving sequence

## 1. The bypass

`src/runtime/hle/save_data.cpp`, in `callback_error()`: when the game's status callback returns
`CB_ERR_BROKEN` (-3) **and the rig is enabled**, the save is accepted and the callback result is
rewritten to `CB_OK_LAST`:

```cpp
if (r == CB_ERR_BROKEN && rt::rig::save_bypass()) {
  LOG_W("savedata", "%s: save accepted (rig bypass; not a player build)", op_name(op));
  const_cast<CBResult&>(res).result = CB_OK_LAST;
  return CELL_OK;
}
```

`rig::save_bypass()` returns true only when a `--rig` script is active, so this cannot affect a
normal (player) run. It is a research instrument, not a shippable change.

Log confirmation:

```
cellSaveDataListLoad2: 'BLUS30027GAMEDAT8MS4R1IYVB3D000' (existing, 7 file(s), 606 KB)
cellSaveDataListLoad2: status callback returned -3
cellSaveDataListLoad2: save accepted (rig bypass; not a player build)
```

## 2. Result: the save loads and the hub is reached

After the bypass the screen showed **"NOW LOADING …"** with the AC schematic overlay, then:

```
[ 191.4215 ] thread 'MissionSelectScene' started
[ 192.5077 ] thread 'FcsManager' started
```

and the hub appeared:

```
Chapter1 : Destination Unknown
FIRST STRIKE — Area: Gryphon, The Independent City
Objective: Defeat Main Enemy   Projection Damage: 8000 AP   Mission Time: 180 S
LAST MISSION OF CHAPTER 1
GARAGE / SCHEMATICS / ACSIS / SYSTEM
```

So the supplied console save's **content is valid and loadable**; only AC4's integrity check objected.

## 3. The exact sequence that gets from boot to the hub

This is now proven end to end. It must be driven live (timings drift between runs), using the rig's
clock file and script reload:

1. Launch with `--rig <script> --rig-shots <dir> --rig-shot-every 2 --hidden --seconds 420`
   and `--test-overlay payload\overlay`.
2. Wait until `clock.txt` passes ~12 s. Press a burst of `A, B, A+B, START, A, B, A+B, A` about 2.5 s
   apart. This clears the port's "HDD access indicator" dialog **and** the game's own
   "Cannot check update file" message, which otherwise blocks the title indefinitely.
3. When the screen reads **PRESS START BUTTON**, press `START`.
4. On the menu, **tap** `DOWN` — a *very short* press (~0.15 s). Holds are swallowed by this frontend;
   taps register. One tap moves one item. Verify from the screenshot that **LOAD GAME** is highlighted
   (the order is NEW GAME / LOAD GAME / MULTIPLAYER; taps can occasionally move two items, so check).
5. Press `A`. The load list opens, the save is accepted (bypass), and the game loads it.
6. Wait ~15 s for "NOW LOADING" → hub.

Notes recorded while doing it:

- The title menu cursor **does not move on a held direction**, only on a tap.
- Each A press at the title menu confirms **NEW GAME**, so A must not be spammed once the menu is up.
- The game re-runs its update check when it returns to the title, so a fresh window of dialogs appears
  after any return.

## 4. Remaining step to the decisive test

From the hub, the current mission ("FIRST STRIKE", last of Chapter 1) was selected and a confirmation
dialog appeared — *"Clearing this mission will lead to the next chapter. Accept the mission?"* with
**OK** highlighted — but the accepted press did not start a sortie in that session (no
`*SortieScene` thread appeared). That needs one more pass with the confirm timed correctly.

Two things still to do:

1. **Start a mission reliably** from this hub (confirm dialog timing, then briefing → sortie).
2. **Pick a mission with a second AC.** With all missions unlocked from this save, the objective is
   `m000`/`m050`/`m170` (Joshua) or `m090` (white AC). `FIRST STRIKE` is the last mission of chapter 1,
   so it is not one of them — the mission list has to be navigated, or the overlay used to force the
   `USERINPUT` change on whatever mission is started first (fine for a first look at whether a
   non-player AC accepts pad input, which is the actual question).

Once a mission is running, the E3 observation is: set the ally entity to
`Mission_EnableAutoPilot(eventOperation, false)` (already done for `m000_joshua.lua` in the overlay) and
see whether the ally responds to the rig's pad.
