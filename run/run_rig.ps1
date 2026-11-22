# Automated AC4 co-op test rig runner.
# Hidden window, silent audio, scripted pad input, screenshots, bounded run.
# Usage: .\run_rig.ps1 -Script <rig script> -Seconds 150 [-Overlay <dir>] [-ShotEvery 2] [-Tag name]
param(
  [Parameter(Mandatory=$true)][string]$Script,
  [int]$Seconds = 150,
  [string]$Overlay = "",
  [double]$ShotEvery = 2,
  [string]$Tag = "run"
)
$ErrorActionPreference = 'Stop'
$B = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$exe = Join-Path $B 'ac4-port\local\build\release\ac4.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "ac4.exe not built: $exe" }
if (-not (Test-Path -LiteralPath $Script)) { throw "rig script not found: $Script" }
if ([string]::IsNullOrWhiteSpace($Overlay)) { $Overlay = Join-Path $B 'payload\overlay' }

$shots = Join-Path $B "run\shots\$Tag"
New-Item -ItemType Directory -Force -Path $shots | Out-Null
Get-ChildItem -LiteralPath $shots -File -ErrorAction SilentlyContinue | Remove-Item -Force

$args = @(
  '--data',        (Join-Path $B 'run\data'),
  '--disc',        (Join-Path $B 'disc'),
  '--flash',       (Join-Path $B 'run\data\dev_flash'),
  '--safe-mode',
  '--windowed',
  '--fps',         '60',
  '--hidden',
  '--seconds',     "$Seconds",
  '--rig',         $Script,
  '--rig-shots',   $shots,
  '--rig-shot-every', "$ShotEvery",
  '--keep-runs',   '3'
)
if ($Overlay -ne 'none') { $args += @('--test-overlay', $Overlay) }

Write-Host "launching: $exe"
# Start-Process does not quote array elements: build one quoted command line instead.
$argLine = ($args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
Write-Host ("args: " + $argLine)

# Idle priority: the run must not compete with the user's own work.
$p = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -WindowStyle Hidden
try { $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::Idle } catch { }

$deadline = (Get-Date).AddSeconds($Seconds + 40)
while (-not $p.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (-not $p.HasExited) {
  Write-Host "rig run overran; stopping"
  try { $p.Kill() } catch { }
}
Write-Host ("exit code: " + $(if ($p.HasExited) { $p.ExitCode } else { 'killed' }))

$runs = Join-Path $B 'run\data\runs'
$latest = Get-ChildItem -LiteralPath $runs -Directory -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latest) {
  $log = Join-Path $latest.FullName 'ac4.log'
  Write-Host "log: $log"
  if (Test-Path -LiteralPath $log) {
    Write-Host "--- rig/overlay/error lines ---"
    Select-String -LiteralPath $log -Pattern 'rig|OVERLAY|JOSHUA|lua|script error|mission started|SortieScene' -ErrorAction SilentlyContinue |
      Select-Object -First 60 | ForEach-Object { $_.Line }
  }
  $out = Join-Path $B "run\logs"
  New-Item -ItemType Directory -Force -Path $out | Out-Null
  if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $out "$Tag.log") -Force }
}
$n = (Get-ChildItem -LiteralPath $shots -Filter '*.png' -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Host "screenshots: $n in $shots"
