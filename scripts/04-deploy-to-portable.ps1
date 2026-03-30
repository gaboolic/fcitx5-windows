#Requires -Version 5.1
<#
.SYNOPSIS
  Step 4 — Robocopy a full stage tree into C:\Fcitx5Portable (or another deploy root). Does not register IME.

.PARAMETER Stage
  Source prefix (same layout as cmake install / 02-build-deps.sh output).

.PARAMETER DeployDir
  Portable root (default C:\Fcitx5Portable).

.PARAMETER SkipCopy
  Skip robocopy (e.g. files already copied); still validates layout is unnecessary here — use 05 only.

.EXAMPLE
  .\scripts\04-deploy-to-portable.ps1 -Stage D:\fcitx5-windows\stage -DeployDir C:\Fcitx5Portable
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $Stage,
    [string] $DeployDir = 'C:\Fcitx5Portable',
    [switch] $SkipCopy
)

$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Fcitx5-Ime.Common.ps1'
. $common

if (-not $SkipCopy) {
    if (-not (Test-Path -LiteralPath $Stage)) {
        Write-Error "Stage not found: $Stage"
    }
    $Stage = (Resolve-Path -LiteralPath $Stage).Path
    $DeployDir = $DeployDir.TrimEnd('\', '/')
    Write-Host "Robocopy: $Stage -> $DeployDir"
    try {
        Copy-FcitxImeStage -Stage $Stage -DeployDir $DeployDir
    } catch {
        Write-Error "Robocopy failed: $($_.Exception.Message)"
    }
} else {
    $DeployDir = $DeployDir.TrimEnd('\', '/')
}

$binDir = Join-Path $DeployDir 'bin'
if (-not (Get-FcitxImeDll -BinDir $binDir)) {
    Write-Error "IME DLL not found under $binDir"
}
Write-Host "Deploy tree ready: $DeployDir (run 05-register-ime.ps1 to register TSF)"
