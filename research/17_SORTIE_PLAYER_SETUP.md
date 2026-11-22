# 17 — Sortie scene player setup: how many human ACs, on which pads, and split screen

**Scope.** Static only (ELF + recompiled C). Every address below is a guest VA in
`EBOOT.elf` (BLUS30027). Code citations are `ppu_gen_*.cpp:<line>`; the recompiled
function is named `f_<guest address>`. A statement is only made here if it was
read out of the files; anything unresolved is marked **NOT DETERMINED**.

**Tooling built for this report** (in `tools/re/`, reusable):

| script | purpose |
|---|---|
| `ac4elf.py` | big-endian ELF64 reader, segment map, string/pointer search |
| `ac4opd.py` | OPD `{code,toc}` decode, vtable walk |
| `refindex.py` | index of `r2`-relative data refs in the 110 MB gen tree (`build` / query) |
| `genidx.py` | function bodies + 151 961 direct-call edges |
| `x.py` | `opds` / `ptrto` / `vt` / `callers` / `vtbody` |
| `showfn.py` | print a function body by address |

## 0. Address-space and addressing facts (needed to read the rest)

Segments (from `ac4elf.py`):

| segment | VA | file size | mem size | perms |
|---|---|---|---|---|
| LOAD0 | `0x00010000` | `0x00decd78` | same | RX (code) |
| LOAD1 | `0x00e00000` | `0x0010b4d4` | same | RW (.data / literal pools / TOC cells) |
| LOAD2 | `0x10000000` | `0x000f5480` | same | R (all strings) |
| LOAD3 | `0x10100000` | `0x00052350` | `0x05ce2dd8` | RW (vtables, then BSS to `0x105e2dd8`) |

**Data addressing.** In the whole gen tree there is exactly **one** hex immediate that
resolves to a data address — every other data access is TOC-relative:

```
F(0)=rf32((u32(R(2))+-17928u));      // ppu_gen_121.cpp:15542, inside f_00b52584
```

So to find "who references address X" you must know the caller's TOC base. A function
pointer is an OPD: 8 bytes `{code, toc}`; an indirect call does
`ctx.ctr=r32(opd); R(2)=r32(opd+4); icall(...)` (`ppu_gen_121.cpp:15575-15577`).

The binary has **8 real per-TU TOCs** (from pointer-referenced OPDs):
`0x00eb8680` (9166 OPDs), `0x00f04e58` (7869), `0x00eeaa0c` (6113), `0x00ef5c94` (4613),
`0x00ed7990` (4162), `0x00ec5b04` (1838), `0x00ecedb8` (1059), `0x00ee5244` (401).

Two TOCs used below were verified by content, not by inference:

* `0x00f04e58` — `[0x00f04e58-17928] = 0x00f00850` holds `bf800000` = **-1.0f** and
  `[0x00f04e58-18268] = 0x00f006fc` holds `3f800000` = **+1.0f**, i.e. exactly the ±1
  clamp of the pad operator. The AC-view/input module uses this TOC.
* `0x00eeaa0c` — the scene TUs: `0x00eeaa0c + 11436 = 0x00eed6b8` = `"VsSortieScene"`,
  which is the constant actually loaded at `ppu_gen_086.cpp:11667`.

---

## T1. Scene-name strings: VA, pointers to them, and the code that references them

All eight requested strings are NUL-terminated in **LOAD2** (`0x10000000`, read-only).

| string | VA | len | unique 32-bit pointer(s) in data |
|---|---|---|---|
| `Ac4SortieScene` | `0x10047100` | 14 | `0x00ee2cc0` |
| `Ac4SortieScene::Init:` | `0x10047110` | 21 | `0x00ee2cc4` |
| `Ac4SortieScene::LoadThread` | `0x10047020` | 26 | `0x00ee2c94` |
| `MissionSortieScene` | `0x1004c7a0` | 18 | `0x00ee8484`, `0x00ee94cc`, `0x00ee9798` |
| `MissionSortieScene::Init` | `0x1004d670` | 24 | `0x00ee979c` |
| `VsSortieScene` | `0x10050658` | 13 | `0x00eed6b8` |
| `ArenaSortieScene` | `0x10047e20` | 16 | `0x00ee3d44` |
| `TutorialSortieScene` | `0x100500a8` | 19 | `0x00eeceb0` |

Adjacent strings worth knowing (same pools, and needed later):

| string | VA | pointer |
|---|---|---|
| `MissionDebugSortieScene` | `0x1004d6a8` | `0x00ee97b8` |
| `MissionBriefingScene` | `0x1004d690` | `0x00ee97b4` |
| `MissionDebriefingScene` | `0x1004d6c0` | `0x00ee97c0` |
| `MissionFailedScene` | `0x1004d6d8` | `0x00ee97c8` |
| `ac_0` | `0x10047018` | (Ac4 pool) |
| `ac_%d` | `0x10050668` | `0x00eed6c0` |
| `$(MenuResource)\Sortie.mrb` | `0x10047098` | `0x00ee2ca4`, `0x00eed6ec` |
| source path `n:/Ac4/data/source/Sw/Source/Scene/VsSortieScene.cpp` | `0x10050620` | — |
| source path `.../Scene/ArenaSortieScene.cpp` | `0x10048008` | — |
| source path `.../Scene/TutorialSortieScene.cpp` | `0x10050000` | — |
| source path `.../Scene/AC4SortieScene.cpp` | `0x10047050` | `0x00ee2c9c` |
| source path `.../Network/Scene/NetVsSortieScene.cpp` | inside `0x1007be49+7` | — |

Note `NetVsSortieScene` exists **only** as a source-path string (`0x1007be69`); there is no
`"NetVsSortieScene"` class-name literal, so its class descriptor cannot be keyed by name.

### Functions that reference these addresses (verified by reading the code)

| string | referencing function | evidence |
|---|---|---|
| `Ac4SortieScene` + `Ac4SortieScene::Init:` | **`f_00740838`** | `ppu_gen_077.cpp:18279` `R(4)=r32((u32(R(2))+-32076u))` → `+0x00eeaa0c` = `0x00ee2cc0`; `ppu_gen_077.cpp:18297` `-32072` → `0x00ee2cc4` |
| `Ac4SortieScene::LoadThread` | `f_007401d4` (`ppu_gen_077.cpp:17869`), `f_00a326c4` (`ppu_gen_109.cpp:9531`, `:9691`) | candidate — TOC-consistent, not read in context |
| `MissionSortieScene` + `MissionSortieScene::Init` | **`f_007cf1cc`** | `ppu_gen_083.cpp:15437` `-4724` → `0x00ee9798`; `ppu_gen_083.cpp:15447` `-4720` → `0x00ee979c` |
| `VsSortieScene` | **`f_00812238`** | `ppu_gen_086.cpp:11667` `R(4)=r32((u32(R(2))+11436u))` → `0x00eed6b8` |
| `VsSortieScene` (vtable cell) | `f_00812128`, `f_00812058` | `ppu_gen_086.cpp:11595`, `:11544` `+11428` → `0x00eed6b0` (= `0x1013c010`) |
| `ArenaSortieScene` | `f_00752640` (`ppu_gen_078.cpp:8084`) | candidate |
| `TutorialSortieScene` | `f_00804574` (`ppu_gen_085.cpp:22440`), `f_00804870` (`:22637`) | candidate |

**Honesty note on "every function".** A full list is produced by `refindex.py <addr>`, but a
raw list mixes true references with coincidences: the same signed delta occurs in functions
belonging to *other* TOCs (the TOC of a function is only recoverable when its OPD is
referenced, and only 21 210 of 55 620 functions have such an OPD). The four bolded rows
above are the ones I verified by reading the surrounding body. Everything else in the
`refindex` output for these addresses should be treated as a candidate.

### Where the descriptors live

The pointer cells sit in **LOAD1** literal pools next to their strings, e.g.

```
00ee9788: 10100148        ; data
00ee978c: 10100370        ; data
00ee9790: 101000b0        ; data
00ee9794: 10130a78        ; <-- Ac4SortieScene vtable
00ee9798: 1004c7a0        ; "MissionSortieScene"
00ee979c: 1004d670        ; "MissionSortieScene::Init"
00ee97a0: 1001a030        ; dantelion2 include path
```

and for Vs:

```
00eed6a8: 101006b0
00eed6ac: 10100790
00eed6b0: 1013c010        ; <-- VsSortieScene vtable
00eed6b4: 1013c080        ; <-- a second, 3-slot vtable
00eed6b8: 10050658        ; "VsSortieScene"
00eed6bc: 101009a0
00eed6c0: 10050668        ; "ac_%d"
```

There is **no** name→factory table: the cells are compiler literal pools (the class name is
copied into the object by its own initialiser, see T2). A `{name, vtable}` pairing is
*suggested* by the layout at `0x00ee97b8..0x00ee97cc` but cannot be relied on — see T2's
ownership caveat.

---

## T2. Mission vs Vs: vtable, ctor, slots, Init

### The three vtables

`x.py vtbody` output; slots are `slot -> OPD -> code` (`OPD` in LOAD1, code in LOAD0).

**Base `Ac4SortieScene` — vtable `0x10130a78`** (21 slots)

| slot | code | slot | code |
|---|---|---|---|
| 0 | `0x007407c4` | 11 | `0x0073ff9c` |
| 1 | `0x00740f10` | 12 | `0x0073fac8` |
| 2 | `0x004dd108` | 13 | `0x0073f6e4` |
| 3 | `0x004dd4d0` | 14 | `0x0073d57c` |
| 4 | `0x0073f6dc` | 15 | `0x0073d580` |
| **5** | **`0x00740838` = `Ac4SortieScene::Init`** | 16 | `0x0073d584` |
| 6 | `0x0073f710` | 17 | `0x0073d58c` |
| 7 | `0x0073f7cc` | 18 | `0x0073d594` |
| 8 | `0x0073d564` | 19 | `0x0073d598` |
| 9 | `0x0073f824` | 20 | `0x0073d5a0` |
| 10 | `0x00740834` | | |

**`MissionSortieScene` family — vtable `0x10138b18`** (21 slots)

| slot | code | note |
|---|---|---|
| 0 | `0x007cfbf8` | writes vtable at `this+0` (ctor thunk) |
| 1 | `0x007cfc5c` | writes vtable at `this+0` (ctor thunk) |
| 2 | `0x004dd108` | same as base |
| 3 | `0x0074d304` | overridden |
| 4 | `0x007ce29c` | overridden |
| **5** | **`0x007cf1cc` = `MissionSortieScene::Init`** | override of the Init slot |
| 6 | `0x0074e91c` | overridden |
| 7 | `0x0074e358` | overridden |
| 8 | `0x0074d920` | overridden |
| 9 | `0x0074d988` | overridden |
| 10 | `0x007cec90` | overridden |
| 11 | `0x0074dfc8` | overridden |
| 12 | `0x0073d56c` | ~base 12 |
| 13 | `0x0073d574` | ~base 13 |
| 14 | `0x0074d98c` | overridden |
| 15 | `0x0074ce8c` | overridden |
| 16 | `0x0074ced8` | overridden |
| 17 | `0x0074cf30` | overridden |
| 18 | `0x0073d594` | same as base |
| 19 | `0x0074d23c` | overridden |
| 20 | `0x0074d2a0` | overridden |

**`VsSortieScene` — vtable `0x1013c010`** (25 slots)

| slot | code | note |
|---|---|---|
| 0 | `0x00812128` | ctor thunk, stores `0x1013c010` at `this+0` |
| 1 | `0x00812058` | ctor thunk, same |
| 2 | `0x004dd108` | shared with base |
| 3 | `0x004dd4d0` | shared with base |
| 4 | `0x0080bfe8` | overridden |
| **5** | **`0x00812238` = `VsSortieScene::Init`** | override of the Init slot |
| 6 | `0x00813698` | overridden |
| 7 | `0x00814ec0` | overridden |
| 8 | `0x0073d564` | shared with base |
| 9 | `0x0080ed78` | overridden |
| 10 | `0x008121ec` | overridden |
| 11 | `0x0080e0ec` | overridden |
| 12 | `0x0080c780` | overridden |
| 13 | `0x0080c044` | overridden |
| 14..20 | `0x0073d57c`, `0x0073d580`, `0x0073d584`, `0x0073d58c`, `0x0073d594`, `0x0073d598`, `0x0073d5a0` | **all shared with base** |
| 21 | `0x00753b68` | extra (beyond base's 21 slots) |
| 22 | `0x0080f09c` | extra |
| 23 | `0x0080c600` | extra |
| 24 | `0x0080c608` | extra |

Also present at `0x00eed6b4`: vtable **`0x1013c080`** with 3 slots
(`0x006815f8`, `0x006815fc`, `0x0080c3e4`) — a small secondary interface of the Vs class.

### Slot-by-slot diff (base → Mission / Vs)

* Both keep slot 2 (`0x004dd108`). Vs also keeps slot 3 (`0x004dd4d0`); Mission overrides it.
* Both override **slot 5 (Init)** — the only slot that matters for this project.
* Vs keeps the whole base tail `14..20` unchanged; Mission overrides 14..17 and 19..20.
* Vs appends 4 new slots (21..24); Mission appends none.
* Net: **Mission overrides 16 of 21 base slots, Vs overrides 11 and extends to 25.**

### Constructors and object size

No vtable slot-0/1 function has a *direct* caller (`x.py callers 0x00812128` → empty), and
the ctors are not reached by a plain `f_XXXXXXXX(ctx)` call: they are invoked indirectly
through the vtable. Therefore the `new`-site allocation size is **NOT DETERMINED**.

What the ctor bodies *do* prove about size (a lower bound on the object):

```c
// VsSortieScene ctor thunk, ppu_gen_086.cpp:11593
R(29)=R(3)+u64(s64(1)<<16);     // this + 0x10000
...
R(3)=R(29)+u64(s64(-1528));     // this + 0x10000 - 1528  (name string at +64008)
R(29)=R(29)+u64(s64(-1712));    // this + 0x10000 - 1712
R(0)=r32((u32(R(2))+11428u));   // 0x1013c010
w32(u32(R(3)),u32(R(0)));       // *(void**)this = vtable
```

```c
// Ac4SortieScene::Init, ppu_gen_077.cpp:18279
R(3)=R(3)+u64(s64(8296));       // name string goes to this+8296
R(4)=r32((u32(R(2))+-32076u));  // "Ac4SortieScene"
```
and later `this+7980`, `this+7984`, `this+8000`, `this+8280`, `this+8112`
(`ppu_gen_077.cpp:18300-18327`).

So: `Ac4SortieScene` ⊇ ~8.3 KB, `VsSortieScene` ⊇ 64 KiB (it addresses `this+0x10000`).
Mission's `Init` writes its name at `this+384` (`ppu_gen_083.cpp:15436`), i.e. inside the
base subobject.

**Vtable ownership caveat (important).** `x.py vt 0x007cf1cc` finds exactly one vtable
containing `MissionSortieScene::Init`: `0x10138b18`. But the literal pool at
`0x00ee97b8..0x00ee97cc` reads as `{MissionDebugSortieScene, 0x10138c98}`,
`{MissionDebriefingScene, 0x10138208}`, `{MissionFailedScene, 0x10138b18}` with stride 8 —
which would assign `0x10138b18` to `MissionFailedScene`. Both readings are consistent with
the code: a derived `MissionFailedScene` that does **not** override Init inherits
`0x007cf1cc` into its own vtable, and there is only one such vtable. I did **not** resolve
which class owns `0x10138b18`; treat it as "the vtable that carries
`MissionSortieScene::Init` at slot 5".

### Init bodies (what the three Inits are)

| class | Init | file:line | span |
|---|---|---|---|
| `Ac4SortieScene` | `f_00740838` | `ppu_gen_077.cpp:18273` | 242 lines |
| `MissionSortieScene` | `f_007cf1cc` | `ppu_gen_083.cpp:15431` | 311 lines |
| `VsSortieScene` | `f_00812238` | `ppu_gen_086.cpp:11659` | 816 lines |

Per-frame update / load functions: **NOT DETERMINED** (the remaining base slots
(`0x0073f710`, `0x0073f7cc`, `0x0073f824`, …) were not analysed, and no `LoadThread`
body was located).

---

## T3. The pad operator `0x00b52584`

| item | value |
|---|---|
| function | `f_00b52584` (`ppu_gen_121.cpp:15514`) |
| OPD | `0x00ea2b00`, TOC `0x00f04e58` |
| in any vtable? | **No** — `x.py vt 0x00b52584` returns nothing |
| direct callers | 3, all `ctx.lr=...;f_00b52584(ctx)` |

Direct call sites (from `genidx.py`):

| caller | return address | file:line |
|---|---|---|
| `f_00b532d0` | `0x00b53310` | `ppu_gen_121.cpp:16429` |
| `f_00b53ba4` | `0x00b53be4` | `ppu_gen_121.cpp:17005` |
| `f_00b54478` | `0x00b544f8` | `ppu_gen_121.cpp:17597` |

The three wrappers are themselves not in any vtable (`x.py vt` empty for each), so the
"class that owns the operator" is **NOT DETERMINED**: no vtable, no constructor and no
object size could be attributed to it. What *is* established is the call shape — a wrapper
forwards its own `this` and one float:

```c
void f_00b532d0(Ctx& ctx){            // ppu_gen_121.cpp:16413
  ...
  R(28)=R(3);                         // this
  F(31)=F(1);                         // the stick/axis value
  R(26)=R(4); R(27)=R(5);
  ctx.lr=0x00b53310u;f_00b52584(ctx);  // operator(this, f1, r4, r5)
  R(30)=(R(28)+80u)&0xffffffffull;
  R(0)=r8((u32(R(30))+16u));           // byte at this+96
  crset<u32>(ctx,7,u32(R(0)),0x13u);   // == 19 ?
  ...
  switch(u32(ctx.ctr)){ ... }          // jump table at 0x00b53340
}
```

Inside the operator itself the axes are clamped to ±1 with the constants proven in §0
(`ppu_gen_121.cpp:15542`, `:15546`, `:15561`, `:15565`), and the turn command is delivered
through an indirect call whose return is `0x00b52708` (the `view_turn_set` call documented
in `hooks.txt`).

**What determines the gamepad port: NOT DETERMINED.** I could not find any store of a
port constant into the pad objects' port fields (pad port at `+384` for
`0x00951f94`, `+736` for `0x005ead28`) that is reachable from these call sites, and the
operator receives no explicit port/index argument — it takes `(this, float, r4, r5)` and
reads a pointer out of `r4`'s first word (`R(9)=r32(u32(R(4))); R(29)=r32(u32(R(9))+20u)` at
`ppu_gen_121.cpp:15531-15535`). Whether that object carries the pad or the port was not
established.

---

## T4 / T5. Participant loop and "create one AC"

**T4 (Vs): NOT DETERMINED.** `VsSortieScene::Init` (`f_00812238`, 816 lines,
`ppu_gen_086.cpp:11659-12474`) was read in part; it does base/string/allocator setup
(`ppu_gen_086.cpp:11667` class name, `:11687-11700` a 12-byte allocation, `:11728` a
**4528-byte** allocation with source line `335`, `:11741-11758` field initialisation) and then
continues through indirect container iteration. No loop over "participants" and no
"create one AC" function with a player index was identified.

Callee diff (`genidx.py`), which bounds where to look next:

* 22 distinct callees in Mission `Init`, 40 in Vs `Init`, 16 in base `Init`.
* Only **`f_008ff49c`** (a `std::string`-like constructor, called with the class name) is
  shared by all three.
* Only **`f_007384e8`** is shared between Mission and Vs.
* Vs-only setup: `f_00811400`, `f_00811948`, `f_00811a34`, plus 36 `0x0073xxxx` helpers.
* Mission-only setup: `f_007c7920`, `f_007c7b14`, `f_007c7cd4`, `f_007c9f9c`, plus
  `0x00737xxx`/`0x0074dxxx` helpers.

So **there is no single shared "create one AC(count, port)" helper called from both Inits** —
at least not one called *directly* from Init. Any shared creation must sit one level deeper
(inside `f_007384e8`, `f_00811a34`, `f_007c7920`, …), which was not traced.

One concrete per-AC mechanism *was* found, though it is node naming rather than controller
creation: `'ac_%d'` (`0x10050668`, cell `0x00eed6c0`) is used in `f_0080f384`
(`ppu_gen_086.cpp:8471-9124`) at `:8908`, with the index in `R(5)=R(22)` and a counter
incremented at `:8889-8892` over a 4-byte-stride list (`R(24)`, `R(22)`):

```c
// ppu_gen_086.cpp:8907
R(5)=R(22);                            // the counter == the AC index
R(4)=r32((u32(R(2))+11444u));          // "ac_%d"  (0x00eed6c0)
R(3)=R(17);
w32((u32(R(1))+144u),u32(R(16)));
ctx.lr=0x0080fa34u;f_0073cc58(ctx);    // format/lookup by index
```

The 2-entry "leaving area" table (T6) and this `ac_%d` loop are the two places where a
per-local-player index is visibly used.

**T5 (Mission): NOT DETERMINED.** I did not find the single player-AC creation site, so I
cannot state the count/port constant or its instruction address. What is established:
`MissionSortieScene::Init` = `f_007cf1cc` (`ppu_gen_083.cpp:15431-15741`) contains no
direct call to any "create AC" helper shared with Vs; its mission-specific work is delegated
to `f_007c7920` (`:15549`), `f_007c7b14` (`:15481`), `f_007c7cd4`, `f_007c9f9c` (`:15543`)
and the `0x0074dxxx` scene helpers.

---

## T6. Split screen: `SplitTimer` / `TwinTimer` / `LEAVING AREA P1|P2`

### A HUD element-name table exists at `0x1012dbf8`

```
1012dbf8: 100399c0  "Friend AC0"
1012dbfc: 100399d0  "Enemy AC0"
1012dc00: 100399e0  "ViewChangeKeyGuide"
1012dc04: 100399f8  "SplitTimerBackground"
1012dc08: 10039a10  "SplitTimer"
1012dc0c: 10039a20  "TwinTimerBackground"
1012dc10: 10039a38  "TwinTimer"
1012dc14: 10039a48  "TimerBackground"
1012dc18: 100398e0  "Timer"
1012dc1c: 00000000
1012dc20: 00000000
1012dc24: 00000000
```

Nine entries then three NULLs. `SplitTimer`/`TwinTimer` are therefore **HUD widget names of
the sortie HUD**, and the presence of `Friend AC0` / `Enemy AC0` in the same table shows the
HUD already has per-AC slots. Nothing in the gen tree loads this table by address
(`refindex.py 0x1012dc08` → 0 refs), i.e. it is reached through a pointer, not an immediate.

### `LEAVING AREA P1` / `P2` are a per-player table

| string | VA | pointer |
|---|---|---|
| `LEAVING AREA P1` | `0x10039a88` | **`0x00ed56c4`** |
| `LEAVING AREA P2` | `0x10039a98` | **`0x00ed56c8`** |

The two cells are adjacent — `[0x00ed56c4] = "…P1"`, `[0x00ed56c8] = "…P2"` — i.e. a 2-entry
array indexed by local player.

Code that reads it (`refindex.py 0x00ed56c4`, TOC `0x00ed7990`, so this is the HUD/message
TU): `f_00419954` (3 refs: `ppu_gen_043.cpp:13004`, `:13056`, `:13095`), `f_00419438`
(`:12262`), `f_004194e0` (`:12419`), `f_00419724` (`:12487`), `f_0041983c` (`:12562`),
`f_0041a158` (`:13540`), plus `f_004ff498`, `f_005a5bc4`, `f_00657f74`, `f_007b4bcc`.

`f_00419954` loads that cell three times (`-8908` = `0x00ed56c4`), each time feeding a small
HUD string-node construction:

```c
// ppu_gen_043.cpp:13003
R(0)=r32((u32(R(2))+-8912u));
R(9)=r32((u32(R(2))+-8908u));      // 0x00ed56c4 = "LEAVING AREA P1"
{u64 x=R(0);R(29)=0-x;}
R(29)=R(29)&0xfull;
{u64 x=R(29),y=R(0),r=x+y;R(29)=r;}
R(29)=R(29)&0xffffffffull;        // R(29) = aligned scratch node
w32(u32(R(29)),u32(R(9)));
w32((u32(R(29))+8u),u32(R(11)));
w32((u32(R(29))+4u),u32(R(11)));
ctx.lr=0x00419f84u;f_002603ec(ctx);
...
w32(u32(R(30)),u32(R(29)));       // publish
```

The surrounding blocks (`ppu_gen_043.cpp:13003-13025` and `:13055-13070`) are structurally
near-identical node constructions, but **both load the same cell** (`-8908`), so they are
not a P1/P2 pair with a literal index. Selecting `"…P2"` must therefore happen by offsetting
from this cell with a runtime player index; that indexing instruction was **not** identified.

**What variable selects split screen: NOT DETERMINED.** I did not identify the read that
gates the second block (nor a viewport/camera setup loop). The concrete handles for the
follow-up are: the two-element string cell `0x00ed56c4`, its consumer `f_00419954`
(`ppu_gen_043.cpp:13003-13025`, `:13055-13070`, `:13095`), and the HUD name table
`0x1012dbf8` whose `SplitTimer`/`TwinTimer` entries imply the HUD already distinguishes
split from non-split.

---

## Open questions

1. **Which class owns vtable `0x10138b18`?** The pool layout at `0x00ee97b8..0x00ee97cc`
   suggests `MissionFailedScene`; slot 5 holds `MissionSortieScene::Init`, and only one
   vtable in the whole binary references it. Resolve by finding the allocation site and the
   `f_007cfbf8`/`f_007cfc5c` callers (they are invoked only through slots 0/1).
2. **Object sizes.** `new`-site sizes were not found for any sortie scene; only lower bounds
   from field offsets (base ⊇ 8.3 KB, Vs ⊇ 64 KiB). The allocating factory is the missing
   link.
3. **The pad port.** `0x00b52584` takes `(this, float, r4, r5)` and no port/index; the three
   wrappers (`0x00b532d0`, `0x00b53ba4`, `0x00b54478`) have no vtable and no located ctor.
   Which field of which object names the pad port is unresolved, so "give Mission a second
   AC on port 1 the way Vs does" cannot yet be implemented from the static picture alone.
4. **The participant structure** (count / per-player control type / pad port / team / start
   position / design pointer) was not located. No shared `create-one-AC(count, port)` helper
   is called from either Init; look one level deeper inside `f_007384e8` (the only callee
   shared by Mission and Vs), and in Vs-only `f_00811400` / `f_00811948` / `f_00811a34`.
5. **Where the local-player count comes from.** Not found. The split-screen selector read is
   unidentified; the two-block duplication in `f_00419954` and the `ac_%d` loop in
   `f_0080f384` are the best observable consequences of it.
6. **False-positive risk in T1's long lists.** `refindex.py` resolves `r2`-relative deltas
   against 8 TOCs; a function's true TOC is only known when its OPD is referenced (21 210 of
   55 620 functions). Every claim in this document that names a *function* was read in the
   source, not taken from an index hit.
