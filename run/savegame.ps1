# Reach the hub, then use SYSTEM > SAVE GAME so the port writes one of ITS OWN saves.
# The port stores save files unencrypted, unlike a console save, so the result is editable.
param([int]$Seconds = 420, [string]$Tag = "savegame")
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
$script:clockOk = $false
function Clock { try { $s=(Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim(); $script:clockOk=$true; return [double]$s } catch { return 0.0 } }
function WaitClock([double]$v, [int]$timeoutMs=180000){ $e=(Get-Date).AddMilliseconds($timeoutMs); while((Get-Date) -lt $e){ if((Clock) -ge $v){return $true}; if($game.HasExited){return $false}; Start-Sleep -Milliseconds 200 }; return $false }
function Press([string]$b,[double]$h=0.15,[double]$g=0.8){ $t=(Clock)+$g; Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} 0 0 0 0" -f $t,$b); Add-Content -LiteralPath $rig -Value ("{0:F2} 0 none 0 0 0 0" -f ($t+$h)); [void](WaitClock ($t+$h+0.3)) }
function NewestLog { $d=Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($d){Join-Path $d.FullName 'ac4.log'} }
function LogLines { $l=NewestLog; if(-not $l -or -not (Test-Path -LiteralPath $l)){return @()}; return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue) }
function LogMark { return (LogLines).Count }
function SinceHas([int]$m,[string]$p){ $a=LogLines; if($a.Count -le $m){return $false}; return [bool]($a[$m..($a.Count-1)] | Select-String -Pattern $p -Quiet) }

$e=(Get-Date).AddSeconds(120); while(-not $script:clockOk -and (Get-Date) -lt $e -and -not $game.HasExited){ [void](Clock); Start-Sleep -Milliseconds 300 }

# boot -> dismiss dialogs -> title
[void](WaitClock 13)
foreach($b in @('A','B','A+B','A','B','A')){ Press $b 0.15 2.4 }
Start-Sleep -Seconds 4
Press 'START' 0.15 1.0
Start-Sleep -Seconds 6
# LOAD GAME is the default with a save present
$m=LogMark; Press 'A' 0.15 1.0; Start-Sleep -Seconds 5
if (-not (SinceHas $m 'cellSaveDataListLoad2')) { Write-Host "load list not opened"; }
for($i=0;$i -lt 40;$i++){ if(SinceHas 0 'MissionSelectScene'){break}; Start-Sleep -Seconds 1 }
Write-Host "hub at clock $([math]::Round((Clock),1))"
Start-Sleep -Seconds 4

# mission panel -> right panel -> SYSTEM
Press 'LEFT' 0.12 1.0; Start-Sleep -Milliseconds 900
Press 'UP' 0.12 1.0;   Start-Sleep -Milliseconds 900
Press 'A' 0.15 1.0;    Start-Sleep -Seconds 4      # open SYSTEM
Write-Host "system menu at clock $([math]::Round((Clock),1))"

# SAVE GAME is the first row
$m=LogMark
Press 'A' 0.15 1.0; Start-Sleep -Seconds 4          # choose SAVE GAME
Press 'A' 0.15 1.5; Start-Sleep -Seconds 4          # confirm (new name / overwrite)
Press 'A' 0.15 1.5; Start-Sleep -Seconds 6
Write-Host "savedata operations since mark:"
LogLines | Select-Object -Skip $m | Select-String -Pattern 'savedata' | Select-Object -First 20 | ForEach-Object { $_.Line.Substring(0,[Math]::Min(150,$_.Line.Length)) }

# leave it running a little, then report the save folder
Start-Sleep -Seconds 6
$sd = Join-Path $base 'run\data\hdd0\home\00000001\savedata'
Write-Host "--- savedata contents ---"
Get-ChildItem -LiteralPath $sd -Recurse -File -ErrorAction SilentlyContinue |
  Select-Object FullName, Length, LastWriteTime | Format-Table -AutoSize
