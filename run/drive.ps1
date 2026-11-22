# Closed-loop driver for the AC4 co-op research rig (v2).
# Feedback is the game's own log (new lines only) plus the rig's clock file.
param(
  [int]$Seconds = 600,
  [string]$Tag = "drive",
  [switch]$Explore,
  [switch]$NewGame
)
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
function Clock {
  try {
    $s = (Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim()
    $script:clockOk = $true
    return [double]$s
  } catch { return 0.0 }
}
function WaitClock([double]$v, [int]$timeoutMs = 120000) {
  $end = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $end) {
    if ((Clock) -ge $v) { return $true }
    if ($game.HasExited) { return $false }
    Start-Sleep -Milliseconds 200
  }
  return $false
}
function Press([string]$btn, [double]$hold = 0.15, [double]$gap = 0.8) {
  $t = (Clock) + $gap
  Add-Content -LiteralPath $rig -Value ("{0:F1} 0 {1} 0 0 0 0" -f $t, $btn)
  Add-Content -LiteralPath $rig -Value ("{0:F2} 0 none 0 0 0 0" -f ($t + $hold))
  [void](WaitClock ($t + $hold + 0.3))
}
function NewestLog {
  $d = Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($d) { return Join-Path $d.FullName 'ac4.log' } else { return $null }
}
function LogLines {
  $l = NewestLog
  if (-not $l -or -not (Test-Path -LiteralPath $l)) { return @() }
  return @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue)
}
function LogMark { return (LogLines).Count }
function Since([int]$mark) {
  $all = LogLines
  if ($all.Count -le $mark) { return @() }
  return @($all[$mark..($all.Count-1)])
}
function SinceHas([int]$mark, [string]$pattern) {
  return [bool](Since $mark | Select-String -Pattern $pattern -Quiet)
}

# ---- wait for the rig clock to exist ----------------------------------------------------------
$end = (Get-Date).AddSeconds(120)
while (-not $script:clockOk -and (Get-Date) -lt $end -and -not $game.HasExited) { [void](Clock); Start-Sleep -Milliseconds 300 }
Write-Host "clock file: $script:clockOk"

# ---- 1. clear startup dialogs (A/B only; no START, so the menu is not touched) ----------------
[void](WaitClock 13)
foreach ($b in @('A','B','A+B','A','B','A')) { Press $b 0.15 2.4 }
Write-Host "dialogs cleared at clock $([math]::Round((Clock),1))"
Start-Sleep -Seconds 4

# ---- 2. title -> main menu --------------------------------------------------------------------
$m0 = LogMark
Press 'START' 0.15 1.0
Start-Sleep -Seconds 6
Write-Host "menu up at clock $([math]::Round((Clock),1))"

# ---- 3. choose LOAD GAME, verified only from new log lines ------------------------------------
$loaded = $false
# With save data present the title menu defaults to LOAD GAME, so try A before moving the cursor.
$loaded = $false
if ($NewGame) {
  # With a save present the menu defaults to LOAD GAME; one tap up selects NEW GAME.
  Press 'UP' 0.12 1.0
  Start-Sleep -Milliseconds 900
  $mark = LogMark
  Press 'A' 0.15 1.0
  Start-Sleep -Seconds 5
  if (SinceHas $mark 'movie/opening') { Write-Host "new game accepted (intro movie)" }
  else { Write-Host "new game: no intro marker (may already have started)" }
  Write-Host "waiting for the first scene (up to ~6 min)..."
  for ($i = 0; $i -lt 180; $i++) {
    if ((SinceHas 0 'SortieScene|TutorialScene|GarageScene|InitialGame')) { break }
    if ($game.HasExited) { break }
    Start-Sleep -Seconds 2
  }
  Write-Host "scene wait done at clock $([math]::Round((Clock),1))"
  Write-Host "advancing with A to reach a sortie..."
  for ($k = 0; $k -lt 30; $k++) {
    if ((SinceHas 0 'SortieScene')) { break }
    if ($game.HasExited) { break }
    Press 'A' 0.15 1.2
    Start-Sleep -Seconds 3
  }
  Write-Host "sortie reached: $((SinceHas 0 'SortieScene')) at clock $([math]::Round((Clock),1))"
}
for ($tap = 0; $tap -le 2 -and -not $NewGame -and -not $loaded; $tap++) {
  if ($tap -gt 0) { for ($i = 1; $i -le $tap; $i++) { Press 'DOWN' 0.12 1.0; Start-Sleep -Milliseconds 800 } }
  $mark = LogMark
  Press 'A' 0.15 1.0
  Start-Sleep -Seconds 5
  if (SinceHas $mark 'cellSaveDataListLoad2') {
    Write-Host "  tap=$tap -> load list opened"
    $loaded = $true
  } elseif (SinceHas $mark 'movie/opening') {
    Write-Host "  tap=$tap -> NEW GAME (intro). cancelling"
    Press 'B' 0.15 1.0
    Start-Sleep -Seconds 4
    $mark2 = LogMark
    Press 'START' 0.15 1.0
    Start-Sleep -Seconds 5
  } else {
    Write-Host "  tap=$tap -> nothing recognisable"
    Press 'B' 0.15 1.0
    Start-Sleep -Seconds 3
  }
}

# ---- 4. wait for the hub ----------------------------------------------------------------------
$hub = $false
for ($i = 0; $i -lt 45; $i++) {
  if ((SinceHas 0 'MissionSelectScene')) { $hub = $true; break }
  Start-Sleep -Seconds 1
}
Write-Host "hub: $hub (clock $([math]::Round((Clock),1)))"

if ($hub -and $Explore) {
  Start-Sleep -Seconds 4
  Write-Host "exploring the hub: START"
  Press 'START' 0.15 1.0
  Start-Sleep -Seconds 4
  Write-Host "  then LEFT"; Press 'LEFT' 0.12 1.0; Start-Sleep -Seconds 3
  Write-Host "  then RIGHT"; Press 'RIGHT' 0.12 1.0; Start-Sleep -Seconds 3
  Write-Host "  then UP"; Press 'UP' 0.12 1.0; Start-Sleep -Seconds 3
  Write-Host "  then DOWN"; Press 'DOWN' 0.12 1.0; Start-Sleep -Seconds 3
  Write-Host "explored; clock $([math]::Round((Clock),1))"
  Start-Sleep -Seconds 4
}

# ---- 5. launch the mission --------------------------------------------------------------------
if ($hub -and -not $Explore) {
  Start-Sleep -Seconds 5
  $mark = LogMark
  Press 'A' 0.15 2.0
  Start-Sleep -Seconds 3
  $mark2 = LogMark
  Press 'A' 0.15 2.0
  Start-Sleep -Seconds 8
  if (-not (SinceHas $mark2 'Briefing|Sortie')) {
    Write-Host "  no scene after accept; trying again"
    $mark3 = LogMark
    Press 'A' 0.15 2.0
    Start-Sleep -Seconds 10
  }
}

# ---- 6. report --------------------------------------------------------------------------------
Start-Sleep -Seconds 10
Write-Host "--- scenes ---"
LogLines | Select-String -Pattern 'Scene.*started|Sortie|TEST OVERLAY:|movie/' |
  Select-Object -Last 20 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(150, $_.Line.Length)) }
Write-Host "clock end: $([math]::Round((Clock),1))"
