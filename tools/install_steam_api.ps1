# Obtain steam_api64.dll at install time from Steamworks.NET (rlabrecque) releases.
# The Steam API DLL is NOT bundled in this repository.
#
# Usage:
#   .\tools\install_steam_api.ps1 [-OutDir <dir>] [-Version 2025.165.0] [-Force]
param(
  [string]$OutDir = "",
  [string]$Version = "2025.165.0",
  [switch]$Force
)
$ErrorActionPreference = 'Stop'
$base = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) {
  $OutDir = Join-Path $base 'ac4-port\local\build\release'
}

$expectedSha256 = @{
  "2025.165.0" = "e6d9bafb9a41e42fba7b21553db49f8719027af3f0deb23ff86cfc44d60e776d"
  "2025.164.1" = "eb17909a76668cf9ae0b92a618a34a50f6c73d3a6787cb4dd8ce36a8b10bfb75"
}

$destDll = Join-Path $OutDir 'steam_api64.dll'
if ((Test-Path $destDll) -and (-not $Force)) {
  if ($expectedSha256.ContainsKey($Version)) {
    $curHash = (Get-FileHash $destDll -Algorithm SHA256).Hash.ToLower()
    if ($curHash -eq $expectedSha256[$Version]) {
      Write-Host "steam_api64.dll ($Version) already present and verified at $destDll" -ForegroundColor Green
      exit 0
    }
  } else {
    Write-Host "steam_api64.dll already present at $destDll (pass -Force to re-download)" -ForegroundColor Green
    exit 0
  }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$zipName = "Steamworks.NET-Standalone_${Version}.zip"
$url = "https://github.com/rlabrecque/Steamworks.NET/releases/download/${Version}/${zipName}"
$tmpDir = Join-Path $env:TEMP ("ac4_steamworks_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
$tmpZip = Join-Path $tmpDir $zipName
$tmpDll = Join-Path $tmpDir 'steam_api64.dll'

try {
  Write-Host "Downloading Steamworks.NET Standalone $Version from GitHub..." -ForegroundColor Cyan
  Write-Host "  $url"
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  Invoke-WebRequest -Uri $url -OutFile $tmpZip -UseBasicParsing -Headers @{ "User-Agent" = "ac4-coop" }

  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $arc = [System.IO.Compression.ZipFile]::OpenRead($tmpZip)
  try {
    $entry = $arc.Entries | Where-Object { $_.FullName -eq "Windows-x64/steam_api64.dll" } | Select-Object -First 1
    if (-not $entry) {
      throw "Windows-x64/steam_api64.dll not found inside $zipName"
    }
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $tmpDll, $true)
  } finally {
    $arc.Dispose()
  }

  $hash = (Get-FileHash $tmpDll -Algorithm SHA256).Hash.ToLower()
  if ($expectedSha256.ContainsKey($Version) -and ($hash -ne $expectedSha256[$Version])) {
    throw "SHA-256 mismatch for steam_api64.dll! Expected $($expectedSha256[$Version]), got $hash"
  }

  Copy-Item -Path $tmpDll -Destination $destDll -Force
  Write-Host "Installed steam_api64.dll ($Version) to $destDll" -ForegroundColor Green
  Write-Host "  SHA-256: $hash"
} finally {
  if (Test-Path $tmpDir) {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
  }
}
