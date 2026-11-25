# Launch Armored Core 4 in 2-Player Campaign Co-op (HOST mode)
# Usage:
#   .\play_coop_host.ps1 [-Port 7777] [-Data run\data] [-Disc disc] [-Fps 60] [-Windowed]
#   .\play_coop_host.ps1 -Steam [-Data run\data] [-Disc disc] [-Fps 60] [-Windowed]
param(
  [int]$Port = 7777,
  [switch]$Steam,
  [string]$Data = "run\data",
  [string]$Disc = "disc",
  [string]$Fps = "60",
  [switch]$Windowed
)
$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$exe = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$exeDir = Split-Path -Parent $exe
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
  Write-Host "=== Armored Core 4 — 2-Player Co-op [HOST | Steam Spacewar] ===" -ForegroundColor Cyan
  Write-Host "Transport   : Steamworks P2P (AppID 480 Spacewar — no IPs or port forwarding needed)"
  Write-Host "Lobby       : Friends-only lobby ID will be copied to clipboard & shown in window title"
} else {
  Write-Host "=== Armored Core 4 — 2-Player Co-op [HOST] ===" -ForegroundColor Cyan
  Write-Host "Listening on UDP port $Port (allow UDP/$Port through Windows Firewall if playing over LAN/Internet)."
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
  $argsList += '--coop-steam-host'
} else {
  $argsList += @('--coop-host', "$Port")
}
if ($Windowed) { $argsList += '--windowed' }

& $exe @argsList
