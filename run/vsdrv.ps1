# Helper library for driving the live AC4 rig from the command queue.
# Dot-source it:  . $B\run\vsdrv.ps1 -Tag vs1
#
# IMPORTANT rig semantics (from ac4-port\src\runtime\rig.cpp):
#   * The numbers in cmd.txt are DURATIONS (ms), not absolute times. A batch runs
#     "<hold> pN BTN" then "<gap> pN none", i.e. press for $hold, release for $gap.
#   * A port only gets pad state if rig::pad() considers it "owned", i.e. the port
#     appeared in the STATIC --rig script at startup. --rig must therefore be a
#     separate, non-empty file that is never rewritten (see vs_run.ps1).
param([string]$Tag = "vs1")
$script:B     = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$script:Shots = Join-Path $B "run\shots\$Tag"
$script:Cmd   = Join-Path $Shots 'cmd.txt'
$script:Clk   = Join-Path $Shots 'clock.txt'

function Clock {
  try { return [double]((Get-Content -LiteralPath $Clk -Raw -ErrorAction Stop).Trim()) } catch { return -1.0 }
}
function GameAlive { return ((Clock) -ge 0) -and [bool](Get-Process ac4 -ErrorAction SilentlyContinue) }

# Press a button (or hold a stick) for $Hold ms on port $Port, then release for $Gap ms.
function Send([string]$Buttons = 'none', [int]$Port = 0, [int]$Hold = 150,
              [int]$Lx = 0, [int]$Ly = 0, [int]$Rx = 0, [int]$Ry = 0, [double]$Gap = 0.45) {
  if (-not (GameAlive)) { throw "rig is gone" }
  Add-Content -LiteralPath $Cmd -Value ("{0} p{1} {2} {3} {4} {5} {6}" -f $Hold, $Port, $Buttons, $Lx, $Ly, $Rx, $Ry)
  Add-Content -LiteralPath $Cmd -Value ("{0} p{1} none 0 0 0 0" -f [int]($Gap * 1000), $Port)
  Start-Sleep -Milliseconds ([int]($Hold + $Gap * 1000 + 60))
  return (Clock)
}
# Hold a state for $Ms then release (stick deflection needs a sustained hold).
function Hold([string]$Buttons = 'none', [int]$Port = 0, [int]$Ms = 1000,
              [int]$Lx = 0, [int]$Ly = 0, [int]$Rx = 0, [int]$Ry = 0) {
  if (-not (GameAlive)) { throw "rig is gone" }
  Add-Content -LiteralPath $Cmd -Value ("{0} p{1} {2} {3} {4} {5} {6}" -f $Ms, $Port, $Buttons, $Lx, $Ly, $Rx, $Ry)
  Start-Sleep -Milliseconds $Ms
  Add-Content -LiteralPath $Cmd -Value ("0 p{0} none 0 0 0 0" -f $Port)
  Start-Sleep -Milliseconds 120
  return (Clock)
}
function LatestShot {
  Get-ChildItem -LiteralPath $Shots -Filter '*.png' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
}
function WaitShot([double]$Sec = 2.5) { Start-Sleep -Milliseconds ([int]($Sec * 1000)); return (LatestShot) }
function LogPath {
  $runs = Join-Path $B 'run\data\runs'
  $d = Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($d) { return (Join-Path $d.FullName 'ac4.log') }
  return $null
}
function LogTail([int]$n = 25) {
  $l = LogPath
  if (-not $l -or -not (Test-Path -LiteralPath $l)) { return @() }
  $a = @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue)
  if ($a.Count -le $n) { return $a }
  return $a[($a.Count - $n)..($a.Count - 1)]
}
function LogGrep([string]$Pattern) {
  $l = LogPath
  if (-not $l -or -not (Test-Path -LiteralPath $l)) { return @() }
  @(Select-String -LiteralPath $l -Pattern $Pattern -ErrorAction SilentlyContinue | ForEach-Object { $_.Line })
}
Write-Host "vsdrv ready: tag=$Tag clock=$(Clock)"
