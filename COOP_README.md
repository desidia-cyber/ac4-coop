# Armored Core 4 — 2-Player Campaign Co-op Mod

Play the full *Armored Core 4* story campaign together with a friend over LAN or the Internet.

## Quick Start

### 1. Host (Player 1)
Double-click **`play_coop_host.bat`** (or run `.\play_coop_host.ps1 -Port 7777`).
- Listens on **UDP port 7777** by default.
- For **LAN**: give your friend your local IPv4 address (run `ipconfig`). Allow UDP/7777 in Windows Firewall if prompted.
- For **Internet**: either forward **UDP/7777** on your router, or connect both PCs via a mesh VPN such as **Tailscale**, **ZeroTier**, or **Radmin VPN** and give your friend your VPN IP.

### 2. Join (Player 2)
Double-click **`play_coop_join.bat`** and enter the Host's IP address (or run `.\play_coop_join.ps1 -HostAddr <HOST_IP> -Port 7777`).
- To test **two instances on the same PC**, pass `-LocalTest` (uses `run\data2` so the two instances keep separate saves and logs):
  ```powershell
  .\play_coop_host.ps1 -Windowed
  .\play_coop_join.ps1 -LocalTest -Windowed
  ```

### CLI Flags (Advanced)
`ac4.exe` directly supports:
- `--coop-host <port>` (e.g. `--coop-host 7777`)
- `--coop-join <ip:port>` (e.g. `--coop-join 192.168.1.50:7777`)
- Co-op mode automatically enables the second AC, freezes non-player local simulation on the joiner, and auto-detects `payload\overlay_coop` (you can still override with `--test-overlay <path>`).

---

## How Campaign Co-op Works

1. **Title Screen & Save Load:** Both players load their own saves independently and enter the **Mission Select** hub.
   - Check the window title bar for live co-op status (`you: menu | partner: menu`).
   - Both players can visit **Garage / Shop / Tuning / Schematics** freely at any time. While the joiner is in the Garage, the mod waits until the joiner returns to the Mission Select screen before launching the host's mission.
2. **Host-Led Mission Selection:** The **Host** picks any chapter, mission, and difficulty (Normal or Hard) and accepts it.
   - The **Joiner** automatically launches that exact same mission and difficulty — even if the joiner's own save has not unlocked that chapter yet.
3. **Real Partner AC Designs:** At the load barrier before the mission starts, both players automatically exchange their full 7,680-byte AC schematics (parts, weapons, tuning, paint, emblems) and load the partner's actual AC into the sortie (`ac_1`).
4. **Synchronized Start & Wing-to-Wing Spawn:** Both games wait at a load barrier until both players finish loading, then start the mission together with Player 2 spawned in a compact echelon formation (`+5 m` right, `-3 m` back) so both ACs clear tight indoor corridors and platforms.
   - If your partner disconnects during loading, the barrier times out cleanly after 20 s (or hold **ESC** for 1.5 s to start solo immediately).
5. **Gameplay & Combat:**
   - The **Host** is authoritative for all enemies, mission timers, dialogue, scripted events, and both players' **AP & Primal Armor (PA)**.
   - The **Joiner's** controller/keyboard inputs drive Player 2's AC on the Host (with automatic control-scheme sync so Host and Joiner can freely use different in-game `TYPE A` / `TYPE B` / `CUSTOM` layouts, plus reliable rising-edge button delivery for Weapon Switch/Purge/OB/AA), while the Host streams delta-culled enemy transforms/HP, instant reinforcement discovery, and scripted mission handlers back to the Joiner.
   - **Partner AP/PA HUD:** During gameplay, a compact top-left overlay shows your partner's live AP and Primal Armor bars (press **`F8`** to toggle).
   - All **39 campaign mission scripts** (`m000`–`m490`, `m650`, `m710`) are patched in `payload\overlay_coop`, **and** mission-ending calls from unpatched character scripts (`m080`, `m090`, `m200`, `m210`, `m220`, `m280`, `m380`, `m000`, `m010`, `m170`, `m310`) are replicated directly at the engine layer:
     - If **either** player dies or leaves the mission area (`OnDeadPlayer`, `mXXX_player.lua` death on Normal/Hard, `OnOutOfMissionArea`, `OnOutOfRegionAndTimeOut`), the mission fails cleanly for both players together.
     - Mission hazards (PA disable, altitude damage, PA supply), region triggers (`ObserveRegionExist`, `ObserveRegionIn`, `WatchEntityDistNearInfinity`, `GetDistEntityPair`), and `GetApRateOfAC` rank/dialogue checks apply across both players.
6. **Debriefing & Next Mission:** When the mission succeeds (or fails), both players proceed through Debriefing, earn rewards/rank on their own saves, return to the hub, and can immediately launch the next mission for as many consecutive missions as desired.

---

## Troubleshooting & Notes

- **Firewall / NAT:** All co-op traffic uses a single UDP port (default `7777`). If the title bar stays on `connecting to the host...`, check Windows Firewall or use Tailscale/ZeroTier.
- **Pause & Unpause:** Pressing `START` opens your local pause menu without pausing your partner's game (`START`/`SELECT` are masked on the remote pad so you never pause each other). When you unpause, your position and enemy state resync automatically with zero desync.
- **Mission Failed / Retry:** On `MISSION FAILED`, both players return cleanly to the Mission Select hub with the failed mission still selected so the Host can immediately re-sortie.
- **Host aborts or restarts mid-mission:** If the Host pauses and quits back to the hub — or clicks **Retry** — while the Joiner is still in the mission, an on-screen HUD banner and window title prompt the Joiner to pause and Retry or Quit to rejoin cleanly.
- **Partner on Minimap & HUD:** Your partner appears as a persistent, live-tracked green ally dot on the radar whenever they are within radar range (including while stationary at `0 km/h`), plus a live **Partner AP/PA** bar in the top-left corner (`F8` to hide/show).
- **New Game Prologue (`m160` *First Strike*):** After creating a callsign and playing/skipping the VR tutorial, the Host reaches Chapter 1 Hub (`FIRST STRIKE`) and can launch it directly in co-op with a Joiner on any chapter save.
- **Building from Source:**
  1. Prepare your own *Armored Core 4* (`BLUS30027`, disc 01.01) data and run the recompiler using `ac4-port`'s build scripts (`ac4-port\README.md`), passing `--hooks src/runtime/game/hooks.txt` to `ac4-recomp emit`.
  2. Build `ac4`:
     ```powershell
     cmake --build ac4-port\local\build\release --target ac4 --parallel 8
     ```
  3. Generate the patched co-op mission overlay (`payload\overlay_coop`) from your own disc files:
     ```powershell
     python tools\coop_patch.py
     ```
- **Architecture:** Host-authoritative state replication over WinSock UDP (`coop_net.cpp`, `coop_session.cpp`, `coop_build.cpp`, `coop_body.cpp`, `coop_entities.cpp`, `coop_hooks.cpp`). With no co-op flags or `AC4_COOP*` environment variables set, solo gameplay is completely untouched.
