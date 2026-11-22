# Play the AC4 tutorial by cycling a repertoire of inputs, watching for lesson progression.
param([int]$Seconds = 1800, [string]$Tag = "tut2", [string]$StopAt = "m717")
$ErrorActionPreference = 'Continue'
$base = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe  = Join-Path $base 'ac4-port\local\build\release\ac4.exe'
$rig  = Join-Path $base 'run\rig\live.txt'
$shots = Join-Path $base "run\shots\$Tag"
$runs = Join-Path $base 'run\data\runs'

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 3
New-Item -ItemType Directory -Force -Path $shots | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force
@('0.0 0 none 0 0 0 0') | Set-Content -LiteralPath $rig -Encoding ascii

$argList = @(
  '--data', (Join-Path $base 'run\data'), '--disc', (Join-Path $base 'disc'),
  '--flash', (Join-Path $base 'run\data\dev_flash'), '--safe-mode', '--windowed', '--fps', '60',
  '--hidden', '--seconds', "$Seconds", '--rig', $rig, '--rig-shots', $shots,
  '--rig-shot-every', '3', '--keep-runs', '3',
  '--test-overlay', (Join-Path $base 'payload\overlay')
)
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}
Write-Host "game pid $($game.Id)"

$clk = Join-Path $shots 'clock.txt'
$script:ok=$false
function Clock { try { $s=(Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim(); $script:ok=$true; return [double]$s } catch { return 0.0 } }
function WaitClock([double]$v,[int]$t=240000){ $e=(Get-Date).AddMilliseconds($t); while((Get-Date) -lt $e){ if((Clock) -ge $v){return $true}; if($game.HasExited){return $false}; Start-Sleep -Milliseconds 200 }; return $false }
function Tap([string]$b,[double]$hold=0.15){ $x=(Clock)+0.7; Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} 0 0 0 0" -f $x,$b); Add-Content -LiteralPath $rig -Value ("{0:F2} 0 none 0 0 0 0" -f ($x+$hold)); [void](WaitClock ($x+$hold+0.25)) }
function Hold([string]$b,[double]$secs,[double]$lx=0,[double]$ly=0,[double]$rx=0,[double]$ry=0){
  $x=(Clock)+0.6
  Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} {2} {3} {4} {5}" -f $x,$b,$lx,$ly,$rx,$ry)
  Add-Content -LiteralPath $rig -Value ("{0:F1} 0 none 0 0 0 0" -f ($x+$secs))
  [void](WaitClock ($x+$secs+0.25))
}
function NewestLog { $d=Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($d){Join-Path $d.FullName 'ac4.log'} }
function LogLines { $l=NewestLog; if(-not $l -or -not (Test-Path -LiteralPath $l)){return @()}; return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue) }
function SinceHas([int]$m,[string]$p){ $a=LogLines; if($a.Count -le $m){return $false}; return [bool]($a[$m..($a.Count-1)] | Select-String -Pattern $p -Quiet) }
function CurMission {
  $l = LogLines
  $m = $l | Select-String -Pattern 'event/mission/(m7\d\d)\.lua' | Select-Object -Last 1
  if ($m) { return [regex]::Match($m.Line,'(m7\d\d)\.lua').Groups[1].Value } else { return '?' }
}

$e=(Get-Date).AddSeconds(120); while(-not $script:ok -and (Get-Date) -lt $e -and -not $game.HasExited){ [void](Clock); Start-Sleep -Milliseconds 300 }
[void](WaitClock 13)
foreach($b in @('A','B','A+B','A','B','A')){ Tap $b }   # clear startup dialogs
Start-Sleep -Seconds 3
Tap 'START'
Start-Sleep -Seconds 5
Tap 'UP'                                                  # NEW GAME (menu defaults to LOAD GAME)
Start-Sleep -Milliseconds 800
Tap 'A'
$saw=$false
for($i=0;$i -lt 150;$i++){
  if(SinceHas 0 'TutorialScene'){$saw=$true;break}
  if($game.HasExited){break}
  $lastMovie = (LogLines | Select-String -Pattern 'movie/(opening|title)\.pam' | Select-Object -Last 1)
  if ($lastMovie -and ($lastMovie.Line -match 'movie/title\.pam')) {
    # Back at PRESS START: A does nothing here, so enter the menu and pick NEW GAME again.
    Tap 'START'; Start-Sleep -Seconds 4
    Tap 'UP';    Start-Sleep -Milliseconds 700
    Tap 'A';     Start-Sleep -Seconds 3
  } else {
    Start-Sleep -Seconds 3          # intro is playing; let it run (A would skip back to the title)
  }
}
Write-Host "TutorialScene: $saw at clock $([math]::Round((Clock),1))"
Start-Sleep -Seconds 2
# advance into the first sortie
for($i=0;$i -lt 12;$i++){ if(SinceHas 0 'TutorialSortieScene'){break}; Tap 'A'; Start-Sleep -Seconds 3 }
Write-Host "sortie: $(SinceHas 0 'TutorialSortieScene') mission=$(CurMission)"

# repertoire loop
$last = ''
for ($cycle = 0; $cycle -lt 400; $cycle++) {
  if (-not (Get-Process ac4 -ErrorAction SilentlyContinue)) { Write-Host "game exited"; break }
  $cur = CurMission
  if ($cur -ne $last) { Write-Host ("  -> {0} at clock {1}" -f $cur, [math]::Round((Clock),1)); $last = $cur }
  if ($cur -ne '?' -and $cur -ge $StopAt) { Write-Host "REACHED $StopAt"; break }

  Hold 'LT' 3.0 0 26000 0 0        # boost forward
  Hold 'none' 1.0 0 26000 0 0      # keep walking forward
  Hold 'RT' 2.5 0 26000 0 0        # other trigger + forward
  Hold 'none' 1.0 0 0 0 0
  Hold 'none' 1.2 0 0 26000 0      # turn right
  Hold 'none' 1.2 0 0 -26000 0     # turn left
  Hold 'none' 1.0 0 0 0 26000      # look up
  Tap 'RB'                          # fire right
  Tap 'LB'                          # fire left
  Tap 'A'                           # confirm / advance
}
Write-Host "final mission: $(CurMission)"
Write-Host "clock end: $([math]::Round((Clock),1))"
Write-Host "--- tutorial missions seen ---"
LogLines | Select-String -Pattern 'event/mission/(m7\d\d)\.lua' | ForEach-Object { [regex]::Match($_.Line,'(m7\d\d)\.lua').Groups[1].Value } | Sort-Object -Unique
