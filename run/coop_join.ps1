# coop_join.ps1 -- instance B of the two-instance co-op test (milestone M3/M4).
#
#   .\coop_join.ps1 [-Host 127.0.0.1] [-Port 7777] [-Seconds 2400] [-Tag join]
#
# Instance B is the JOINER: AC4_COOP_JOIN=<ip>:<port> gives it the peer's address, so its port-1 pad
# operator is driven by whatever instance A sends. It uses run\data2 (a copy of run\data, made by
# coop_setup_data2.ps1) with its own rig shots/cmd dir and its own run log.
#
# Never touches SAVE GAME, the disc, or the original save data.
param(
  [string]$HostAddr = "127.0.0.1",
  [int]$Port = 7777,
  [int]$Seconds = 2400,
  [string]$Tag = "join",
  [double]$ShotEvery = 2,
  [string]$Overlay = "payload\overlay",
  [switch]$Req
)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot

$env:AC4_COOP = '1'
$env:AC4_COOP_JOIN = "${HostAddr}:${Port}"
Remove-Item Env:AC4_COOP_HOST -ErrorAction SilentlyContinue
# The join is host-authoritative: freeze its own published entity simulation (research\27 A1).
# Set AC4_COOP_FREEZE=0 before calling to opt out.
if (-not $env:AC4_COOP_FREEZE) { $env:AC4_COOP_FREEZE = '1' }
$env:AC4_COOP_POKE_FILE = Join-Path $here "shots\$Tag\poke.txt"
if (-not $env:AC4_COOP_DUMP) { $env:AC4_COOP_DUMP = '1' }

& (Join-Path $here 'coop_launch.ps1') -Tag $Tag -Data 'run\data2' -Seconds $Seconds -ShotEvery $ShotEvery `
  -Overlay $Overlay -Req:$Req
