# 05 — Online / LAN architecture

Requirement: 2 players, full campaign, over a network (online and/or LAN).
This document covers the transport and state-sync decision. It does **not** depend on the outcome of
the single-player entity test in doc 02 — do that first, then build this.

---

## 1. What AC4 already contains (measured)

This is the most important architectural finding in the whole dossier: **AC4 ships a complete
UDP-based netcode for its versus mode.** Extracted from embedded build paths and symbol strings in
`EBOOT.elf`:

### Game-side network module — `Sw/Source/Network/`

```
Network/Ac/AcNetAction.cpp                  AC network action
Network/Behavior/AcNetBehaviorFactory.cpp
Network/Behavior/AcNetSendBehavior.cpp      AC state send
Network/Behavior/AcNetReceiveBehavior.cpp   AC state receive
Network/Weapon/AcNetWeaponFactory.cpp       weapon fire replication
Network/Scene/NetVsSortieScene.cpp          networked sortie scene
```

### Middleware network layer — Dantelion2

```
ClassLibrary/Extended/Network/Source/Common/Fsdp/FSDPInputStream.cpp
                                        /FSDPOutputStream.cpp
                                        /FSDPSequenceControl.cpp
                                        /FSDPServerSocket.cpp
                                        /FSDPSocket.cpp
                              Common/Udp/UDPChannel.cpp
                                        /UDPHeader.cpp
                                        /UDPInputStream.cpp
                                        /UDPOutputStream.cpp
                              DLNetworkServiceImpl.cpp
                              Ps3/DLNetworkServiceImpl_PS3.cpp
                              Ps3/PS3NetworkSystemDriver.cpp
                              Ps3/Udp/PS3UDPSocket.cpp
                              Ps3/Udpp2p/UDPP2PSocket.cpp
```

`FSDP` is FromSoftware's own datagram protocol with sequence control on top of UDP, plus a P2P socket
variant. This is real, shipped netcode — not something we have to invent.

### Matchmaking modes present

```
SystemLinkMatch     <-- LAN / system-link play
OnlineMatch
PlayerMatch, RankMatch, DivideMatch
MatchSelectManager, MatchScene
```

**`SystemLinkMatch` is the LAN path.** That matters because it is designed to work without PSN
matchmaking, which is exactly what the port cannot provide.

### Socket API the game actually calls

The port's `sys_net` HLE registers exactly the functions AC4 imports
(`ac4-port/src/runtime/hle/sys_net.cpp:97-122`):

```
sys_net_initialize_network_ex, finalize_network, free_thread_context, if_ctl,
set_udpp2p_test_param, show_ifconfig, show_nameserver, show_route,
socket, bind, listen, getsockname, setsockopt, getsockopt, shutdown,
sendto, recvfrom, socketpoll, socketclose, gethostbyname, gethostbyaddr,
inet_addr, inet_ntoa
```

Note what is **absent**: no `connect`, no `send`/`recv`. The game is **UDP-only**, which matches the
FSDP/UDPP2P design. That makes the HLE surface small and well-bounded.

Today every one of those returns `ENETDOWN` (`sys_net.cpp:45-48`), so the netcode initialises and
then cannot open a socket.

---

## 2. Route A (recommended first spike): bring up the game's own netcode

Because the netcode exists, the cheapest *plausible* path to online co-op is to feed it sockets,
rather than write a synchronisation layer from scratch.

### Work items

1. **Implement `sys_net` for real over WinSock** in `src/runtime/hle/sys_net.cpp`.
   - `socket`/`bind`/`listen`/`getsockname`/`setsockopt`/`getsockopt`/`shutdown`/`socketclose`
   - `sendto`/`recvfrom` — straightforward mapping to `WSASendTo`/`WSARecvFrom`; guest memory is
     big-endian, and `sockaddr_in` fields must be byte-swapped (the port already does this for
     `inet_addr`, `sys_net.cpp:64-82`).
   - `socketpoll` → `WSAPoll`
   - `gethostbyname`/`gethostbyaddr` → or just return loopback/LAN addresses
   - `sys_net_if_ctl` → report a link (currently `ENETDOWN`)
   - Reference implementation: RPCS3's `sys_net` (same guest API, same PS3 semantics).
2. **`cellNetCtl`**: report link up and a plausible NAT type so the game's connection-quality logic
   proceeds (`ac4-port/src/runtime/hle/cellNetCtl.cpp`).
3. **Choose `SystemLinkMatch`** in the versus menu and see whether the netcode completes a handshake
   between two instances on a LAN.
4. **Only then**: move the sync from `NetVsSortieScene` to the mission scene.

### Why this is the better first bet

- It reuses proven synchronisation (`AcNetAction`, `AcNetSendBehavior`, `AcNetWeaponFactory`) instead
  of reverse-engineering AC state to replicate it ourselves.
- The socket work is bounded and well-specified.
- It also unlocks the game's original online versus mode as a side effect.

### Risks to validate in the spike

| risk | why it matters |
|---|---|
| `SystemLinkMatch` may still require a PSN/NP identity | would force implementing parts of `sceNp` |
| The netcode is **versus-shaped** — likely one authority per client with no shared mission state | co-op needs mission event replication, which may not exist |
| `NetVsSortieScene` is a different scene class from `Ac4SortieScene`/`MissionSortieScene` | the sync components may not be reusable outside VS |
| The port has no network interface at all today, including timing behaviour | expect latency-sensitive bugs the console never hit |

Treat Route A as a **spike with a decision point**, not a commitment.

---

## 3. Route B (fallback): custom transport + own state sync

If Route A stalls (most likely at the "mission scene is not a VS scene" boundary), the fallback is a
mod-owned synchronisation layer.

### Transport

- **Do not** route through the guest's `sys_net`. The mod's own C++ may call WinSock directly; the
  guest never needs to see a socket. Link `ws2_32`.
- Lobby: trivial — host/join by IP, one peer, fixed 2-player lobby. No matchmaking service needed.

### Synchronisation model

Two processes, two independent worlds. There is **no lockstep option**:

> Guest PPU threads are real host threads driven by real mutexes/condition variables with
> best-effort LV2 priority emulation (`ac4-port/src/runtime/ppu_thread.h:1,31`, `handoff`/`note_block`).
> There is no cross-machine determinism.

So: **snapshot delta sync with per-AC authority.**

| state | owner | notes |
|---|---|---|
| own AC transform / velocity / pose | that player | sent every tick (or every N ticks) |
| weapon fire and projectiles | firing player | event-based, with a sequence number |
| damage dealt to AI | the shooter | apply on both sides, or send damage events |
| AI units (Normals, MTs, bosses) | host | guest renders host-authoritative AI |
| mission events (triggers, objectives, completion) | host | guest must accept host's mission result |
| destroying/spawning mission entities | host | guest mirrors |

### Where to hook

The port already exposes the natural seams, so start from these rather than hunting:

| seam | address | use |
|---|---|---|
| AC turn input, per frame | `0x009fc210` | outbound own-AC orientation |
| AC look control | `0x00a372e8` | camera/aim |
| AC view matrix | `0x009ac8d4` | confirm transform |
| pad object update | `0x00951f94` / `0x005ead28` | inject remote input if desired |
| frame present | `0x001406b8` | tick fence / snapshot boundary |

For discovering the unit structures themselves, use `datawatch`
(`ac4-port/src/runtime/datawatch.h`) with the transform you find from the view-matrix hook, then
`--verify-native` to confirm any native replacement.

### Determining "who is P2" at runtime

`ppu_thread.h` records guest thread names and `frame_policy.cpp:5-8` already classifies
`*SortieScene` / `FcsManager` threads — reuse that to detect mission start/end on both peers.

---

## 4. Recommendation

1. Do the doc 02 §4 entity test first (single machine, two pads). It is a prerequisite for both routes.
2. Then spend a time-boxed spike on Route A item 1–3 (real UDP sockets + `cellNetCtl` + SystemLink).
   It is the only path that might avoid writing world sync at all.
3. If the spike shows the mission scene cannot use the VS sync, commit to Route B and budget it as
   the single largest work item in the project.

## 5. Honest sizing

| item | character |
|---|---|
| `sys_net` UDP + poll over WinSock | bounded, well-specified, RPCS3 as reference |
| `cellNetCtl` link reporting | small |
| Reusing FSDP/AcNet for missions | **unknown — the key spike** |
| Custom world sync (Route B) | largest item; pure reverse engineering of undocumented structures |
| Lobby / join / disconnect handling | moderate, mostly new code |
| Desync/drift handling, reconnects | moderate; expect to iterate |

## 6. Non-goals worth stating

- No 3+ player support: AC4's scripts know exactly one extra player id, and ACFA capped at 2.
- No matchmaking service: LAN/直接 IP is enough and avoids PSN entirely.
- No attempt at deterministic lockstep: it is not achievable on this runtime.
