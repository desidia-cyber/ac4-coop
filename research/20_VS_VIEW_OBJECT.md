# 20 - The per-player view objects of VsSortieScene, and why the campaign mission has none

**Scope.** Static only: `EBOOT.elf` (BLUS30027) + the recompiled C in
`ac4-port/local/gen/ppu/`. Every code citation is `ppu_gen_NNN.cpp:line`; every address is a guest VA.
A statement is made only if it was read in those files; anything unresolved is marked
**NOT DETERMINED**. No game/binary was run for this report.

**Tool added** (`tools/re/vsview.py`, plus a private index cache so the shared `genidx.pkl` is not
touched): `thunk` (resolve a TOC-thunk chain), `body`, `cellsc` (read TOC cells with annotation),
`vt` (dump a vtable slot -> OPD -> code), `callers`, `xref`.

**Headline.** The 716-byte object is **not a camera and not a 3D view**: it is the per-player
versus **frontend/HUD** (`VsPlayerFrontend`, resource `VsFE_full`). It is constructed at exactly
**one** call site in the whole program, inside `VsSortieScene::Init`'s per-player helper. The
campaign mission never constructs it, nor the 300-byte sibling, and has no per-player record array
at all.

---

## Q1. Thunk resolution and the three constructed classes

### Q1a. The twelve thunks

All eleven resolvable thunks are pure TOC stubs of the form
`w64(sp+40, r2); r2 += delta; {f_XXXX(ctx); return;}`, and all live in `ppu_gen_077.cpp`.
`delta` is measured from the caller's TOC **0x00eeaa0c**; each resulting `toc` is one of the eight
real per-TU TOCs of the binary, which is what validates the base.

| thunk | file:line | delta | resulting TOC | real target |
|---|---|---|---|---|
| `f_00737888` | `ppu_gen_077.cpp:5953` | +45704 | `0x00ef5c94` | **`f_00a6e120`** |
| `f_00739ba8` | `ppu_gen_077.cpp:9328` | -77948 | `0x00ed7990` | **`f_006c8530`** (112-byte ctor) |
| `f_00737ad8` | `ppu_gen_077.cpp:6175` | -77948 | `0x00ed7990` | **`f_006ca970`** (300-byte ctor) |
| `f_00737ef8` | `ppu_gen_077.cpp:6571` | -77948 | `0x00ed7990` | **`f_0069e908`** (716-byte ctor) |
| `f_0073caf8` | `ppu_gen_077.cpp:13870` | +107596 | `0x00f04e58` | **`f_00b12634`** |
| `f_00739068` | `ppu_gen_077.cpp:8248` | +45704 | `0x00ef5c94` | **`f_00a5307c`** |
| `f_00738318` | `ppu_gen_077.cpp:6970` (`_guest`) | +107596 | `0x00f04e58` | **`f_00b514e8`** (VS pad-operator ctor) |
| `f_00805450` | `ppu_gen_085.cpp:23385` | - | - | **itself - not a thunk** (real 16-line predicate) |
| `f_0073d0f8` | `ppu_gen_077.cpp:14446` | +45704 | `0x00ef5c94` | **`f_00a4f210`** |
| `f_00738008` | `ppu_gen_077.cpp:6673` | +107596 | `0x00f04e58` | **`f_00b4fe84`** |
| `f_0073bb58` | `ppu_gen_077.cpp:12370` | +107596 | `0x00f04e58` | **`f_00b128ec`** |
| `f_00739638` | `ppu_gen_077.cpp:8806` | +107596 | `0x00f04e58` | **`f_00b50d78`** |

Cross-check that the base TOC is right: `f_00738318 -> f_00b514e8` is exactly the versus
pad-operator constructor measured at runtime in
[18_PLAYER_CREATE_TRACE.md](./18_PLAYER_CREATE_TRACE.md) (`this+0 = 0x10147998, this+12 = port`).

```c
// ppu_gen_077.cpp:9328  (representative thunk shape)
void f_00739ba8(Ctx& __restrict ctx){
  w64((u32(R(1))+40u),u64(R(2)));        // save caller TOC
  R(2)=R(2)+u64(s64(-1)<<16);            // -65536
  R(2)=R(2)+u64(s64(-12412));            //  = -77948 -> 0x00ed7990
  {f_006c8530(ctx);return;}              // tail-call, real 112-byte ctor
}
```

### Q1b. Class 112 bytes - `f_006c8530` (`ppu_gen_072.cpp:11822`)

Vtable cell: **`0x00edbbf8`** = `toc(0x00ed7990) + 17000` -> value **`0x1012f3a0`**
(loaded at `ppu_gen_072.cpp:11824` and stored at `11832`).

| slot | code |
|---|---|
| 0 | `0x006c85c8` (`ppu_gen_072.cpp:11862`) |

The class is a 4x4 **identity matrix** at `this+16` plus one float:

* `ppu_gen_072.cpp:11832` `w32(this+0, cell)` - the vtable.
* `11835`-`11850` write the identity matrix into `this+16..this+79` (`+16/+36/+56/+76 = 1.0f`,
  everything else `0.0f`; `1.0f = rf32(toc+16912)` = cell `0x00edbba0` = `3f800000`).
* `11852`-`11853` `F(13) = rf32(r32(toc+17004))` -> cell `0x00edbbfc` = `0x1012f3a8`, and
  `[0x1012f3a8] = 0x42200000` = **40.0f**, stored to `this+80` (`11853`).
* `11854`/`11855` `this+84 = 1`, `this+88 = 0`; `11856`-`11859` `this+92..this+107` = `{0,0,1,1}`.
* Its literal pool neighbours `"DL_FREG_000"` / `"DL_FREG_000[000]"` (`0x00edbbc8`, `0x00edbbcc`),
  i.e. it is a float-register / shader-parameter block ("FREG"), used as a matrix+radius pair.

### Q1c. Class 300 bytes - `f_006ca970` (`ppu_gen_072.cpp:14330`)

**Its `this+0` is not a static vtable.** The constructor takes the value from **argument r5**:

```c
// ppu_gen_072.cpp:14330, field layout of the 300-byte object
void f_006ca970(Ctx& ctx){
  R(29)=R(3);                          // this            (:14342)
  ...
  w32(u32(R(29)),       u32(R(5)));    // this+0   = r5   (:14363)
  w32((u32(R(29))+4u),  u32(R(4)));    // this+4   = r4   (:14365)
  ...
  R(0)=r32((u32(R(2))+18132u));        // cell 0x00edc064 = 0x1012f3b8 (:14400)
  w32(u32(R(20)),       u32(R(0)));    // this+264 = 0x1012f3b8        (:14433)
  ctx.lr=0x006cab10u;f_006c8794(ctx);  // this+264 slot-3 init, called directly (:14434)
  w32((u32(R(29))+292u),u32(R(0)));    // this+292 = 1
  wf32((u32(R(29))+296u),F(0));        // this+296 = 0.0f
}
```

The only call site supplies `r5 = f_006c90a4(0)` (a **runtime lookup**, see Q1d), so a static
primary vtable for this class cannot be read out of the image. What *is* static is the **embedded
sub-object at `this+264`**, whose vtable is `0x1012f3b8`:

| slot | code |
|---|---|
| 0 | `0x006c87b8` (`ppu_gen_072.cpp:11996`) |
| 1 | `0x006c88f8` (`ppu_gen_072.cpp:12086`) |
| 2 | `0x006c8c04` (`ppu_gen_072.cpp:12291`) |
| 3 | `0x006c8794` (`ppu_gen_072.cpp:11985`) |

### Q1d. Class 716 bytes - `f_0069e908` (`ppu_gen_070.cpp:17164`)

Vtable cell: **`0x00ed879c`** = `toc(0x00ed7990) + 3596` -> value **`0x1012ea18`**, loaded at
`ppu_gen_070.cpp:17178` and stored at `17182`. This is an **Itanium-style primary + 3 secondary
vptrs** (offset-to-top words visible at `0x1012ea78 = -4`, `0x1012eac0 = -8`, `0x1012ead4 = -12`):

| field | value | how |
|---|---|---|
| `this+0`  | `0x1012ea18` | primary vtable (cell `0x00ed879c`) |
| `this+4`  | `0x1012ea80` | `0x1012ea18 + 104` (`ppu_gen_070.cpp:17181`, stored `17188`) |
| `this+8`  | `0x1012eac8` | `0x1012ea18 + 176` (`17186`, stored `17190`) |
| `this+12` | `0x1012eadc` | `0x1012ea18 + 196` (`17181`, stored `17185`) |

```c
// ppu_gen_070.cpp:17164  (vptr setup)
void f_0069e908(Ctx& ctx){
  R(29)=R(3);                            // this
  ctx.lr=0x0069e93cu;f_0069e540(ctx);     // base ctor
  R(9)=r32((u32(R(2))+3596u));           // 0x1012ea18   (:17178)
  R(10)=R(9)+196;  R(0)=R(9)+104;  R(11)=R(9)+176;      // (:17181-17187)
  w32(u32(R(29)),      u32(R(9)));       // this+0  = 0x1012ea18  (:17182)
  w32((u32(R(29))+12u),u32(R(10)));      // this+12 = 0x1012eadc  (:17185)
  w32((u32(R(29))+4u), u32(R(0)));       // this+4  = 0x1012ea80  (:17188)
  w32((u32(R(29))+8u), u32(R(11)));      // this+8  = 0x1012eac8  (:17190)
  w32((u32(R(29))+708u),u32(R(25)));     // this+708 = arg r4 ( = scene+436 ) (:17210)
}
```

**Primary vtable `0x1012ea18` - 24 slots** (slot -> code; OPD -> TOC is `0x00ed7990` for all):

| slot | code | | slot | code |
|---|---|---|---|---|
| 0 | `0x0069ea04` | | 12 | `0x00686110` |
| 1 | `0x0069e2b0` | | 13 | `0x0068611c` |
| 2 | `0x004dd108` | | 14 | `0x0068b144` |
| 3 | `0x004dd4d0` | | 15 | `0x0068b128` |
| 4 | `0x004dd100` | | 16 | `0x0068b10c` |
| 5 | `0x0069e228` | | 17 | `0x00686128` |
| 6 | `0x0069dc64` | | 18 | `0x00686134` |
| 7 | `0x00686594` | | 19 | `0x00686140` |
| 8 | `0x006893e0` | | 20 | `0x0068614c` |
| 9 | `0x0069d198` | | 21 | `0x0068b100` |
| 10 | `0x006872e8` | | 22 | `0x0068b0ec` |
| 11 | `0x00686104` | | **23** | **`0x0069e0a8`** |

Secondary vtables (not enumerated slot-by-slot here): `0x1012ea80` (16 slots, `0x1012ea80`-`0x1012eabc`),
`0x1012eac8` (3 slots, `0x1012eac8`-`0x1012ead0`), `0x1012eadc` (2 slots, `0x1012eadc`-`0x1012eae0`).

---

## Q2. What the 716-byte class is, and what slot 23 does

### Q2a. Identity: it is the versus per-player **frontend / HUD**, not a camera or a 3D view

The class's own method (`0x0069e0a8`, slot 23) allocates a child object with the memory tracker tag
**`n:/Ac4/data/source/Sw/Source/Menu/Frontend/VsPlayerFrontend.cpp`** and loads the UI resource
**`VsFE_full`**:

| cell | value | meaning |
|---|---|---|
| `0x00ed8790` = `toc+3584` | `0x1003c558` | `"n:/Ac4/data/source/Sw/Source/Menu/Frontend/VsPlayerFrontend.cpp"` |
| `0x00ed8794` = `toc+3588` | `0x1003c490` | `"VsFE_full"` |
| `0x00ed8798` = `toc+3592` | `0x1012eae8` | versus/simu message-name table (see Q2c) |
| `0x00ed879c` = `toc+3596` | `0x1012ea18` | the primary vtable |

```c
// ppu_gen_070.cpp:16676  (the 656-byte child, tagged with the VsPlayerFrontend.cpp path)
R(4)=r32((u32(R(2))+3584u));   // "n:/.../Menu/Frontend/VsPlayerFrontend.cpp"
R(5)=u64(s64(91));             // source line 91
R(3)=u64(s64(656));            // size
ctx.lr=0x0069e1a8u;f_005dc574(ctx);   // operator new(656, file, 91)
...
R(5)=r32((u32(R(2))+3588u));   // "VsFE_full"                       (:16692)
R(6)=R(31);                    // this                              (:16693)
R(3)=R(30);                    // r32(scene+40)                     (:16695)
ctx.lr=0x0069e1d8u;f_005e6b4c(ctx);   // load/attach the VsFE_full HUD    (:16696)
```

So: a 716-byte per-player object owning a 656-byte GUI instance built from `VsFE_full`. The nearest
sibling TU is `.../Menu/Frontend/VersusFrontend.cpp` (`0x1003c450`). No camera, projection or
render-target API is reached from this class's constructor or slot 23. **It is the HUD.**

### Q2b. Slot 23 = `f_0069e0a8` (`ppu_gen_070.cpp:16621`) - the per-player HUD setup

Call shape (from Q1d's caller, `ppu_gen_086.cpp:8376`-`8415`):

```
o716->slot23(v, split, r32(scene+36), r32(scene+40), body, r32(scene+44), r32(scene+48),
             /*stack*/ 1, -1, 0, 0, 0, 0, 1)
```

Argument bindings inside the slot (`ppu_gen_070.cpp`): `this=R(31)` (:16625), `v=R(27)` (:16631),
`split=R(26)` (:16627), `scene36=R(4)` (:16633), `scene40=R(30)` (:16637), `body` -> `r5`
(:16629), `scene44=R(6)` (:16635), `scene48=R(7)` (:16639).

Fields it writes:

| store | file:line | value |
|---|---|---|
| `this+712 = v` | `16680` | the scene's slot-24 result (the player index, see Q3b) |
| `this+704 = r32(scene+40)` | `16683` | this frame's `scene+40` |
| `this+628 = 0` | `16679` | |
| `this+632 = new 656-byte child` | `16694` | replaces (and slot-1-releases) the previous child |
| `this+636 = 0x1012eae8 + split*32` | `16697`,`16703` | message-name block for this player's mode |
| `this+640` region | `16674`,`16704` | `f_00679f8c(this+640)`, then `f_0067a0cc(this+640, scene40, 0)` |

The **`r6,r7,r9,r10` "viewport-like" arguments are pointers, and `body` is forwarded, not stored**:

```c
// ppu_gen_070.cpp:16657  - the only place r6/r7/r9/r10/body are consumed
//   r3 = this
//   r4 = r32(scene+36)      (was arg r6)   (:16633)
//   r5 = body               (was arg r8)   (:16629)
//   r6 = r32(scene+44)      (was arg r9)   (:16635)
//   r7 = r32(scene+48)      (was arg r10)  (:16639)
//   r8 = 1, r9 = -1, r10 = 0               (:16643,16638,16641)
//   stack +112..+136 = 0,0,0,1             (:16652,16654,16655,16656)
ctx.lr=0x0069e138u;f_0068a1a0(ctx);
```

`f_0068a1a0` is the sub-function that consumes the AC `body` and the three scene pointers; it is
**NOT DETERMINED** beyond that (it itself constructs a 112-byte FREG block at
`ppu_gen_069.cpp:21236`). The `split` flag is **not** passed to it; it only indexes the
message-name table.

### Q2c. What `split` (`mgr+432 != 0`) actually selects

`split` is normalised to 0/1 at `ppu_gen_086.cpp:8388`-`8406` (`(x ^ (x>>31)) - (x>>31)`, then
`rotl32(...,1)&1`, i.e. `x != 0`), then:

```c
// ppu_gen_070.cpp:16697
R(0)=rotl32d(R(26),5)&0xffffffe0ull;   // split * 32
R(9)=r32((u32(R(2))+3592u));           // 0x1012eae8
R(0)=R(0)+R(9);
w32((u32(R(31))+636u),u32(R(0)));      // this+636 = &table[split]  (8 string ptrs = 32 bytes)
```

`0x1012eae8` is two 8-entry blocks of result-message names:

```
0x1012eae8: Versus_Start, Result_WIN, Result_LOSE, Result_DRAW,
            Result_TeamWIN, Result_TeamLOSE, Result_TeamDRAW, Result_DESTROYED
0x1012eb08: Simu_Start, Simu_WIN, Simu_LOSE, Simu_LOSE,
            Simu_WIN, Simu_LOSE, Simu_LOSE, Simu_LOSE
```

So `mgr+432 != 0` selects the **simulation/CPU-match** message set, not the screen split.

---

## Q3. `mgr` = the VsSortieScene itself

### Q3a. Identification

`mgr` is **not a separate manager object**. `VsSortieScene::Init` (`f_00812238`) keeps `this` in
`R(26)` for the whole function (`ppu_gen_086.cpp:11671` `R(26)=R(3)`) and passes it as `r4` at the
per-player call (`:12326` `R(4)=R(26)`). Confirmation is arithmetic, not inference: the runtime
trace in [18_PLAYER_CREATE_TRACE.md](./18_PLAYER_CREATE_TRACE.md) has `r4 = 0x107d4f00` and
`r8 = 0x107d535c`, and `0x107d4f00 + 436 + 680 = 0x107d535c`, which is exactly
`R(8) = i*7848 + R(20) + 680` with `R(20) = this+436` (`:11905`, `:11908`, `:12312`, `:12316`,
`:12320`).

| item | value |
|---|---|
| class | `VsSortieScene` (vtable `0x1013c010`, 25 slots) |
| constructor | slot 0 = `0x00812128` (`ppu_gen_086.cpp:11595`); writes `0x1013c010` at `this+0` from cell `toc+11428` (`:11601`, `:11604`) |
| size | `>= 0x10000` - it addresses `this+0x10000-1528` and `this+0x10000-1712` (`:11672`, `:11704`) and `this+0x10000-2160/-2164` (`:11915`, `:11919`). Alloc site **NOT DETERMINED** |
| slot 22 | `0x0080f09c` - the per-player helper (has **0** direct callers; only reachable through this vtable) |
| **slot 24** | **`0x0080c608`** |

### Q3b. Slot 24 is the identity function

```c
// ppu_gen_086.cpp:5396
void f_0080c608(Ctx& __restrict ctx){
  R(3)=R(4);     // return the argument
  return;
}
```

Called at `ppu_gen_086.cpp:8377`-`8386` as `mgr->slot24(i)` (`R(9)=r32(r32(mgr)+96)`, r3=mgr,
r4=i). It returns **`i`** unchanged. Its purpose is therefore a **virtual indirection point**
("map local player -> view index") that the versus scene overrides as identity; a subclass can
re-map it. So in the versus path the slot-23 argument `v` equals the player index.

### Q3c. Where `+36`, `+40`, `+44`, `+48`, `+432` come from

These are **pointers / object handles, not numbers**, and only two of them are written by
`VsSortieScene::Init`:

| field | written | read/used | value |
|---|---|---|---|
| `+28` | `:11747` | `:13071`, `:14608` | a **4528-byte** object (`alloc` at `:11734`-`:11737`, source line 335) |
| `+32` | not in Init | `:11741`, `:14630` | pre-existing |
| `+36` | **never in Init or the ctor** | `:11855` -> `f_0073b738`, `:8389`(as r6) | **NOT DETERMINED**; at runtime it is a guest **stack** address (`0xd0100634`) i.e. a pointer to caller scratch |
| `+40` | **never in Init or the ctor** | `:11873` -> `f_0073b1f8`, `:8389`(as r7) | **NOT DETERMINED**; pointer |
| `+44` | `:11790` `w32(this+44,R(31))` | `:8384`(as r9) | the **400-byte** object allocated at `:11773`-`:11776`, which itself owns a **112-byte** FREG sub-object constructed at `obj+288` (`:11780`-`:11782`) |
| `+48` | `:11856` `w32(this+48,R(31))` | `:8385`(as r10) | the **3008-byte** object allocated at `:11794`-`:11797`, initialised by `f_0073b738(obj, r32(this+36), this+436)` (`:11858`) |
| `+184` | `:11875` | `:13137`, `:13163`, `:14635` | a **16-byte** object (`:11860`-`:11866`) |
| `+432` | **never in Init or the ctor** | `:8388` `R(0)=r32(mgr+432)` | **NOT DETERMINED**; used only as a boolean, selects the versus vs simu message set (Q2c) |
| `+436` | ctor `:11613`,`:11626` | `:11857`,`:11868`,`:11905`,`:12316`,`:13201` | base of the **per-player blocks, stride 7848**; the 716 ctor receives `&this[436+i*7848]` |
| `+52` / `+180` | `:12350`-`:12355` / `:12346` | see Q5 | 16-byte record array + its count |

### Q3d. "640x720 halves"? - No. The split selector is `+20`, copied to `+24`

There are no pixel rectangles in any of the fields above. The value that actually selects the
split-view code path is **`scene+20`**, mirrored into **`scene+24`** every frame by slot 7
(`ppu_gen_086.cpp:14630`-`14633`: `R(0)=r32(this+20); w32(this+24, R(4))`).

| writer | file:line | value |
|---|---|---|
| base `Ac4SortieScene::Init` `f_00740838` | `ppu_gen_077.cpp:18307` | **0** (single view) |
| `VsSortieScene::Init` `f_00812238` | `ppu_gen_086.cpp:12204` | **9** |

The versus value 9 is set only in branch `L_00812a30`, reached when
`r32(this+0x10000-2160) != 0` (`:11915`-`:11917`). Consumers treat `{8,9}` as the split modes:

```c
// ppu_gen_086.cpp:13091-13105  (slot 6)  and :14643-14648 (slot 7) - same test
R(9)=r32(this+24);  R(9)=R(9)-8;
crset<u32>(ctx,7,u32(R(9)),0x1u);      // unsigned: R(9) > 1 ?
if(!ctx.cr[29]) ...                    //   R(9) in {0,1}  i.e. this+24 in {8,9}  -> split branch
                                       //   otherwise                              -> the other branch
```

So `8` vs `9` distinguishes two layout modes, and `0` means "one view". Which of 8/9 is
horizontal vs vertical, and where the actual half-screen rectangles are stored, is **NOT
DETERMINED** (the rectangles live behind the `VsFE_full` resource / the 656-byte child, which is
data, not code).

---

## Q4. THE KEY QUESTION - every construction site of the three classes

### Q4a. Call sites, resolved through the thunks

| class | real ctor | direct callers of real ctor | callers of its thunk | total |
|---|---|---|---|---|
| **716** (`VsPlayerFrontend`) | `f_0069e908` | **0** | `f_0080f09c` @ `0x0080f1f8` (`ppu_gen_086.cpp:8372`) only | **1** |
| **300** | `f_006ca970` | **0** | `f_0080f09c` @ `0x0080f1bc` (`ppu_gen_086.cpp:8357`) only | **1** |
| **112** (FREG block) | `f_006c8530` | **20** (see below) | - | 21 |

The 112-byte class is a shared utility, not a view: its 20 direct callers are
`f_00662e98 :3979`, `f_00665380 :6419`, `f_006655bc :6566`, `f_006662f4 :7534`, `f_00667ed8 :9362`,
`f_00669e68 :11609`, `f_0066bf70 :13753`+`:13755`, `f_0067c094 :5771`, `f_0067c228 :5873`,
`f_0068449c :14681`, `f_0068a1a0 :21236`, `f_00690e08 :2613`, `f_00699834 :11728`,
`f_0069a630 :12715`, `f_0069a760 :12782`, `f_006bb594 :23685`, `f_006bb5dc :23704`,
`f_006bd27c :25682`, `f_006bd394 :25753` (all `ppu_gen_068`-`071`), plus `f_0080f09c :8340` and
`f_00812238 :11782` from the versus scene. **None of them is on the campaign-mission path.**

`f_0080f09c` has **zero direct callers**; it is reached only through slot 22 of two vtables, both
versus-family:

| vtable | what it is |
|---|---|
| `0x1013c010` | `VsSortieScene` (slot 5 `Init` = `f_00812238`) |
| `0x10131e08` | a **VsSortieScene-derived** scene: its slot-5 `Init` `0x00753e00` calls `f_00812238` **directly** at `ppu_gen_078.cpp:9630`, then adds the same 320-byte debug object (`:9633`-`:9636`) |

So the 716/300 classes are built **only** by versus-family sortie scenes. The construction loop in
`VsSortieScene::Init` is `L_00812af4` (`ppu_gen_086.cpp:12254`) -> `L_00812b30` (`:12271`) with the
index in `R(23)` (`:12263` `R(23)=0`, `:12277`-`:12279` `R(23)=R(23)+1`), and the call at
**`0x00812c20`** (`:12332`):

```
o716-slot23 args: r4 = mgr->slot24(i)          (= i)
                  r5 = (mgr+432 != 0)          (mode flag)
                  r6 = r32(mgr+36)   r7 = r32(mgr+40)
                  r8 = body          r9 = r32(mgr+44)  r10 = r32(mgr+48)
                  stack = 1, -1, 0, 0, 0, 0, 1
```

### Q4b. The campaign mission constructs none of them

`Ac4SortieScene::Init` = `f_00740838` (`ppu_gen_077.cpp:18279`) writes **only four** scene fields:

| file:line | store |
|---|---|
| `18307` | `this+20 = 0`  (**single view**; the field Q3d showed selects the layout) |
| `18317` | `this+7984 = 0` |
| `18333` | `this+8280 = ` a **240-byte** object (allocated `:18320`-`:18322`, ctor `f_00737b68` -> `f_00b0c6cc` at `ppu_gen_118.cpp:16504`) |
| `18469` | `this+8292 = r32(...)` |

It creates **no** `+52`/`+180` record array, **no** 7848-byte per-player blocks, **no** `+436`,
and never calls `f_0080f09c`, `f_0069e908` or `f_006ca970`.

`f_007ce61c` (`ppu_gen_083.cpp:14655`) then wraps it and adds a **different class**:

```c
// ppu_gen_083.cpp:14662-14684
ctx.lr=0x007ce638u;f_00740838(ctx);           // Ac4SortieScene::Init
R(3)=u64(s64(5)); ctx.lr=...;f_008fdc24(ctx); // (:14664)
R(4)=u64(s64(320));                           // size 320                        (:14665)
R(5)=r32((u32(R(2))+-5092u));                 // cell 0x00ee9628                 (:14666)
   // = 0x10047510 "n:/Ac4/data/source/Sw/Source/Scene/DebugSortieScene.inl"
R(6)=u64(s64(34));  f_008fe1a0(ctx);          // operator new(320, .inl, 34)     (:14667)
if(ctx.cr[2])goto ...;                        // null check                      (:14672)
ctx.lr=0x007ce670u;f_007836a0(ctx);           // ctor f_007836a0(o320, scene)    (:14673)
w32((u32(R(30))+8304u),u32(R(31)));           // scene+8304 = o320               (:14675)
R(9)=r32(u32(R(31))); R(11)=r32((u32(R(9))+20u));   // vptr slot 5              (:14677-14678)
icall(ctx,u32(ctx.ctr));                      // o320->slot5()                   (:14683)
```

| item | value |
|---|---|
| class | the DebugSortieScene helper allocated from `DebugSortieScene.inl`; ctor `f_007836a0` (`ppu_gen_080.cpp:9803`) with `r4 = 12345`, `r5 = scene+8000` |
| vtable | cell `0x00ee64a8` = `toc(0x00eeaa0c) - 17764` -> **`0x10134138`** (stored at `ppu_gen_080.cpp:9817`), 21 slots |
| field | `obj+304 = scene` (`ppu_gen_080.cpp:9815`) |
| slot 5 | **`0x0077f000`** (`ppu_gen_080.cpp:5327`) - creates the HUD widget set (several 28-byte objects, `f_007ea8c4` each), reading `scene+8000`, `scene+8280` |

**Answer to Q4:** the campaign mission does **not** construct the 716-byte class (nor the 300-byte
one) for its single player. It uses a **different class** - the 320-byte object with vtable
`0x10134138`, constructor `f_007836a0`, stored at `scene+8304`, whose slot 5 is `0x0077f000` - and
the 240-byte object with ctor `f_00b0c6cc` at `scene+8280`. There is therefore **no
viewport/split argument to copy**: the mission path has no split-screen machinery at all, and its
`scene+20` stays `0`.

---

## Q5. Per-frame driving: who iterates the views

### Q5a. The per-player record array

The `out` struct of `f_0080f09c` is 16 bytes: `+0 body`, `+4 o112`, `+8 o300`, `+12 o716`
(`ppu_gen_086.cpp:8309`, `:8344`, `:8363`, `:8375`; `o300+8 = body` at `:8365`). The scene stores
them in an inline array at **`this+52`** with the count at **`this+180`**:

```c
// ppu_gen_086.cpp:12342-12355   (append one 16-byte record)
R(9)=r32((u32(R(28))+128u));          // count  (R(28) = this+52, set at :12266)
R(10)=R(9)+1;  R(9)=R(9)<<4;          // count+1 ; count*16
w32((u32(R(28))+128u),u32(R(10)));    // store count  -> this+180
w32(u32(R(28)+R(9)),  u32(R(7)));     // record+0  = body
w32((u32(R(11))+4u),  u32(R(6)));     // record+4  = o112
w32((u32(R(11))+8u),  u32(R(5)));     // record+8  = o300
w32((u32(R(11))+12u), u32(R(8)));     // record+12 = o716
```

Independent confirmation that `this+52`/`count==this+180` is the layout: another constructor clears
it with `w32((u32(R(25))+128u),0)` where `R(25)=R(3)+52` (`ppu_gen_086.cpp:8106`, `:8120`).

### Q5b. VsSortieScene per-frame slots that walk it

Vtable `0x1013c010`: slot 6 = `0x00813698`, slot 7 = `0x00814ec0`, slot 9 = `0x0080ed78`,
slot 12 = `0x0080c780`, slot 13 = `0x0080c044`, slot 22 = `0x0080f09c`, slot 23 = `0x0080c600`,
slot 24 = `0x0080c608`.

**Slot 6 = `f_00813698` - the update loop over the records:**

```c
// ppu_gen_086.cpp:13077-13117  (loop addresses; the record stride is 16)
R(0)=R(26)+u64(s64(52));  R(30)=R(0);              // :13077  base = this+52
R(11)=r32((u32(R(30))+128u));                      // :13079  count = this+180
if(!ctx.cr[29])goto L_0081378c;                    // :13081  count > 0 ?
R(31)=0;                                           // :13083  i = 0
L_00813724:                                        // :13091
  R(9)=rotl32d(R(31),4)&0xfffffff0ull;             // i*16
  R(9)=R(30)+R(9);                                 // &records[i]
  R(29)=r32(u32(R(9)));                            // record->body          (:13095)
  if(ctx.cr[30])goto L_00813714;                   // body == 0 -> next
  R(9)=r32(u32(R(29)));                            // body->vptr
  R(11)=r32((u32(R(9))+112u));                     // *** slot 28 (112/4) *** (:13100)
  icall(ctx,u32(ctx.ctr));                         // body->slot28(0)        (:13105)
L_00813714: R(31)=i+1; loop while i < count;       // :13087-13117
```

Also inside the same slot: `record+4` (the 112-byte FREG block) is used as
`f_00737e68(cell, o112+16)` (`:13182`-`:13187`), i.e. its matrix at `+16`.

**Slot 7 = `f_00814ec0`** - camera + split gate: `r32(this+28)+16+28/+32` are two floats handed to
`f_00923758` (`:14608`-`:14617`); then `this+24` is refreshed from `this+20` (`:14630`-`:14633`);
then `if (!(count > 1)) return` (`:14629`-`:14634`); then dispatch on `this+24`:
`f_00814cd4` vs `f_00813ef4` (`:14643`-`:14665`).

**Slot 9 = `f_0080ed78`** - HUD dispatch, same gating:

```c
// ppu_gen_086.cpp:8087-8096
R(0)=r32((u32(R(3))+180u));                 // count
crset<s32>(ctx,7,s32(R(0)),1);
if(!ctx.cr[29])return;                      // only when count > 1  (i.e. split screen)
R(9)=r32((u32(R(3))+24u)) ; R(9)=R(9)-8;
crset<u32>(ctx,7,u32(R(9)),0x1u);
if(!ctx.cr[29])goto L_0080ed98;             // this+24 in {8,9}
{f_0080ead8(ctx);return;}
L_0080ed98: {f_0080e29c(ctx);return;}
```

Per-view virtual slot invoked by the scene: **slot 28 of the AC body** (`body+112`) for the update,
plus the per-record slot-1 release through `o716`'s child and the `body`-slot calls inside
`f_0080e29c`/`f_0080ead8`. Note the record's `o716` is *not* itself updated per frame; it is
configured once by slot 23 and then only re-pointed through `this+632`/`this+636`.

### Q5c. The mission scene's single view

The mission's vtable (`0x10138b18`) slots 6/7 are the base ones, `0x0074e91c` and `0x0074e358`.
There is **no** loop and no record array - the single view is drawn unconditionally:

```c
// ppu_gen_078.cpp:3589-3612   f_0074e358  (mission slot 7)
R(9)=r32((u32(R(3))+20u));            // scene+20
crset<s64>(ctx,7,s64(R(9)),s64(0));
if(ctx.cr[30])goto L_0074e390;        // scene+20 == 0  ->  single-view path
R(0)=r32((u32(R(3))+32u));
crset<s32>(ctx,7,s32(R(0)),0);
if(ctx.cr[30])goto L_0074e3f4;        // (else a virtual chain on that object)
L_0074e390:
  R(4)=u64(s64(9));      f_008ee624(ctx);    // begin view 9
  ... f_0074d3e4(...);   f_008ee160(ctx);    // draw; end view
```

`scene+20` is 0 in the mission because `Ac4SortieScene::Init` writes 0 there
(`ppu_gen_077.cpp:18307`), so the mission always takes the single-view arm.

### Q5d. Does a view register itself in a global list? - **No; it is driven explicitly**

Verified by reading every store in the two relevant bodies:

* `f_0069e908` (716 ctor, `ppu_gen_070.cpp:17164`-`17225`): every `w32`/`wf32` target is
  `this`-relative - `this+0`, `+4`, `+8`, `+12`, `this+532+96`, `this+632/636/704/708/712`. There is
  no TOC-relative store and no store through an unrelated pointer.
* `f_0069e0a8` (slot 23, `ppu_gen_070.cpp:16621`-`16715`): same - `this+628/632/636/704/712`, plus
  calls.

So the object cannot self-register at construction, and nothing walks a global view list: it is
reachable **only** via `records[i].o716` (`scene+52+16i+12`) and is driven by the scene's slot 6/7/9
loops shown above. A second instance created elsewhere would therefore **not** render by itself; it
must be appended to the `this+52` array (count `this+180`) to be drawn.

Registration inside the downstream callees (`f_0069e540`, `f_00679f8c`, `f_0067a0cc`, `f_005e6b4c`)
was not analysed - see NOT DETERMINED.

---

## NOT DETERMINED

1. **The primary vtable of the 300-byte class.** Its constructor stores argument `r5` at `this+0`
   (`ppu_gen_072.cpp:14363`) and the single call site passes the return of `f_006c90a4(0)`
   (`ppu_gen_086.cpp:8345`-`8355`), which is a runtime lookup that can return 0
   (`f_006c90a4`, `ppu_gen_072.cpp:12631`-12670). No static vtable can be attributed to `this+0`.
   The embedded sub-object vtable `0x1012f3b8` at `this+264` is the only static one.
2. **`scene+36` and `scene+40`** are read but never written by `VsSortieScene::Init` or its
   constructor; the writer was not located. At runtime `scene+36` held a guest **stack** pointer.
3. **`scene+432`** (the versus/simu selector) is likewise only read in the code analysed; the
   writer was not located.
4. **The actual half-screen rectangles** (e.g. 640x720) are not computed at any field of the scene
   or of the 716-byte object; they are inside the `VsFE_full` UI resource / the 656-byte child.
   Their values are data and were not extracted.
5. **The allocation size of `VsSortieScene`** and its exact construction site: not found.
6. **Which of `scene+24 == 8` vs `9`** is horizontal vs vertical split: not established.
7. **The full semantics of `f_0068a1a0`** (the function that receives the AC `body` and the three
   scene pointers from 716 slot 23): only its call shape was read.
8. **Whether any downstream callee registers the HUD/view in a global**: the bodies of
   `f_0069e540`, `f_00679f8c`, `f_0067a0cc` and `f_005e6b4c` were not analysed.
9. **The class identity of the 320-byte mission object**: only its allocation source
   (`DebugSortieScene.inl`) and vtable (`0x10134138`) are established; no class-name string exists
   for it.
