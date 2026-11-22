# s28_pair.ps1 -- research\28: host + join on the co-op overlay, both driven to a mission.
param([string]$Tag = "s28", [int]$Seconds = 1500, [switch]$HubOnly, [int]$AutoKill = 0, [switch]$NoP2Host, [int]$KillP2 = 0)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$base = Split-Path -Parent $here
Get-Process ac4 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$env:AC4_COOP_DUMP = '0'
if ($AutoKill -gt 0) { $env:AC4_COOP_TEST_AUTOKILL = "$AutoKill" }
if ($NoP2Host) { $env:AC4_COOP_NO_P2 = '1' }
if ($KillP2 -gt 0) { $env:AC4_COOP_TEST_KILLP2 = "$KillP2" }
& (Join-Path $here 'coop_host.ps1') -Port 7777 -Seconds $Seconds -Tag "${Tag}h" -Overlay 'payload\overlay_coop'
Start-Sleep -Seconds 2
Remove-Item Env:AC4_COOP_TEST_AUTOKILL -ErrorAction SilentlyContinue
Remove-Item Env:AC4_COOP_NO_P2 -ErrorAction SilentlyContinue
Remove-Item Env:AC4_COOP_TEST_KILLP2 -ErrorAction SilentlyContinue
& (Join-Path $here 'coop_join.ps1') -HostAddr 127.0.0.1 -Port 7777 -Seconds $Seconds -Tag "${Tag}j" -Overlay 'payload\overlay_coop'
Start-Sleep -Seconds 3
foreach ($t in @("${Tag}h", "${Tag}j")) {
  $data = if ($t -like '*h') { 'run\data' } else { 'run\data2' }
  $log = Join-Path $base "research\${t}_drive.log"
  $a = @('-u', 'coop_mission.py', '--tag', $t, '--shots', ('"' + (Join-Path $here "shots\$t") + '"'),
         '--runs', ('"' + (Join-Path $base "$data\runs") + '"'), '--timeout', '600')
  if ($t -like '*j' -and $HubOnly) { $a += '--hub-only' }
  Start-Process -FilePath python -ArgumentList $a -WorkingDirectory $here -RedirectStandardOutput $log `
    -RedirectStandardError "$log.err" -WindowStyle Hidden | Out-Null
}
Write-Host "started"
