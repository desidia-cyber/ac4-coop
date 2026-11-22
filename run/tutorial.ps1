# Reach the tutorial lesson list and try to select a specific lesson by index.
param([int]$Downs = 0, [int]$Seconds = 300, [string]$Tag = "tut")
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
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}
Write-Host "game pid $($game.Id)"

$clk = Join-Path $shots 'clock.txt'
$script:ok=$false
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
# NEW GAME (menu defaults to LOAD GAME with a save present)
Press 'UP' 0.12 1.0
Start-Sleep -Milliseconds 900
Press 'A' 0.15 1.0
# wait for TutorialScene
$gotTut=$false
for($i=0;$i -lt 90;$i++){ if(SinceHas 0 'TutorialScene'){$gotTut=$true;break}; if($game.HasExited){break}; Start-Sleep -Seconds 1 }
Write-Host "TutorialScene: $gotTut at clock $([math]::Round((Clock),1))"
Start-Sleep -Seconds 3
$last = Get-ChildItem -LiteralPath $shots -Filter '*.png' | Sort-Object Name | Select-Object -Last 1
Write-Host "lesson-menu screenshot: $($last.Name)"

if ($gotTut) {
  if ($Downs -gt 0) {
    for ($i=0; $i -lt $Downs; $i++) { Press 'DOWN' 0.12 0.9; Start-Sleep -Milliseconds 700 }
    Start-Sleep -Seconds 1
    $after = Get-ChildItem -LiteralPath $shots -Filter '*.png' | Sort-Object Name | Select-Object -Last 1
    Write-Host "after $Downs down: $($after.Name)"
  }
  $m = LogMark
  Press 'A' 0.15 1.0
  Start-Sleep -Seconds 12
  Write-Host "--- mission ids loaded after selecting ---"
  LogLines | Select-Object -Skip $m | Select-String -Pattern 'event/mission/m7\d\d\.lua|chara/m7\d\d' | ForEach-Object { $_.Line.Substring(0,[Math]::Min(140,$_.Line.Length)) }
}
Start-Sleep -Seconds 8
Write-Host "--- all tutorial missions seen ---"
LogLines | Select-String -Pattern 'event/mission/(m7\d\d)\.lua' | ForEach-Object { [regex]::Match($_.Line,'event/mission/(m7\d\d)\.lua').Groups[1].Value } | Sort-Object -Unique
