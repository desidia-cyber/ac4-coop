# coop_launch.ps1 -- start one ac4.exe instance for the co-op work.
#
#   .\coop_launch.ps1 -Tag m1 [-Data run\data] [-Seconds 900] [-NoCoop]
#
# Two instances can run at once (milestone M3): each needs its own -Data and its own -Tag (rig shots,
# clock.txt, cmd.txt and the run log all live under those). Nothing here touches SAVE GAME, the disc
# or <data>\hdd0's save data.
#
# Environment variables for the instance are taken from the caller's environment (AC4_COOP,
# AC4_COOP_DUMP, AC4_COOP_POKE_FILE, AC4_COOP_HOST, AC4_COOP_JOIN, AC4_COOP_FREEZE, AC4_COOP_NOSNAP,
# AC4_COOP_ENTS, AC4_COOP_METER, AC4_SURVEY_AC, ...), so set them before calling this script. AC4_COOP
# defaults to 1 here because that is what the co-op runs need.
param(
  [Parameter(Mandatory = $true)][string]$Tag,
  [string]$Data = "run\data",
  [int]$Seconds = 900,
  [double]$ShotEvery = 2,
  [switch]$NoCoop,
  [switch]$Visible,
  [string]$Rig = "rig\coop.txt",
  [string]$Extra = "",
  [string]$Overlay = "payload\overlay",
  [switch]$Req
)
$ErrorActionPreference = 'Continue'
$base = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$shots = Join-Path (Join-Path $base 'run\shots') $Tag
$dataAbs = if ([System.IO.Path]::IsPathRooted($Data)) { $Data } else { Join-Path $base $Data }
$rigAbs = if ([System.IO.Path]::IsPathRooted($Rig)) { $Rig } else { Join-Path $base (Join-Path 'run' $Rig) }
$ovlAbs = if ([System.IO.Path]::IsPathRooted($Overlay)) { $Overlay } else { Join-Path $base $Overlay }

if (-not (Test-Path $exe)) { throw "no ac4.exe at $exe" }

New-Item -ItemType Directory -Force -Path $shots | Out-Null
# The rig's screenshot names restart at shot_000 each launch, so a stale directory makes a new frame
# look like an old one. Clear it, keeping cmd.txt out until after the clear.
Get-ChildItem -LiteralPath $shots -File -Filter 'shot_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $rigAbs) | Out-Null
# A static rig script that owns both pads and never changes while the game runs: input goes through
# <shots>\cmd.txt, because rewriting the --rig file reloads it and drops the owned ports.
@('0.0 0 none 0 0 0 0', '0.0 1 none 0 0 0 0') | Set-Content -LiteralPath $rigAbs -Encoding ascii
Set-Content -LiteralPath (Join-Path $shots 'cmd.txt') -Value '' -Encoding ascii
Remove-Item -LiteralPath (Join-Path $shots 'poke.txt') -ErrorAction SilentlyContinue

if ($NoCoop) { Remove-Item Env:AC4_COOP -ErrorAction SilentlyContinue } elseif (-not $env:AC4_COOP) { $env:AC4_COOP = '1' }

$argList = @(
  '--data', $dataAbs, '--disc', (Join-Path $base 'disc'),
  '--flash', (Join-Path $dataAbs 'dev_flash'), '--safe-mode', '--windowed', '--fps', '60',
  '--seconds', "$Seconds", '--rig', $rigAbs, '--rig-shots', $shots,
  '--rig-shot-every', "$ShotEvery", '--keep-runs', '6',
  '--test-overlay', $ovlAbs
)
# AC4_COOP_REQ: the entity-table instrument reads <shots>\req.txt once a second (coop_entities.cpp).
# The file starts empty, so nothing happens until a driver writes a command into it.
if ($Req) {
  Set-Content -LiteralPath (Join-Path $shots 'req.txt') -Value '' -Encoding ascii
  $env:AC4_COOP_REQ = Join-Path $shots 'req.txt'
}
if (-not $Visible) { $argList += '--hidden' }
if ($Extra) { $argList += $Extra.Split(' ') }
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '

$envNames = 'AC4_COOP', 'AC4_COOP_DUMP', 'AC4_COOP_POKE_FILE', 'AC4_COOP_POS_OFFS', 'AC4_COOP_VEL_OFFS',
            'AC4_COOP_HOST', 'AC4_COOP_JOIN', 'AC4_SURVEY_AC', 'AC4_TRACE_CREATE',
            'AC4_COOP_REQ', 'AC4_COOP_ENTS', 'AC4_COOP_METER', 'AC4_COOP_FREEZE', 'AC4_COOP_NOSNAP'
$shown = ($envNames | ForEach-Object { if (Test-Path "Env:$_") { "$_=$((Get-Item "Env:$_").Value)" } }) -join ' '
Write-Host "[$Tag] $shown"
Write-Host "[$Tag] data $dataAbs shots $shots"

$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Normal } catch {}
Write-Host "[$Tag] pid $($game.Id)"
