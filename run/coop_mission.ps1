# coop_mission.ps1 -- campaign runner for the second-player build (E7).
#
# RESTORED 15.11.2026: this file was deleted by mistake while tidying the co-op scripts in the
# following session (it had been shot 20261115-... E7/E8 era). This is a reconstruction of what it
# did -- its header and its documented purpose ("load the save, reach the hub, enter the highlighted
# mission, and stop in gameplay with the game still running so it can be driven; both pads owned from
# the first rig line") -- not the original bytes. For new work use run\coop_mission.py, which does the
# same job closed-loop and verifies every step; this script is kept so research\19's file table still
# resolves.
#
#   AC4_COOP=1 must be set by the caller (inherited by ac4.exe); AC4_TRACE_CREATE=1 / AC4_SURVEY_AC=1
#   are optional. Never touches SAVE GAME.
param(
  [int]$Seconds = 1800,
  [string]$Tag = "coop"
)
$ErrorActionPreference = 'Continue'
$base = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$rig = Join-Path $base 'run\rig\coop.txt'
$shots = Join-Path $base "run\shots\$Tag"

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $shots | Out-Null
Get-ChildItem -LiteralPath $shots -File -Filter 'shot_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
# Both pads owned from the first rig line: rewriting this file while the game runs drops the ports,
# so all later input goes through <shots>\cmd.txt.
@('0.0 0 none 0 0 0 0', '0.0 1 none 0 0 0 0') | Set-Content -LiteralPath $rig -Encoding ascii
Set-Content -LiteralPath (Join-Path $shots 'cmd.txt') -Value '' -Encoding ascii

$argList = @(
  '--data', (Join-Path $base 'run\data'), '--disc', (Join-Path $base 'disc'),
  '--flash', (Join-Path $base 'run\data\dev_flash'), '--safe-mode', '--windowed', '--fps', '60',
  '--hidden', '--seconds', "$Seconds", '--rig', $rig, '--rig-shots', $shots,
  '--rig-shot-every', '2', '--keep-runs', '6',
  '--test-overlay', (Join-Path $base 'payload\overlay')
)
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
Write-Host "AC4_COOP=$($env:AC4_COOP) AC4_TRACE_CREATE=$($env:AC4_TRACE_CREATE) AC4_SURVEY_AC=$($env:AC4_SURVEY_AC)"
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}
Write-Host "pid $($game.Id); drive it with: python -u run\coop_mission.py --tag $Tag"
