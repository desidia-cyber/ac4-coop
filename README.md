# Armored Core 4 — 2-Player Campaign Co-op Mod (`ac4-coop`)

Full 2-player online/LAN host-led story campaign co-op mod for *Armored Core 4* (`BLUS30027`), built on top of [`ac4-port`](ac4-port/README.md).

See **[COOP_README.md](COOP_README.md)** for full setup, gameplay, and troubleshooting instructions, and **[HANDOFF.md](HANDOFF.md)** + **[research/](research/)** for architecture and reverse-engineering notes.

## Repository Layout

- **`ac4-port/`** — Full PC port & recompiler source code with the co-op runtime (`src/runtime/game/coop_*.cpp/.h`, `hooks.txt`, `input_hooks.cpp`, `frame_hooks.cpp`, `rig.cpp`).
- **`tools/`** — Mission script patcher (`coop_patch.py`, `bnd3tool.py`), install-time Steamworks.NET `steam_api64.dll` fetcher (`install_steam_api.ps1`), and reverse-engineering utilities (`tools/re/`).
- **`run/`** — Co-op launcher scripts and automated dual-instance rig verification scripts (`coop_launch.ps1`, `coop_host.ps1`, `coop_join.ps1`, `coop_mission.py`, `rig/`).
- **`play_coop_host.{bat,ps1}` / `play_coop_join.{bat,ps1}` / `play_coop_steam_{host,join}.bat`** — One-click Host & Join launchers (direct UDP or Steam Spacewar AppID 480).
- **`research/`** — Design docs and reverse-engineering findings (`*.md`).

> **Note:** No copyrighted game files, disc images, extracted assets, saves, or compiled binaries are included in this repository. Supply your own *Armored Core 4* (`BLUS30027`) disc to prepare `run/data`, generate PPU/SPU code via `ac4-recomp` / `ac4-spurecomp`, build `ac4.exe`, and generate `payload/overlay_coop` via `python tools/coop_patch.py`.

## Credits

- [AC4-Port](https://github.com/sulphurs16/ac4-port) by SulphurS16 — the unofficial native PC port and static recompiler this co-op mod is built on (`ac4-port/`).
