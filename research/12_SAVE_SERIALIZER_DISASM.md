# 12 — Disassembling the save serialiser: what the code shows

Target: find the `GPROG.DAT` structure layout so the save can be edited to select a mission.

## 1. The save request path (established from the code)

The save is initiated at **`0x00d41b64`**:

```
00d41b64: stdu  r1,-144(r1)              ; prologue
00d41b78: bl    0x00d42b70               ; trivial getter (lwz r3,60(r3); blr) - not the serialiser
00d41b80: addi  r5,r29,20                ; setBuf  = state+0x14
00d41b84: lwz   r8,22000(r2)             ; funcFile  (TOC slot)
00d41b8c: lwz   r6,21992(r2)             ; funcList  (TOC slot)
00d41b94: lwz   r7,21996(r2)             ; funcStat  (TOC slot)
00d41b98: rldicl r4,r29,0,32             ; setList = state
00d41ba0: li    r3,0                     ; version  = 0
00d41ba4: bl    0x00ddaa78               ; cellSaveDataListSave2
00d41ba8: ...                            ; (this is the LR the port saw: 0x00d41ba8)
```

Runtime-confirmed by the port log: `SAVE REQUEST from lr 0x00d41ba8`.

### Callback objects

| callback | OPD | code | toc |
|---|---|---|---|
| file | `0x00EAD880` | **`0x00d44590`** | `0x00f04e58` |
| status | `0x00EAD8B8` | **`0x00d467b4`** | `0x00f04e58` |

The save-init function's own OPD is at **`0x00EAD6D0`** (code `0x00d41b64`, toc `0x00f04e58`).

## 2. The file callback is a pure dispatcher

`0x00d44590` is **173 instructions with no `bl` at all** and no size constants (no `0xE28`, no
`0x1EB0`). It walks a runtime-built descriptor table:

```
00d44590: lwz  r7,22580(r2)      ; global pointer -> save-state struct
00d445a8: lwz  r9,0(r7)          ; index
00d445b4: addi r4,r9,56          ; descriptor array
00d445c0: lwz  r9,0(r6)          ; counter
...
00d44628: lwz  r0,36(r10)        ; descriptor +36
00d44640: lwz  r9,40(r10)        ; descriptor +40
00d44648: stw  r9,44(r5)         ; -> CellSaveDataFileCallback fields
```

So it only copies {name, buffer, size} out of a descriptor into the file callback structure. **It does
not serialise anything**, and the descriptor table is built at runtime (the global at TOC+22580 points
to heap), so there is nothing to follow statically.

## 3. Per-file buffers and exact sizes (measured at save time)

The port now logs the guest buffer each file is written from:

| file | guest buffer | size |
|---|---|---|
| `ACDES.DAT` | `0x00089f10` | 7856 |
| **`GPROG.DAT`** | **`0xe0269f10`** | **3624** |
| `OSET.DAT` | `0xe0818d10` | 62 |
| `PDATA.DAT` | `0x50828d10` | 76 |
| `ICON0.PNG` | `0x2064a710` | 88324 |
| `PIC1.PNG` | `0x4046b110` | 447923 |

These are heap scratch buffers (they sit far above the game image and are identical across two saves
in one process), i.e. the persistent structures are **copied into them just before the save**. The copy
is what encodes the layout, and it is not visible from the save callback.

## 4. Why the static route stops here

The save-init function has:

- **no `bl` callers** anywhere in `.text` (searched all 3.59 M instructions), and
- **no static reference** to its OPD `0x00EAD6D0` (searched `lis`+`addi` and `lis`+`ori` producers).

Both mean the save action is dispatched through a **runtime table** (a menu-action table), so walking
up from the save call cannot reach the serialiser by address analysis alone.

## 5. The one remaining step, and how to do it

The serialiser can be found dynamically, cheaply:

**Scan guest memory for a copy of the `GPROG` image at save time.** The persistent structure is
byte-identical to the 3624-byte scratch buffer, so searching the 4 GiB guest space for that exact
block names its address. That address is a static global, and *that* one can be xref'd in the code
(`lis`+`addi`/`ori`), which lands directly on the reader/writer — and therefore on the field offsets,
including whatever selects the current mission.

Concretely: add to the port, in the save path, a one-shot scan of the guest address space for the
`GPROG` buffer contents and log the matching address(es). The port already has the guest memory base
(`ppu::mem()`) and the buffer, so this is a few lines.

Alternative without any memory scan: dump a known-progress save. Completing one mission writes real
values into `GPROG.DAT`, and a diff against the all-zero save shows exactly which fields a mission
clear touches — including the mission index.

## 6. Decisive finding: the save does not store a "current mission"

While reading the path, one fact settles the original question:

> A save with **`GPROG.DAT` entirely zero** still produces a hub showing `FIRST STRIKE` (m160).

So the mission the hub offers is **not** a field in the save — it is derived from progress (the first
mission not yet cleared, in story order). There is therefore nothing in `GPROG.DAT` to overwrite that
would point the game at `m000`.

Consequences:

- Save editing can only mark missions **cleared**. To reach `m000` that way, every mission before it
  in the story order would have to be marked cleared, and the "cleared" encoding plus the record
  stride would have to be known.
- The memory-scan trick in §5 also needs non-zero data to be meaningful (an all-zero block matches
  everywhere), so it cannot be used on the current blank save.

Both routes therefore need one thing we do not have: **a save with real progress in it**. The console
save that has progress is encrypted (entropy 7.98) and unusable without the console's key.

Practical conclusion: the save cannot be used to jump to an arbitrary mission. The remaining ways to
reach a mission containing a second AC are to **play** — either the story (starting from `m160`) or the
tutorial with real inputs (m710 → … → `m717`, which has `m717_enemy.lua`).
