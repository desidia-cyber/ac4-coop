# 01 — Engine analysis (binary facts)

All figures below were measured in this workspace. No claim here is inferred from documentation.

---

## 1. How the executable was obtained (reproducible)

The port never decrypts anything; its launcher drives the user's own RPCS3. The same two steps were
run here against the user's own disc and firmware:

1. Extract `PS3_GAME/USRDIR/EBOOT.BIN` from the ISO (7-Zip).
   - 17,244,256 bytes, sha256 `166f4a6b024f0abe6349b7cf481c95f445f6af31f3e2846fa0523d2fd8db2325`
   - this is exactly `ac4::inputs::kEbootBin` (`ac4-port/src/common/known_inputs.cpp:5`) — correct disc.
2. RPCS3 portable, `PS3UPDAT.PUP` installed with `--installfw`, then:
   ```
   rpcs3.exe --allow-any-location --decrypt "<...>\EBOOT.BIN"
   ```
   - produced `EBOOT.elf`, 17,241,824 bytes,
     sha256 `0fd1e2ee4dbc599874fabccd596f922904955d8f5c1dbd92a34101ac930daaa1`
   - exactly `ac4::inputs::kEbootElf` (`known_inputs.cpp:6`) — this is the same file the port's
     recompiler consumes.

**Gotcha worth recording:** when invoking RPCS3 from PowerShell, `Start-Process -ArgumentList` splits
paths containing spaces into separate argv entries. RPCS3 then receives `--decrypt G:\Freespace\Armored`
and silently produces nothing. Use the call operator with a quoted path (`& $rp --decrypt "$path"`).

RPCS3 version used: `0.0.43-20565-eee3f0f7`. Firmware installed reports version `4.92` in
`log/RPCS3.log` (the port's Prepare step checks for 4.93 module hashes; that check is not needed for
decryption, and the resulting ELF matches `kEbootElf` byte-for-byte).

## 2. Recompiler analysis of `EBOOT.elf`

`ac4-recomp analyze` (built from `ac4-port` with mingw g++ 15.2.0, cmake, ninja):

```
analysis: 2.52s, 2 passes
opd entries 53922, functions 53904 (secondary 0), reachable insns 3590461
switch tables 758 (18751 targets), unresolved bctr 0, invalid reachable words 0
code words 3615886, covered 99.30%
```

Zero unresolved indirect calls and zero invalid words means the analysis is clean — every function
boundary the recompiler emits is trustworthy, which is what makes address-level hooks safe.

## 3. The pad object — structure and port binding

The port's `hooks.txt` names two pad classes. Disassembly confirms both are the same design with
different field offsets, and that **the port number is a field in the object**, not a hardcoded
argument.

### Class A — `pad_update` at `0x00951f94`

```
00951fb4  lwz   r0,384(r31)        ; r31 = pad object; +384 = PORT NUMBER
00951fb8  extsw r0,r0
00951fbc  cmpwi cr7,r0,0
00951fc0  bc    12,28,0x952230     ; port == 0  -> separate path (keyboard/mouse is merged here)
00951fc4  addi  r4,r31,392         ; +392 = CellPadData buffer
00951fc8  rldicl r3,r0,0,32
00951fd0  bl    0x00dd9098         ; cellPadGetData(port, &buf)
00951fd4  ...
00951fe0  lhz   r9,404(r31)        ; +404 = button word
00951ff0  ...
00952034  stfs  f0,1172(r31)       ; +1172 = normalised left stick X
```

### Class B — `pad_update_b` at `0x005ead28`

Identical shape: **port at `+736`**, `CellPadData` at `+744`, buttons at `+756`, normalised sticks
from `+1524`.

### Consequences for the mod

- A pad object is fully parameterised by its own port field, so a second object with `port = 1` reads
  controller 1 through the same `cellPadGetData` the port's HLE already implements for ports 0–3
  (`ac4-port/src/runtime/hle/sys_io.cpp:2-4`).
- Only port 0 takes the branch at `0x952230` where keyboard/mouse is folded in. Port ≥ 1 is a clean
  raw-pad path, which is exactly what player 2 wants (no keyboard conflicts).
- `0x00dd9098` is the shared `cellPadGetData` import thunk — a useful anchor.

## 4. Engine capability — what exists, what does not

The Lua 5.0 interpreter registers C functions **by name**, and those names are present as strings in
the executable. That gives a dependable presence test. Method presence was then cross-checked against
every `eventOperation:Method` call in both games' script trees.

### Already present in AC4 (and already used by AC4's own scripts)

| primitive | AC4 uses |
|---|---|
| `SetEventPropertyGroup` | 138 |
| `SetEventHandleGroup` | 307 |
| `DeleteHandlGroup` | 106 |
| `SetRadarShapeIDGroup` | 43 |
| `ObserveRegionEventGroupInsideSomeone` | 33 |
| `GetEventGroupAlives` | 22 |
| `IsEntityExist` | 12 |
| `KillEventGroup` | 10 |
| `SetEnableImmortalGroup` | 8 |
| `SetEnableMoveGroup` | 5 |
| `SetEnableAttackGroup` | 2 |
| `SetEnableGravityGroup`, `RegistHandle`, `SetMissionEndingFlowCAPEvent`, `SetTutorialInputMode` | present |

Constants in `common/event/include/msgid.lua` (198 constants total) already include the whole
`WM_*` family AC4 needs:

```
WM_ENTITYFACTORY_NOTIFY            = WM_APP+4
WM_TEAM_NOTIFY                     = WM_APP+7
WM_EVENT_GROUP_MSG                 = WM_APP+17
WM_REGION_IN_GROUPSOMEONE_NOTIFY   = WM_APP+33
```

`msgmacro.lua` already ships `MissionFailure_PlayerIsDead` … `_5`, `MissionFailure_RegionOut`,
`ObserveMissionRegionOut`, `Mission_EnableAutoPilot`, `Mission_EnableBlendAutoPilot`.

`SetTutorialInputMode` exists, with `TUTORIAL_MODE__USERINPUT = 0`, `__AUTO = 1`,
`__BLEND__AUTO_USER = 2` — the per-character "who is driving this AC" switch.

### Absent from AC4 (ACFA-only)

| primitive | ACFA uses | note |
|---|---|---|
| `IsCooperationMission` | 7 | engine method; **but trivially replaceable by a Lua stub** |
| `IsExistFriend` | 115 | **already a Lua function in ACFA** — port it |
| `ObservePlayerMissionRegionOutOrTimeout` | 95 | **Lua wrapper** around an engine call AC4 lacks |
| `SetMissionModeAllEntity` | 5 | per-entity fallback |
| `AttachEntityEventGroup` | 30 | AC4 lacks it; attach per entity instead |
| `GetEventGroupDestroys` | 2 | low priority |
| `IsEventGroupInRegionAll` / `…Someone` | 15 / 34 | low priority |
| `SetEnableSuperOverBoostGroup` | 10 | ACFA feature (overed boost), not in AC4 |
| `SetEnablePAGroup` | 1 | use per-entity `SetEnablePA` |
| `SendMessageToGroup` | 4 | send per member |
| `GP_ID__PC_PLAYERS`, `ENTITY_ID__PLAYER_1`, `ENTITY_ID__FRIEND` | — | **Lua constants, not engine** — define them |

The important consequence: **the list of things that genuinely require engine work is short**, and
most of ACFA's co-op layer turns out to be Lua-level and portable verbatim.

## 5. Why a runtime hook fallback is credible

The port already proves every mechanism a fallback would need:

- `hooks.txt` routes any guest address to C++ `hook_name(Ctx&, orig)` with full register access.
- `hle::call_guest` / `call_guest_addr` call guest functions from C++ (`ac4-port/src/runtime/hle.h:117`).
- `--verify-native` proves a native replacement matches the original
  (`ac4-port/src/runtime/game/native_hooks.cpp`).
- `datawatch` gives hardware watchpoints for locating structures (`ac4-port/src/runtime/datawatch.h`).
- `ppu_thread.h` records thread names, so `*SortieScene` / `FcsManager` can be detected at runtime
  (`ac4-port/src/runtime/game/frame_policy.cpp:5-8`).

So even if the second-player path needs engine help, the work is additive hooks in this framework,
not a recompiler change.

## 6. Anchor addresses worth keeping

| address | meaning | source |
|---|---|---|
| `0x00951f94` | pad class A update; port at `+384` | disassembly |
| `0x005ead28` | pad class B update; port at `+736` | disassembly |
| `0x00dd9098` | `cellPadGetData` import thunk (shared) | disassembly |
| `0x00b6b148` | AC view slot 6 turn setter | `hooks.txt` |
| `0x00b67cc8` | plain input class slot 6 (VS/SIMULATOR path) | `hooks.txt` |
| `0x00a372e8` | AC look control | `hooks.txt` |
| `0x009ac8d4` | AC view matrix | `hooks.txt` |
| `0x009fc210` | AC turn input, per frame | `hooks.txt` |
| `0x001406b8` | frame present / pacer | `hooks.txt` |
| `0x002b12dc` | `luaD_throw` (Lua 5.0) | `hooks.txt` |
| `0x00d88e78` / `0x00d88f9c` | guest `setjmp` / `longjmp` | `hooks.txt` |
| `0x008fdbe8` / `0x008fdbac` | game heap alloc / free | `hooks.txt` |
