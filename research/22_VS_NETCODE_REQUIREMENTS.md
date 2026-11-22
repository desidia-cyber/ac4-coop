# 22 — VS netcode requirements (N1–N6)

Static analysis only. Every claim below carries a guest address plus `file:line` into
`ac4-port/local/gen/ppu/`. Anything I could not prove from the files is listed as
**NOT DETERMINED** — nothing here is inferred from "it probably works like X".

Tooling added for this note (all under `tools/re/`, read-only against the repo):

| tool | purpose |
|---|---|
| `netimp.py` | walks the ELF import-stub tables (PRX param `0x60000002`, `+24`/`+28`) → `(library, NID, slot VA)`; resolves NIDs by the port's own `fnid_of` SHA-1 scheme (`runtime/hle.cpp:22-29`) |
| `netstub.py` | maps every import stub **in the gen code** to its slot, then counts call sites from `genidx` |
| `netrefs.py` | resolves `r2`-relative data refs using each function's **own** OPD TOC (the shipped `refindex.pkl` only indexes the 8 most common TOCs and therefore misses most `.rodata` refs) |
| `nettu.py` | `n:/Ac4/.../*.cpp` literal → per-TU descriptor record → the functions compiled from that source file |
| `vtidx.py` | real vtable index: maximal runs of ≥4 OPD pointers (a lone OPD also "looks like" a vtable, which is why a run test is required) |

---

## N1. Imported network functions

### N1.0 What AC4 imports at all

The executable imports **18 libraries** (`netimp.py -> imports()`): `cellGcmSys`,
`sys_io`, `sys_fs`, `cellRtc`, `cellSysutil`, `cellSysmodule`, `cellL10n`, `cellResc`,
`sys_net`, `cellAudio`, `cellSpurs`, `cellNetCtl`, `sceNp`, `cellHttp`, `cellHttpUtil`,
`cellSail`, `cellPamf`, `sysPrxForUser`.

* There is **no `cellRudp`, no `cellAdhoc`, no `cellNet`, no `socket` PRX, and no
  separate `sceNpMatching` / `sceNpSignaling` / `sceNpBasic` library**: all 56 NP
  entry points live in the single `sceNp` module (NID table at slot `0x00e0dc9c`).
* `cellNetCtl` has 8 functions; the network-related `cellSysutil` functions do **not**
  include a net-start dialog — the dialog is `cellNetCtlNetStartDialog*`.

### N1.1 Import stubs in the generated code

All import stubs of the main executable are one contiguous block of 10-line functions
in `ppu_gen_148.cpp`, from `f_00dd8eb8` (`:435`) to `f_00ddb414`, in the code section at
`0x00dd8e94` (`size 0x2580`). A stub is always this shape:

```c
ppu_gen_148.cpp:445 void f_00dd8ed8(Ctx& __restrict ctx){
:447   R(12)=R(12)|0xe10000ull;                      // slot page
:448   R(12)=r32((u32(R(12))+-9660u));               // slot_va = 0xe10000 - 9660
:450   R(0)=r32(u32(R(12))); R(2)=r32((u32(R(12))+4u));   // OPD {code, TOC}
:452   ctx.ctr=R(0);
:453   {ctx.cia=0x00dd8ef4u;icall(ctx,u32(ctx.ctr));return;}
```

`loader.cpp:89` writes an HLE OPD into the slot and `ppu_runtime.cpp` `icall()` dispatches
it (incrementing `imp->calls`), so *the slot VA is the identity of the import* and the stub
function address is what game code calls.

### N1.2 sys_net — 23 imports (slot base `0x00e0de68`)

| # | funcid (NID) | name | slot | stub (`ppu_gen_148.cpp`) | call sites | port behaviour today (`runtime/hle/sys_net.cpp`) |
|---|---|---|---|---|---|---|
| 1 | 051ee3ee | `sys_net_socketpoll` | 00e0de68 | `f_00dd9b18` :1428 | 2 | `fail(EINVAL)` → `-1`, `*errno=22` (`:58`) |
| 2 | 10b81ed6 | `sys_net_set_udpp2p_test_param` | 00e0de6c | `f_00dd9b38` :1438 | 2 | `CELL_OK` (`:93`, no-op) |
| 3 | 139a9e9b | `sys_net_initialize_network_ex` | 00e0de70 | `f_00dd9b58` :1448 | 1 | `CELL_OK` (`:38-41`) |
| 4 | 13efe7f5 | `sys_net_getsockname` | 00e0de74 | `f_00dd9b78` :1458 | 4 | `fail(EBADF)` (`:52`) |
| 5 | 1f953b9f | `sys_net_recvfrom` | 00e0de78 | `f_00dd9b98` :1468 | 7 | `fail(EBADF)` (`:55`) |
| 6 | 28e208bb | `sys_net_listen` | 00e0de7c | `f_00dd9bb8` :1478 | 1 | `fail(EBADF)` (`:51`) |
| 7 | 5420e419 | `sys_net_show_nameserver` | 00e0de80 | `f_00dd9bd8` :1488 | 1 | `CELL_OK` (`:91`) |
| 8 | 5a045bd1 | `sys_net_getsockopt` | 00e0de84 | `f_00dd9bf8` :1498 | 2 | `fail(EBADF)` (`:53`) |
| 9 | 6005cde1 | `_sys_net_errno_loc` | 00e0de88 | `f_00dd9c18` :1508 | 15 | real (allocates a per-thread errno cell, `:16-25`) |
| 10 | 6db6e8cd | `sys_net_socketclose` | 00e0de8c | `f_00dd9c38` :1518 | 19 | `fail(EBADF)` (`:49`) |
| 11 | 71f4c717 | `sys_net_gethostbyname` | 00e0de90 | `f_00dd9c58` :1528 | 2 | returns `0` (NULL) (`:59-62`) |
| 12 | 79b61646 | `sys_net_show_route` | 00e0de94 | `f_00dd9c78` :1538 | 1 | `CELL_OK` (`:92`) |
| 13 | 858a930b | `sys_net_inet_ntoa` | 00e0de98 | `f_00dd9c98` :1548 | 5 | **real** (`:83-88`) |
| 14 | 88f03575 | `sys_net_setsockopt` | 00e0de9c | `f_00dd9cb8` :1558 | 11 | `fail(EBADF)` (`:54`) |
| 15 | 9647570b | `sys_net_sendto` | 00e0dea0 | `f_00dd9cd8` :1568 | 6 | `fail(EBADF)` (`:56`) |
| 16 | 9c056962 | `sys_net_socket` | 00e0dea4 | `f_00dd9cf8` :1578 | 5 | `fail(ENETDOWN)` → `-1`, `*errno=50` (`:45-48`, one-shot warning `:30-36`) |
| 17 | a50777c6 | `sys_net_shutdown` | 00e0dea8 | `f_00dd9d18` :1588 | 4 | `fail(EBADF)` (`:57`) |
| 18 | b0a59804 | `sys_net_bind` | 00e0deac | `f_00dd9d38` :1598 | 5 | `fail(EBADF)` (`:50`) |
| 19 | b48636c4 | `sys_net_show_ifconfig` | 00e0deb0 | `f_00dd9d58` :1608 | 1 | `CELL_OK` (`:90`) |
| 20 | b68d5625 | `sys_net_finalize_network` | 00e0deb4 | `f_00dd9d78` :1618 | 2 | `CELL_OK` (`:42`) |
| 21 | dabbc2c0 | `sys_net_inet_addr` | 00e0deb8 | `f_00dd9d98` :1628 | 6 | **real** (`:64-82`) |
| 22 | f7ac8941 | `sys_net_gethostbyaddr` | 00e0debc | `f_00dd9db8` :1638 | 1 | returns `0` (`:63`) |
| 23 | fdb8f926 | `sys_net_free_thread_context` | 00e0dec0 | `f_00dd9dd8` :1648 | 1 | `CELL_OK` (`:44`) |

Two facts that matter for the port:

* **`sys_net_if_ctl` is *not* imported** by AC4 (23 NIDs, none is it) even though
  `sys_net.cpp:89,101` implements it. Conversely **`connect`, `accept`, `send`, `recv`
  and `select` are not imported either** — so `listen()` at `f_0024e0b4` can never be
  followed by an `accept()`.
* Port registers the *wrong* return for `socketpoll`: it returns `EINVAL`, not `ENETDOWN`
  (`sys_net.cpp:58`), and `socketclose`/`bind`/… return `EBADF` rather than `ENETDOWN`.

### N1.3 cellNetCtl — 8 imports (slot base `0x00e0dae8`)

| funcid | name | slot | stub | call sites | port behaviour (`runtime/hle/cellNetCtl.cpp`) |
|---|---|---|---|---|---|
| 04459230 | `cellNetCtlNetStartDialogLoadAsync` | 00e0dae8 | `f_00dda038` :1838 | 3 | posts `0x0801`+`0x0802`, `CELL_OK` (`:97-109`) |
| 0ce13c6b | `cellNetCtlAddHandler` | 00e0daec | `f_00dda058` :1848 | 1 | stores handler, **never invoked**, `CELL_OK` (`:57-68`) |
| 0f1f13d3 | `cellNetCtlNetStartDialogUnloadAsync` | 00e0daf0 | `f_00dda078` :1858 | 3 | `*result = 0x80130182` (NET_NOT_CONNECTED), `CELL_OK` (`:110-120`) |
| 105ee2cb | `cellNetCtlTerm` | 00e0daf4 | `f_00dda098` :1868 | 4 | `CELL_OK` (`:43`) |
| 1e585b5d | `cellNetCtlGetInfo` | 00e0daf8 | `f_00dda0b8` :1878 | 3 | code 1 → `0` (wired), code 4 → `0` (link **down**), else `0x80130108` (`:76-88`) |
| 8b3eba69 | `cellNetCtlGetState` | 00e0dafc | `f_00dda0d8` :1888 | 3 | `*state = 0` = `Disconnected` (`:50-56`) |
| 901815c3 | `cellNetCtlDelHandler` | 00e0db00 | `f_00dda0f8` :1898 | 1 | `CELL_OK` (`:69`) |
| bd5a59fc | `cellNetCtlInit` | 00e0db04 | `f_00dda118` :1908 | 5 | `CELL_OK` (`:36-42`) |

### N1.4 sceNp — 56 imports (slot base `0x00e0dc9c`)

Offline values from `runtime/hle/sceNp.cpp` (constants `:12-42`, handlers below).

| funcid | name | slot | stub (`ppu_gen_148.cpp`) | sites | offline result |
|---|---|---|---|---|---|
| 03c741a7 | `sceNpMatchingGetResult` | 00e0dc9c | `f_00dda138` :1918 | 2 | `0x8002aa0c` OFFLINE |
| 168a3117 | `sceNpBasicAddPlayersHistory` | 00e0dca0 | `f_00dda158` :1928 | 1 | `0x8002aa0c` |
| 1a2704f7 | `sceNpScoreWaitAsync` | 00e0dca4 | `f_00dda178` :1938 | 25 | `0x8002a1a3` INVALID_ID |
| 21206642 | `sceNpScoreGetRankingByRangeAsync` | 00e0dca8 | `f_00dda198` :1948 | 1 | `0x8002a106` NO_LOGIN |
| 259113b8 | `sceNpScoreDestroyTitleCtx` | 00e0dcac | `f_00dda1b8` :1958 | 3 | id-dependent |
| 26b3bc94 | `sceNpMatchingGetResultGUI` | 00e0dcb0 | `f_00dda1d8` :1968 | 4 | `0x8002aa0c` |
| 27c69eba | `sceNpBasicAddFriend` | 00e0dcb4 | `f_00dda1f8` :1978 | 1 | `0x8002a678` NOT_CONNECTED |
| 2ad7837d | `sceNpMatchingAcceptInvitationGUI` | 00e0dcb8 | `f_00dda218` :1988 | 1 | `0x8002aa0c` |
| 2e1c5068 | `sceNpMatchingDestroyCtx` | 00e0dcbc | `f_00dda238` :1998 | 1 | `CELL_OK` |
| 32cf311f | `sceNpScoreInit` | 00e0dcc0 | `f_00dda258` :2008 | 3 | `CELL_OK` |
| 32febb4c | `sceNpMatchingSearchJoinRoomGUI` | 00e0dcc4 | `f_00dda278` :2018 | 1 | `0x8002aa0c` |
| 34cc0ca4 | `sceNpMatchingKickRoomMember` | 00e0dcc8 | `f_00dda298` :2028 | 1 | `0x8002aa0c` |
| 36d0c2c5 | `sceNpManagerGetAvatarUrl` | 00e0dccc | `f_00dda2b8` :2038 | 1 | `0x8002aa0c` |
| 3b02418d | `sceNpScoreGetGameData` | 00e0dcd0 | `f_00dda2d8` :2048 | 2 | `0x8002a106` |
| 3cc8588a | `sceNpMatchingCreateRoomGUI` | 00e0dcd4 | `f_00dda2f8` :2058 | 1 | `0x8002aa0c` |
| 3db7914d | `sceNpScoreGetRankingByNpIdAsync` | 00e0dcd8 | `f_00dda318` :2068 | 1 | `0x8002a106` |
| 3f0808aa | `sceNpBasicSetPresence` | 00e0dcdc | `f_00dda338` :2078 | 1 | `0x8002a678` |
| 43b989f5 | `sceNpBasicSendMessageAttachment` | 00e0dce0 | `f_00dda358` :2088 | 1 | `0x8002a678` |
| 4885aa18 | `sceNpTerm` | 00e0dce4 | `f_00dda378` :2098 | 1 | `CELL_OK` |
| 52a6b523 | `sceNpManagerUnregisterCallback` | 00e0dce8 | `f_00dda398` :2108 | 1 | `CELL_OK` |
| 60897c38 | `sceNpSignalingActivateConnection` | 00e0dcec | `f_00dda3b8` :2118 | 2 | `0x8002a80e` CONN_NOT_FOUND |
| 6356082e | `sceNpSignalingCreateCtx` | 00e0dcf0 | `f_00dda3d8` :2128 | 1 | **`CELL_OK` + a fake ctx id** (`:351-360`) |
| 64a704cc | `sceNpBasicRecvMessageAttachmentLoad` | 00e0dcf4 | `f_00dda3f8` :2138 | 1 | `0x8002a673` INVALID_DATA_ID |
| 691f429d | `sceNpMatchingGetRoomInfo` | 00e0dcf8 | `f_00dda418` :2148 | 2 | `0x8002aa0c` |
| 6f5e8143 | `sceNpScoreCreateTransactionCtx` | 00e0dcfc | `f_00dda438` :2158 | 10 | `0x8002a106` |
| 8b7bbd73 | `sceNpMatchingSendInvitationGUI` | 00e0dd00 | `f_00dda458` :2168 | 1 | `0x8002aa0c` |
| 9851f805 | `sceNpScoreTerm` | 00e0dd04 | `f_00dda478` :2178 | 3 | `CELL_OK` |
| a284bd1d | `sceNpMatchingSetRoomSearchFlag` | 00e0dd08 | `f_00dda498` :2188 | 2 | `0x8002aa0c` |
| a7a090e5 | `sceNpScorePollAsync` | 00e0dd0c | `f_00dda4b8` :2198 | 7 | `0x8002a1a3` |
| a7bff757 | `sceNpManagerGetStatus` | 00e0dd10 | `f_00dda4d8` :2208 | 1 | `*status = -1` OFFLINE, `CELL_OK` (`:91-96`) |
| a8cf8451 | `sceNpSignalingDestroyCtx` | 00e0dd14 | `f_00dda4f8` :2218 | 1 | `CELL_OK` |
| ac66568c | `sceNpMatchingCreateCtx` | 00e0dd18 | `f_00dda518` :2228 | 1 | **`CELL_OK` + a fake ctx id** (`:309-318`) |
| acb9ee8e | `sceNpBasicUnregisterHandler` | 00e0dd1c | `f_00dda538` :2238 | 1 | `CELL_OK` |
| af505def | `sceNpBasicGetMatchingInvitationEntryCount` | 00e0dd20 | `f_00dda558` :2248 | 4 | `0x8002aa06` ID_NOT_FOUND |
| b5cb2d56 | `sceNpBasicRecvMessageAttachment` | 00e0dd24 | `f_00dda578` :2258 | 1 | `0x8002a678` |
| b9f93bbb | `sceNpScoreCreateTitleCtx` | 00e0dd28 | `f_00dda598` :2268 | 4 | `CELL_OK` + id |
| bcc09fe7 | `sceNpBasicRegisterHandler` | 00e0dd2c | `f_00dda5b8` :2278 | 1 | `CELL_OK` |
| bd28fdbf | `sceNpInit` | 00e0dd30 | `f_00dda5d8` :2288 | 1 | `CELL_OK` (`:72-80`) |
| bdc07fd5 | `sceNpManagerGetNetworkTime` | 00e0dd34 | `f_00dda5f8` :2298 | 1 | `0x8002aa0c` |
| c5f4cf82 | `sceNpScoreDestroyTransactionCtx` | 00e0dd38 | `f_00dda618` :2308 | 25 | id-dependent |
| ca0a2d04 | `sceNpSignalingGetConnectionStatus` | 00e0dd3c | `f_00dda638` :2318 | 1 | `0x8002a80e` |
| d208f91d | `sceNpUtilCmpNpId` | 00e0dd40 | `f_00dda658` :2328 | 5 | **real byte compare** (`:218-231`) |
| dae2d351 | `sceNpMatchingGrantOwnership` | 00e0dd44 | `f_00dda678` :2338 | 1 | `0x8002aa0c` |
| db2e4dc2 | `sceNpScoreGetGameDataAsync` | 00e0dd48 | `f_00dda698` :2348 | 1 | `0x8002a106` |
| ddce7d15 | `sceNpScoreGetBoardInfoAsync` | 00e0dd4c | `f_00dda6b8` :2358 | 2 | `0x8002a106` |
| e035f7d6 | `sceNpBasicGetEvent` | 00e0dd50 | `f_00dda6d8` :2368 | 1 | `0x8002a66a` NO_EVENT |
| e7dcd3b4 | `sceNpManagerRegisterCallback` | 00e0dd54 | `f_00dda6f8` :2378 | 1 | `CELL_OK`, callback never fires |
| ec0a1fbf | `sceNpBasicSendMessage` | 00e0dd58 | `f_00dda718` :2388 | 1 | `0x8002a678` |
| ee5b20d9 | `sceNpScoreAbortTransaction` | 00e0dd5c | `f_00dda738` :2398 | 1 | id-dependent |
| f0b1e399 | `sceNpScoreRecordScoreAsync` | 00e0dd60 | `f_00dda758` :2408 | 1 | `0x8002a106` |
| f42c0df8 | `sceNpManagerGetOnlineName` | 00e0dd64 | `f_00dda778` :2418 | 1 | `0x8002aa0c` |
| f4e0f607 | `sceNpScoreGetBoardInfo` | 00e0dd68 | `f_00dda798` :2428 | 1 | `0x8002a106` |
| f76847c2 | `sceNpScoreRecordGameDataAsync` | 00e0dd6c | `f_00dda7b8` :2438 | 2 | `0x8002a106` |
| f806c54c | `sceNpMatchingLeaveRoom` | 00e0dd70 | `f_00dda7d8` :2448 | 1 | `0x8002aa0c` |
| fd0eb5ae | `sceNpSignalingDeactivateConnection` | 00e0dd74 | `f_00dda7f8` :2458 | 3 | `0x8002a80e` |
| fe37a7f4 | `sceNpManagerGetNpId` | 00e0dd78 | `f_00dda818` :2468 | 5 | `0x8002aa0c` |

Note the two `CreateCtx` calls succeed offline, so NP-facing code gets past context
creation and only fails at the first real request.

### N1.5 Which layer calls which import family

From the call-site lists in N1.2–N1.4 (the `sites` columns plus `netstub.py` callers):

| import family | callers live at |
|---|---|
| `sys_net` | `0x0024b-0x00253` and `0x0029a-0x0029d` (Dantelion2 transport: `PS3UDPSocket`, `UDPP2PSocket`, `DLNetworkServiceImpl_PS3`), `0x0034a-0x0035` , `0x0032b/0x0032b3` (`inet_addr`/`CmpNpId` helpers), `0x0090`, `0x00d3` |
| `sceNp` | `0x0032b`, `0x0034`, `0x0035`, `0x0036`, `0x0037`, `0x0090`, `0x00d3` — the NexusRevolution `NRSession*` layer and the menu/score UI |
| `cellNetCtl` | `0x0034b-0x0037f`, `0x0090`, `0x00d3` |

**No `sceNp`, `cellNetCtl` or `sys_net` import is called from the AC netcode ranges**
(`0x005b08-0x005bd5`, `0x00a65-0x00a68`, `0x00b7d-0x00b8f`). The game-side netcode
(`Sw/Source/Network/*`) reaches the wire only through Dantelion2 socket objects.

---

## N2. SystemLink (LAN) path

### N2.1 The strings are UI screen names, not netcode entry points

```
0x1004ca60 "SessionSystemLink"   stored in table cell 0x00ee87c8
0x1004caa8 "SystemLinkMatch"     stored in table cell 0x00ee87dc
0x1004ca67 "SystemLink"          (no pointer to it in the image)
```

The table at `0x00ee87b8`–`0x00ee87f8` is a contiguous block of *online-menu screen /
widget names*, and it is the only thing that references them:

```
0x00ee87b8 LobbyBack        0x00ee87d0 TopSelect
0x00ee87c0 SessionSearch    0x00ee87d4 OnlineSelect
0x00ee87c4 SessionCreate    0x00ee87d8 OnlineMatch
0x00ee87c8 SessionSystemLink 0x00ee87dc SystemLinkMatch
0x00ee87e0 LobbyClient     0x00ee87e8 SettingPopup   0x00ee87f4 Wait
0x00ee87e4 LobbyHost       0x00ee87ec ReadyClient    0x00ee87f8 LineHelp
                            0x00ee87f0 ReadyHost
```

So `SystemLinkMatch` is a *menu element* in the same list as `OnlineMatch`. There is no
function in the image whose code reads these literals directly (`netrefs.py` on both
VAs → 0 refs from code). **The system-link session logic is not keyed by these strings.**

### N2.2 What the LAN/UDP path actually is

Verified pieces of the transport, in call order (all from the `sys_net` call sites):

1. **UDP socket creation, broadcast enabled, non-blocking** — `f_00250640`
   (`ppu_gen_024.cpp:3122-3161`):
   `sys_net_socket(2, 2, 0)` (AF_INET, SOCK_DGRAM), fd stored at `this+36`, then
   `setsockopt(fd, 0xffff, 32, &1, 4)` (broadcast) and
   `setsockopt(fd, 0xffff, 0x1100, &1, 4)`.
2. **UDP-P2P socket creation** — `f_0029aaf4` (`ppu_gen_027.cpp:6282-6321`):
   `sys_net_socket(2, 6, 0)` — **type 6 = `SOCK_DGRAM_P2P`**, fd at `this+36`, then the
   same two `setsockopt`s.
3. **Bind to a fixed port 3658 plus a virtual port** — `f_0029ade8`
   (`ppu_gen_027.cpp:6490-6504`):

```c
:6492   R(0)=u64(s64(16));                    // sockaddr len
:6494   R(9)=u64(s64(3658));                  // PORT
:6495   w8((u32(R(1))+112u),u8(R(0)));        // sa.len  = 16
:6498   w32((u32(R(1))+120u),u32(R(11)));     // sa+8  = 0
:6499   w32((u32(R(1))+124u),u32(R(11)));     // sa+12 = 0
:6500   w32((u32(R(1))+116u),u32(R(29)));     // sa+4  = addr
:6501   w16((u32(R(1))+114u),u16(R(9)));      // sa+2  = 3658
:6502   w8((u32(R(1))+113u),u8(R(0)=2));      // sa+1  = AF_INET
:6503   w16((u32(R(1))+120u),u16(R(28)));     // sa+8  = VPORT  <-- sockaddr_in_p2p
:6504   ctx.lr=0x0029ae80u;f_00dd9d38(ctx);   // sys_net_bind
```

   `+8..+9` holding a second 16-bit value *after* `addr` is the `sockaddr_in_p2p`
   extension (`vport`), matching `UDPP2PSocket.cpp` in the ELF's path list.
4. **Send** — `f_0025276c` (`ppu_gen_024.cpp:5327-5345`): the 16-byte sockaddr is a plain
   `memcpy` of a prepared `sockaddr_in` taken from `f_002957a4`'s result, then
   `sys_net_sendto(fd = this+36, buf, len, flags = 0, &sa, 16)` — **blocking, flags 0**.
   The P2P twin is `f_0029c188` (`ppu_gen_027.cpp:7828-7836`), same shape.
5. **Receive + poll** — `sys_net_recvfrom` sites `f_0025188c`, `f_0029c8d4` and
   `sys_net_socketpoll` sites `f_00253168`, `f_0029d3e8` (see N3).

### N2.3 Does the LAN path need `sceNp`?

Answer I can prove: **the data path needs only `sys_net`.** Every `sendto`/`recvfrom`/
`socket`/`bind`/`setsockopt` site is in `0x0024…`/`0x0029…` (Dantelion2), and none of the
`sceNp` or `cellNetCtl` imports is called from those functions (N1.5). **`sceNp` is only
reached from the NexusRevolution session layer** (`0x0034…`–`0x0037…`), which is also the
only caller of `cellNetCtlNetStartDialog*` (`f_00351894`, `f_0036a67c`, `f_0095cf88`) —
consistent with the existence of `NRSessionManagerRequestNetStartDialog.cpp`.

**NOT DETERMINED:** whether choosing the LAN/system-link option in the menu enters the
NexusRevolution session code (and therefore `sceNp`) or skips it. I could not find a code
path selected by `SystemLinkMatch`/`SessionSystemLink`/`SessionCreate` (N2.1), and the
NexusRevolution session objects are virtual-dispatched (`f_0029ade8` has no direct
callers), so the host/search/join ordering is not statically visible from call sites.
Consequently I also cannot state that LAN needs no `sceNp` — only that the wire path does not.

Also **NOT DETERMINED**: any `sendto` to `255.255.255.255`. No broadcast literal exists in
the image (`inet_addr` is only ever fed caller-supplied strings — `f_0024b324`,
`f_0024b3d4`, `f_003250f0`, `f_00325274`, `f_003252b4`, `f_003252f4`), and the send
sockaddrs are copies of prepared `sockaddr_in`s. Broadcast *is* enabled
(`SO_BROADCAST`-style `setsockopt` N2.2 item 1/2), which is the only in-file evidence of
broadcast use.

---

## N3. Socket-level argument values

| import | site | arguments as written in the gen code | interpretation |
|---|---|---|---|
| `sys_net_socket` | `f_0024e0b4` `ppu_gen_024.cpp:535-544` | `R(3)=2, R(4)=1, R(5)=0` | AF_INET, **SOCK_STREAM**, 0 → then `bind(fd,&sa,16)` `:549`, `listen(fd, backlog)` `:560` (backlog = arg, default `5` `:553`) |
| `sys_net_socket` | `f_00250640` `ppu_gen_024.cpp:3122-3129` | `2, 2, 0` | AF_INET, **SOCK_DGRAM**, 0; fd → `this+36` |
| `sys_net_socket` | `f_0029aaf4` `ppu_gen_027.cpp:6282-6289` | `2, 6, 0` | AF_INET, **SOCK_DGRAM_P2P (6)**, 0; fd → `this+36` |
| `sys_net_socket` | `f_0034b9f8` `ppu_gen_034.cpp:18421` | args set earlier in the function | see note below |
| `sys_net_socket` | `f_0034bba8` `ppu_gen_034.cpp:18538` | args set earlier in the function | see note below |
| `sys_net_bind` | `f_0024e0b4` `ppu_gen_024.cpp:541-549` | `sa.len(?)`, `sa+1 = 2`, `sa+2 = arg4 (port)`, `sa+4 = arg5 (addr)`, `len=16` | listener bind, port/addr from the caller |
| `sys_net_bind` | `f_00250934` `ppu_gen_024.cpp:3331-3342` | `sa.len=16`, `sa+1=2`, `sa+2=R(28)`, `sa+4=R(29)`, `sa+8=sa+12=0`, `len=16` | UDP bind, port from caller |
| `sys_net_bind` | `f_0029ade8` `ppu_gen_027.cpp:6490-6504` | **port 3658**, `sa+8 = vport`, family 2, `len=16` | UDPP2P bind (see N2.2) |
| `sys_net_bind` | `f_0034b9f8` `ppu_gen_034.cpp:18429-18443` | zeroed 16-byte sa, `sa+1=2`, port from `f_00329824`+`r16`, `len=16` | port from a config/string parse |
| `sys_net_bind` | `f_0034bba8` `ppu_gen_034.cpp:18549-18563` | `sa+0=16`, `sa+1=2`, `sa+2=port`, `sa+4=0` | same |
| `sys_net_setsockopt` | `f_00250640` `ppu_gen_024.cpp:3140-3149` | `R(4)=(-1)&0xffff`→`0xffff`, `R(5)=32`, `R(6)=&1`, `R(7)=4` | level `0xffff` (SOL_SOCKET), opt `32`, value 1 |
| `sys_net_setsockopt` | `f_00250640` `ppu_gen_024.cpp:3154-3161` | `0xffff`, `4352 (=0x1100)`, `&1`, `4` | non-blocking-style option |
| `sys_net_setsockopt` | `f_0029aaf4` `ppu_gen_027.cpp:6300-6309` | `0xffff`, `32`, `&1`, `4` | P2P twin of the above |
| `sys_net_setsockopt` | `f_0029aaf4` `ppu_gen_027.cpp:6314-6321` | `0xffff`, `0x1100`, `&1`, `4` | P2P twin |
| `sys_net_setsockopt` | `f_0024f194` `ppu_gen_024.cpp:1815-1823` | `level=r32(sp+116)`, `opt=r32(sp+112)`, `val=R(26)`, `len=r32(R(28))` | **pass-through wrapper** (no constants) |
| `sys_net_setsockopt` | `f_0029a500` `ppu_gen_027.cpp:5846-5855` | same pass-through shape | wrapper |
| `sys_net_setsockopt` | `f_0034bba8` `ppu_gen_034.cpp:18565-18575` | `level = 6`, `opt = 1`, `val = &1`, `len = 4` | level 6 / opt 1 |
| `sys_net_setsockopt` | `f_0034e314` `ppu_gen_034.cpp:21190-21199`, `:21238-21245`, `:21677-21686`, `:21728-21735` | `0xffff`, `32`, value 1 or a stored flag | broadcast, 4 sites |
| `sys_net_sendto` | `f_0025276c` `ppu_gen_024.cpp:5331-5345` | `fd=this+36`, `len=R(28)`, **`flags=0`** `:5334`, `sa=sp+128` (16-byte memcpy), `alen=16` `:5337` | UDP send, **blocking** |
| `sys_net_sendto` | `f_0029c188` `ppu_gen_027.cpp:7828-7836` | same, `alen=16`, flags set earlier | P2P send |
| `sys_net_sendto` | `f_0034a00c` `ppu_gen_034.cpp:16728-16736` | `fd=R(4)`, `buf=R(5)`, `len=R(29)`, `alen=16` | session-layer send |
| `sys_net_sendto` | `f_0034c550` `ppu_gen_034.cpp:19171-19179` | `fd=r32(R(9)+176)`, `buf=R(9)`, `len=128`, `sa=R(9)+160`, `alen=16` | fixed 128-byte datagram |
| `sys_net_sendto` | `f_0034d9c4` `ppu_gen_034.cpp:20496-20504` | `len=128`, `alen=16` | fixed 128-byte datagram |
| `sys_net_sendto` | `f_00351008` `ppu_gen_034.cpp:24047-24055` | `fd=r32(R(9)+176)`, `len=128`, `sa=R(9)+160`, `alen=16` | fixed 128-byte datagram |

The two `f_0034b9f8`/`f_0034bba8` socket types are the only ones whose type/protocol
operands are not literal in the same basic block; I did not trace their register
provenance, so their family/type are **NOT DETERMINED**.

Names of the option numbers are *not* in the binary; `0xffff` as level together with
`32`/`0x1100` matches the PS3 `SOL_SOCKET` / `SO_BROADCAST` / `SO_NBIO` numbering, but the
file contains only the numbers. For a WinSock implementation the numbers themselves are
the requirement: a `setsockopt(s, 0xffff, 32, &one, 4)` must map to
`SO_BROADCAST`, and `0x1100` to non-blocking (or the port must make the socket
non-blocking another way, since nothing else in the code sets it).

`recvfrom`/`socketpoll` sites (addresses only; their argument values are register-derived
inside the same functions and I did not decode each one):
`recvfrom` → `f_0025188c`, `f_0029c8d4`, `f_0034a0d4`, `f_0034c3b0`, `f_0034c480`,
`f_0034fc14`, `f_0029c8d4`; `socketpoll` → `f_00253168`, `f_0029d3e8`;
`getsockname` → `f_0024e544`, `f_0024ec7c`, `f_0029924c`, `f_00299a28`;
`getsockopt` → `f_0024ea34`, `f_00299760`; `shutdown` → `f_0024dda8`, `f_0024e330`,
`f_0024e3d8`, `f_0024e480`.

Transport class vtables found (run-tested): the listener/socket object =
**`0x10105358`** (8 slots; slot 0 = `f_0024e0b4`), the UDP-P2P socket object =
**`0x10118f70`** (18 slots; slot 2 = `f_0029ade8`, slot 3 = `f_0029b208`, slot 6 =
`f_0029d3e8` = one of the two `socketpoll` users). I did not prove which source file of the
Dantelion2 transport list each of these two classes comes from.

---

## N4. AC replication (`AcNetSendBehavior` / `AcNetReceiveBehavior` / `AcNetBehaviorFactory`)

### N4.0 Source file → code ranges

The `n:/Ac4/data/source/…` literals are referenced from a **per-TU descriptor record**
(one `u32` pointer per source file, in `0x00e0xxxx`/`0x00f03xxx`), and each TU's functions
reference that record. Resolving that chain (`nettu.py`, `netrefs.py`) gives:

| source file | descriptor | code range (verified function addresses) |
|---|---|---|
| `Network/Behavior/AcNetBehaviorFactory.cpp` | `0x00f032d0` | `0x00b7df80`, `0x00b7e050`, `0x00b7e194`, `0x00b7e564`, `0x00b7e644` |
| `Network/Behavior/AcNetReceiveBehavior.cpp` | `0x00f033c8` | `0x00b7ef1c`, `0x00b7f04c`, `0x00b80460`, `0x00b840fc`, `0x00b843cc` |
| `Network/Behavior/AcNetSendBehavior.cpp` | `0x00f03548` | `0x005b0828`, `0x005b1d9c`, `0x005b3724`, `0x005b3c1c`, `0x005b413c`, `0x005b4570`, `0x005b5ebc`, `0x005b6cfc`, `0x005b7b1c`, `0x005b898c`, `0x005b97ac`, `0x005ba5e4`, `0x005bb53c`, `0x005bd5e8` (19 functions, 101 descriptor refs) |
| `Network/Scene/NetVsSortieScene.cpp` | `0x00f03774` | `0x00b871d0`, `0x00b87210`, `0x00b8724c`, `0x00b872b0`, `0x00b873ec`, `0x00b87474`, `0x00b874e0`, `0x00b87b0c`, `0x00b89420`, `0x00b89a14`, `0x00b89c54`, `0x00b89db4` |
| `Network/Weapon/AcNetWeaponFactory.cpp` | `0x00f03784` | `0x00b89ed0`, `0x00b89fec` |
| `Network/Ac/AcNetAction.cpp` | `0x00f04584` | `0x00a65590`, `0x00a656e4`, `0x00a66758`, `0x00a66e30`, `0x00a6778c`, `0x00a67ab8` |

### N4.1 (a) The classes and their vtables

Real vtables (≥4 consecutive OPD pointers, `vtidx.py`):

| vtable | slots | identifying slots | TU |
|---|---|---|---|
| `0x10148b78` | 20 | slot 0 `0x00b7e050`, slot 6 `0x00b7e194`, slot 16 `0x00b7e644`, slot 18 `0x00b7e564`, slot 19 `0x00b7df80`; slots 1-5,7-15,17 are `0x009e9xxx/0x009exxxx` (base class) | `AcNetBehaviorFactory.cpp` |
| `0x10148c68` | 56 | slot 44 `0x00b7ef1c` (the only slot from this TU) | `AcNetReceiveBehavior.cpp` |
| `0x10149138` | 5 | slot 0 `0x00b89ed0`, slot 1 `0x00b89fec` | `AcNetWeaponFactory.cpp` |
| `0x101491d8` | 33 | slot 5 `0x00ba46b0` | `AcNetAction.cpp` family |
| `0x101490c8` | 25 | slot 0 `0x00b89db4`, slot 1 `0x00b89a14`, slot 22 `0x00b89420` | `NetVsSortieScene.cpp` |

* `AcNetBehaviorFactory`: slot 0 = `f_00b7e050` (`ppu_gen_123.cpp:11105-11190`, the
  conventional ctor slot) and the workhorse is slot 16 = `f_00b7e644`
  (`ppu_gen_123.cpp:11507-11809`, 303 lines); slot 6 = `f_00b7e194`, slot 18 =
  `f_00b7e564`, slot 19 = `f_00b7df80`.
* `AcNetReceiveBehavior` has a **56-slot vtable** and only one method from its own TU in
  it (`f_00b7ef1c`, slot 44) — i.e. it is a small override on a large base class.
* The `AcNetSendBehavior` functions are in **no** run-tested vtable. Either its vtable is
  a run shorter than 4 OPD pointers, or its methods are reached through the base class's
  vtable. **NOT DETERMINED** which.

**(a) Where is the factory called from?** **NOT DETERMINED.** I could not show that
`f_00b7e050`/`f_00b7e644` are reached from `f_00a6e120` (the generic AC body creator) nor
from the `NetVsSortieScene` code: `AcNetBehaviorFactory`'s methods are vtable slots
(slot 0/6/16/18), and I found no direct call site from `0x00a6e1xx` or `0x00b8xxxx`.
What *is* verified: `NetVsSortieScene`'s big method `f_00b89420` builds 16-byte strings
from the names **`"Ac"`, `"SendAc"`, `"LANAc"`, `"NetAc"`** and passes them to
`f_00d7dd28`, i.e. object creation there is **keyed by name**, not by a class pointer:

```c
ppu_gen_123.cpp:23578  f_00af6bd0(dest, R(1)+220, 16);        // build 16-byte name buffer
:23581  R(4)=r32((u32(R(2))+-5872u));                          // cell 0xf03768 = "SendAc"
:23585  ctx.lr=0x00b89934u;f_00af4ec0(ctx);                    // assign
:23589  ctx.lr=0x00b89944u;f_00d7dd28(ctx);                    // <-- create by name
:23607  R(4)=r32((u32(R(2))+-5868u));                          // 0xf0376c = "LANAc"
:23630  R(4)=r32((u32(R(2))+-5864u));                          // 0xf03770 = "NetAc"
```

The same four names are used from `0x00b8e4b4`, `0x00a51158`, `0x00a517c4`
(`ppu_gen_124/110`) via cells `0x00ef4980/84/88`.

### N4.2 (b) What selects "local (send)" vs "remote (receive)"?

**NOT DETERMINED as an argument/field.** What the files show:

* The discriminator vocabulary is the name set `"Ac"` / `"SendAc"` / `"LANAc"` / `"NetAc"`
  (`0x1006fd88`, `0x1006fd90`, `0x1006fd98`, `0x1006fda0`), used as *creation keys* by
  `f_00b89420` (N4.1) — so a behaviour is chosen by name at creation time.
* `f_00b89420` chooses between them by comparing a value (`f_00af4ec0` on a 16-byte
  buffer; the three branches at `ppu_gen_123.cpp:23569`, `:23595`, `:23619` each set
  `R(19)` to `1`, `0`, and a third value, and each also stores `-1` into a field at
  `this+20` in one branch, `ppu_gen_123.cpp:23593`).
* I did **not** find any parameter of `f_00a6e120` or any scene field documented as
  "player index / operator / is-remote". The scene object built by `f_00b89a14`/
  `f_00b89db4` is ~`0x10000` bytes (sub-objects at `this+0xf950`, `+0xfa08`, `+0xfa10`,
  `+0xfa88`, see `ppu_gen_123.cpp:23652-23669`) but which of those holds the local-player
  flag is **NOT DETERMINED**.

### N4.3 (c) What the send behaviour serialises, and at what rate

**NOT DETERMINED.** I located the send TU (`0x005b0828`–`0x005bd5e8`, 19 functions) and
its largest member `f_005bb53c` (35 source-descriptor references — the assert-dense one,
`ppu_gen_061.cpp:2304`), but I did not complete a stream-write inventory, so I cannot
name the serialised fields or the send rate/timer constant. No claim is made here.

---

## N5. The scene

### N5.1 Three distinct scenes, all verified by vtable

| vtable | slots | slot 0 (ctor) | slot 5 | source file |
|---|---|---|---|---|
| `0x1013c010` | 25 | `0x00812128` | **`0x00812238`** | `Sw/Source/Scene/VsSortieScene.cpp` |
| `0x10131e08` | 25 | `0x00755884` (slot 1 `0x0075592c`) | **`0x00753e00`** | `Sw/Source/Scene/ArenaSortieScene.cpp` |
| `0x101490c8` | 25 | `0x00b89db4` (slot 1 `0x00b89a14`) | **`0x00b874e0`** | `Sw/Source/Network/Scene/NetVsSortieScene.cpp` |

* `0x1013c010` **is** `VsSortieScene`: `vtidx.py --dump` gives slot 0 = `0x00812128`,
  slot 5 = `0x00812238`, matching notes 17 (`:190-199`) and 20 (`:274-275,365`).
* **The vtable 0x10131e08 that note 20 left unnamed is `ArenaSortieScene`, not
  `NetVsSortieScene`.** Proof: the only TU whose functions reference the cell holding
  `0x10131e08` (`0x00ee3f94`) is `f_007556b4` (`ppu_gen_078.cpp:11268`), and
  `f_007556b4` belongs to the `Sw/Source/Scene/ArenaSortieScene.cpp` descriptor
  (`nettu.py`). It shares slots 4, 9, 12, 13 and 14-19 with `VsSortieScene`, i.e. it is a
  `VsSortieScene` subclass — consistent with note 20 (`:366`) that its Init `0x00753e00`
  calls `f_00812238` directly.
* `NetVsSortieScene` is a **third, separate** class: its TU (`0x00b871d0`–`0x00b89db4`)
  contains the ctor pair that are slots 0/1 of `0x101490c8`, and `0x101490c8` also
  inherits the `VsSortieScene` slots (4, 8, 11, 12, 13, 14-20) while overriding
  0, 1, 5, 6, 7, 9, 10, 21-24.

### N5.2 NetVsSortieScene's constructor installs *VsSortieScene's* vtable last

```c
ppu_gen_123.cpp:23878  R(0)=r32((u32(R(2))+-5856u));   // cell 0xf03778 = 0x101490c8
:23881  w32(u32(R(3)),u32(R(0)));                      // this+0 = 0x101490c8  (own vtable)
:23885  f_00b9f178(this+0x10000-1520);                 // this+0xfa10
:23889  R(0)=r32((u32(R(2))+-5852u));                  // cell 0xf0377c = 0x1013c010
:23890  w32(u32(R(28)),u32(R(0)));                     // this+0 = 0x1013c010  (VsSortieScene!)
```

Both `f_00b89a14` (`:23648-23660`) and `f_00b89db4` do this. Two vtable stores in that
order (own, then base) is the destructor shape, but the surrounding code also constructs
sub-objects, so **whether these are the ctor, the dtor, or a ctor/dtor thunk pair is NOT
DETERMINED**. A third and fourth member of the same family (`f_00b89af4`,
`f_00b89c54`) also read cell `0xf03778`.

### N5.3 NetVsSortieScene's "Init"

`0x101490c8` slot 5 (the slot notes 17/20 call `Init`) = **`f_00b874e0`**, and the big
386-line method is **slot 22 = `f_00b89420`** (`ppu_gen_123.cpp:23256-23641`). So the
network scene does *not* reuse `VsSortieScene::Init`; it overrides the Init slot.

### N5.4 What NetVsSortieScene's Init does beyond VsSortieScene::Init

Verified differences (from `f_00b874e0`/`f_00b89420` being different functions, and from
`f_00b89420`'s body): it creates objects by the name keys `"Ac"`, `"SendAc"`, `"LANAc"`,
`"NetAc"` (N4.1) and it builds a 16-byte name string before each `f_00d7dd28` call. The
full call list, the objects created and **how many local views it creates** are
**NOT DETERMINED** — I did not complete `f_00b874e0`.

---

## N6. Non-player entity replication (AI units, mission events, objects)

**No evidence found.**

What I searched and what it shows:

1. The complete set of network source files compiled into the image is exactly 6
   game-side files plus Dantelion2 transport (`nettu.py`/string inventory):
   `Network/Ac/AcNetAction.cpp`, `Network/Behavior/AcNetBehaviorFactory.cpp`,
   `Network/Behavior/AcNetReceiveBehavior.cpp`, `Network/Behavior/AcNetSendBehavior.cpp`,
   `Network/Scene/NetVsSortieScene.cpp`, `Network/Weapon/AcNetWeaponFactory.cpp`;
   then `Dantelion2/…/Fsdp/{FSDPInputStream,FSDPOutputStream,FSDPSequenceControl,
   FSDPServerSocket,FSDPSocket}.cpp`, `…/Udp/{UDPChannel,UDPHeader,UDPInputStream,
   UDPOutputStream}.cpp`, `DLNetworkServiceImpl.cpp`, `Ps3/DLNetworkServiceImpl_PS3.cpp`,
   `Ps3/PS3NetworkSystemDriver.cpp`, `Ps3/Udp/{PS3UDPSocket,PS3UDPSocketFactory}.cpp`,
   `Ps3/Udpp2p/{UDPP2PSocket,UDPP2PSocketFactory}.cpp`, and the NexusRevolution session
   files. **There is no `Network/…` file for enemies, AI, mission events, or world
   objects** — the only replicated game entities named anywhere are ACs
   (`AcNetAction`, `AcNetSend/ReceiveBehavior`, `AcNetWeaponFactory`).
2. Grepping the ELF for `NetEnemy`, `NetAI`, `NetUnit`, `NetObject`, `NetMission`,
   `NetEvent` yields nothing; the only `Net*` game-layer names are `NetAc`
   (`0x1006fda0`) and `AcNetAction.cpp`-family strings.
3. No `Sw/Source/Network/*` code calls any `sys_net`/`sceNp`/`cellNetCtl` import (N1.5),
   so nothing in the netcode touches the wire outside the Dantelion2 transport.

What this does **not** prove: the FSDP/UDP transport is entity-agnostic, so if some other
subsystem fed it streams, it could carry anything. I found no such caller.
**NOT DETERMINED:** whether `AcNetWeaponFactory` replication also covers projectiles
fired by AI (its two functions `f_00b89ed0`/`f_00b89fec` are in a 5-slot vtable; I did not
read their bodies).

---

## NOT DETERMINED (consolidated)

1. **N2** — whether the SystemLink/LAN menu path enters the NexusRevolution session layer
   (which is the only user of `sceNp`); therefore whether LAN *session setup* needs `sceNp`.
   Also: no `255.255.255.255` destination is present anywhere in the image.
2. **N2** — the exact host/search/join call ordering of a system-link match (session
   objects are virtual-dispatched; `f_0029ade8` has no direct callers).
3. **N3** — socket family/type for the two sites `f_0034b9f8` (`ppu_gen_034.cpp:18421`) and
   `f_0034bba8` (`:18538`); the operands are register-derived.
4. **N3** — argument values of the `recvfrom`, `socketpoll`, `getsockname`, `getsockopt`
   and `shutdown` call sites (only their addresses and enclosing functions are listed).
5. **N4(a)** — which function constructs `AcNetSendBehavior` / `AcNetReceiveBehavior`, and
   whether `AcNetBehaviorFactory` is reachable from the generic AC body creation
   (`f_00a6e120`) or only from `NetVsSortieScene`. The factory methods are vtable slots
   with no direct call sites found in either range.
6. **N4(a)** — where `AcNetSendBehavior`'s class vtable is (its TU functions are in no
   ≥4-entry OPD run).
7. **N4(b)** — the concrete argument/field that selects "local (pad operator + send)" vs
   "remote (receive)". Only the name-keyed vocabulary `Ac`/`SendAc`/`LANAc`/`NetAc` is
   verified.
8. **N4(c)** — the serialised field list and the send rate of `AcNetSendBehavior`.
9. **N5** — what `0x1013c080` is (it is stored next to `0x1013c010` in three cells but is
   not an OPD array: its first words are `0x00e7b678`, `0x00e7b680`, `0x00e89b58`, …).
10. **N5** — full call/creation list and local-view count of `NetVsSortieScene`'s Init
    (`f_00b874e0` / `f_00b89420`).
11. **N5** — whether `f_00b89a14`/`f_00b89db4` are constructors or destructors.

## Corrected prior findings

* Note 20 (`:365-366`) left the `VsSortieScene`-derived scene with vtable `0x10131e08`
  unnamed. It is **`ArenaSortieScene`** (`Sw/Source/Scene/ArenaSortieScene.cpp`;
  its TU contains `f_007556b4`, the only function that reads the cell `0x00ee3f94`
  holding `0x10131e08`).
* Note 17 (`:87-88`) says `NetVsSortieScene` "cannot be keyed by name". Correct, but the
  class is still locatable: its vtable is `0x101490c8`, its ctor slots are
  `0x00b89db4`/`0x00b89a14`, and its Init slot is `0x00b874e0` — all reachable through
  the `NetVsSortieScene.cpp` per-TU descriptor at `0x00f03774`.
