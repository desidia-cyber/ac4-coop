# AC4 2-player co-op — research dossier

Working folder: `G:\Freespace\Armored Core 4 (USA)\coop-mod\`
Status: **research complete enough to start implementation.** Everything here is evidenced; the one
open question is called out explicitly in §4 and has a cheap test.

---

## 0. Executive summary

The mod is **feasible**, and much closer than the earlier "research project" verdict suggested,
because of two discoveries:

1. **AC4's campaign logic is plaintext Lua 5.0 loaded from disc files at runtime.** 874 scripts, one
   per mission plus per-entity character scripts. The mission layer is therefore *editable data*, not
   opaque PowerPC.
2. **AC4's engine already contains the group machinery ACFA's co-op is built on** — and AC4's own
   scripts already use it heavily (`SetEventPropertyGroup` 138×, `SetEventHandleGroup` 307×,
   `DeleteHandlGroup` 106×, `IsEntityExist`, `RegistHandle`, `WM_ENTITYFACTORY_NOTIFY`). ACFA's
   co-op layer is a thin *script-level* addition on top of that same machinery.

What ACFA adds on top (proven from its disc) is small and portable:

- `ENTITY_ID__PLAYER_1 = ENTITY_ID__PLAYER + 1` (= 1001), which is the **same id as `ENTITY_ID__FRIEND`**
  — player 2 occupies the AI-ally slot.
- `GP_ID__PLAYERS = 100`, a player group.
- `OnPlayerAllDestroyed` — fail only when the whole player group is gone.
- `ObservePlayerMissionRegionOutOrTimeout` — region-out judged for the group.
- `IsCooperationMission()` / `IsExistFriend()` gating.

Crucially, **the two Lua helpers that carry most of that behaviour are themselves written in Lua**
(`IsExistFriend`, `OnPlayerAllDestroyed`) and depend only on primitives AC4 already has.

### The remaining blocker, isolated

Whether AC4's engine will drive a **second, pad-controlled AC**. Everything else is authoring work.
§4 of `02_ENTITY_AND_GROUP_MODEL.md` gives the exact test, which is pure Lua plus a config flag and
takes minutes to run — no reverse engineering needed to answer it.

### Deliberately out of scope of "reuse ACFA's code"

ACFA's *networking* is not reusable. ACFA's Partnership is PSN netcode inside the PPC binary; neither
port implements it. Online/LAN co-op means writing transport plus world-state sync from scratch
(see `05_ONLINE_LAN_ARCHITECTURE.md`). That is the largest single work item in the project.

---

## 1. What was produced, and how

| Item | Where |
|---|---|
| Decrypted executable | `tools/work/files/EBOOT.elf` |
| Recompiler (built here) | `build/ac4-recomp.exe` |
| AC4 disc Lua, UTF-8 | `extracted/ac4_lua/` |
| ACFA disc Lua, UTF-8 | `extracted/acfa_lua_all/` |
| Build/runtime source | `ac4-port/`, `acfa-port/` |
| Documents | `research/` |

Reproduction details are in `01_ENGINE_ANALYSIS.md` §1.

---

## 2. Document map

| File | Contents |
|---|---|
| `01_ENGINE_ANALYSIS.md` | Binary facts: hashes, recompiler stats, entity/pad structures, what the engine has |
| `02_ENTITY_AND_GROUP_MODEL.md` | AC4 vs ACFA entity/group model, the exact Lua delta, **the open question and its test** |
| `03_MISSION_CONVERSION_PLAN.md` | All 48 missions, what breaks, per-mission change list, categories |
| `04_BALANCING.md` | How ACFA balances 2 players (it does not scale), and the concrete AC4 plan |
| `05_ONLINE_LAN_ARCHITECTURE.md` | Transport, lobby, world-state sync, authority, pitfalls |
| `06_DELIVERY_AND_EXPERIMENTS.md` | Packaging via VFS overlay, folder layout, ordered experiment plan, risks |
| `A1_ac4_lua_api_reference.md` | Generated: every `eventOperation:` method, engine presence, ACFA usage |
| `A2_mission_matrix.md` | Generated: per-mission hazard counts |
| `A3_engine_capability_matrix.md` | Generated: engine present/absent matrix, co-op primitives |
| `AC4-COOP-ACFA-REFERENCE.md` | Earlier deep dive on ACFA's PARTNERSHIP implementation |
| `AC4-COOP-DISC-FINDINGS.md`, `AC4-COOP-CAMPAIGN-FEASIBILITY.md`, `AC4-COOP-FEASIBILITY.md` | Earlier passes, kept as background |

---

## 3. Facts at a glance

**Disc / executable**

| | |
|---|---|
| ISO | 15,538,585,600 B, ISO9660+UDF, 10,353 entries |
| `EBOOT.BIN` | 17,244,256 B, sha256 `166f4a6b…d8db2325` (= `kEbootBin`) |
| `EBOOT.elf` | 17,241,824 B, sha256 `0fd1e2ee…930daaa1` (= `kEbootElf`) |
| Recompiled | 53,904 functions, 3,590,461 reachable insns, 99.30% code coverage, 0 unresolved `bctr` |

**Scripting**

| | |
|---|---|
| Lua files on disc | 874 |
| Mission event scripts | 48 (`m000`–`m490`, `m710`–`m719`) |
| Missions with a P1-death handler | **48 / 48** |
| Missions with AP-threshold logic | 13 |
| Missions with an ally/second-AC character script | **27** |
| Distinct `eventOperation:` methods used | 104 |
| Lua-defined functions (AC4 tree) | 1,373 |

**Engine**

| | |
|---|---|
| Group ops | present and already used by AC4 |
| `WM_ENTITYFACTORY_NOTIFY` | present (`WM_APP+4`) |
| `SetTutorialInputMode` | present |
| `IsCooperationMission` / `IsExistFriend` | absent — but both are Lua-level, so free to add |
| Player entity ids | `ENTITY_ID__PLAYER = 1000` only |

---

## 4. The one open question

**Does AC4's engine drive a second AC from a second pad?**

- AC4 registers *one* player entity id and its scripts assume one player.
- ACFA proved the engine family supports a second one at id 1001.
- AC4's `SetTutorialInputMode` (the per-character "user input" switch) *is* present.
- What is unknown is whether an AC at id 1001 (or a mission-local id) is accepted by the operator /
  input / camera path, and whether that path can bind to pad port 1.

Test: `02_ENTITY_AND_GROUP_MODEL.md` §4. Do it **before** committing to the architecture.
If it fails, fallback is a small runtime hook using the pad object's port field
(`+384` / `+736`, see `01_ENGINE_ANALYSIS.md` §3) — the port already proves this style of hook works.
