# One-command setup for the AC4 co-op mod. Run from the extracted release folder:
#   powershell -ExecutionPolicy Bypass -File setup.ps1 [-Disc <folder containing PS3_GAME>]
# Everything game-derived is generated on YOUR PC from YOUR copy of Armored Core 4 (BLUS30027).
param([string]$Disc = "")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

# 1. Game files: link/copy the user's disc folder to .\disc
if ($Disc) {
  if (-not (Test-Path (Join-Path $Disc "PS3_GAME"))) { throw "'$Disc' does not contain PS3_GAME." }
  if (-not (Test-Path "$root\disc")) { New-Item -ItemType Junction -Path "$root\disc" -Target (Resolve-Path $Disc) | Out-Null }
}
if (-not (Test-Path "$root\disc\PS3_GAME")) { throw "Game files not found. Re-run with -Disc <folder that contains PS3_GAME>." }

# 2. Python (needed by the mission patcher)
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { throw "Python 3 is required (https://www.python.org/downloads/)." }

# 3. Build the port + launcher (downloads the pinned toolchain once, ~250 MB)
Write-Host "== Building ac4.exe and the launcher ==" -ForegroundColor Cyan
& "$root\ac4-port\tools\bootstrap.ps1" -BuildOnly
& "$root\ac4-port\tools\build.ps1" -Config Release -Target ac4
if ($LASTEXITCODE -ne 0) { throw "ac4 build failed" }
& "$root\ac4-port\tools\build-launcher.ps1"

# 4. Generate the co-op mission overlay from your disc
Write-Host "== Patching mission scripts ==" -ForegroundColor Cyan
python "$root\tools\coop_patch.py"
if ($LASTEXITCODE -ne 0) { throw "coop_patch failed" }

Write-Host @"

Setup finished.
Last one-time step: run ac4-port\AC4-Launcher.exe (or .\AC4-Launcher.exe), choose your game folder,
click 'Prepare files' (needs your RPCS3 + PS3UPDAT.PUP) to create run\data.
Then play: play_coop_host.bat (host) / play_coop_join.bat (joiner). See COOP_README.md.
"@ -ForegroundColor Green
