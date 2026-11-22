# 08 — Firmware 4.93 and the supplied save

## 1. The PUP you provided is the correct one

`G:\Freespace\Armored Core 4 (USA)\PS3UPDAT.PUP`

| | |
|---|---|
| size | 206,197,916 bytes |
| sha256 | `158471fd834f8ea8036136b6aab43cd86c7ba73d79ca30e0af3c0fe0001cf365` |
| version in the header | **`4.93`** (the earlier file in Downloads was `4.92`, 206,177,436 bytes) |

Installed into the private RPCS3 copy. All ten decrypted modules now match the port's expected
4.93 hashes exactly:

```
libsre      9e7ee6edc99b1d372686cf927b883b18f19ed4d32d1faf35e214cca4ae0737b6  ✔
libsail     ac33b80980d45113d887c868ff02619dc145e8a80bc102749a31fbd750431bbb  ✔
libadec     88a8f75b97f59930c88695e4436383300f2112badfa44ea4e6dccad6eda60f61  ✔
libpamf / libdmux / libdmuxpamf / libvpost / libatrac3plus / libatxdec / libat3dec  ✔
```

The difference is measurable in the build: SPU record generation went from

```
before (4.92): 733 written, 0 unchanged, 198 skipped; 196 rebuilt in part
after  (4.93): 394 written, 537 unchanged,   0 skipped;   0 rebuilt in part
```

The whole chain (import → recompile PPU with `--module` → SPU records → spurecomp → build) was redone
with the correct firmware. `ac4.exe` is now the canonical build.

**Note:** swapping firmware requires wiping `dev_flash*` first — RPCS3's `--installfw` did not
overwrite an existing install (it hung, then left 4.92 in place).

## 2. The save is a genuine AC4 US save

`PS3\SAVEDATA\BLUS30027GAMEDAT8MS4R1IYVB3D000`

```
ACDES.DAT     7856      GPROG.DAT     3632      PDATA.DAT   80
OSET.DAT        64      PARAM.SFO     2736      PARAM.PFD 32768
ICON0.PNG    88324      PIC1.PNG    447923
```

PARAM.SFO contents:

```
TITLE              ARMORED CORE4
SUB_TITLE          Game Progress Data
SAVEDATA_DIRECTORY BLUS30027GAMEDAT8MS4R1IYVB3D000
DETAIL             PLAY TIME: 08:42 / LINX: Heavenflame / AC: Old Default
                   CASH: 16673823   C.COMPLETE: 150   MISSION RANK: SS
```

So: the US title id, a completed campaign, 150 missions, SS rank. Installed into
`run\data\hdd0\home\00000001\savedata\`, which is where the port's HLE looks
(`/*/dev_hdd0/home/00000001/savedata/<DIR>`).

## 3. The game finds the save but rejects it

With the save in place the port reports:

```
cellSaveDataListLoad2: prefix 'BLUS30027GAMEDAT', 1 save(s) found
cellSaveDataListLoad2: 'BLUS30027GAMEDAT8MS4R1IYVB3D000' (existing, 7 file(s), 606 KB)
cellSaveDataListLoad2: status callback returned -3
```

`-3` is the **game's own** status callback return value (`CB_ERR_BROKEN`), i.e. AC4 itself decides the
save is corrupted — it is not the port failing to read the directory. Removing `PARAM.PFD` changed the
file count to 6 but the result was the same (-3), so it is not merely the signature file.

Likely cause: the port stores saves unencrypted and without the console's `PARAM.PFD` signing chain,
so a **console-signed** save does not satisfy AC4's integrity check. The port's README's claim that
"PS3 saves work once decrypted" may require a save whose `PARAM.PFD` matches the emulator's fake
account, or a save produced by this port / RPCS3 rather than dumped from hardware.

## 4. Rig improvements made during this round

1. **Live steering.** `--rig` scripts are now re-read whenever the file changes, and the rig publishes
   its elapsed time to `<shots>\clock.txt` every 0.2 s. A run can therefore be driven from outside
   without restarting (boot to title costs ~1 min each time, so this matters).
2. **Auto-acknowledge** of port-rendered system dialogs (`pad_now` pulses Cross) — covers the port's
   own dialogs, not the game's message boxes.
3. **Hidden and isolate decoupled** (`--hidden` no longer implies `--isolate-input`), so the rig's pad
   reaches system dialogs.

## 5. What was successfully driven

Full sequence achieved in one live session:

1. Boot, hidden and silent (~57 fps).
2. Dismiss the port's "HDD access indicator" dialog.
3. Dismiss the game's "Cannot check update file" message (it blocks the title until acknowledged).
4. Reach `PRESS START BUTTON`.
5. Press START → main menu (NEW GAME / LOAD GAME / MULTIPLAYER).
6. Move the cursor to **LOAD GAME** and press Cross.
7. The load list opens and finds the save.

**Navigation finding worth recording:** a *held* D-pad press or a held stick deflection did **not**
move the menu cursor in this frontend, but **discrete taps** did (three `UP` taps moved the cursor and
wrapped it onto LOAD GAME). Scripts should tap, not hold.

## 6. Remaining blocker

The supplied save is rejected as corrupted by AC4 itself, so the run cannot proceed past the load
list into the hub. Without the hub there is no way to reach mission `m000`, and therefore the decisive
E3 test (a second, pad-driven AC in a mission scene) is still not run.

### Ways forward, in order of cost

1. **A save produced by this port or RPCS3** (not dumped from a console). The port writes its own
   save format; a save made in the same runtime should pass AC4's check. Cost: one playthrough to the
   first save point, or a save exported from RPCS3 with the same fake account.
2. **Dismiss the corruption check** for the test rig: patch the port's savedata HLE to report the
   save as valid (a rig-only patch, clearly not shippable) so the content can be loaded and the E3 test
   run. This tests the game with `GPROG.DAT` data but bypasses an integrity check — acceptable as a
   research rig, not as a mod.
3. **One interactive session**: the user loads the save (or the new game) once with the window
   visible; after the hub is reached, automation can take over because the rig can already drive the
   hub's menus.
