# coop_host.ps1 -- instance A of the two-instance co-op test (milestone M3/M4).
#
#   .\coop_host.ps1 [-Port 7777] [-Seconds 2400] [-Tag host]
#
# Instance A is the HOST: AC4_COOP_HOST=<port> makes it listen for the peer's UDP packets, and AC4_COOP
# makes it build the second AC whose pad operator reads port 1 (coop_net.h). It uses run\data; the
# joiner (coop_join.ps1) uses run\data2, so the two processes never touch the same files: separate
# data dirs, separate rig shots/cmd dirs, separate logs (<data>\runs\<stamp>).
#
# Never touches SAVE GAME, the disc, or the original save data.
param(
  [int]$Port = 7777,
  [int]$Seconds = 2400,
  [string]$Tag = "host",
  [double]$ShotEvery = 2,
  [string]$Overlay = "payload\overlay",
  [switch]$Req
)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot

$env:AC4_COOP = '1'
$env:AC4_COOP_HOST = "$Port"
Remove-Item Env:AC4_COOP_JOIN -ErrorAction SilentlyContinue
$env:AC4_COOP_POKE_FILE = Join-Path $here "shots\$Tag\poke.txt"
if (-not $env:AC4_COOP_DUMP) { $env:AC4_COOP_DUMP = '1' }

& (Join-Path $here 'coop_launch.ps1') -Tag $Tag -Data 'run\data' -Seconds $Seconds -ShotEvery $ShotEvery `
  -Overlay $Overlay -Req:$Req
