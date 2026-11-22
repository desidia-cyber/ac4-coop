# trace_vs.ps1 -- versus scenario for the player-creation trace (AC4_TRACE_CREATE=1 set by the caller).
# vs_split.ps1's closed-loop panel walk sometimes stalls; this one re-reads the gold band until the
# target row is selected, then runs the same proven accept sequence and waits for the match HUD.
param([int]$Seconds = 900, [string]$Tag = "traceVS")
$ErrorActionPreference = 'Continue'
$B = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe = Join-Path $B 'ac4-port\local\build\release\ac4.exe'
$shots = Join-Path $B "run\shots\$Tag"
$rig = Join-Path $B 'run\rig\vs_live.txt'
$runs = Join-Path $B 'run\data\runs'
$cmd = Join-Path $shots 'cmd.txt'
Add-Type -AssemblyName System.Drawing

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $shots | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force
Set-Content -LiteralPath $rig -Encoding ascii -Value @('0 0 none 0 0 0 0', '0 1 none 0 0 0 0')
Set-Content -LiteralPath $cmd -Value '' -Encoding ascii

$argList = @('--data', (Join-Path $B 'run\data'), '--disc', (Join-Path $B 'disc'), '--flash', (Join-Path $B 'run\data\dev_flash'),
  '--safe-mode', '--windowed', '--fps', '60', '--hidden', '--seconds', "$Seconds", '--rig', $rig, '--rig-shots', $shots,
  '--rig-shot-every', '2', '--keep-runs', '6', '--test-overlay', (Join-Path $B 'payload\overlay'))
$argLine = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
$env:AC4_SURVEY_AC = '1'
Remove-Item Env:AC4_P2_AUTO  -ErrorAction SilentlyContinue
Remove-Item Env:AC4_P2_UNIT  -ErrorAction SilentlyContinue
Get-Process ac4 -ErrorAction SilentlyContinue | Out-Null
Start-Process -FilePath $exe -ArgumentList $argLine -WindowStyle Hidden | Out-Null
Start-Sleep -Seconds 2
Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.PriorityClass = 'Idle' } catch {} }

function FreshShot([int]$t = 9000) {
  $b = (Get-ChildItem $shots -Filter 'shot_*.png' | Sort-Object LastWriteTime | Select-Object -Last 1).Name
  $dl = (Get-Date).AddMilliseconds($t)
  while ((Get-Date) -lt $dl) {
    $s = Get-ChildItem $shots -Filter 'shot_*.png' | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($s.Name -ne $b) { Start-Sleep -Milliseconds 250; return (Get-ChildItem $shots -Filter 'shot_*.png' | Sort-Object LastWriteTime | Select-Object -Last 1).FullName }
    Start-Sleep -Milliseconds 120
  }
  return (Get-ChildItem $shots -Filter 'shot_*.png' | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
}
function Press([string]$b, [int]$h = 220, [int]$s = 2000) {
  Add-Content $cmd '0 p0 none 0 0 0 0'; Start-Sleep -Milliseconds 350
  Add-Content $cmd ("{0} p0 {1} 0 0 0 0" -f $h, $b); Start-Sleep -Milliseconds ($h + 250)
  Add-Content $cmd '0 p0 none 0 0 0 0'; Start-Sleep -Milliseconds $s
  return (FreshShot)
}
function ReadPanel([string]$shot) {
  $im = [System.Drawing.Bitmap]::FromFile($shot)
  try {
    $rows = @{ 'ONLINE' = 158; 'SYSTEM LINK' = 220; 'SPLIT-SCREEN' = 282; 'SYSTEM' = 344 }
    $best = 'NONE'; $bv = -1
    foreach ($k in $rows.Keys) {
      $yc = $rows[$k]; $sum = 0.0; $n = 0
      for ($y = $yc - 6; $y -le $yc + 6; $y += 3) { for ($x = 115; $x -le 305; $x += 4) { $p = $im.GetPixel($x, $y); $sum += ([int]$p.R - [int]$p.B); $n++ } }
      $v = $sum / $n; if ($v -gt $bv) { $bv = $v; $best = $k }
    }
    if ($bv -lt 10) { return 'NONE' }
    return $best
  } finally { $im.Dispose() }
}
function NewestLog { $d = Get-ChildItem $runs -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($d) { Join-Path $d.FullName 'ac4.log' } }
function Has([string]$p) { $l = NewestLog; if (-not $l -or -not (Test-Path $l)) { return $false }; return [bool](Select-String -Path $l -Pattern $p -Quiet) }

$e = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $e -and -not (Test-Path (Join-Path $shots 'clock.txt'))) { Start-Sleep -Milliseconds 300 }
foreach ($b in @('A', 'B', 'A+B', 'A', 'B', 'A')) { [void](Press $b 160 2200) }
Write-Host ("startup dialogs cleared at {0}" -f (Get-Content (Join-Path $shots 'clock.txt')))

$panel = $false
for ($attempt = 1; $attempt -le 6; $attempt++) {
  [void](Press 'START' 160 2500)
  [void](Press 'START' 160 2500)
  [void](Press 'DOWN' 200 1400)
  [void](Press 'A' 240 3500)
  if (Has 'VsSortieScene') { $panel = $true; break }
  Write-Host "  attempt ${attempt}: panel not up"
}
if (-not $panel) { Write-Host 'FAILED: MULTIPLAYER panel'; return }
Write-Host 'MULTIPLAYER panel open'

[void](Press 'A' 240 2500)     # Load game progress data? YES
$dl = (Get-Date).AddSeconds(40)
while ((Get-Date) -lt $dl) { if (Has 'cellSaveDataLoad2|cellSaveDataListLoad2') { break }; Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 4

$hl = ReadPanel (FreshShot)
for ($i = 1; $i -le 10; $i++) {
  if ($hl -eq 'SPLIT-SCREEN') { break }
  [void](Press 'DOWN' 200 1500)
  $hl = ReadPanel (FreshShot)
  Write-Host ("    walk {0} -> {1}" -f $i, $hl)
}
if ($hl -ne 'SPLIT-SCREEN') { Write-Host "FAILED: highlight is $hl"; return }
Write-Host 'SPLIT-SCREEN highlighted'

[void](Press 'A' 260 3000)     # enter SPLIT-SCREEN setup
[void](Press 'A' 260 2600)     # Load 1P data? YES
[void](Press 'A' 260 2600)     # Load 2P data? YES
[void](Press 'A' 280 2600)     # START
[void](Press 'A' 280 6000)     # Commencing multiplayer match? YES
$dl = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $dl) { if (Has 'vssortie\.000') { break }; Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 4
[void](Press 'A' 220 2500)     # clear the BATTLE START banner
Start-Sleep -Seconds 6
Write-Host ("vssortie: {0}  AC#2: {1}  clock {2}" -f (Has 'vssortie\.000'), (Has 'AC#2'), (Get-Content (Join-Path $shots 'clock.txt')))
Write-Host ("log: {0}" -f (NewestLog))
