# Enter the highlighted hub mission and wait for gameplay. Retries the accept sequence.
param([string]$Tag = "p2", [int]$Slot = 1, [int]$TimeoutSec = 240)
$ErrorActionPreference = 'Continue'
$B = 'G:\Freespace\Armored Core 4 (USA)\coop-mod'
$d = Join-Path $B "run\shots\$Tag"
$runs = Join-Path $B 'run\data\runs'
function Send([string[]]$l) { Add-Content -LiteralPath "$d\cmd.txt" -Value $l }
function Newest { Get-ChildItem -LiteralPath $runs -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1 }
function Has([string]$p) {
  $x = Newest; if (-not $x) { return $false }
  $f = Join-Path $x.FullName 'ac4.log'
  if (-not (Test-Path -LiteralPath $f)) { return $false }
  [bool](Select-String -Path $f -Pattern $p -Quiet)
}
function Wait([string]$p, [int]$s) {
  $e = (Get-Date).AddSeconds($s)
  while ((Get-Date) -lt $e) { if (Has $p) { return $true }; Start-Sleep -Milliseconds 500 }
  return $false
}

for ($attempt = 1; $attempt -le 3; $attempt++) {
  Write-Host "attempt $attempt"
  for ($i = 1; $i -lt $Slot; $i++) { Send @('250 DOWN 0 0 0 0', '700 none 0 0 0 0') }
  Send @('300 A 0 0 0 0', '1800 none 0 0 0 0', '300 LEFT 0 0 0 0', '900 none 0 0 0 0', '300 A 0 0 0 0')
  if (Wait 'input context gameplay' 70) { Write-Host "GAMEPLAY reached"; exit 0 }
  Write-Host "  (not yet; nudging any cutscene)"
  Send @('400 START 0 0 0 0', '1200 none 0 0 0 0', '400 A 0 0 0 0', '1200 none 0 0 0 0')
  if (Wait 'input context gameplay' 30) { Write-Host "GAMEPLAY reached"; exit 0 }
  Send @('300 B 0 0 0 0', '1200 none 0 0 0 0')
}
Write-Host "gave up"
exit 1
