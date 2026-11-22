# Engine capability matrix (authoritative)

Ground truth: the string table of the decrypted `EBOOT.elf`
(`tools/work/files/EBOOT.elf`, 17,241,824 bytes, sha256 `0fd1e2ee4dbc599874fabccd596f922904955d8f5c1dbd92a34101ac930daaa1`).
Lua 5.0 registers C functions by name, so a name present there means the engine has the function.

Counts are occurrences across each game's whole script tree.

## 1. Group machinery — AC4 already has it and already uses it

| method | in AC4 ELF | AC4 uses | ACFA uses |
|---|---|---|---|
| `SetEventPropertyGroup` | YES | 137 | 404 |
| `SetEventHandleGroup` | YES | 305 | 292 |
| `DeleteHandlGroup` | YES | 106 | 40 |
| `SetRadarShapeIDGroup` | YES | 43 | 113 |
| `GetEventGroupAlives` | YES | 22 | 114 |
| `KillEventGroup` | YES | 10 | 56 |
| `SetEnableImmortalGroup` | YES | 8 | 4 |
| `SetEnableMoveGroup` | YES | 5 | 88 |
| `SetEnableAttackGroup` | YES | 2 | 110 |
| `SetEnableGravityGroup` | YES | 56 | 64 |
| `ObserveRegionEventGroupInsideSomeone` | YES | 1 | 2 |
| `IsEntityExist` | YES | 0 | 0 |
| `RegistHandle` | YES | 71 | 82 |
| `SetMissionEndingFlowCAPEvent` | YES | 14 | 5 |
| `SetMissionMode` | YES | 13 | 0 |

## 2. Co-op layer — present in ACFA, missing in AC4

| primitive | in AC4 ELF | defined in ACFA Lua | ACFA uses | status for AC4 mod |
|---|---|---|---|---|
| `IsCooperationMission` | no | - | 7 | author as a Lua stub returning true |
| `IsExistFriend` | no | def | 0 | Lua helper: port verbatim (uses IsEntityExist) |
| `OnPlayerAllDestroyed` | no | def | 0 | Lua helper: port verbatim (uses RegistHandle + WM_ENTITYFACTORY_NOTIFY, both present in AC4) |
| `ObservePlayerMissionRegionOutOrTimeout` | no | def | 1 | Lua helper wraps an engine call AC4 lacks; reimplement with per-player observers |
| `SetMissionModeAllEntity` | no | - | 5 | reimplement per entity |
| `AttachEntityEventGroup` | no | - | 30 | AC4 lacks it; find equivalent or attach per entity |
| `GetEventGroupDestroys` | no | - | 2 | low priority (2 uses) |
| `IsEventGroupInRegionAll` | no | - | 15 | low priority |
| `IsEventGroupInRegionSomeone` | no | - | 34 | low priority |
| `SetEnableSuperOverBoostGroup` | no | - | 10 | ACFA-only feature (overed boost), irrelevant to AC4 |
| `SetEnablePAGroup` | no | - | 1 | use per-entity SetEnablePA |
| `SendMessageToGroup` | no | - | 4 | send per member |

## 3. Constants AC4 already defines (no Lua work needed)

`WM_ENTITYFACTORY_NOTIFY = WM_APP+4`, `WM_REGION_IN_GROUPSOMEONE_NOTIFY = WM_APP+33`,
`WM_TEAM_NOTIFY = WM_APP+7`, `WM_EVENT_GROUP_MSG = WM_APP+17`, and the whole `WM_*` family
(`common/event/include/msgid.lua`). Entity IDs: only `ENTITY_ID__PLAYER = 1000` exists.
