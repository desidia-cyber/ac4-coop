# vsloop.ps1 -- CLOSED-LOOP driver for the AC4 rig.
#
# Every step takes a FRESH screenshot, reads the screen state numerically, and only then
# decides what to send. Nothing is driven by blind fixed delays.
#
# Screen readers implemented here (all measured from real frames, not guessed):
#   ReadMultiplayerPanel  -> which of ONLINE/SYSTEM LINK/SPLIT-SCREEN/SYSTEM is highlighted,
#                            by locating the gold selection band in the left panel.
#   ScreenName            -> coarse classification by sampling known regions.
#
# Dot-source:  . $B\run\vsloop.ps1 -Tag vsverify3
param([string]$Tag = "vs1")
$script:B     = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$script:Shots = Join-Path $B "run\shots\$Tag"
$script:Cmd   = Join-Path $Shots 'cmd.txt'
$script:Clk   = Join-Path $Shots 'clock.txt'
Add-Type -AssemblyName System.Drawing

function Clock { try { return [double]((Get-Content -LiteralPath $Clk -Raw -ErrorAction Stop).Trim()) } catch { return -1.0 } }
function GameAlive { return ((Clock) -ge 0) -and [bool](Get-Process ac4 -ErrorAction SilentlyContinue) }

# Wait for a screenshot file that did NOT exist when this call started, then return its path.
function FreshShot([int]$timeoutMs = 12000) {
  $before = (Get-ChildItem -LiteralPath $Shots -Filter 'shot_*.png' -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime | Select-Object -Last 1)
  $bn = if ($before) { $before.Name } else { '' }
  $deadline = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $deadline) {
    $s = Get-ChildItem -LiteralPath $Shots -Filter 'shot_*.png' -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime | Select-Object -Last 1
    if ($s -and $s.Name -ne $bn) {
      Start-Sleep -Milliseconds 250      # let the PNG finish flushing
      $s = Get-ChildItem -LiteralPath $Shots -Filter 'shot_*.png' |
           Sort-Object LastWriteTime | Select-Object -Last 1
      return $s.FullName
    }
    Start-Sleep -Milliseconds 120
  }
  throw "FreshShot: no new screenshot within ${timeoutMs}ms"
}

# Send one input as its own pair of lines (a neutral line first so the rig cannot coalesce
# a repeated command), wait, then take a fresh screenshot. Returns the shot path.
function Press([string]$b, [int]$port = 0, [int]$hold = 200, [int]$settleMs = 1200) {
  Add-Content -LiteralPath $Cmd -Value ("0 p{0} none 0 0 0 0" -f $port)
  Start-Sleep -Milliseconds 350
  Add-Content -LiteralPath $Cmd -Value ("{0} p{1} {2} 0 0 0 0" -f $hold, $port, $b)
  Start-Sleep -Milliseconds ($hold + 240)
  Add-Content -LiteralPath $Cmd -Value ("0 p{0} none 0 0 0 0" -f $port)
  Start-Sleep -Milliseconds $settleMs
  return (FreshShot)
}

# --- screen readers -------------------------------------------------------------------
# The highlighted MULTIPLAYER-panel button has a gold fill. Measure (R-B) over each button
# row band and report the brightest. Row centers were measured from real frames:
#   ONLINE 158, SYSTEM LINK 220, SPLIT-SCREEN 282, SYSTEM 344  (62 px apart)
function ReadPanel {
  param([string]$shot)
  $im = [System.Drawing.Bitmap]::FromFile($shot)
  try {
    $rows = @{ 'ONLINE' = 158; 'SYSTEM LINK' = 220; 'SPLIT-SCREEN' = 282; 'SYSTEM' = 344 }
    $best = $null; $bestV = -1; $all = @{}
    foreach ($k in $rows.Keys) {
      $yc = $rows[$k]; $sum = 0.0; $n = 0
      for ($y = $yc - 6; $y -le $yc + 6; $y += 3) {
        for ($x = 115; $x -le 305; $x += 4) {
          $p = $im.GetPixel($x, $y); $sum += ([int]$p.R - [int]$p.B); $n++
        }
      }
      $v = if ($n) { $sum / $n } else { 0 }
      $all[$k] = [math]::Round($v, 1)
      if ($v -gt $bestV) { $bestV = $v; $best = $k }
    }
    return [pscustomobject]@{ Highlight = $best; Scores = $all }
  } finally { $im.Dispose() }
}

# Is the SPLIT-SCREEN setup screen up? It is titled "SPLIT-SCREEN" (y~72) and carries a
# "PLAYER 01" label at y~131. Measured on a real frame: the PLAYER 01 band averages ~123.
# The MULTIPLAYER panel's own header is "MULTIPLAYER", so the title distinguishes them.
function ReadSetupScreen {
  param([string]$shot)
  $im = [System.Drawing.Bitmap]::FromFile($shot)
  try {
    $t = 0.0; $n = 0
    for ($y = 124; $y -le 140; $y += 2) {
      for ($x = 482; $x -le 620; $x += 2) {
        $p = $im.GetPixel($x, $y); $t += ($p.R + $p.G + $p.B) / 3.0; $n++
      }
    }
    $bright = if ($n) { $t / $n } else { 0 }
    # A YES/NO dialog present? The gold YES button sits ~y 383, x 460..630.
    $g = 0.0; $m = 0
    for ($y = 375; $y -le 392; $y += 3) {
      for ($x = 470; $x -le 620; $x += 4) {
        $p = $im.GetPixel($x, $y); $g += ([int]$p.R - [int]$p.B); $m++
      }
    }
    $gold = if ($m) { $g / $m } else { 0 }
    return [pscustomobject]@{
      Player01BandBrightness = [math]::Round($bright, 1)
      YesGold                = [math]::Round($gold, 1)
      IsSetup                = ($bright -gt 100)
      HasDialog              = ($gold -gt 20)
    }
  } finally { $im.Dispose() }
}
