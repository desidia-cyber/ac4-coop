# Drive the AC4 rig: complete missions (auto-succeed bundles) and save, all in one run.
# Usage: .\autoprog.ps1 -Tag prog1 -Missions 6 -Seconds 3600
param(
  [string]$Tag = "prog",
  [int]$Missions = 6,
  [int]$Seconds = 3600
)
$ErrorActionPreference = 'Continue'
$B = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$d = Join-Path $B "run\shots\$Tag"
$runs = Join-Path $B 'run\data\runs'

function Clk { try { [double]((Get-Content -LiteralPath "$d\clock.txt" -Raw -ErrorAction Stop).Trim()) } catch { 0.0 } }
function Send([string[]]$lines) { Add-Content -LiteralPath "$d\cmd.txt" -Value $lines }
function NewestLog {
  $x = Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($x) { Join-Path $x.FullName 'ac4.log' }
}
function Since([string]$pattern, [int]$mark) {
  $l = NewestLog; if (-not $l -or -not (Test-Path -LiteralPath $l)) { return $false }
  $a = @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue)
  if ($a.Count -le $mark) { return $false }
  return [bool]($a[$mark..($a.Count - 1)] | Select-String -Pattern $pattern -Quiet)
}
function Mark { $l = NewestLog; if (-not $l) { return 0 }; @(Get-Content -LiteralPath $l -ErrorAction SilentlyContinue).Count }
function WaitFor([string]$pattern, [int]$mark, [int]$timeoutMs = 60000) {
  $end = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $end) {
    if (Since $pattern $mark) { return $true }
    Start-Sleep -Milliseconds 400
  }
  return $false
}

# Fixed, short input sequences (each line: ms buttons lx ly rx ry).
# Accept the highlighted mission: A (open dialog), LEFT (focus OK), A (confirm).
function AcceptMission { Send @('250 A 0 0 0 0','1500 none 0 0 0 0','250 LEFT 0 0 0 0','800 none 0 0 0 0','250 A 0 0 0 0') }
# Dismiss whatever report/summary is showing and return to the hub list.
function DismissAll {
  Send @('400 A 0 0 0 0','1300 none 0 0 0 0','400 A 0 0 0 0','1300 none 0 0 0 0',
         '400 A 0 0 0 0','1300 none 0 0 0 0','400 A 0 0 0 0','15000 none 0 0 0 0')
}

Write-Host "clock at start: $(Clk)"
$completed = 0
for ($i = 1; $i -le $Missions; $i++) {
  $m = Mark
  AcceptMission
  if (WaitFor 'RIGSUCC' $m 70000) {
    $completed++
    Write-Host "mission $i : auto-succeeded"
  } else {
    Write-Host "mission $i : no RIGSUCC (not a mission, or menu drifted)"
  }
  DismissAll
  Write-Host "  clock $(Clk)  completed=$completed"
}

# Now persist: SYSTEM panel -> SAVE GAME.
Write-Host "saving..."
Send @('500 RIGHT 0 0 0 0','900 none 0 0 0 0','500 RIGHT 0 0 0 0','900 none 0 0 0 0')
Send @('500 A 0 0 0 0','1800 none 0 0 0 0')
Send @('500 DOWN 0 0 0 0','700 none 0 0 0 0')
Send @('500 A 0 0 0 0','2500 none 0 0 0 0')
Send @('500 A 0 0 0 0','2500 none 0 0 0 0')
Send @('500 A 0 0 0 0','8000 none 0 0 0 0')

Write-Host "clock at end: $(Clk)  completed=$completed"
