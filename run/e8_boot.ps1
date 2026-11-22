# e8_boot.ps1 -- launch the port for E8 and drive it to the HUB with the proven hubcheck.ps1 timeline
# (boot-dialog burst, one START ~2 s into the first attract movie, one A for LOAD GAME).
#   The caller sets AC4_COOP / AC4_TRACE_CREATE / AC4_SURVEY_AC / AC4_COOP_SKIP; they are inherited.
# Leaves the game running at the hub with both pads owned by the rig (run\rig\coop.txt) so the caller
# drives the mission through run\e8_go.py and gameplay through cmd.txt.
# Never touches SAVE GAME.
param([int]$Seconds = 1800, [string]$Tag = "e8")
$ErrorActionPreference = 'Continue'
$base = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe  = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$rig  = Join-Path $base 'run\rig\coop.txt'
$shots = Join-Path $base "run\shots\$Tag"
$runs = Join-Path $base 'run\data\runs'

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

$clk = Join-Path $shots 'clock.txt'
$script:ok = $false
function Clock { try { $s=(Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim(); $script:ok=$true; return [double]$s } catch { return 0.0 } }
function WaitClock([double]$v,[int]$t=180000){ $e=(Get-Date).AddMilliseconds($t); while((Get-Date) -lt $e){ if((Clock) -ge $v){return $true}; if($game.HasExited){return $false}; Start-Sleep -Milliseconds 200 }; return $false }
function Press([string]$b,[double]$h=0.15,[double]$g=0.8){
  $x=(Clock)+$g
  for ($i=0; $i -lt 40; $i++) {
    try { Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} 0 0 0 0" -f $x,$b) -ErrorAction Stop } catch { Start-Sleep -Milliseconds 40; continue }
    try { Add-Content -LiteralPath $rig -Value ("{0:F2} 0 none 0 0 0 0" -f ($x+$h)) -ErrorAction Stop } catch { Start-Sleep -Milliseconds 40; continue }
    break
  }
  [void](WaitClock ($x+$h+0.3))
}
function NewestLog { $d=Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($d){Join-Path $d.FullName 'ac4.log'} }
function LogLines { $l=NewestLog; if(-not $l -or -not (Test-Path -LiteralPath $l)){return @()}; return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue) }
function Has([string]$p){ return [bool](LogLines | Select-String -Pattern $p -Quiet) }

$e=(Get-Date).AddSeconds(120); while(-not $script:ok -and (Get-Date) -lt $e -and -not $game.HasExited){ [void](Clock); Start-Sleep -Milliseconds 300 }
[void](WaitClock 13)
foreach($b in @('A','B','A+B','A','B','A')){ Press $b 0.15 2.4 }
Start-Sleep -Seconds 4
Press 'START' 0.15 1.0
Start-Sleep -Seconds 6
Press 'A' 0.15 1.0
Start-Sleep -Seconds 5
$hub=$false
for($i=0;$i -lt 60;$i++){ if(Has 'MissionSelectScene'){$hub=$true;break}; Start-Sleep -Seconds 1 }
Write-Host ("hub: {0} at clock {1}" -f $hub, [math]::Round((Clock),1))
while (-not $game.HasExited -and (Clock) -lt $Seconds) { Start-Sleep -Seconds 5 }
Write-Host ("done: exited {0} at clock {1}" -f $game.HasExited, (Clock))
