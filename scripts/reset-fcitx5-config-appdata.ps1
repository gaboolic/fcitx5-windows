#Requires -Version 5.1
<#
.SYNOPSIS
  Move %AppData%\Fcitx5\config to config.backup.<timestamp> and create an empty config folder.
  Next fcitx5 / IME start will regenerate defaults.
#>
$ErrorActionPreference = 'Stop'
$cfg = Join-Path $env:APPDATA 'Fcitx5\config'
if (-not (Test-Path -LiteralPath $cfg)) {
    Write-Host "Nothing to reset (missing): $cfg"
    exit 0
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bak = Join-Path $env:APPDATA "Fcitx5\config.backup.$stamp"
Move-Item -LiteralPath $cfg -Destination $bak -Force
New-Item -ItemType Directory -Path $cfg -Force | Out-Null
Write-Host "Backed up: $bak"
Write-Host "Fresh empty: $cfg"
Write-Host "Restart Fcitx5ImePipeServer / IME or log off if files were locked."
