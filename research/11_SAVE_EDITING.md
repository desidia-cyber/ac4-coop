# 11 — Can the save be edited to reach a specific mission?

Short answer: **the console save cannot be edited at all, and the port's own save can be edited but
contains no mission mapping.** Details and evidence below.

## 1. The supplied console save is encrypted

Measured entropy (8.00 = perfectly random):

| file | size | entropy | editable? |
|---|---|---|---|
| `GPROG.DAT` | 3632 | **7.96** | no |
| `ACDES.DAT` | 7856 | **7.98** | no |
| `PDATA.DAT` | 80 | 6.02 | no |
| `OSET.DAT` | 64 | 5.78 | no |

No zlib/raw-deflate stream anywhere in them. That is ciphertext: PS3 save data is encrypted by
`cellSaveData` with the console's per-title key, which is why the game called it "corrupted" when the
port handed it the raw bytes. Without that key these files cannot be read or written meaningfully.

## 2. The port's own save IS plaintext — and can be edited

Using the hub (SYSTEM → SAVE GAME) the port wrote its own save, and the log confirmed the game's
buffers are stored raw:

```
cellSaveDataListSave2: 'BLUS30027GAMEDAT8MS4R1IYVB3D000' recreated
  write ACDES.DAT: 7856 byte(s) at 0
  write GPROG.DAT: 3624 byte(s) at 0
  write OSET.DAT: 62 byte(s) at 0
  write PDATA.DAT: 76 byte(s) at 0
cellSaveDataListSave2: '...' committed (6 file(s))
```

The result:

| file | size | entropy | content |
|---|---|---|---|
| `GPROG.DAT` | 3624 | **0.00** | **all zero** |
| `ACDES.DAT` | 7856 | **0.00** | **all zero** |
| `OSET.DAT` | 62 | 0.68 | zeros + `"ver100"` at +46 |
| `PDATA.DAT` | 76 | 0.40 | zeros + `41 f8 aa 12` at +72 |

`PARAM.SFO` for it reads `PLAY TIME: 00`, `LINX:` (empty), `AC:` (empty), `CASH: 0`,
`C.COMPLETE: 0`, `MISSION RANK: E`.

So a port save is a **blank, plaintext, freely writable file** — editing is genuinely possible. A
backup of this state is kept at `run\save_backup_blank`.

## 3. But editing it does not select a mission

With `GPROG.DAT` entirely zero the hub still offers one mission, `FIRST STRIKE` = **m160**. So the
mission the hub offers is *derived* from progress, not stored as a literal id we can overwrite.

Two experiments:

- **All-`FF` flood** (`GPROG.DAT` = 3624 × `0xFF`): the game **black-screens** — the data is not a
  simple flag field and garbage breaks it. (Save restored afterwards; see the backup.)
- No literal `160` appears anywhere in the file, so the current mission is not a plain u16/u32 in
  `GPROG.DAT`.

`GPROG.DAT` is 3624 bytes, which fits a per-mission record array (≈48 missions × 75 bytes, or
150 × 24 bytes — the console save reported `C.COMPLETE: 150`). If that is the layout, the hub picks
the **first uncleared mission in story order**, which is why a pristine file yields the first mission
and nothing else.

## 4. What would make save editing work

We would need the record layout: stride, and the value that means "cleared". Neither is guessable, and
guessing crashes the game. It can be obtained two ways:

1. **Diff method (empirical).** Clear `m160` in game, save, and diff `GPROG.DAT` against the blank
   copy. That reveals where a cleared mission is recorded and — if records are ordered by story order
   — the stride. Then every record could be stamped "cleared", which should unlock the whole mission
   list (the postgame MISSION LIST), from which `m000`/`m050`/`m170`/`m090` could be chosen.
   Cost: one mission actually played to completion.
2. **Code method (rigorous).** Disassemble the game's save serialisation around the
   `cellSaveDataListSave2` file-write callback and read the struct offsets directly. More work, but
   exact, and it would also explain the `41 f8 aa 12` trailer in `PDATA.DAT` (likely a version or
   checksum tag).

## 5. Practical conclusion

Save editing is not a shortcut **today**, because the only save we can write is a blank one with no
mapping, and the one save that contains real progress is encrypted. The two realistic ways to reach a
mission with a second AC remain:

- **Play the tutorial with real inputs** (NEW GAME → m710 … → `m717`, which has `m717_enemy.lua`),
  which the rig can do since the lessons are stick/button inputs; or
- **Do the one diff above** (clear m160 once) and then stamp all records cleared, unlocking the
  mission list.
