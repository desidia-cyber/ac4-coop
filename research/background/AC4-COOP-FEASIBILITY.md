# Feasibility: adding a 2-player co-op mod to Armored Core 4 via `SulphurS16/AC4-Port`

Repo analyzed: <https://github.com/SulphurS16/AC4-Port> (v0.6.0, commit `219c7bf`, MIT).
Local checkout: `./ac4-port`.

## 0. What the project actually is

Not an emulator wrapper — a **static recompilation**. `ac4-recomp` decodes the PS3 `EBOOT.BIN`
(~3.6M PPC instructions, ~54k functions, 99.3% coverage), emits C++, and CMake/MinGW builds a
native x64 Windows program. RSX draws through D3D11 with shaders translated to HLSL. SPU programs
are recompiled ahead-of-time with an interpreter fallback. Platform services are hand-written C++
under `src/runtime/hle/`.

Consequences that matter for this question:

- The **game's own netcode is present in the binary** you build. Only the platform services under
  it are replaced.
- No game bytes are in the repo; the recompiled code is generated on each user's PC from their own
  disc. Any mod must ship as **source compiled locally against the user's own copy** — which is
  compatible with the MIT license and the project's stated legal position.

## 1. Anticipate the wrong answer: "just turn on multiplayer"

The port is offline **by explicit design**, not by omission:

| Component | File | State |
|---|---|---|
| `sys_net` (BSD sockets) | `src/runtime/hle/sys_net.cpp:1-3,45-48` | `socket()` returns `ENETDOWN`; `gethostbyname` returns NULL. Only `inet_addr`/`inet_ntoa` work. |
| `sceNp` / PSN | `src/runtime/hle/sceNp.cpp:1-4,61` | Manager status pinned `OFFLINE`; every net op fails with `SCE_NP_ERROR_OFFLINE`. |
| `sceNpMatching2` rooms | `src/runtime/hle/sceNp.cpp:308-347` | Local dummy context IDs only; all room ops stub out. |
| `cellNetCtl` | `src/runtime/hle/cellNetCtl.cpp` | Reports no link. |
| AV chat | `src/runtime/hle/cellSysutilAvcExt.cpp:1` | "Voice chat needs a PSN matching room" — absent. |

So the game boots, sees "no network", and stays offline. There is no switch to flip.

## 2. Scope check: AC4 has no co-op to restore

AC4's multiplayer was **online versus only** (up to 8-player battles) — there was never a local
split-screen mode and never a cooperative mode. "Add a 2-player co-op mod" is therefore **new game
mode creation**, not re-enabling a hidden feature. That distinction drives the entire verdict.

## 3. The modding substrate is genuinely good

This is the strongest part of the answer. The port is unusually mod-friendly for a recomp project:

- **Function-level hooks at any address.** `src/runtime/game/hooks.txt` lists `0xADDR name` pairs;
  `ac4-recomp emit --hooks` (`src/recomp/main.cpp:189-202`) rewrites the generated `f_<addr>` to call
  `ppu::hook_<name>(ctx, orig)`. Hooks chain to the original function and read/write guest state
  directly (`ctx.r[3]`, `ctx.lr`, `r32()`, `w32()`, `ppu::mem()`). You can add entries.
- **Instruction patching.** `patches.txt` + `--patches` (`src/recomp/main.cpp:105-142`) rewrites
  `addis`/`lis` immediates with a **required match count**, so a wrong game version fails the build
  instead of corrupting it. Control flow cannot be patched.
- **Host → guest calls.** `hle::call_guest` / `call_guest_addr` (`src/runtime/hle.h:117-122`) call
  arbitrary guest functions from C++, with `StackAlloc` for out-params. This is the key primitive
  for a mod: you can invoke game logic, not just intercept it.
- **Reverse-engineering tooling.** Hardware data watchpoints (`src/runtime/datawatch.h`),
  `guest_backtrace`, `thread_dump`, `--verify-native` to prove a native replacement matches the
  original (`src/runtime/game/native_hooks.cpp:1-5`), and `fingerprint` to remap functions across
  game builds (`src/recomp/main.cpp:22-27`).
- **Proof it works.** The mouse/keyboard support is itself a deep mod: `input_hooks.cpp` hooks the
  player's input operator and *identifies the player by call-site return address*
  (`kPlayerTurnSetReturn = 0x00b52708`, `input_hooks.cpp:42`). `frame_hooks.cpp` resizes the game's
  draw arenas. `system_menu.cpp` injects a whole settings page into the retail menu.

### Three specific assets for co-op

1. **Second-controller input is already plumbed.** `sys_io.cpp:2-4,66` — "Pad port 0 combines the
   first controller... **ports 1-3 map to further controllers**". Pad data for players 2-4 already
   reaches guest memory; the game just never reads port 1.
2. **The engine has two pad classes with explicit port fields.** `hooks.txt:5-6`:
   `0x00951f94 pad_update` (port at `+384`) and `0x005ead28 pad_update_b` (port at `+736`).
   Multi-pad is a first-class engine concept.
3. **A VS scene with multiple player-input classes exists.** `hooks.txt:9` — the plain input class
   (vtable `0x1013f400`) carries "the player's turn command in the SIMULATOR (**VS scenes**)".
   `input_hooks.cpp:37-41` documents that missions, tutorial and VS all route through the same
   operator sites. The engine can already hold several player-controlled ACs in one scene.
4. **The game ships Lua 5.0** (`lua_hooks.cpp`) — a data/script layer exists above the machine code.

## 4. The three architectures, honestly assessed

### A. In-process dual instance → local split-screen
**Feasibility: very low.**
Guest memory is a single 4 GiB region at a **compile-time constant**:
`#define PPU_MEM_BASE 0x0000200000000000ull`, `ppu::mem()` = `reinterpret_cast<u8*>(PPU_MEM_BASE)`
(`src/ppu/ppu_recomp.h:17,58`). Every generated function and all HLE bake this in. A second game
state needs a second region, so `mem()` becomes per-instance/per-thread — and then every singleton
the runtime owns (RSX device + FIFO + `g_display[8]`, GCM context, audio out, VFS, and all LV2
objects keyed by *guest address*, which would now collide between instances) must be instance-scoped.
Guest threads are created by guest code, so attribution is at least tractable.

On top of that you still need the two simulations to agree about enemies and mission state — the
same sync problem as B, but with a far worse substrate. And it doubles sim + render cost on an
engine that already needed a 32 MiB draw-arena patch to survive m010's swarm (`patches.txt:6-11`,
138 `addis` rewrites).

### B. Two processes + restore the game's own netcode → online versus
**Feasibility: medium. Highest probability of a working result.**
The netcode already exists in your binary; you replace what's under it. Scope is well-bounded and
has prior art: RPCS3 — which this project already ships/bundles for decryption — implements these
exact libraries, and RPCN is the matching-server reference.

Work: real `sys_net` over WinSock (~BSD surface), `cellNetCtl` reporting a link, then `sceNp` +
`sceNpMatching2` (rooms/signaling) + a replacement backend + PSN identity/ticket shims.

**But this delivers versus, not co-op.** It is a prerequisite and a de-risking milestone, not the
goal.

### C. Custom co-op: two processes, per-AC authority, snapshot sync
**Feasibility: low, but it is the only path that produces actual co-op.**
Design: each player runs their own copy and renders their own camera (so **no split-screen needed**);
each owns their own AC; positions, rotations, velocities, weapon fire, damage and mission events are
exchanged over a custom thin protocol hooked at the seams the port already exposes —
`pad_update` (0x951f94), `ac_turn_input` (0x9fc210), `look_update` (0xa372e8), `view_matrix` (0x9ac8d4).

The hard part is not transport, it is **authority and divergence**. Enemy AI, collision, hit
detection and mission scripting are all computed locally in each instance. Lockstep is off the table
because PPU threads are real host threads driven by real mutexes/condition variables with best-effort
LV2 priority emulation (`src/runtime/ppu_thread.h:1,31`; `handoff`/`note_block`) — there is no
cross-machine determinism guarantee. So you need snapshot sync plus rules for who is authoritative
over AI and mission state, which means reverse-engineering the entity/AC structures. That
reconnaissance is not in the repo: `docs/GAME_NOTES.md` is referenced throughout but absent, and the
hook list only covers input/draw/menu concerns.

## 5. Risk register

| Risk | Severity | Evidence |
|---|---|---|
| **Version lock.** Every hook and patch is BLUS30027 disc 01.01. Some hooks key on *call-site return addresses*, so they break on any other build. | High | `hooks.txt:2`; `patches.txt:2-3`; `input_hooks.cpp:42-43` |
| **No determinism** → no lockstep netcode. | High | `ppu_thread.h:1,31`; host-thread PPU with real sync primitives |
| **Offline by policy**, not by gap. | High | `sys_net.cpp:1-3`; `sceNp.cpp:1-4` |
| **Single fixed guest space** blocks in-process duplication. | High | `ppu_recomp.h:17,58` |
| **No plugin ABI.** Mods are applied at *recompiler* time → a mod is a port fork + rebuild, not a DLL. | Medium | `recomp/main.cpp:189-202` |
| **Performance headroom.** Engine already fights CPU in places; doubling sim+render is expensive. | Medium | `CHANGELOG.md` 0.6.0; `patches.txt:6-11` |
| **Game-internals RE required** for entity syncing; notes not published. | Medium | missing `docs/GAME_NOTES.md` |
| **Toolchain constraint.** `-march=haswell` (x86-64-v3), C++20, MinGW. | Low | `CMakeLists.txt:92,127` |
| **Split-screen render** is at least tractable if ever needed. | Low | `rsx_core.cpp:86` (`g_display[8]`), viewport/scissor in `renderer.cpp` |

## 6. Verdict

- **Mechanically, a mod is very feasible.** The hooking, patching, guest-call and verification
  substrate is better than most recomp projects, and three assets (multi-port pad, engine-level
  multi-pad objects, an existing VS scene with multiple player input classes) point the right way.
- **A co-op *mode* is not feasible as a weekend mod.** AC4 has no co-op to restore; you are
  inventing a mode and then building a state-synchronization layer for an engine that was never
  written to be networked and is not deterministic across machines.
- **The best first deliverable is not co-op.** It is *online versus restored* (path B): bounded,
  prior art exists, and it de-risks everything path C needs (sockets, matching, two-instance
  rendezvous).
- **Cheapest decisive experiment:** hook `pad_update` (`0x951f94`), point its port field to 1, and
  verify a second controller's state reaches the game inside the SIMULATOR/VS scene. If that works,
  the engine's multi-pad path is real and path C becomes credible. If it doesn't, everything else
  gets much harder.

### Suggested staging
1. Second-pad probe (above) — small, decisive.
2. `sys_net` over WinSock + `cellNetCtl` link, confirm the game's net paths initialize.
3. Restore `sceNp`/matching far enough for the VS lobby (path B) → shipping online versus.
4. Only then: custom per-AC snapshot sync for co-op (path C), hooked at the AC state seams.
5. Splitscreen (path A) last, or never.

Note also that the sister **ACFA port shares this runtime** (`README.md:24-26`), so infrastructure
built generically here (sockets, matching, sync) transfers to Armored Core: For Answer.
