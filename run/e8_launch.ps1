# e8_launch.ps1 -- launch the port for E8 only (no scripted input; run\e8_run.py drives the menus).
#   The caller sets AC4_COOP / AC4_TRACE_CREATE / AC4_SURVEY_AC / AC4_COOP_SKIP; they are inherited.
# Both pads are owned by the rig from the first line of run\rig\coop.txt; in-game input goes through
# <shots>\cmd.txt ("<ms> p<0|1> <buttons> [lx ly rx ry]"), which does not reload the script.
# Never touches SAVE GAME.
param([int]$Seconds = 1800, [string]$Tag = "e8")
$ErrorActionPreference = 'Continue'
$base = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe  = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$rig  = Join-Path $base 'run\rig\coop.txt'
$shots = Join-Path $base "run\shots\$Tag"

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $shots | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force
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
Remove-Item Env:AC4_P2_AUTO  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_P2_UNIT  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_DUMP_STATE -ErrorAction SilentlyContinue
if (-not $env:AC4_SURVEY_AC) { $env:AC4_SURVEY_AC = '1' }
Write-Host "AC4_COOP=$($env:AC4_COOP) AC4_TRACE_CREATE=$($env:AC4_TRACE_CREATE) AC4_SURVEY_AC=$($env:AC4_SURVEY_AC) AC4_COOP_SKIP=$($env:AC4_COOP_SKIP)"
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}
while (-not $game.HasExited -and $Seconds -gt 0) { Start-Sleep -Seconds 5 }
Write-Host ("exited {0}" -f $game.HasExited)
