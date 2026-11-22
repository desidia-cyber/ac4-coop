# Launch the AC4 rig for versus-mode probing and LEAVE IT RUNNING.
# Hidden, silent, screenshots every 2 s, live command queue in <shots>\cmd.txt.
# Deliberately does NOT set AC4_P2_AUTO / AC4_P2_UNIT: the point of the experiment is to
# observe what the GAME ITSELF does with pad port 1, not what the P2 hook forces onto some unit.
param(
  [int]$Seconds = 1800,
  [string]$Tag = "vs",
  [double]$ShotEvery = 2,
  [string]$Overlay = ""
)
$ErrorActionPreference = 'Continue'
$B = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe   = Join-Path $B 'ac4-port\local\build\release\ac4.exe'
$shots = Join-Path $B "run\shots\$Tag"
$runs  = Join-Path $B 'run\data\runs'
$rig   = Join-Path $B 'run\rig\vs_live.txt'
if ([string]::IsNullOrWhiteSpace($Overlay)) { $Overlay = Join-Path $B 'payload\overlay' }

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $shots | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $rig) | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force

# STATIC script: this file is what makes the rig "own" a pad port (rig::pad returns false for a
# port that never appears in --rig). It must list BOTH ports, and must never be rewritten while the
# game runs, or the reload clears the owned-port list and every queued input is silently dropped.
Set-Content -LiteralPath $rig -Encoding ascii -Value @(
  '0 0 none 0 0 0 0',
  '0 1 none 0 0 0 0'
)
# Empty command queue: every command is appended live from the driving shell.
Set-Content -LiteralPath (Join-Path $shots 'cmd.txt') -Value '' -Encoding ascii

$argList = @(
  '--data',  (Join-Path $B 'run\data'),
  '--disc',  (Join-Path $B 'disc'),
  '--flash', (Join-Path $B 'run\data\dev_flash'),
  '--safe-mode', '--windowed', '--fps', '60',
  '--hidden', '--seconds', "$Seconds",
  '--rig',        $rig,
  '--rig-shots',  $shots,
  '--rig-shot-every', "$ShotEvery",
  '--keep-runs',  '6',
  '--test-overlay', $Overlay
)
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '

$env:AC4_SURVEY_AC = '1'
# explicitly clear the P2 hook so the observation is untainted
Remove-Item Env:AC4_P2_AUTO  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_P2_UNIT  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_DUMP_STATE -ErrorAction SilentlyContinue

Write-Host "launching: $exe"
Write-Host "args: $argLine"
Write-Host "shots: $shots"
Write-Host "cmd queue: $(Join-Path $shots 'cmd.txt')"

$g = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $g.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}

$clk = Join-Path $shots 'clock.txt'
$deadline = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $deadline) {
  if ($g.HasExited) { Write-Host "GAME EXITED early, code $($g.ExitCode)"; break }
  if (Test-Path -LiteralPath $clk) {
    $v = 0.0; try { $v = [double](Get-Content -LiteralPath $clk -Raw).Trim() } catch {}
    if ($v -gt 0) { Write-Host ("clock running, t={0}" -f $v); break }
  }
  Start-Sleep -Milliseconds 300
}
$latest = Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latest) { Write-Host "log: $(Join-Path $latest.FullName 'ac4.log')" }
Write-Host "game left RUNNING, pid $($g.Id)"
