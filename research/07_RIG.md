# 07 — The automated test rig (built and working)

A hidden, silent, scripted-input rig for running AC4 unattended. Modelled on the user's
`G:\Freespace\dollsMP` setup: the game runs with no visible window, no audio, no input devices
touched, driven by a timeline file, and can be inspected afterwards from screenshots and logs.

Everything lives under `G:\Freespace\Armored Core 4 (USA)\coop-mod\`.

---

## 1. What the rig does

| Property | How |
|---|---|
| **Hidden** | `--hidden` → the D3D11 window is created with `visible = false`; nothing appears on the desktop and no focus is taken. |
| **Silent** | `rig::muted()` is checked in `audio::out_write`, so no audio is ever submitted to the device. No device volume is touched. |
| **No input interference** | Pad state comes from a timeline file, not from real devices. Optional `--isolate-input` makes the real keyboard/mouse/pads ignored entirely. |
| **Scripted pad** | `--rig <file>`: `<seconds> <port> <buttons> <lx> <ly> <rx> <ry>`, buttons `A+B+START`, sticks −32768..32767. |
| **Observable** | `--rig-shots <dir> --rig-shot-every N`: PNG of the presented back buffer every N seconds. |
| **Bounded** | `--seconds N`: the watchdog stops the run at N+2 s. |
| **Overlay** | `--test-overlay <dir>`: files under a disc-layout folder replace the disc's (proven working — see §4). |

Low priority: the runner sets the process to `Idle` so it does not compete with the user's work.

## 2. Source changes made (all inside `ac4-port/`)

The repository is the **player build**: `AC4_DEV` is absent and every developer option in
`config.h` is a `static constexpr false`. So the rig had to be added.

| File | Change |
|---|---|
| `src/runtime/rig.h`, `rig.cpp` | **new**: timeline parser, pad provider, mute flag, screenshot scheduler |
| `src/runtime/config.h` | `seconds`, `isolate_input`, `hidden_window`, `test_overlay`, `rig_script`, `rig_shots`, `rig_shot_every` made settable |
| `src/runtime/main.cpp` | parses `--seconds --hidden --isolate-input --test-overlay --rig --rig-shots --rig-shot-every`; calls `rig::init()` |
| `src/runtime/hle/sys_io.cpp` | `read_port`: `rig::pad(port,&xs) \|\| (!input_isolated() && pads::get(...))` |
| `src/runtime/sysdialog.cpp` | `pad_now()` consults the rig first (system dialogs were otherwise undrivable) |
| `src/runtime/audio/audio_out.cpp` | `out_write` returns early when muted |
| `src/runtime/gpu/display.cpp` | `rig_capture()` in `after_present`; back buffer → staging → PNG |
| `src/common/known_inputs.cpp` | 4.92 module hashes registered as an accepted alternate version |
| `src/prep/import.cpp` | accept modules from a known alternate firmware version |

## 3. Exact build pipeline (reproducible)

```
# 1. decrypt EBOOT.BIN (RPCS3 portable + PS3UPDAT.PUP)
rpcs3.exe --allow-any-location --decrypt "<...>/EBOOT.BIN"     # quote the path!

# 2. decrypt the 10 firmware modules from dev_flash/sys/external (one call per file)
rpcs3.exe --allow-any-location --decrypt "<...>/libsre.sprx"

# 3. prepare
ac4-prep import --disc <disc> --eboot EBOOT.elf --modules-dir <modules> \
                --flash-file flashATRAC.pic --out <data>/game/BLUS30027 --flash-out <data>/dev_flash

# 4. recompile PPU  (--module for EACH lle module is mandatory)
ac4-recomp emit <data>/game/BLUS30027/eboot.elf <gen>/ppu \
   --module <data>/game/BLUS30027/lle/libsre.elf   ... (all 10) \
   --hooks src/runtime/game/hooks.txt --patches src/runtime/game/patches.txt

# 5. SPU
ac4-prep spu-records --manifest data/spu_manifest.txt --out <data>/spu_cache <data>/game/BLUS30027
ac4-spurecomp <gen>/spu <data>/spu_cache

# 6. build with the PINNED toolchain (llvm-mingw 20260908, sha256 verified)
ac4-port/local/tools/llvm-mingw-20260908-ucrt-x86_64/
cd ac4-port && . .\tools\env.ps1 && .\tools\build.ps1 -Config Release -Target ac4
#  -> ac4-port/local/build/release/ac4.exe
```

Outputs: `gen/ppu` 163 files / 108.8 MB, `gen/spu` 13 files / 11.1 MB, `ac4.exe` 97.5 MB.

### Pitfalls hit (worth recording)

1. **`Start-Process -ArgumentList` splits spaced paths.** Silently broke the RPCS3 decrypt and later
   the game launch (exit code 2 "unknown argument"). Quote args or build one command line.
2. **`--module` is mandatory for the LLE firmware modules.** Without it the recompiled game lacks
   every firmware function and dies at `call to unknown guest address 0x0a008578` (libsre+0x8578).
3. **msys2 mingw headers are too old** (`D3D11_FILTER_REDUCTION_TYPE_STANDARD` missing). Must use the
   pinned llvm-mingw.
4. **`sysdialog` ignores input when `input_isolated()`**, and `--hidden` originally implied it. The
   hidden window and input isolation had to be decoupled, and dialog input routed through the rig.
5. **The early network dialog** ("Cannot check update file") blocks the title until dismissed — it
   needs an `A` press at ~20 s.

## 4. What the rig proved

- The game **boots and runs hidden and silent**, ~57.6 fps, 1280×720, process at Idle priority.
- **The overlay mechanism works**: `vfs` reports `TEST OVERLAY ... its files replace disc files`, and
  the overlay directory is consulted for `/app_home`/`/dev_bdvd` paths.
- **Scripted pad input reaches the game**: pressing START at the title opens the main menu
  (NEW GAME / LOAD GAME / MULTIPLAYER), and pressing A dismisses the network dialog. Screenshots
  confirm both.
- **Screenshots work** (`after_present` → PNG), 40–80 per run, used to navigate.

## 5. Current blocker

**NEW GAME does not complete.** Selecting NEW GAME plays `movie/opening.pam` (~166 s) and then the
game returns to the **title screen**; the attract movie starts ~70 s later. Reproduced four times
with different scripts. Log evidence:

```
 75.04 cellSaveDataListLoad2: prefix 'BLUS30027GAMEDAT', 0 save(s) found
 85.19 cellFsOpen('.../movie/opening.pam')
251.49 cellFsOpen('.../movie/title.pam')            <- back to the title
251.49 cellSaveDataListLoad2: 0 save(s) found
321.19 cellFsOpen('.../movie/opening.pam')          <- title attract mode
```

No `SortieScene`, `TutorialScene` or `InitialGameSetting` scene is ever entered. Only
`cellSaveDataListLoad` is called — never a save — so the flow is not reaching the point where the
game commits a new save.

### Most likely cause

The game was prepared from **firmware 4.92**, but the port targets **4.93** (`kModules493`). Three
modules differ (`libsre`, `libsail`, `libadec` — the last two are the movie/audio decoders used by
`libsail`). They were accepted through the port's "other version" path, and the SPU record builder
noted *"196 rebuilt in part (code shared with another system software version)"*. A subtle
divergence in the movie-end / system-event path is a plausible cause of the return to title.

### Two ways to unblock (either is enough)

1. **A real PS3 firmware 4.93 `PS3UPDAT.PUP`.** The user's `C:\Users\ALVIBO\Downloads\PS3UPDAT.PUP`
   is **4.92** (verified: the PUP header contains `4.92.`). Sony's download works from a browser;
   this environment cannot reach playstation.com or the mirrors (TLS blocked, `curl` and
   `Invoke-WebRequest` both fail).
2. **An existing AC4 save** (PS3 or RPCS3). `LOAD GAME` then goes straight to the hub, and the whole
   NEW GAME problem disappears. No save exists on this machine today
   (`Saved Games\ARMORED CORE 4` absent; 0 saves found by the game).

## 6. What was *not* reached

The decisive E3 test (does a mission scene accept a second, pad-driven AC?) has **not** been run,
because it requires entering mission `m000`, which requires the hub, which requires either a working
NEW GAME or a save. Everything up to that point is built and verified.
