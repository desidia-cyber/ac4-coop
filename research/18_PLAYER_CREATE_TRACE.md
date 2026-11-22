# 18 窶・Who creates player 2's AC, controller and pad operator (runtime trace)

**Question.** By which exact call chain does `VsSortieScene` create player 2's AC, its controller and
its pad operator with **port 1**, and what is the corresponding chain for the single player in
`MissionSortieScene`?

**Method.** Runtime only: a guest stack walk plus probes on the constructors, gated behind
`AC4_TRACE_CREATE=1` (see "Instrument" below). Two scenarios were run with
`AC4_TRACE_CREATE=1 AC4_SURVEY_AC=1`:

| run | scenario | log (verbatim copy) |
|---|---|---|
| `20261115-033106` | A 窶・offline split-screen versus, match HUD visible, `AC#2` registered | `research\18_raw\A2_vs_creators.log` |
| `20261115-033416` | B 窶・save loaded, campaign mission **SWARMS OF RED EYES** (m010), `input context gameplay` | `research\18_raw\B2_mission_creators.log` |

Earlier runs of the same instrument (fewer hooks, or the stack walk broken) are kept for the record:
`A_vs_trace.log`, `A1_vs_operators.log`, `B_mission_trace.log`.

Everything below is what the logs printed; anything I could not measure is marked **NOT DETERMINED**.

---

## 0. One correction to the brief

The pad operator has **two different classes** with two different vtables, one per mode 窶・there is no
single operator class:

| mode | operator vtable | image cell | operator ctor | update entry that runs |
|---|---|---|---|---|
| versus (VsSortieScene) | `0x10147998` | `0x00f00864` (TOC-17908) | **`0x00b514e8`** (`0x00b5129c` are twins) | `0x00b54f24` |
| campaign mission | `0x10147a50` | `0x00f00868` (TOC-17904) | **`0x00b5193c`** 竊・`0x00b5129c` | `0x00b54e94` |

`0x10147998` occurs exactly once in `EBOOT.elf` (file offset `0xef0864`, i.e. the TOC cell), so every
live operator object is built by a function that reads that cell 窶・this is what makes the two ctor
families identifiable statically. Both objects are updated through the same
`0x00b54478(operator, input, dt)` and both read the port at **`operator+12`** there (that hook is
where the port was measured).

Field layout, measured in both runs: `+0` vtable, `+4` = 1 (enabled), `+8` the AC input state object
(`r32` of it equals the `state` in the `AC#n` survey line), `+12` the **gamepad port**.

---

## (i) The creation chains

### A 窶・versus, player 1 **and** player 2 (identical chain, called twice)

```
VsSortieScene::Init  f_00812238
   笏披楳 [0x00812c20, an INDIRECT call through a vtable, r5 = the player index R(23)]
      f_0080f09c        "CREATE_VS_ENTRY"      (r5 = 0 for P1, 1 for P2)
        笏披楳 [0x0080f318]
           f_0080c9c8   "CREATE_VS_LOOP"       (r4 = 0 / 1, r5 = that player's AC body)
             笏披楳 [0x0080ca74]
                f_00738318  "CREATE_VS_PLAYER" (r4 = 0 / 1 = THE PAD PORT, r7 = the AC body)
                  笏披楳 f_00b514e8  pad-operator ctor: this+0 = 0x10147998, +4 = 1, +12 = r4
```

Both calls come from the *same* call site `0x0080ca74`; the only difference is the index (right
column). Times: `114.9500` (P1) and `114.9685` (P2) 窶・18 ms apart, inside the same match setup.

Controller: the AC controller (`ctl`, vtable **`0x10140e28`** in both modes) is **not** created by this
chain 窶・its constructor was **NOT DETERMINED** (hooks on the four functions that reference its vtable
cell `0x00f04588` 窶・`0x007dcfcc`, `0x00ba46b0` 窶・never fired; the two other candidates were false
positives and were removed). What *is* measured: in versus each AC has its own `ctl`, `input`
(`vt 0x1013f400`), `state` and `body`, and the operator drives that player's `state`:

```
AC#1 ctl 0x10fcba10 input 0x10fcdb80 state 0x10fba140 obj 0x10fc9720 body 0x10fae650
AC#2 ctl 0x10fce540 input 0x10ff42e0 state 0x10fe08b0 obj 0x10fefe90 body 0x10fd4dc0
OPERATOR 0x10fce7a0 vt 0x10147998 +4(enabled) 1 +8(input) 0x10fba140 +12(port) 0     <- P1
OPERATOR 0x10ff4ef0 vt 0x10147998 +4(enabled) 1 +8(input) 0x10fe08b0 +12(port) 1     <- P2
```

### B 窶・campaign mission, the one player

```
Ac4SortieScene::Init  f_00740838            (the BASE class Init, not MissionSortieScene::Init)
   笏披楳 [0x00740a88]
      f_007ce61c
        笏披楳 [0x007ce638]
           f_00b0d354
             笏披楳 [0x00b0d6ac]
                f_00b0b8f0   "CREATE_MS_PLAYER"   -- called ONCE
                  笏披楳 [0x00b0b9a0]
                     f_00b5193c  controller ctor: this+0 = 0x10147a50, this+116 = vtable+76,
                                 this+124 = -1, this+128 = r6 (the AC body)
                       笏披楳 [0x00b51964]
                          f_00b5129c  base ctor: this+0 = 0x10147998 (overwritten by 0x00b5193c),
                                      this+8 = r5, this+12 = r4
```

The operator the mission later updates is the **same object** `0x1302b760` that `0x00b5193c`
constructed; its `+12` stays at the ctor's default **0**, so the mission's single player is on
pad 0:

```
OPERATOR 0x1302b760 vt 0x10147a50 +4(enabled) 1 +8(input) 0x13017590 +12(port) 0
AC#1 ctl 0x13028a10 input 0x1302ab80 state 0x13017590 obj 0x13026740 body 0x13009780  <== PLAYER
```

Note the mission chain does **not** go through `MissionSortieScene::Init` (`0x007cf1cc`) at all 窶・the
base `Ac4SortieScene::Init` (`0x00740838`) is the one that reaches the player creation.

---

## (ii) The function called twice in versus / once in the mission

**Measured answer: there is no such function.** The two chains share only the scene-framework frames
(listed at the end of this section); below them they are **disjoint**:

| function | versus | mission |
|---|---|---|
| `f_0080f09c` | 2 calls | 0 |
| `f_0080c9c8` | 2 calls | 0 |
| `f_00738318` | 2 calls | 0 |
| `f_00b0b8f0` | 0 | **1 call** |
| `f_00b514e8` (operator ctor, VS class) | 2 | 0 |
| `f_00b5193c` (controller ctor, mission class) | 0 | 1 |

So the deepest function that is called once per player is **`f_00738318`** in versus (twice) and the
whole `f_00b0b8f0` path in the mission (once) 窶・they are different functions on different paths. The
engine has no shared "create one player AC(count, port)".

### Arguments (r3..r8) and 256-byte dumps, versus

```
CREATE_VS_ENTRY 0x80f09c this 0xd0100740  sp 0xd0100430 lr 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- ...
   args: r3 0xd0100740 r4 0x107d4f00 r5 0x00000000 r6 0xd0100634 r7 0x158d2210 r8 0x107d535c   (P1)
CREATE_VS_ENTRY 0x80f09c this 0xd0100740  sp 0xd0100430 lr 0x00812c20 ...
   args: r3 0xd0100740 r4 0x107d4f00 r5 0x00000001 r6 0xd0100634 r7 0x158d2050 r8 0x107d7204   (P2)
CREATE_VS_LOOP 0x80c9c8 this 0x107d4f00  sp 0xd0100240 lr 0x0080f318 <- 0x00812c20 <- ...
   args: r3 0x107d4f00 r4 0x00000000 r5 0x10fae650 r6 0x00000000 r7 0x00000000 r8 0x00000000   (P1)
CREATE_VS_LOOP 0x80c9c8 this 0x107d4f00  sp 0xd0100240 lr 0x0080f318 <- 0x00812c20 <- ...
   args: r3 0x107d4f00 r4 0x00000001 r5 0x10fd4dc0 r6 0x00000000 r7 0x00000000 r8 0x00000000   (P2)
CREATE_VS_PLAYER 0x738318 this 0x10fce7a0  sp 0xd0100180 lr 0x0080ca74 <- 0x0080f318 <- 0x00812c20 <- ...
   args: r3 0x10fce7a0 r4 0x00000000 r5 0x00000000 r6 0xd01001f0 r7 0x10fae650 r8 0x1056c720   (P1)
CREATE_VS_PLAYER 0x738318 this 0x10ff4ef0  sp 0xd0100180 lr 0x0080ca74 <- 0x0080f318 <- 0x00812c20 <- ...
   args: r3 0x10ff4ef0 r4 0x00000001 r5 0x00000000 r6 0xd01001f0 r7 0x10fd4dc0 r8 0x1056c76c   (P2)
```

Dumps taken at hook entry (P1; identical shape for P2):

`r3` = the operator object being constructed (still zero, only `+0x90` onwards populated):

```
r3+000: 00010000 00000000 ffff0000 00000000
r3+010: 00010000 00000000 ffff0000 00000000
r3+020: 00010000 00000000 ffff0000 00000000
r3+030: 267f788c c7788cc7 ffff0000 00000000
r3+040: 00010000 00000000 ffff0000 00000000
r3+050: 34f2deed dde66f06 ffff0000 00000000
r3+060: 3affb66d db490200 ffff0000 00000000
r3+070: 3affb66d db490200 ffff0000 00000000
r3+080: 10fce790 11004090 1056c720 1056c720
r3+090: 14bf7662 27ff2349 ffff0000 00000000
r3+0a0: 00010000 00000000 ffff0000 00000000
r3+0b0: 7f004990 04499004 ffff0000 00000000
r3+0c0: 26ffb19d 240110db ffff0000 00000000
r3+0d0: 26ffb69d 240060db ffff0000 00000000
r3+0e0: 26c4b6fd ff48e0c4 ffff0000 00000000
r3+0f0: 26ffb69d 240060db ffff0000 00000000
```

`r6` = the caller's stack frame; `+0` is the **key-assignment table** the ctor copies from, and the
frame also holds the chain's own return address `0x0080f318` at `+0x68` (confirms the walk):

```
r6+000: 00010203 06070405 11100d09 0f0b0a08
r6+010: 0e0c1300 107d52cc 00000000 107e4f00
r6+020: 00000000 00000000 00000000 d0100740
r6+030: 00000000 101009a0 00000000 d01002f0
r6+040: 00000000 107d4f00 00000000 107d535c
r6+050: 00000000 d0100430 00000000 00000280
r6+060: 00000000 0080f318 ffffffff ffffffff
r6+070: 00000000 d01002e0 00000000 00eeaa0c
r6+080: 00000000 004deb44 00000000 156281f4
r6+090: 00000000 c2d57880 00000000 00ec5b04
r6+0a0: 00000000 d0100380 00000000 00eeaa0c
r6+0b0: 00000000 0055f630 00000000 00ecedb8
r6+0c0: 00000000 0055e0e4 00000000 00ecedb8
r6+0d0: 00000000 d01003d0 00000000 10fae13c
r6+0e0: 00000000 10031710 00000000 10fac8c0
r6+0f0: 00000000 d0100430 00000000 10fae1d0
```

`r7` = the AC body of that player (P1's `body` from the survey = `0x10fae650`; the dump shows the body
vtable `0x10142638` at `+0`, mass/scale fields following):

```
r7+000: 10142638 000003e8 00000001 00000000
r7+010: 3f800000 00000000 00000000 00000000
r7+020: 00000000 3f800000 00000000 00000000
r7+030: 00000000 00000000 3f800000 00000000
```

### Arguments, mission (single call)

```
CREATE_MS_PLAYER 0xb0b8f0 this 0x10d3d540  sp 0xd0100020 lr 0x00b0d6ac <- 0x00740a88 <- 0x007ce638 <- ...
   args: r3 0x10d3d540 r4 0x13009780 r5 0x10883ee0 r6 0x00000000 r7 0x13009780 r8 0x11a225a4
CTOR 0xb5193c (controller) this 0x1302b760  lr 0x00b0b9a0 <- 0x00b0d6ac <- 0x00740a88 <- ...
   args: r3 0x1302b760 r4 0x00000000 r5 0x00000000 r6 0x13009780 r7 0xd00ffea4 r8 0x13009780
CTOR 0xb5129c (operator)   this 0x1302b760  lr 0x00b51964 <- 0x00b0b9a0 <- ...
   args: r3 0x1302b760 r4 0x00000000 r5 0x00000000 r6 0xd00ffea4 r7 0x13009780 r8 0x13009780
```

`r4`/`r7` of `f_00b0b8f0` and `r6`/`r8` of `f_00b5193c` are the player's AC body `0x13009780`
(mission `AC#1 body`). `r6 = 0` of `f_00b0b8f0` is the only per-player-looking scalar in that call;
whether it *is* the port/index is **NOT DETERMINED** (see the last section).

---

## (iii) Where port 1 comes from for player 2

**It is the loop index in `VsSortieScene::Init`, passed down as a call argument 窶・not a field.** There
is no struct offset to give for the source; the destination field is `operator+12`.

Measured path (all values from the log):

1. `VsSortieScene::Init` (`f_00812238`) reaches the creator with an **indirect call at `0x00812c20`**.
   Statically that site is `R(5)=R(23); ... ctx.ctr = vtable_slot; icall(...)` with
   `R(8) = R(23)*7848 + R(20) + 680` 窶・i.e. `R(23)` is a per-player index with a **7848-byte stride**
   (`ppu_gen_086.cpp:12326` region).
2. It is called **twice**, with `r5 = 0` then `r5 = 1`:
   `CREATE_VS_ENTRY ... r5 0x00000000` / `r5 0x00000001`.
3. `f_0080f09c` forwards it as **`r4`** of `f_0080c9c8`: `r4 0x00000000` / `r4 0x00000001`.
4. `f_0080c9c8` forwards it as **`r4`** of `f_00738318`: same values.
5. `f_00738318` forwards it as **`r4`** of the operator ctor `f_00b514e8`, which stores it at
   `this+12` (`w32(this+12, R(4))` in the ctor body): `r4 0` 竊・`operator+12 = 0`,
   `r4 1` 竊・`operator+12 = 1`.
6. `operator+12` is read every frame by `f_00b54478` (`R(3)=r32(R(3)+12)` 竊・`f_00af5290` 竊・pad fetch),
   which is how pad 1 drives AC 2.

So the "port" is the same number as the player index in versus: **P1 = index 0 = port 0, P2 = index
1 = port 1**, one index for the stage slot (stride 7848) and for the pad.

In the mission the same field exists but is left at the ctor default: `f_00b5193c` passes no port
(it writes `this+128 = r6` = the AC body and `this+124 = -1`), and `f_00b5129c`, which *does* write
`this+12 = r4`, is entered with `r4 = 0`. Result: `+12(port) 0`.

---

## (iv) The frame with no counterpart in the mission chain

Both chains, aligned **from the oldest frame** (the printed order is `lr` first, then successively
older frames):

| # | versus (A) | mission (B) |
|---|---|---|
| 1 | `0x000113cc` | `0x000113cc` |
| 2 | `0x00011518` | `0x00011518` |
| 3 | `0x00024484` | `0x00024484` |
| 4 | `0x0091b188` | `0x0091b188` |
| 5 | `0x0091b0e4` | `0x0091b0e4` |
| 6 | `0x005edb10` | `0x005edb10` |
| 7 | `0x0074eae8` | `0x0074eae8` |
| 8 | **`0x007b2c7c`** | `0x0074eae8`  竊・first difference |
| 9 | `0x0074eb40` | `0x0074eae8` |
| 10 | `0x0074e8c8` | `0x0074eb40` |
| 11 | **`0x00812c20`** (in `VsSortieScene::Init` `f_00812238`) | `0x0074e8c8` |
| 12 | `0x0080f318` (in `f_0080f09c`) | **`0x007ce638`** (in `f_007ce61c`) |
| 13 | `0x0080ca74` (in `f_0080c9c8`) | `0x00740a88` (in `Ac4SortieScene::Init` `f_00740838`) |
| 14 | 窶・| `0x00b0d6ac` (in `f_00b0d354`) |

**The versus frame with no counterpart in the mission is the scene-level one:
`0x00812c20` inside `VsSortieScene::Init` (`f_00812238`).** In the mission the corresponding slot of
the chain is `0x00740a88` inside the **base** `Ac4SortieScene::Init` (`f_00740838`), which is followed
by the mission-only `f_007ce61c` / `f_00b0d354` steps.

(The numerically first differing frame is `0x007b2c7c` at #8, but that is the repeated-update
function `0x0074eae8` appearing a different number of times 窶・the mission chain has three
`0x0074eae8` frames where versus has one plus `0x007b2c7c`. The meaningful divergence is the scene
Init at #11.)

---

## Raw trace lines, verbatim

### A 窶・versus (`run\data\runs\20261115-033106\ac4.log`)

```
[ 114.9200 I 58364 create] CREATE_VS_ENTRY 0x80f09c this 0xd0100740  sp 0xd0100430 lr 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9200 I 58364 create]    args: r3 0xd0100740 r4 0x107d4f00 r5 0x00000000 r6 0xd0100634 r7 0x158d2210 r8 0x107d535c
[ 114.9495 I 58364 create] CREATE_VS_LOOP 0x80c9c8 this 0x107d4f00  sp 0xd0100240 lr 0x0080f318 <- 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9495 I 58364 create]    args: r3 0x107d4f00 r4 0x00000000 r5 0x10fae650 r6 0x00000000 r7 0x00000000 r8 0x00000000
[ 114.9500 I 58364 create] CREATE_VS_PLAYER 0x738318 this 0x10fce7a0  sp 0xd0100180 lr 0x0080ca74 <- 0x0080f318 <- 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9500 I 58364 create]    args: r3 0x10fce7a0 r4 0x00000000 r5 0x00000000 r6 0xd01001f0 r7 0x10fae650 r8 0x1056c720
[ 114.9505 I 58364 create] CTOR 0xb514e8 (operator, port = r4) this 0x10fce7a0  sp 0xd0100180 lr 0x0080ca74 <- 0x0080f318 <- 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9532 I 58364 create] CREATE_VS_ENTRY 0x80f09c this 0xd0100740  sp 0xd0100430 lr 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9533 I 58364 create]    args: r3 0xd0100740 r4 0x107d4f00 r5 0x00000001 r6 0xd0100634 r7 0x158d2050 r8 0x107d7204
[ 114.9683 I 58364 create] CREATE_VS_LOOP 0x80c9c8 this 0x107d4f00  sp 0xd0100240 lr 0x0080f318 <- 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9683 I 58364 create]    args: r3 0x107d4f00 r4 0x00000001 r5 0x10fd4dc0 r6 0x00000000 r7 0x00000000 r8 0x00000000
[ 114.9685 I 58364 create] CREATE_VS_PLAYER 0x738318 this 0x10ff4ef0  sp 0xd0100180 lr 0x0080ca74 <- 0x0080f318 <- 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 114.9685 I 58364 create]    args: r3 0x10ff4ef0 r4 0x00000001 r5 0x00000000 r6 0xd01001f0 r7 0x10fd4dc0 r8 0x1056c76c
[ 114.9687 I 58364 create] CTOR 0xb514e8 (operator, port = r4) this 0x10ff4ef0  sp 0xd0100180 lr 0x0080ca74 <- 0x0080f318 <- 0x00812c20 <- 0x0074e8c8 <- 0x0074eb40 <- 0x007b2c7c <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 118.8971 I 58364 create] OPERATOR 0x10fce7a0 vt 0x10147998 +4(enabled) 1 +8(input) 0x10fba140 +12(port) 0
[ 118.9013 I 58364 create] OPERATOR 0x10ff4ef0 vt 0x10147998 +4(enabled) 1 +8(input) 0x10fe08b0 +12(port) 1
[ 115.0449 I 58364 input] AC#1 ctl 0x10fcba10 input 0x10fcdb80 state 0x10fba140 obj 0x10fc9720 body 0x10fae650
[ 115.0449 I 58364 input] AC#1 vt: ctl 0x10140e28 input 0x1013f400 state 0x00000000 obj 0x10148210 body 0x10142638
[ 115.0467 I 58364 input] AC#2 ctl 0x10fce540 input 0x10ff42e0 state 0x10fe08b0 obj 0x10fefe90 body 0x10fd4dc0
[ 115.0467 I 58364 input] AC#2 vt: ctl 0x10140e28 input 0x1013f400 state 0x00000000 obj 0x10148210 body 0x10142638
```



### B 窶・campaign mission (`run\data\runs\20261115-033416\ac4.log`)

```
[ 533.4756 I 74932 create] CREATE_MS_PLAYER 0xb0b8f0 this 0x10d3d540  sp 0xd0100020 lr 0x00b0d6ac <- 0x00740a88 <- 0x007ce638 <- 0x0074e8c8 <- 0x0074eb40 <- 0x0074eae8 <- 0x0074eae8 <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 533.4756 I 74932 create]    args: r3 0x10d3d540 r4 0x13009780 r5 0x10883ee0 r6 0x00000000 r7 0x13009780 r8 0x11a225a4
[ 533.4763 I 74932 create] CTOR 0xb5193c (controller, port = r6) this 0x1302b760  sp 0xd00ffe30 lr 0x00b0b9a0 <- 0x00b0d6ac <- 0x00740a88 <- 0x007ce638 <- 0x0074e8c8 <- 0x0074eb40 <- 0x0074eae8 <- 0x0074eae8 <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 533.4763 I 74932 create]    args: r3 0x1302b760 r4 0x00000000 r5 0x00000000 r6 0x13009780 r7 0xd00ffea4 r8 0x13009780
[ 533.4771 I 74932 create] CTOR 0xb5129c (operator, port = r4) this 0x1302b760  sp 0xd00ffda0 lr 0x00b51964 <- 0x00b0b9a0 <- 0x00b0d6ac <- 0x00740a88 <- 0x007ce638 <- 0x0074e8c8 <- 0x0074eb40 <- 0x0074eae8 <- 0x0074eae8 <- 0x0074eae8 <- 0x005edb10 <- 0x0091b0e4 <- 0x0091b188 <- 0x00024484 <- 0x00011518 <- 0x000113cc
[ 533.4771 I 74932 create]    args: r3 0x1302b760 r4 0x00000000 r5 0x00000000 r6 0xd00ffea4 r7 0x13009780 r8 0x13009780
[ 533.5312 I 74932 create] OPERATOR 0x1302b760 vt 0x10147a50 +4(enabled) 1 +8(input) 0x13017590 +12(port) 0
[ 533.5852 I 74932 input] AC#1 ctl 0x13028a10 input 0x1302ab80 state 0x13017590 obj 0x13026740 body 0x13009780  <== PLAYER
[ 533.5852 I 74932 input] AC#1 vt: ctl 0x10140e28 input 0x1013f400 state 0x02000000 obj 0x10148210 body 0x10142638
```

The operator-update lines (`OPERATOR(0xb54f24)` / `OPERATOR(0xb54e94)`) and the full 256-byte dumps are
in the two saved logs; only the lines needed for (i)窶・iv) are repeated here.

---

## Instrument (what was added, and how to re-run)

**New 窶・`ac4-port/src/runtime/game/create_hooks.{h,cpp}`**

* `rt::game::guest_backtrace(ctx, out, depth)` 窶・reusable guest stack walk: `sp = R(1)`, then
  `next = r64(sp)`, return address `= (u32)r64(next+16)`, `sp = next`; guarded against a null/unaligned
  sp, a non-increasing chain, a jump over 0x100000 and a non-stack address (accepts only
  `0xD0000000..0xF0000000`, the guest thread-stack range). `backtrace_text()` prints
  `sp 窶ｦ lr 窶ｦ <- 窶ｦ <- 窶ｦ`. **The saved LR is a 64-bit slot 窶・its low word is the address; reading
  `r32(next+16)` yields the zero high word and an empty walk (that was the first bug here).**
* an **allocation ring** on the game heap hook (`0x008fdbe8`): address, size, caller, r3..r8 and a
  20-frame chain per allocation (16384 entries, in memory only), plus `create_trace_object()` which
  reports the allocation that created a given object. It found nothing for these objects 窶・see
  NOT DETERMINED.
* `create_trace_args()` 窶・logs r3..r8 and a 256-byte hex dump of every argument that is a live guest
  pointer.

**Hooks added (`hooks.txt`)** 窶・all inert without `AC4_TRACE_CREATE=1`:

```
0x00b54478 operator_update     0x00b54dd0/0x00b54e94/0x00b54f24/0x00b54fa0 operator_update_a..d
0x00b50e14 operator_ctor       0x00b51058 operator_ctor_b
0x00b5129c player_ctor_e       0x00b514e8 player_ctor_f
0x00b5193c player_ctor_a       0x00b519ac player_ctor_b
0x00b51a1c player_ctor_c       0x00b51a78 player_ctor_d
0x00738318 create_vs_player    0x0080c9c8 create_vs_loop
0x0080f09c create_vs_entry     0x00b0b8f0 create_ms_player
0x007dcfcc/0x00ba46b0 acctl_ctor_a/d (did not fire)
```

Probes live in `input_hooks.cpp` (they reuse the existing `AC4_SURVEY_AC` survey to name the objects);
the allocation ring is wired into `frame_hooks.cpp::hook_heap_alloc`.

**Re-run**

```powershell
$env:AC4_TRACE_CREATE='1'; $env:AC4_SURVEY_AC='1'
G:\...\coop-mod\run\vs_split.ps1 -Tag traceVS        # scenario A; drive the panel by hand if it stalls
G:\...\coop-mod\run\trace_mission.ps1 -Tag traceMS   # scenario B (load save -> hub -> first mission)
```

## Source files changed

| file | change |
|---|---|
| `ac4-port/src/runtime/game/create_hooks.h` | **new** 窶・backtrace / ring / arg-dump API |
| `ac4-port/src/runtime/game/create_hooks.cpp` | **new** 窶・the instrument (all gated by `AC4_TRACE_CREATE`) |
| `ac4-port/src/runtime/game/input_hooks.cpp` | vtables in the `AC#n` survey; operator/ctor/create probes |
| `ac4-port/src/runtime/game/frame_hooks.cpp` | one call: `create_trace_alloc(ctx)` after the heap alloc |
| `ac4-port/src/runtime/game/hooks.txt` | 19 new hook lines (26 -> 45; no existing line changed) |
| `run/trace_mission.ps1` | **new** 窶・campaign scenario runner (no P2 hooks) |
| `run/trace_vs.ps1` | **new** 窶・versus runner with a re-reading panel walk |
| `ac4-port/local/gen/ppu/*` | regenerated by `ac4-recomp emit 窶ｦ --hooks hooks.txt --patches patches.txt` |

No existing hook was removed; `0x00b54478` and the four update variants are hooked only for the probe.

## NOT DETERMINED

1. **The AC controller's constructor.** `ctl` has vtable `0x10140e28` in both modes, but none of the
   four functions that reference its vtable cell (`0x00f04588`: `0x007dcfcc`, `0x00539990`,
   `0x00a65590`, `0x00ba46b0`) is the live constructor 窶・the two I hooked never fired with a plausible
   `this`, and the other two were false positives (one floods 250+ times with an unrelated object).
   Who allocates and constructs `ctl` is unmeasured.
2. **Which allocation creates the operator / ctl / input objects.** The ring on the game heap
   (`0x008fdbe8`) held only 351窶・76 allocations per run and never contained any of these addresses, so
   they come from a different allocator. Which one: unmeasured.
3. **The meaning of `r6` in `f_00b0b8f0`** (mission): it is the only per-player-looking scalar in that
   call and it is `0`. Whether a second call with `r6 = 1` would produce a pad-1 operator 窶・and
   therefore whether this is the campaign hook point 窶・is NOT DETERMINED by this trace (the mission
   path never passes the port to the operator ctor; `f_00b5193c` writes `this+128 = r6` = the AC body).
4. **The exact object layout of the versus operator's owner.** `f_00b5193c` writes `this+0` (vtable
   `0x10147a50`), `this+116` (vtable+76), `this+124 = -1`, `this+128 = r6`; which sub-object those
   belong to is inferred from the ctor code, not from a class descriptor.
5. **Whether `f_00738318` alone suffices** to create a second pad-driven AC in a campaign mission (the
   end goal): out of scope here, unmeasured.
