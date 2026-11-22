# 13 — Swapping mission files to run m000 in the m160 slot: tried, crashes

Idea: the hub can only start `FIRST STRIKE` (mission **m160**). Serve **m000**'s files under m160's
paths with `--test-overlay`, so the m160 slot runs m000's content — which has Joshua.

## What the disc contains (per mission)

| file group | m160 | m000 |
|---|---|---|
| `bind/world/<id>_event.000` | 7191 | 11630 |
| `bind/world/<id>_mission.000` | 4366 | 9637 |
| `bind/world/<id>_world.00N` | **12 parts** (`.000`–`.011`) | **8 parts** (`.000`–`.007`) |
| `cap/mission/<id>.cap` | 280 | 2040 |
| `cap/briefing/<id>.cap` | 544 | 528 |
| `briefing/<id>_b0.bin`, `_d0.bin` | — | — |
| `common/event/mission/<id>.lua` | 7956 | 18082 |
| `common/event/mission/chara/<id>_*.lua` | player only | player, ac, joshua, dummy, vehicle |

## What was swapped

Built an overlay at `payload\overlay\PS3_GAME\USRDIR\` with m000's bytes under m160's names:

```
bind\world\m160_event.000      <- m000_event.000
bind\world\m160_mission.000    <- m000_mission.000
bind\world\m160_world.000..007 <- m000_world.000..007
cap\mission\m160.cap           <- m000.cap
cap\briefing\m160.cap          <- m000.cap
common\event\mission\m160.lua  <- m000.lua
```

(plus the existing `msgid.lua` and `chara\m000_joshua.lua` edits).

## Result: the overlay worked, the game crashed

The VFS served the swapped files exactly as intended:

```
[134.8951 W vfs] TEST OVERLAY: /dev_bdvd/.../bind/world/m160_world.000 read from ...overlay...
[134.9148 W vfs] TEST OVERLAY: /dev_bdvd/.../bind/world/m160_mission.000 read from ...overlay...
```

and then, milliseconds later:

```
[134.9177 F] host exception 0xc0000005 at rip ... (ac4.exe+0x1e9773c) in thread 'main_thread'
```

`Ac4SortieScene` had started (96.7 s) and the swap was consumed at 134.9 s; the process died at
134.9177. So the mechanism is sound but the payload is not interchangeable.

## Why it cannot work as-is

A mission is not just scripts — it is a **bundle bound to its map**:

- the world/mission/event `.000` files are map data, and m160 has **12** world parts while m000 has
  **8**, so the loader reads a part set that does not match the header it is following;
- `cap/mission/<id>.cap` is entity placement for that specific map;
- the Lua references entity ids and trigger points defined by that placement (`include/<id>/entityid.lua`,
  `pointid.lua`).

Swapping everything makes the mission id claim one map while carrying another's data, which is what the
loader trips over. Swapping **only the Lua** would be safe but pointless: m000's event logic looks for
Joshua and the other `m000_ET_*` entities in the m160 map, where they do not exist, and nothing would
create a second AC.

## Second attempt: an internally consistent bundle

The bundles turned out to be **BND3 archives with plaintext entry names** and a map tag:

```
m160_world.000: 42 4e 44 33 | 30 37 42 31 32 4d 33 31 | ... | 6d 31 36 30 5f 77 6f 72
                "BND3"        "07B12M31"                    "m160_wor..."
m000_world.000: 42 4e 44 33 | 30 37 42 31 32 4d 32 35 | ... | 6d 30 30 30 5f 77 6f 72
                "BND3"        "07B12M25"                    "m000_wor..."
```

So m000's map is **M25** and m160's is **M31**, and the entry names embed the mission id. Both are
fixed-length, so the copies can be retargeted in place without moving anything:

- rewrote every `m000` → `m160` inside the 8 world bundles (10 occurrences, all in the name table);
- rewrote the map tag `M25` → `M31` in all 9 world/mission bundles.

Verified afterwards: `head now: BND307B12M31` with entry `m160_wor...`.

**Result: the identical crash.**

```
[134.8749 F] host exception 0xc0000005 at rip ... (ac4.exe+0x1e9773c)
```

Same instruction pointer, same point — immediately after `m160_mission.000` is read — as the first
attempt. So neither the entry names nor the map tag are what the loader objects to.

## What that means

The loader is not validating names. Something else binds mission id → map/asset set, and it is not in
the mission's own bundle: most likely a global mission table (outside `bind/world`, e.g. in the boot
archives) that states which map and how many world parts a mission uses. Serving m000's map data under
m160's id leaves that table pointing at a different map, and the parse fails deterministically.

Producing a genuinely consistent converted bundle therefore requires:

1. understanding/repacking the **BND3** archives, and
2. retargeting the **mission→map** table(s) so the slot's record names the map and part count of the
   data actually supplied.

That is mission-conversion work — the same work the co-op mod's tier-A conversions need — not a test
shortcut. For the E3 experiment it is far cheaper to reach a mission with a second AC by playing
(the tutorial to `m717`), and to do the bundle conversion as a separate, deliberate job.
