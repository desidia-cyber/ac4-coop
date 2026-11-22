# coop_run_driver.ps1 -- run coop_drive.py detached so it survives the calling shell.
#   .\coop_run_driver.ps1 -Tag host -Shots <dir> -Runs <dir> [-Seq m4] [-After 30]
param(
  [Parameter(Mandatory = $true)][string]$Tag,
  [Parameter(Mandatory = $true)][string]$Shots,
  [Parameter(Mandatory = $true)][string]$Runs,
  [string]$Seq = "",
  [double]$After = 15
)
$ErrorActionPreference = 'Continue'
$base = Split-Path -Parent $PSScriptRoot
$log = Join-Path (Join-Path $base 'run') ("drive_" + $Tag + ".log")
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$script:py = Join-Path $base 'run\coop_drive.py'
$argList = @('-u', ('"' + $script:py + '"'), '--tag', $Tag, '--shots', ('"' + $Shots + '"'), '--runs', ('"' + $Runs + '"'),
             '--after', "$After")
if ($Seq) { $argList += @('--seq', $Seq) }
$p = Start-Process -FilePath 'python' -ArgumentList ($argList -join ' ') -PassThru -WindowStyle Hidden `
     -RedirectStandardOutput $log -RedirectStandardError ($log + '.err') `
     -WorkingDirectory $base
Write-Host "[$Tag] driver pid $($p.Id) -> $log"
