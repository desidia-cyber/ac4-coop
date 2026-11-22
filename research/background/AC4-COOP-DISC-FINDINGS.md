# AC4 campaign co-op — disc analysis findings

Source: the user's ISO, `G:\Freespace\Armored Core 4 (USA)\Armored Core 4 (USA).iso`
(15,538,585,600 bytes, ISO9660+UDF, 10,353 entries).
Extracted scripting tree: `./disc` (raw, Shift-JIS) and `./disc_utf8` (UTF-8) — 897 files, 4.44 MB.

**This supersedes the earlier "research project" verdict.** The campaign's logic is *not* opaque
machine code. It is plaintext, per-mission Lua that the game loads from disc files at runtime.

---

## 1. Disc identity — confirmed

| Check | Result |
|---|---|
| `PS3_GAME\USRDIR\EBOOT.BIN` | **17,244,256 bytes** — exactly matches `ac4::inputs::kEbootBin` (`known_inputs.cpp:5`) |
| Disc layout | `PS3_DISC.SFB`, `PS3_GAME\{PARAM.SFO, ICON0.PNG, ...}`, `PS3_UPDATE` — retail US |
| Conclusion | This is the exact disc the port's hooks target (BLUS30027, 01.01) |

## 2. The game is Lua-scripted, and the scripts are source

874 `.lua` files, plaintext Lua 5.0, loaded at runtime:

- `common/event/include/inc.lua` does `dofile("game:\\common\\event\\include\\misc.lua")` etc.
- Mission scripts call `LoadEventStdScriptFile(CURR_DLLUA_INSTANCE)` and
  `LoadMissionConstValue("000", false)` — i.e. **one Lua instance per character/entity**.
- Comments are Shift-JIS Japanese (decoded to UTF-8 in `./disc_utf8`).

Layer map:

| Path | Contents |
|---|---|
| `common/event/mission/mXXX.lua` | 48 numbered mission event scripts (m000–m490, m710–m719) |
| `common/event/mission/chara/mXXX_player.lua` | **per-mission player character script** — one for every mission |
| `common/event/mission/chara/mXXX_{joshua,ac,boss,ene,fortress,ship}.lua` | per-entity character scripts, incl. **ally ACs** |
| `common/event/include/mXXX/{constvalue,entityid,pointid,eventgroup}.lua` | per-mission constants, entity IDs, trigger points |
| `common/event/include/{misc,msgmacro,msgid,inlfunc}.lua` | the API surface and macros |
| `common/airesource/script/mXXX/` | per-mission **AI behaviour** scripts |
| `cap/mission/mXXX.cap` | per-mission placement/entity capture data |

## 3. The entity model: exactly one player

From the shared header-equivalent `common/event/include/msgid.lua:65-67` (section header literally
says `WorldFwdDef.h — エンティティ識別id`):

```lua
ENTITY_ID__INVALID       = 0
ENTITY_ID__PLAYER        = 1000
ENTITY_ID__BRIEFINGCARRIER = 10000
```

A full-text search of all 874 scripts for `PLAYER2`, `PLAYER_2`, `2P`, `coop`, `MultiPlayer`,
`NetPlayer`, `PlayerIndex` returns **nothing relevant** (only false positives like `m200_PT_DROP2_VIEW`).

Every mission is written around that single ID:

```lua
ObserveMissionRegionOut( eventOperation, ENTITY_ID__PLAYER, "OnOutOfMissionArea" )
ON_DEAD_ENTITY(         eventOperation, ENTITY_ID__PLAYER, "OnDeadPlayer" )
ObserveRegionExist(     eventOperation, m210_PT_DOCK01, ENTITY_ID__PLAYER, "OnPtDock01" )
eventOperation:GetApRateOfAC( ENTITY_ID__PLAYER )
```

## 4. The decisive primitive: per-character input mode

`common/event/include/msgid.lua:180-182`:

```lua
TUTORIAL_MODE__USERINPUT      = 0
TUTORIAL_MODE__AUTO           = 1
TUTORIAL_MODE__BLEND__AUTO_USER = 2
```

`common/event/include/msgmacro.lua:1631-1639`:

```lua
function Mission_EnableAutoPilot( eventOperation, isEnable)
    if ( true == isEnable) then eventOperation:SetTutorialInputMode( TUTORIAL_MODE__AUTO);
    else                     eventOperation:SetTutorialInputMode( TUTORIAL_MODE__USERINPUT); end
end
```

Two things matter here:

1. **Input mode is set on `eventOperation`** — a property of a *specific character instance*, not a
   global. The engine therefore tracks "who is being driven by user input" per entity.
2. **The game already swaps ally ACs into `USERINPUT` mode.** `m000_joshua.lua:126`,
   `m050_joshua.lua:105`, `m090_ac.lua:60`, `m170_joshua.lua:108`, `m200_ac.lua:86`,
   `m380_ac.lua:86` all call `Mission_EnableAutoPilot(eventOperation, false)` →
   `TUTORIAL_MODE__USERINPUT` on a **non-player AC**.

That is the co-op hinge: the engine has a per-character "driven by user input" state, and it is
already applied to ACs that are not the player.

**What is *not* exposed:** no Lua API for pad port/controller index anywhere
(`PadPort`, `SetPort`, `ControllerIndex`, `GetPad`, `InputPort` → zero hits). So *which* pad
services `USERINPUT` is decided in the binary, not the scripts. That is the one remaining question.

## 5. Ally ACs are first-class, controllable entities

`m000_joshua.lua` is a complete ally-AC script: `ON_DESTROY_ME`, `ON_RECIEVE_USER_MESSAGE`,
`SetEnableImmortalEnergy`, `SetEnablePA`, `SetEnableEntityCollide`, `SetEnableAutoBoost`,
`Mission_EnableAutoPilot`, plus scripted camera and mission-result calls. The player's own script
`m000_player.lua` even commands Joshua via `SendUserMessageToEntity(eventOperation, m000_ET_JOSHUA, ...)`
to call in reinforcements.

So "spawn a second AC that the mission script fully controls" is an existing, supported capability.

## 6. The engine already has a second viewport

`m000_joshua.lua:77` calls `OpenSubwindowFixedPos(eventOperation, m000_PT_camera_003, duration)` —
the game renders a **sub-window (picture-in-picture) camera** during missions. That is not
split-screen, but it proves the renderer/scene system supports more than one camera view inside a
mission, which materially de-risks player 2's view.

## 7. Mods can be delivered as data, not a rebuilt port

The port has two shadowing mechanisms:

- `rt::vfs::replace(disc_relative, host)` — used by `system_menu.cpp` to serve a patched
  `systemsetting.000` (`vfs.cpp:154`).
- **`--test-overlay DIR`** — a directory laid out like the disc root whose files are read instead of
  the disc's, read-only, every hit logged (`vfs.h:6-7`, `vfs.cpp:141-150`).

Because the Lua is loaded from files at runtime, **a co-op mod can ship as an overlay of edited
`.lua` files with no binary patching and no game recompile.** That removes the distribution blocker
noted in the previous assessment.

## 8. Revised verdict

| Question | Before disc | After disc |
|---|---|---|
| Is mission logic moddable? | Unknown (opaque code) | **Yes — plaintext Lua, editable** |
| Does a second player slot exist? | Unknown | **No** — single `ENTITY_ID__PLAYER = 1000` |
| Can an AC be driven by "user input"? | Unknown | **Yes, per character** — `SetTutorialInputMode(USERINPUT)`, already used on ally ACs |
| Can a second camera render? | Unknown | **Partly** — sub-window PiP exists |
| Can a mod ship without recompiling? | No | **Yes** — `--test-overlay` |
| Which pad drives a USERINPUT entity? | Unknown | **Still unknown** — engine-side |

### So: feasible, with one narrow gate and a large but *ordinary* workload

**The gate (small, testable in Lua, no binary work):** does `TUTORIAL_MODE__USERINPUT` on a second
AC entity consume pad port 0 (globally) or a pad bound to that entity? Test plan:

1. Overlay a patched `m000_joshua.lua` that sets `SetTutorialInputMode(TUTORIAL_MODE__USERINPUT)`
   at mission start and removes the autopilot/arrival logic.
2. Launch m000 and see whether Joshua responds to the pad.
3. If yes, the next question is pad selection: whether the engine takes port 0 only, or whether the
   port's `cellPad` ports 1-3 (`sys_io.cpp:2-4`) can be bound to him — likely needing a small
   runtime hook at `pad_update` (0x951f94) to serve port 1 for that character's pad object.

**If the gate passes**, the remaining work is mission authoring, not reverse engineering:

- 48 mission scripts, each written around one `ENTITY_ID__PLAYER`. Every mission needs a second
  entity acknowledged: death/fail conditions, objective triggers, dialogue, and the per-mission
  `chara/mXXX_player.lua` duplicated for a second player. That is *scripting* work with a fast
  iteration loop (edit Lua → relaunch mission), which is a completely different risk profile from
  PPC disassembly.
- Player 2 needs a loadout: either a copied AC build or a stripped/limited set, driven by the port's
  overlay UI (`ui/overlay.h`) or by a new entry patched into the menu archive.
- Split-screen: reuse/duplicate the sub-window path, or accept two windows / remote play.

**Still genuinely hard, and unchanged:**
- **Online two-PC co-op** still needs world-state sync (two processes, two worlds), which remains
  undocumented reverse-engineering. Everything above gives you *local* co-op.
- Being exact about "start to finish" — missions with scripted cameras, timed sequences and
  single-AC narratives (the duels) will each need bespoke attention.
- The base port's own stability (`ppu_thread.cpp:125-128` still references a mission-start crash in
  the sound lookup, with `STATUS` unpublished).

## 9. What I'd do next

1. Build `ac4-recomp` from the repo (g++ 15.2.0 / cmake / ninja are present here) and confirm the
   disc's `EBOOT.BIN` decrypts to the exact `kEbootElf` hash — requires your RPCS3 step.
2. Run the `--test-overlay` experiment in §8. It is a data edit and answers the gate directly.
3. Only if the gate passes, scope the 48-mission scripting pass.
