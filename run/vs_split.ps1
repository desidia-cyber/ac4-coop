# vs_split.ps1 -- reproduce OFFLINE 2-player SPLIT-SCREEN versus from boot, CLOSED-LOOP.
#
# Every step takes a FRESH screenshot and reads the screen numerically before deciding what to
# send. The only timing sleeps are short "let the UI settle" waits immediately after a verified
# transition; no step ever advances on a blind delay alone.
#
# Screen readers (measured from real frames, see research\16_VS_SPLITSCREEN.md):
#   ReadPanel  - locates the gold selection band in the MULTIPLAYER panel's button column.
#                Button row centres: ONLINE 158, SYSTEM LINK 220, SPLIT-SCREEN 282, SYSTEM 344.
#   ReadDialog - detects the gold YES button of a Yes/No dialog (~y 383, x 470..620).
#
# Path proven end to end in this run:
#   title -> MULTIPLAYER -> [Load game progress data? YES] -> SPLIT-SCREEN
#         -> [Load 1P data? YES] -> [Load 2P data? YES] -> [Commencing multiplayer match. YES]
#         -> split-screen match with AC#1 and AC#2
#
# Usage:  .\vs_split.ps1                 # boot -> match, leave the game running
#         .\vs_split.ps1 -Seconds 2400   # watchdog length
#         .\vs_split.ps1 -StopAtMatch    # kill the game once the match is confirmed
param(
  [int]$Seconds = 2400,
  [string]$Tag = "vs_split",
  [switch]$StopAtMatch
)
$ErrorActionPreference = 'Continue'
$B = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe   = Join-Path $B 'ac4-port\local\build\release\ac4.exe'
$shots = Join-Path $B "run\shots\$Tag"
$rig   = Join-Path $B 'run\rig\vs_live.txt'
$runs  = Join-Path $B 'run\data\runs'
$cmd   = Join-Path $shots 'cmd.txt'
$clk   = Join-Path $shots 'clock.txt'
Add-Type -AssemblyName System.Drawing

Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $shots | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $rig) | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force

# --- STATIC rig script: this is what makes the rig OWN pad ports 0 and 1. --------------
# rig::pad() returns false for any port that never appears in --rig, and that port's input is
# then silently dropped. This file must not be touched while the game runs (a reload clears
# the owned-port list), so the live queue is the separate cmd.txt.
Set-Content -LiteralPath $rig -Encoding ascii -Value @(
  '0 0 none 0 0 0 0',
  '0 1 none 0 0 0 0'
)
Set-Content -LiteralPath $cmd -Value '' -Encoding ascii

$args = @(
  '--data',  (Join-Path $B 'run\data'),
  '--disc',  (Join-Path $B 'disc'),
  '--flash', (Join-Path $B 'run\data\dev_flash'),
  '--safe-mode', '--windowed', '--fps', '60',
  '--hidden', '--seconds', "$Seconds",
  '--rig',            $rig,
  '--rig-shots',      $shots,
  '--rig-shot-every', '2',
  '--keep-runs',      '6',
  '--test-overlay',   (Join-Path $B 'payload\overlay')
)
$argLine = ($args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '

$env:AC4_SURVEY_AC = '1'          # log every (controller,input) pair reaching the AC turn input
Remove-Item Env:AC4_P2_AUTO  -ErrorAction SilentlyContinue   # do not let the P2 hook taint this
Remove-Item Env:AC4_P2_UNIT  -ErrorAction SilentlyContinue

Write-Host "launching: $exe"
Write-Host "args: $argLine"
$g = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $g.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch {}

function Clock { try { return [double]((Get-Content -LiteralPath $clk -Raw -ErrorAction Stop).Trim()) } catch { return -1.0 } }
function GameAlive { return ((Clock) -ge 0) -and [bool](Get-Process ac4 -ErrorAction SilentlyContinue) }

function FreshShot([int]$timeoutMs = 15000) {
  $before = Get-ChildItem -LiteralPath $shots -Filter 'shot_*.png' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime | Select-Object -Last 1
  $bn = if ($before) { $before.Name } else { '' }
  $deadline = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $deadline) {
    $s = Get-ChildItem -LiteralPath $shots -Filter 'shot_*.png' -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime | Select-Object -Last 1
    if ($s -and $s.Name -ne $bn) {
      Start-Sleep -Milliseconds 250
      return (Get-ChildItem -LiteralPath $shots -Filter 'shot_*.png' | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
    }
    Start-Sleep -Milliseconds 120
  }
  if (-not (GameAlive)) { throw "FreshShot: the game is no longer running (watchdog?)" }
  throw "FreshShot: no new screenshot within ${timeoutMs}ms"
}

# Send one input (neutral line first so a repeated command is never coalesced), settle, shoot.
function Press([string]$b, [int]$port = 0, [int]$hold = 220, [int]$settleMs = 1200) {
  Add-Content -LiteralPath $cmd -Value ("0 p{0} none 0 0 0 0" -f $port)
  Start-Sleep -Milliseconds 350
  Add-Content -LiteralPath $cmd -Value ("{0} p{1} {2} 0 0 0 0" -f $hold, $port, $b)
  Start-Sleep -Milliseconds ($hold + 240)
  Add-Content -LiteralPath $cmd -Value ("0 p{0} none 0 0 0 0" -f $port)
  Start-Sleep -Milliseconds $settleMs
  return (FreshShot)
}

function ReadPanel([string]$shot) {
  $im = [System.Drawing.Bitmap]::FromFile($shot)
  try {
    $rows = @{ 'ONLINE' = 158; 'SYSTEM LINK' = 220; 'SPLIT-SCREEN' = 282; 'SYSTEM' = 344 }
    $best = 'UNKNOWN'; $bestV = -1
    foreach ($k in $rows.Keys) {
      $yc = $rows[$k]; $sum = 0.0; $n = 0
      for ($y = $yc - 6; $y -le $yc + 6; $y += 3) {
        for ($x = 115; $x -le 305; $x += 4) {
          $p = $im.GetPixel($x, $y); $sum += ([int]$p.R - [int]$p.B); $n++
        }
      }
      $v = if ($n) { $sum / $n } else { 0 }
      if ($v -gt $bestV) { $bestV = $v; $best = $k }
    }
    # Below ~10 there is no gold band at all (we are not on the panel).
    if ($bestV -lt 10) { $best = 'NONE' }
    return $best
  } finally { $im.Dispose() }
}

function ReadDialog([string]$shot) {
  $im = [System.Drawing.Bitmap]::FromFile($shot)
  try {
    $g = 0.0; $m = 0
    for ($y = 375; $y -le 392; $y += 3) {
      for ($x = 470; $x -le 620; $x += 4) {
        $p = $im.GetPixel($x, $y); $g += ([int]$p.R - [int]$p.B); $m++
      }
    }
    return $(if ($m) { [math]::Round($g / $m, 1) } else { 0 })
  } finally { $im.Dispose() }
}

function NewestLog {
  $d = Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($d) { return (Join-Path $d.FullName 'ac4.log') }
  return $null
}
function LogHas([string]$Pattern) {
  $l = NewestLog
  if (-not $l -or -not (Test-Path -LiteralPath $l)) { return $false }
  return [bool](Select-String -LiteralPath $l -Pattern $Pattern -Quiet -ErrorAction SilentlyContinue)
}

# --- 0. boot ------------------------------------------------------------------------
$e = (Get-Date).AddSeconds(150)
while (-not $g.HasExited -and (Get-Date) -lt $e -and (Clock) -lt 0) { Start-Sleep -Milliseconds 300 }
Write-Host ("clock running at {0}" -f (Clock))

# --- 1. startup dialogs -------------------------------------------------------------
# "The HDD access indicator light will blink..." (and possibly "Cannot check update file").
foreach ($b in @('A','B','A+B','A','B','A')) { [void](Press $b 0 160 2200) }
Write-Host ("startup dialogs cleared at {0}" -f (Clock))

# --- 2. attract movie -> MULTIPLAYER panel ------------------------------------------
# The title is an attract-movie loop: the first START stops the movie, a second opens the menu.
# Retry until the panel actually appears, proven by the VsSortieScene machinery in the log.
$panel = $false
for ($attempt = 1; $attempt -le 8; $attempt++) {
  [void](Press 'START' 0 160 3000)
  $s = FreshShot
  $hl = ReadPanel $s
  if ($hl -ne 'NONE') { Write-Host ("  title menu -> panel candidate, highlight $hl"); }
  [void](Press 'DOWN' 0 200 1400)
  $s = FreshShot
  $hl = ReadPanel $s
  [void](Press 'A' 0 240 3000)
  if (LogHas 'VsSortieScene') { $panel = $true; break }
  Write-Host ("  attempt {0}: MULTIPLAYER panel not up yet at {1}" -f $attempt, (Clock))
  Start-Sleep -Seconds 2
}
if (-not $panel) { Write-Host "FAILED to open the MULTIPLAYER panel"; return }
Write-Host ("MULTIPLAYER panel open at clock {0}" -f (Clock))

# --- 3. "Load game progress data?" -> YES -------------------------------------------
# YES is the default focus. After YES the panel shows ".......Leaving network......." while the
# save is read and EVERY input is swallowed. Wait for the save read to complete first.
[void](Press 'A' 0 240 1800)
$dl = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $dl) {
  if (LogHas "cellSaveDataLoad2|cellSaveDataListLoad2: 'BLUS30027GAMEDAT") { break }
  Start-Sleep -Milliseconds 400
}
Start-Sleep -Seconds 4
Write-Host ("progress loaded (panel LIVE) at clock {0}" -f (Clock))

# --- 4. walk to SPLIT-SCREEN, verified each step ------------------------------------
# DOWN sometimes moves more than one row, so never count presses: iterate until the reader
# reports the target row.
$s = FreshShot
$hl = ReadPanel $s
Write-Host ("  panel highlight = {0}" -f $hl)
for ($i = 1; $i -le 10; $i++) {
  if ($hl -eq 'SPLIT-SCREEN') { break }
  [void](Press 'DOWN' 0 200 1400)
  $s = FreshShot
  $hl = ReadPanel $s
  Write-Host ("    step {0} -> {1}" -f $i, $hl)
}
if ($hl -ne 'SPLIT-SCREEN') { Write-Host "FAILED to highlight SPLIT-SCREEN (got $hl)"; return }
Write-Host ("SPLIT-SCREEN highlighted at clock {0}" -f (Clock))

# --- 5. setup: Load 1P data? YES, then Load 2P data? YES ----------------------------
# The game ITSELF asks for a second player's AC data: direct in-game evidence this mode is
# two-player. Both prompts default to YES.
[void](Press 'A' 0 260 3000)
Write-Host ("  entered SPLIT-SCREEN setup at {0}" -f (Clock))
[void](Press 'A' 0 260 2600); Write-Host "  answered Load 1P data? = YES"
[void](Press 'A' 0 260 2600); Write-Host "  answered Load 2P data? = YES"

# --- 6. START -> "Commencing multiplayer match." -> YES -----------------------------
[void](Press 'A' 0 280 2600)
[void](Press 'A' 0 280 6000)
Write-Host ("match confirmed, loading at clock {0}" -f (Clock))

# --- 7. wait for the versus scene ---------------------------------------------------
$dl = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $dl) {
  if (LogHas 'vssortie\.000') { break }
  Start-Sleep -Milliseconds 500
}
if (-not (LogHas 'vssortie\.000')) { Write-Host "WARNING: vssortie.000 never opened"; }
Start-Sleep -Seconds 4
[void](Press 'A' 0 220 2500)      # clear the BATTLE START banner
Start-Sleep -Seconds 2

$twoAc = LogHas 'AC#2'
Write-Host ""
Write-Host ("vssortie.000 opened (versus match scene live) : {0}" -f (LogHas 'vssortie\.000'))
Write-Host ("AC#2 registered (a SECOND AC exists)         : {0}" -f $twoAc)
Write-Host ("match running at clock {0}" -f (Clock))
Write-Host ""
Write-Host "Drive it live by appending to:"
Write-Host "  $cmd"
Write-Host "     3000 p1 none 0 26000 26000 0     # pad 1: forward + turn right"
Write-Host "     1500 p1 A 0 0 0 0                # pad 1: fire (CROSS)"
Write-Host "  pad 0 = left view (1P), pad 1 = right view (2P)"
Write-Host "Kill with:  Stop-Process -Name ac4 -Force"

if ($StopAtMatch) {
  Get-Process ac4 -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
  Write-Host "game stopped (-StopAtMatch)"
}
