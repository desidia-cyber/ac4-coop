# Feasibility: 2-player **full-story campaign co-op** for Armored Core 4 via `AC4-Port`

Scope: two players playing Armored Core 4's campaign together, start to finish — not versus arenas.
Build analyzed: `SulphurS16/AC4-Port` v0.6.0 (`219c7bf`), MIT. Local checkout `./ac4-port`.

---

## 1. Summary verdict

**Possible in principle, but it is a research project, not a mod.** The port hands you an excellent
substrate — address-level hooks with original-chaining, guest function calls, instruction patching,
game-archive patching, and an injected UI layer. What it does **not** give you is any knowledge of the
game's unit, entity, mission or scripting structures. That missing knowledge is almost the whole job.

Two things must be separated, because they have very different difficulty:

- **A second player-controlled AC existing in a mission scene.** This is the make-or-break item. If
  it works, the rest is engineering. If it doesn't, there is no co-op.
- **Making two machines agree about the world.** Only needed for online co-op, and it is a
  wide reverse-engineering surface.

---

## 2. Correcting an assumption from the first pass

The earlier note "splitscreen needs a runtime-wide dual-instance refactor" applies **only if the two
players have independent simulations**. For **one world shared by two players**, there is
**one** guest state: one set of enemies, one mission script, one save. That removes the
`PPU_MEM_BASE` duplication problem entirely.

So the real axis is not "split vs online", it is:

| Design | Guest worlds | Render cost | State sync | Extra runtime work |
|---|---|---|---|---|
| **One world, 2 pads, splitscreen** | 1 | 2 views/frame | **none** | second player AC, second camera, dual render |
| **Two worlds, online** | 2 (separate processes) | 1 each | **the whole problem** | full unit/world sync + authority |

Splitscreen on one machine (or via Parsec/Steam Remote Play for a distant second player) avoids the
entire world-sync problem. Online co-op avoids the dual-render problem but pays the sync cost. Those
are the two real options; both are large.

---

## 3. Requirements for "full story, start to finish"

| # | Requirement | Feasibility | Why |
|---|---|---|---|
| 1 | A **second player** (not AI) AC in every mission scene | **Low — decisive** | Mission scene is a different class from VS; see §4 |
| 2 | Second controller routed to that AC | **High** | `cellPad` already exposes ports 0–3 (`sys_io.cpp:2-4,66`); the game's pad objects carry explicit port fields (`hooks.txt:5-6`, port at `+384` / `+736`) |
| 3 | Second camera + second view | **Medium** | Needs dual render; `rsx_core.cpp:86` has 8 display buffers and the renderer has viewport/scissor, but the RSX FIFO is a linear stream consumed once |
| 4 | Mission scripts tolerating a second player | **Unknown — high risk** | Bespoke per mission; no notes published |
| 5 | Death / revive / fail semantics for P2 | **Medium** | Needs the player-unit concept from §4 anyway |
| 6 | P2 has an AC build (parts, garage) | **Medium** | Port can inject menus and overlays; economy rules are design work |
| 7 | Campaign progression / save for two players | **Medium** | `cellSaveData` is one directory per save (`save_data.cpp:1-10`) |
| 8 | Co-op entry UI (mission select, lobby) | **High** | Proven: `system_menu.cpp` patches the game's `systemsetting.000` archive to add a row; `ui/overlay.h` + D2D/DWrite draws custom screens |
| 9 | Hub, briefings, cutscenes, mission reports | **Medium** | Single-player UI; in two-process both just watch, in split you show fullscreen to both |
| 10 | Networking (online route only) | **Medium** | Does **not** need the game's netcode — see §5 |

---

## 4. The decisive problem: a second *player* in a mission scene

Everything hinges on this. Three routes, best first:

**(a) Reuse the VS-mode player machinery inside mission scenes.** The engine demonstrably supports
multiple player-controlled ACs: `input_hooks.cpp:37-41` documents that in the SIMULATOR/VS scene the
player's input object is the plain class `0x1013f400`, and `hooks.txt:9` names it "the player's turn
command in the SIMULATOR (VS scenes)". VS also proves multi-pad: `pad_update_b` (`0x5ead28`) is a
second pad class with its own port. The catch: missions are `Ac4SortieScene` and VS is
`VsSortieScene` (`frame_policy.cpp:5-7`) — different scene classes. You must teach the *mission*
scene to instantiate a second player's operator + input + view + AC graph, using the VS path as the
template. This is the highest-value RE target in the whole project.

**(b) Repurpose an AI AC.** If a mission already spawns an ally (or you hijack a spawned AC), re-parent
its controller to pad port 1. Cheaper per mission, but the mission script still treats it as AI, and
not every mission has a suitable second AC.

**(c) Forge the object graph from scratch.** Deepest; fallback only.

**Cheapest decisive experiment** — do this before anything else:
1. Hook `pad_update` (`0x951f94`) and `pad_update_b` (`0x5ead28`), log the port field and the owning
   object for every pad object the game creates.
2. Confirm the game ever creates a second pad object / controller with port ≥ 1 anywhere.
3. In the SIMULATOR/VS scene, get pad port 1 to drive a second AC. If that works, the engine's
   multi-pad path is real.
4. Then attempt the same inside `Ac4SortieScene`.

Steps 1–3 are the gate. Everything in this assessment is speculative until they pass.

---

## 5. Correction on networking

The first analysis treated the offline network stack as a blocker. For a **custom** co-op transport
it is not:

- `sys_net` is stubbed **for the guest** (`sys_net.cpp:1-3,45-48`), because the port deliberately has
  no network interface for the game to use.
- **Your mod's host code can just use WinSock directly.** `CMakeLists.txt:127` already links what you
  need; add `ws2_32`. You are not required to route through `sys_net` HLE unless you want the game's
  own netcode to run.

That said, restoring `sys_net`, `cellNetCtl`, `sceNp` and `sceNpMatching2` is still the right move if
you ever want the game's native online mode — RPCS3 implements all of them and is the reference.

---

## 6. What the world-sync problem actually costs (online route)

Two processes means two divergent worlds. You must sync, at minimum:

- unit transforms (position, rotation, velocity) and animation/pose state
- weapon fire events, projectiles, hit registration and damage
- AI decision state — or accept host authority and mostly suppress guest-side AI
- spawn / despawn / destruction
- mission events: objective progress, triggers, completion and failure

None of these structures are documented, and the port contains **no game logic at all** — it never
touches units. Expect to spend the bulk of the project in `datawatch` (`src/runtime/datawatch.h`) and
`--verify-native` sessions identifying them. The available seams are the ones the port already found:
`ac_turn_input` (0x9fc210), `look_update` (0xa372e8), `view_matrix` (0x9ac8d4), `pad_update` (0x951f94).

Note also there is **no cross-machine determinism**: guest PPU threads are real host threads driven
by real mutexes/condition variables with best-effort LV2 priority emulation (`ppu_thread.h:1,31`;
`handoff`/`note_block`). Lockstep netcode is off the table — snapshot sync and authority rules are
the only option.

---

## 7. Risks specific to a full campaign

| Risk | Severity | Notes |
|---|---|---|
| **Second player AC in mission scenes** | Fatal if unsolved | §4. Gate everything on it. |
| **Bespoke mission scripting** | High | The long tail. "Start to finish" means every mission with scripted events — escorts, timers, dialogue triggers, scripted cameras — must be made co-op safe. Each is potentially a one-off fix. |
| **Base port stability** | High | `ppu_thread.cpp:125-128` records a **mission-start crash in the sound lookup** that "still occurs, see STATUS" — and `STATUS`/`docs/GAME_NOTES.md` are not published. v0.6.0 is roughly a month old with two GitHub issues. You would be building on a moving target. |
| **Dual render budget** | High (split route) | The engine needed its draw arenas patched from 3 MiB to 32 MiB to survive m010's swarm at 60 fps (`patches.txt:6-11`, 138 rewrites). Two views per frame is a hard CPU/GPU cost. |
| **World sync width** | High (online route) | §6. Largest single work item; entirely undocumented. |
| **Economy / loadout for P2** | Medium | Design + save integration; port can patch the menu archive and draw overlays, so the plumbing is proven. |
| **Version lock** | Medium | All hooks/patches target BLUS30027 disc 01.01; some key on call-site return addresses (`input_hooks.cpp:42-43`). `fingerprint` in `recomp/main.cpp:22-27` is the remap tool for other discs. |
| **Distribution** | Medium | Mods apply at *recompiler* time (`recomp/main.cpp:189-202`), so this ships as a port fork users rebuild against their own disc — not a drop-in DLL. Consistent with MIT and the project's legal stance, but no clean user-facing install. |

---

## 8. Practical verdict

- **"Full story co-op" as a finished, distributable mod: not currently realistic** on this port.
  Nothing in the repo shows the game's mission/unit structures being understood, and that
  understanding is the core of the work. The single gating question — *can a mission scene host a
  second human-driven AC?* — is unanswered and unanswerable from the source alone.
- **A realistic first milestone**: two pads, two ACs, one mission, one machine. No network, no menus,
  no progression. If that cannot be made to work, no amount of networking or UI will help. If it can,
  the project becomes a long but tractable engineering effort.
- **Sequencing I'd recommend:**
  1. Pad-object survey + second AC in VS/SIMULATOR (gate).
  2. Second AC inside a real mission scene, driven by pad 1, single view.
  3. Dual render (splitscreen) → playable local co-op on one mission.
  4. Co-op UI via the proven menu-archive patch + `ui/overlay` (P2 loadout, co-op entry).
  5. Roll across the full mission list, fixing scripted missions one by one.
  6. Only then consider networking: two processes + per-AC authority + snapshot sync.
- **Cheaper deliverable that still satisfies the literal goal** if the gate fails: shared-save
  alternating co-op (both players finish the campaign on one save, handing off per mission), which
  needs almost no engine work. Worth offering as a fallback rather than shipping nothing.

**Bottom line:** the port is the right foundation and the tooling is genuinely good, but "2 players,
full campaign" is a months-long reverse-engineering project with one unproven make-or-break
hypothesis. Start by disproving or proving the second-player-AC question; everything else is
downstream of it.
