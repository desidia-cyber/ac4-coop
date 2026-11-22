# trace_mission.ps1 -- campaign scenario for the player-creation trace: load the save, reach the hub,
# enter the highlighted mission and stop in gameplay. Same closed loop as hubcheck.ps1 +
# enter_mission.ps1, but with only the survey instruments the trace needs (no P2 hooks).
#   AC4_TRACE_CREATE=1 must be set by the caller (it is inherited by ac4.exe).
# Never touches SAVE GAME.
param([int]$Seconds = 1200, [string]$Tag = "traceMission", [int]$Slot = 1)
$ErrorActionPreference = 'Continue'
$base = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe  = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$rig  = Join-Path $base 'run\rig\live.txt'
$shots = Join-Path $base "run\shots\$Tag"
$runs = Join-Path $base 'run\data\runs'

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $shots | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force
@('0.0 0 none 0 0 0 0') | Set-Content -LiteralPath $rig -Encoding ascii
Set-Content -LiteralPath (Join-Path $shots 'cmd.txt') -Value '' -Encoding ascii

$argList = @(
  '--data', (Join-Path $base 'run\data'), '--disc', (Join-Path $base 'disc'),
  '--flash', (Join-Path $base 'run\data\dev_flash'), '--safe-mode', '--windowed', '--fps', '60',
  '--hidden', '--seconds', "$Seconds", '--rig', $rig, '--rig-shots', $shots,
  '--rig-shot-every', '2', '--keep-runs', '4',
  '--test-overlay', (Join-Path $base 'payload\overlay')
)
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
$env:AC4_SURVEY_AC = '1'
Remove-Item Env:AC4_P2_AUTO  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_P2_UNIT  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_DUMP_STATE -ErrorAction SilentlyContinue
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}

$clk = Join-Path $shots 'clock.txt'
$cmd = Join-Path $shots 'cmd.txt'
$script:ok = $false
function Clock { try { $s=(Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim(); $script:ok=$true; return [double]$s } catch { return 0.0 } }
function WaitClock([double]$v,[int]$t=180000){ $e=(Get-Date).AddMilliseconds($t); while((Get-Date) -lt $e){ if((Clock) -ge $v){return $true}; if($game.HasExited){return $false}; Start-Sleep -Milliseconds 200 }; return $false }
function Press([string]$b,[double]$h=0.15,[double]$g=0.8){ $x=(Clock)+$g; Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} 0 0 0 0" -f $x,$b); Add-Content -LiteralPath $rig -Value ("{0:F2} 0 none 0 0 0 0" -f ($x+$h)); [void](WaitClock ($x+$h+0.3)) }
function Send([string[]]$l) { Add-Content -LiteralPath $cmd -Value $l }
function NewestLog { $d=Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($d){Join-Path $d.FullName 'ac4.log'} }
function LogLines { $l=NewestLog; if(-not $l -or -not (Test-Path -LiteralPath $l)){return @()}; return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue) }
function SinceHas([int]$m,[string]$p){ $a=LogLines; if($a.Count -le $m){return $false}; return [bool]($a[$m..($a.Count-1)] | Select-String -Pattern $p -Quiet) }
function Has([string]$p){ return [bool](LogLines | Select-String -Pattern $p -Quiet) }
function LogMark { return (LogLines).Count }
function WaitFor([string]$p,[int]$s){ $e=(Get-Date).AddSeconds($s); while((Get-Date) -lt $e){ if(Has $p){return $true}; Start-Sleep -Milliseconds 500 }; return $false }

$e=(Get-Date).AddSeconds(120); while(-not $script:ok -and (Get-Date) -lt $e -and -not $game.HasExited){ [void](Clock); Start-Sleep -Milliseconds 300 }
[void](WaitClock 13)
foreach($b in @('A','B','A+B','A','B','A')){ Press $b 0.15 2.4 }
Start-Sleep -Seconds 4
Press 'START' 0.15 1.0
Start-Sleep -Seconds 6
$m=LogMark; Press 'A' 0.15 1.0; Start-Sleep -Seconds 5
if (SinceHas $m 'cellSaveDataListLoad2') { Write-Host "load list opened" } else { Write-Host "no load list marker" }
$hub=$false
for($i=0;$i -lt 45;$i++){ if(Has 'MissionSelectScene'){$hub=$true;break}; Start-Sleep -Seconds 1 }
Write-Host "hub: $hub at clock $([math]::Round((Clock),1))"
if (-not $hub) { Write-Host "FAILED to reach the hub"; return }

Start-Sleep -Seconds 5
# --- enter the highlighted mission (mission-accept dialog focuses CANCEL: LEFT then A) -----------
for ($attempt = 1; $attempt -le 3; $attempt++) {
  Write-Host "enter attempt $attempt"
  for ($i = 1; $i -lt $Slot; $i++) { Send @('250 DOWN 0 0 0 0', '700 none 0 0 0 0') }
  Send @('300 A 0 0 0 0', '1800 none 0 0 0 0', '300 LEFT 0 0 0 0', '900 none 0 0 0 0', '300 A 0 0 0 0')
  if (WaitFor 'input context gameplay' 70) { Write-Host "GAMEPLAY reached"; break }
  Write-Host "  (not yet; nudging any cutscene)"
  Send @('400 START 0 0 0 0', '1200 none 0 0 0 0', '400 A 0 0 0 0', '1200 none 0 0 0 0')
  if (WaitFor 'input context gameplay' 30) { Write-Host "GAMEPLAY reached"; break }
  Send @('300 B 0 0 0 0', '1200 none 0 0 0 0')
}
Start-Sleep -Seconds 8
Write-Output ("mission scene: {0}" -f (Has 'MissionSortieScene'))
Write-Output ("player AC survey lines: {0}" -f (@(LogLines | Select-String -Pattern 'AC#\d+ ctl').Count))
Write-Output ("clock {0}" -f (Clock))
