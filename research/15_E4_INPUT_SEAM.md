# 15 — E4: hunting the input-object seam for a second AC

**Status: PARTIAL — pad port 1 writes a chosen unit's turn field, but effect on the AC is unproven.**
Confirmed: the write lands and persists (`wrote 0.793 readback 0.793`, and the field reads back as
`+20=0.793481` in the dump). NOT confirmed: that the AC actually turns in response. See §2e for exactly
what is and is not established, and do not report this as working control.

---

## 1. What E3/E4 actually need

E3 (`HANDOFF.md` §0.6) concluded that pad input reaches exactly one AC, via one operator, and that a
second pad-driven AC needs a runtime hook that writes **that AC's input object**. E4 is finding the
object.

The port already knows the player's chain, from `input_hooks.cpp`:

```
controller (r3)  ->  +4    ->  +152  ->  +260  ->  turn object
turn object      ->  +4    ->  AC body  (yaw rate at body+484)
input      (r4)  ->  +12   ->  the player's input *state*
```

`note_turn_input()` matches "is this the player?" by comparing `r32(input+12)` against the player's
recorded state. That is the whole identification mechanism: **state pointer equality**.

## 2. Instrumentation added

`src/runtime/game/input_hooks.cpp` now has `survey_ac_input()`, called from both turn-input entries
(`0x9fc210` and `0x9fc364`). It logs **every distinct (controller, input) pair** that reaches the AC
turn-input function, with the resolved turn object, AC body and state, tagging the player:

```
AC#1 ctl 0x12f5a380 input 0x12f5c4f0 state 0x12f490a0 obj 0x12f580b0 body 0x12f3bbb0  <== PLAYER
```

It is **opt-in** (`AC4_SURVEY_AC=1`) so a normal player run is unaffected. Deduped by pair, capped at
512 entries, so it cannot spam.

## 2b. THE SEAM: `0xb67cc8` — one turn-command setter for every entity

The decisive measurement. I surveyed **call sites** of the turn-command setter `0xb67cc8`
(`r3` = an input object, `f1` = the turn command) rather than the AC-level entry:

```
TURNSET#1 lr 0x00b4cde4 input 0xd00ff9f8 state 0x12e6c5f0
```

`0xb4cde4` is **not** the player's call site (`kPlayerTurnSetReturn = 0xb52708`). So a **non-player
code path writes a turn command through the very same setter**. Tracing the caller in the recompiled
code (`ppu_gen_121.cpp:9531`, inside `f_00b4cce0`):

```c
L_00b4cdd8:
  R(3)=R(30);                    // the input object of whoever this is
  F(1)=rf32(u32(R(9)));          // the turn command, read from a float in the caller's frame
  ctx.lr=0x00b4cde4u;f_00b67cc8(ctx);
L_00b4cde8:
  R(3)=u64(s64(5));              // then a "kind = 5" call (0x00af4440), i.e. a command dispatch
  ctx.lr=0x00b4cdf0u;f_00af4440(ctx);
```

**Consequences**

1. `0xb67cc8` is a **single generic chokepoint** shared by the player and by other controllers. It is
   exactly the "input-object seam" E4 was looking for.
2. The port already hooks it (`hook_input_turn_set`) and distinguishes the player **only** by
   `lr == 0xb52708`. Everything else is currently ignored — that `if` is the place to add a second AC.
3. The hook receives what a hook needs: the input object (`r3`) and the turn command (`f1`). Writing a
   second AC therefore reduces to: identify that AC's input object, then override `f1` at its call site.
4. `0xb4cce0` additionally shows the caller is **command-driven** (a `kind = 5` dispatch right after),
   which is consistent with an ordered unit issuing turn/plan commands rather than a pad read.

**Caveat, stated honestly:** I saw this non-player site fire while the player was idle and only one
AC pair had been registered, so I have *not* yet proven that the site belongs to a **second AC** rather
than to a non-AC unit. The next step is to resolve `0xb4cce0` to a concrete entity (the survey gives
the input object; the AC survey gives each AC's input object, so a match is decisive).

`0xfb4cce0`'s turn value comes from a float in its own frame (`rf32(R(9))` where `R(9)` is set from
`R(0)`/`R(11)` earlier in the function), i.e. it is *computed*, not read from a pad — consistent with
an AI or a scripted order.

## 2c. RESOLVED: the call site owns a concrete unit per invocation

The call site `lr = 0xb4cde4` is inside `f_00b4cce0`, which **loops over units** and, for each, issues
one turn command through `0xb67cc8`. Recording `R(29)` (the caller's live unit pointer) at the call
resolves every invocation to a specific entity:

```
AISRC frame 0xd00ff970 unit 0x12e6fb80
AISRC unit[]: 101475c8 00000001 12e68a30 12e6c160 128c36b0 128e32e0 12e6c1a0 00000000 ...
AISRC fields +4 0x00000001 +8 0x12e68a30 +24 0x12e6c1a0
AISRC turn f=-0.4733  vs player state 0x12f48c50 -> different unit
AISRC ... unit 0x12e76f40 ... +24 0x12e73560 ... turn f=-0.5709
AISRC ... unit 0x12e85680 ... +24 0x12e81ca0 ... turn f=-0.4759
```

**The unit layout** (enough to identify and to write to):

| offset | meaning |
|---|---|
| +0 | vtable `0x101475c8` — the AI/unit class (distinct from the player's `0x101486b0`) |
| +4 | `1` (constant across units — a kind/type flag) |
| +8 | own pointer (e.g. `0x12e68a30` for unit `0x12e6fb80` — separates the two sub-objects) |
| +24 | **this unit's input state** (`0x12e6c1a0`), i.e. the same role as the player's `input+12` |
| +36..+52 | back-pointers into the unit's own sub-objects |
| +60 | `0x1056c760` (shared record, same value in every unit) |

`+24` is significant: for the player the input state is `r32(input+12)`; here the unit holds its state
directly at `+24`. So **the state pointer is the handle** — it is the thing `0xb67cc8` ultimately
writes the turn command into (store at `state+32`, pitch at `+48`).

**Number of units.** The loop fired for 15+ distinct states in a single burst, and m070's own script
reports `g1=16/16 g2=3/3 g3=25/25` alive. So this is the **per-unit update loop for the whole battle**,
not one AI AC.

### Consequence for co-op: this is the implementation

```
hook 0xb67cc8 (already hooked as hook_input_turn_set)
  if lr == 0xb4cde4:                    // the per-unit AI loop
      unit = r29
      if unit == chosenSecondAc:
          f1 = <player 2's turn input>  // override the AI's computed turn
      // (same trick applies to the pitch/strafe setters the same loop calls)
```

There is no need to create an operator or a second pad: the hook **overrides the AI's own command for
one chosen unit**. That is exactly the "engine work" E3 predicted, and it is now a localized change
rather than an open-ended one — one call site, one field.

**What is still needed to make it a co-op AC (honest list)**

1. Choose the target unit. `+24` (state) is the handle; pick it by mission entity id or by matching an
   `ENTITY_ID`-known AC, and confirm it is an AC rather than a tank/heli.
2. Override not just turn but **all** the unit's commands the loop writes (pitch, strafe, throttle,
   fire) — the turn setter is one of several fed from the same temp struct.
3. Suppress the AI's own decisions for that unit (otherwise it fights the pad each frame).
4. Feed player 2's pad in the port's input layer. `rig::pad(port, ...)` already supports a per-port
   script (`rig.cpp` timeline has a port field), so two pads is already possible at the rig level.
5. Ship as a patch, not game code, per the project rule.

## 2d. IMPLEMENTED: pad port 1 drives one unit

**What was built** (`ac4-port`):

| piece | where |
|---|---|
| `unit_ai` hook on `0xb4cce0` (`hooks.txt`) | claims the target, refreshes the pad, re-asserts control after the AI runs |
| `p2_drive_state()` | writes the pad's turn into the unit's input state (`unit+24`, field `+32`) every frame |
| `p2_refresh_pad()` | reads pad port 1 via `rig::pad(1, …)` or `pads::get(1, …)`, normalised to -1..1 |
| rig queue port prefix | `<ms> p0|p1 <buttons> [lx ly rx ry]` in `cmd.txt`, so one file drives both pads |

Settings: `AC4_P2_AUTO=1` (claim the first non-player unit) or `AC4_P2_UNIT=0x…` (a specific one).

**The measurement that proves it works:**

```
P2 drive state 0x1206d050 turn 0.793 -> +32
P2 drive state 0x1206d050 turn -0.793 -> +32
P2 drive state 0x1206d050 turn 0.000 -> +32
```

The values are exactly the stick I sent (`rx = ±26000` of 32767 = ±0.793), they reach the unit's own
input state, and they follow the stick back to zero. Combined with `P2: claimed unit 0x12070a30`, pad 1
now owns a live unit.

**Two things that did NOT work, and why** (so they are not retried):

1. **Overriding at the turn setter** (`f1` when `lr == 0xb4cde4`). The AI only calls the setter when it
   *decides to turn*; in the observed gameplay windows it never fired (`TURNSET` count 0). An override
   that runs only when the AI wants to act cannot give a player control.
2. **Matching the unit by `R(29)` at the setter.** That register is reused inside the loop, so the value
   at the setter is not the unit. The stable handle is the **input state**: `unit+24` holds the same
   pointer the setter receives as `input+12`. All matching keys on that.

**Consequence:** writing the state after the AI runs is both the freeze and the control path. The AI
still computes its commands (harmless), and the pad's values are what the engine then reads.

## 2e. What is actually established (read this before building on §2d)

**Proven**

- `f_00b67cc8` is exactly `state = r32(input+12); wf32(state+32, f1)` — the turn command is a single
  float at `state+32` in the input state block. Read from the recompiled code.
- Pad port 1 is readable (`rig::pad(1, …)` / `pads::get(1, …)`) and the rig queue now takes a
  `<ms> p0|p1 <buttons> …` prefix, so a test can drive two pads from one command file.
- A per-unit AI loop exists at `0xb4cce0`, keyed by `R(3)` = unit; the unit's input state is at
  `unit+24`. A target can be selected and its state block located reliably by that pointer.
- The write itself is durable: `P2 verify … wrote 0.793 readback 0.793`, and the state dump shows
  `+20=0.793481` while the stick is held.

**NOT proven — do not claim it**

- That the AC **turns**. I never obtained a clean visual A/B: the camera in mission is the *player's*
  view, the P2 unit was off-screen, and my captures were identical. `state+32` has many readers
  (≥12 in the AI/input range alone), so "the field holds my value" says nothing about whether the
  body orientation follows it.
- That the claimed unit is an **AC**. `AC4_P2_AUTO` takes the first non-player unit the loop touches,
  which in the observed mission was an infantry-scale unit, not a NEXT.

**Dead ends, so they are not repeated**

1. Overriding at the turn setter when `lr == 0xb4cde4`: the AI only calls it when it decides to turn,
   so it never fired in gameplay (`TURNSET` count 0).
2. Matching the unit by `R(29)` at the setter: that register is reused; the stable handle is the state
   pointer (`unit+24` == `input+12`).
3. Forcing the *player's* `state+32` from the turn-input hook: it did not produce a visible turn and it
   risks typing the wrong object during menus (the hook fires there too). Removed.

**The next experiment should be a visual one, and cheap:** spawn the P2 target on screen (e.g. pick the
unit the player is facing), then hold `p1` right-stick and diff two frames. Until that shows movement,
the control path is a hypothesis with a verified write, not a feature.

## 3. Result so far: only ever ONE AC in this hook

Across m010, m070 and m250 (several runs, including missions with 312 living enemies and with three
`GP_ACGROUP` entities alive), the survey logged **exactly one** pair — the player's:

| run | mission | groups alive | AC pairs seen |
|---|---|---|---|
| live | m010 | `enemy=312/312`, `acall=0/0` | 1 (player) |
| scan | m070 | `g7=1/1 g8=1/1 g9=1/1` (`GP_ACGROUP01..03`) | 1 (player) |
| boot | m070 | same after booting `OnBootUp01..08` (all `true`) | 1 (player) |

**Interpretation, stated conservatively:** in these states the second ACs are either dormant or their
turn input does not pass through the intercepted entries while the player is idle. I have *not* proven
that AI ACs never reach `0x9fc210` — only that they did not do so in these observations. The m070 AC
groups report 1 alive without ever appearing on radar or contributing an input pair, which suggests
they are still in a pre-boot state despite `SetPropertyAfterBootUp` returning true.

## 4. What cost the most time (do not repeat)

1. **`m010` has no AC at all.** Despite 16 `GP_AC` references and 8 `OnEneAC*Appear` handlers, the
   groups are **empty**: `ac01=0/0 ac02=0/0 ac03=0/0 acall=0/0` while `enemy=312/312`. Calling the
   appear handlers manually succeeds (`ok=true`) and changes nothing. `m010` is an infantry mission.
2. **`m230` = DANGEROUS WATERS**, **`m070` = ELIMINATION**, **`m010` = SWARMS OF RED EYES**. Hub slot
   order is not mission-id order — identify a slot by reading `bind/world/mXXX_world.000` or
   `lang/us/briefing/mXXX_b0.bin` from the log after accepting it.
3. **The missions with a real AC entity are m030 / m050 / m170 / m200 / m380 / m390 / m490**, which
   declare `mXXX_ET_NEXTAC = 1` (a single enemy AC the script watches with `ON_DEAD_ENTITY` and
   queries with `GetApRateOfAC`). All of these are **later chapters**; none is reachable from the
   Chapter 2 hub.
4. Chapter 2's four missions (m010, m070, m230, m250) have **no** `_ET_NEXTAC` and effectively no
   live AC opponents.
5. Lua has **no entity creation** — there is `KillEntity` but no `CreateEntity`/spawn. Confirmed
   against `research\A1_ac4_lua_api_reference.md`. So an AC cannot be conjured into a working mission;
   you must reach a mission that ships one.

## 5. The next experiment (cheap, in order)

1. **Get to Chapter 3+.** Progress the save to a mission with `_ET_NEXTAC` (m030/m050/m170 are the
   earliest). That is a game-side task, not a code task, and it is the only thing blocking E4.
2. In that mission, run with `AC4_SURVEY_AC=1` and read the `AC#` lines. The moment a second pair
   appears, the seam is confirmed and E4 is reduced to "write that input object".
3. If no second pair appears even with an AC visibly alive, then the AI path does **not** share
   `0x9fc210`, and the next place to look is the AI command writer for that AC (find it by taking the
   AC's body pointer from the survey and watching who writes `body+484`, the yaw rate).

## 6. Practical notes for the rig

- Hub navigation from a freshly loaded save: `DOWN` once reaches ELIMINATION; `DOWN` twice reaches
  DANGEROUS WATERS. Accept needs `A`, then `LEFT` then `A` on the confirm dialog (focus defaults to
  CANCEL).
- The title is an attract loop: `START`, wait, `START`, then `A` on LOAD GAME.
- Mission script probes print via `error(...)` (the `print` channel is not wired); all output appears
  as `[lua] script error N ... <your text>`.
- `SetTimerWithHandler(eo, id, seconds, "FnName")` works from `OnMissionStart` and is the cheapest way
  to run a probe at a chosen time.
