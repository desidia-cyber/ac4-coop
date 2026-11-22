# Load the current save and stop at the hub, so a save edit can be observed.
param([int]$Seconds = 300, [string]$Tag = "hub")
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

$argList = @(
  '--data', (Join-Path $base 'run\data'), '--disc', (Join-Path $base 'disc'),
  '--flash', (Join-Path $base 'run\data\dev_flash'), '--safe-mode', '--windowed', '--fps', '60',
  '--hidden', '--seconds', "$Seconds", '--rig', $rig, '--rig-shots', $shots,
  '--rig-shot-every', '2', '--keep-runs', '3',
  '--test-overlay', (Join-Path $base 'payload\overlay')
)
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
$env:AC4_SURVEY_AC = '1'
$env:AC4_P2_AUTO = '1'
$env:AC4_DUMP_STATE = '1'
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}

$clk = Join-Path $shots 'clock.txt'
$script:ok = $false
function Clock { try { $s=(Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim(); $script:ok=$true; return [double]$s } catch { return 0.0 } }
function WaitClock([double]$v,[int]$t=180000){ $e=(Get-Date).AddMilliseconds($t); while((Get-Date) -lt $e){ if((Clock) -ge $v){return $true}; if($game.HasExited){return $false}; Start-Sleep -Milliseconds 200 }; return $false }
function Press([string]$b,[double]$h=0.15,[double]$g=0.8){ $x=(Clock)+$g; Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} 0 0 0 0" -f $x,$b); Add-Content -LiteralPath $rig -Value ("{0:F2} 0 none 0 0 0 0" -f ($x+$h)); [void](WaitClock ($x+$h+0.3)) }
function NewestLog { $d=Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($d){Join-Path $d.FullName 'ac4.log'} }
function LogLines { $l=NewestLog; if(-not $l -or -not (Test-Path -LiteralPath $l)){return @()}; return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue) }
function SinceHas([int]$m,[string]$p){ $a=LogLines; if($a.Count -le $m){return $false}; return [bool]($a[$m..($a.Count-1)] | Select-String -Pattern $p -Quiet) }
function LogMark { return (LogLines).Count }

$e=(Get-Date).AddSeconds(120); while(-not $script:ok -and (Get-Date) -lt $e -and -not $game.HasExited){ [void](Clock); Start-Sleep -Milliseconds 300 }
[void](WaitClock 13)
foreach($b in @('A','B','A+B','A','B','A')){ Press $b 0.15 2.4 }
Start-Sleep -Seconds 4
Press 'START' 0.15 1.0
Start-Sleep -Seconds 6
$m=LogMark; Press 'A' 0.15 1.0; Start-Sleep -Seconds 5
if (SinceHas $m 'cellSaveDataListLoad2') { Write-Host "load list opened" } else { Write-Host "no load list marker" }
$hub=$false
for($i=0;$i -lt 45;$i++){ if(SinceHas 0 'MissionSelectScene'){$hub=$true;break}; Start-Sleep -Seconds 1 }
Write-Host "hub: $hub at clock $([math]::Round((Clock),1))"
Start-Sleep -Seconds 5
$last = Get-ChildItem -LiteralPath $shots -Filter '*.png' | Sort-Object Name | Select-Object -Last 1
Write-Host "hub screenshot: $($last.Name)"





