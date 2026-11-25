# Launch Armored Core 4 in 2-Player Campaign Co-op (JOIN mode)
# Usage:
#   .\play_coop_join.ps1 -HostAddr 192.168.1.10 [-Port 7777] [-Data run\data] [-LocalTest] [-Windowed]
#   .\play_coop_join.ps1 -Steam [-LobbyId <id>] [-Data run\data] [-Windowed]
param(
  [string]$HostAddr = "",
  [int]$Port = 7777,
  [switch]$Steam,
  [string]$LobbyId = "",
  [string]$Data = "",
  [string]$Disc = "disc",
  [string]$Fps = "60",
  [switch]$LocalTest,
  [switch]$Windowed
)
$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$exe = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$exeDir = Split-Path -Parent $exe

if (-not $Steam -and -not $HostAddr) {
  if ($LocalTest) {
    $HostAddr = "127.0.0.1"
  } else {
    $HostAddr = Read-Host "Enter Host IP address (press Enter for 127.0.0.1)"
    if (-not $HostAddr) { $HostAddr = "127.0.0.1" }
  }
}
if (-not $Data) {
  $Data = if ($LocalTest -or (-not $Steam -and $HostAddr -eq "127.0.0.1")) { "run\data2" } else { "run\data" }
}

$dataAbs = if ([System.IO.Path]::IsPathRooted($Data)) { $Data } else { Join-Path $base $Data }
$discAbs = if ([System.IO.Path]::IsPathRooted($Disc)) { $Disc } else { Join-Path $base $Disc }
$flashAbs = Join-Path $dataAbs 'dev_flash'
$overlayAbs = Join-Path $base 'payload\overlay_coop'

if (-not (Test-Path $exe)) { throw "ac4.exe not found at $exe. Run ac4-port\tools\build.ps1 -Config Release -Target ac4 first." }
if (-not (Test-Path $overlayAbs)) { throw "Co-op mission overlay not found at $overlayAbs. Run python tools\coop_patch.py first." }

if ($Steam) {
  $steamDll = Join-Path $exeDir 'steam_api64.dll'
  if (-not (Test-Path $steamDll)) {
    Write-Host "steam_api64.dll not found next to ac4.exe; obtaining at install time from Steamworks.NET..." -ForegroundColor Yellow
    & (Join-Path $base 'tools\install_steam_api.ps1') -OutDir $exeDir
  }
  Write-Host "=== Armored Core 4 — 2-Player Co-op [JOIN | Steam Spacewar] ===" -ForegroundColor Green
  if ($LobbyId) {
    Write-Host "Transport   : Steamworks P2P (joining lobby $LobbyId)"
  } else {
    Write-Host "Transport   : Steamworks P2P (auto-joining friend in Spacewar or waiting for Steam invite)"
  }
} else {
  Write-Host "=== Armored Core 4 — 2-Player Co-op [JOIN] ===" -ForegroundColor Green
  Write-Host "Connecting to Host at ${HostAddr}:${Port}"
}
Write-Host "Data folder : $dataAbs"
Write-Host "Mission pack: $overlayAbs"

$argsList = @(
  '--data', $dataAbs,
  '--disc', $discAbs,
  '--flash', $flashAbs,
  '--fps', $Fps,
  '--test-overlay', $overlayAbs,
  '--keep-runs', '8'
)
if ($Steam) {
  if ($LobbyId) {
    $argsList += @('--coop-steam-join', $LobbyId)
  } else {
    $argsList += @('--coop-steam-join', 'auto')
  }
} else {
  $argsList += @('--coop-join', "${HostAddr}:${Port}")
}
if ($Windowed) { $argsList += '--windowed' }

& $exe @argsList
