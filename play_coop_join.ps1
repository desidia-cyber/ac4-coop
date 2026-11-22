# Launch Armored Core 4 in 2-Player Campaign Co-op (JOIN mode)
# Usage:
#   .\play_coop_join.ps1 -HostAddr 192.168.1.10 [-Port 7777] [-Data run\data] [-LocalTest] [-Windowed]
param(
  [string]$HostAddr = "",
  [int]$Port = 7777,
  [string]$Data = "",
  [string]$Disc = "disc",
  [string]$Fps = "60",
  [switch]$LocalTest,
  [switch]$Windowed
)
$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$exe = Join-Path $base 'ac4-port\local\build\release\ac4.exe'

if (-not $HostAddr) {
  if ($LocalTest) {
    $HostAddr = "127.0.0.1"
  } else {
    $HostAddr = Read-Host "Enter Host IP address (press Enter for 127.0.0.1)"
    if (-not $HostAddr) { $HostAddr = "127.0.0.1" }
  }
}
if (-not $Data) {
  $Data = if ($LocalTest -or $HostAddr -eq "127.0.0.1") { "run\data2" } else { "run\data" }
}

$dataAbs = if ([System.IO.Path]::IsPathRooted($Data)) { $Data } else { Join-Path $base $Data }
$discAbs = if ([System.IO.Path]::IsPathRooted($Disc)) { $Disc } else { Join-Path $base $Disc }
$flashAbs = Join-Path $dataAbs 'dev_flash'
$overlayAbs = Join-Path $base 'payload\overlay_coop'

if (-not (Test-Path $exe)) { throw "ac4.exe not found at $exe. Run ac4-port\tools\build.ps1 -Config Release -Target ac4 first." }
if (-not (Test-Path $overlayAbs)) { throw "Co-op mission overlay not found at $overlayAbs. Run python tools\coop_patch.py first." }

Write-Host "=== Armored Core 4 — 2-Player Co-op [JOIN] ===" -ForegroundColor Green
Write-Host "Connecting to Host at ${HostAddr}:${Port}"
Write-Host "Data folder : $dataAbs"
Write-Host "Mission pack: $overlayAbs"

$argsList = @(
  '--data', $dataAbs,
  '--disc', $discAbs,
  '--flash', $flashAbs,
  '--fps', $Fps,
  '--coop-join', "${HostAddr}:${Port}",
  '--test-overlay', $overlayAbs,
  '--keep-runs', '8'
)
if ($Windowed) { $argsList += '--windowed' }

& $exe @argsList
