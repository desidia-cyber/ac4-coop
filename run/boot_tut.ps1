# Boot to the first tutorial sortie and leave the game running, driven afterwards through
# <shots>\cmd.txt. Command format: <milliseconds> <buttons|none> [lx ly rx ry]
param([int]$Seconds = 2400, [string]$Tag = "live")
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
  '--rig-shot-every', '2', '--keep-runs', '3',
  '--test-overlay', (Join-Path $base 'payload\overlay')
)
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
$game = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}
Write-Host "game pid $($game.Id)  shots=$shots"

$clk = Join-Path $shots 'clock.txt'
$scripts=[ordered]@{}
function Cmd([string]$line) { Add-Content -LiteralPath $cmd -Value $line }
function Clock { try { return [double]((Get-Content -LiteralPath (Join-Path $shots 'clock.txt') -Raw -ErrorAction Stop).Trim()) } catch { return 0.0 } }
function WaitClock([double]$v,[int]$t=240000){ $e=(Get-Date).AddMilliseconds($t); while((Get-Date) -lt $e){ if((Clock) -ge $v){return $true}; if($game.HasExited){return $false}; Start-Sleep -Milliseconds 200 }; return $false }
function Press([string]$b,[int]$ms=150){ Cmd ("{0} {1} 0 0 0 0" -f $ms,$b); Cmd "400 none 0 0 0 0"; [void](WaitClock ((Clock)+((($ms+400)/1000.0)))) }
function NewestLog { $d=Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if($d){Join-Path $d.FullName 'ac4.log'} }
function LogLines { $l=NewestLog; if(-not $l -or -not (Test-Path -LiteralPath $l)){return @()}; return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue) }
function Has([string]$p){ return [bool](LogLines | Select-String -Pattern $p -Quiet) }

$cmd = Join-Path $shots 'cmd.txt'
@('# live command queue: <ms> <buttons|none> [lx ly rx ry]') | Set-Content -LiteralPath $cmd -Encoding ascii
# give the rig a moment to create its own files
$e=(Get-Date).AddSeconds(60); while((Clock) -le 0 -and (Get-Date) -lt $e){ Start-Sleep -Milliseconds 300 }
[void](WaitClock 13)
foreach($b in @('A','B','A+B','A','B','A')){ Press $b }
Start-Sleep -Seconds 2
Press 'START'
Start-Sleep -Seconds 5
Press 'UP'
Start-Sleep -Milliseconds 700
Press 'A'
Write-Host "waiting for the tutorial (intro may play ~3 min, no presses)..."
$saw=$false
for($i=0;$i -lt 200;$i++){
  if (Has 'TutorialScene') { $saw=$true; break }
  if ($game.HasExited) { break }
  Start-Sleep -Seconds 2
}
Write-Host "TutorialScene: $saw at clock $([math]::Round((Clock),1))"
Start-Sleep -Seconds 2
for($i=0;$i -lt 15;$i++){ if (Has 'TutorialSortieScene') { break }; Press 'A'; Start-Sleep -Seconds 3 }
Write-Host "sortie: $(Has 'TutorialSortieScene')"
Write-Host "READY - drive with $cmd"
