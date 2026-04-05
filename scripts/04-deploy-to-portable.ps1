#Requires -Version 5.1
<#
.SYNOPSIS
  Step 4 — Robocopy a full stage tree into C:\Fcitx5Portable (or another deploy root). Does not register IME.

.PARAMETER Stage
  Source prefix (cmake install prefix: same layout as 01/02 build output, usually repo\stage).
  If omitted, the script prompts (with a default path).

.PARAMETER DeployDir
  Portable root (default C:\Fcitx5Portable).

.PARAMETER SkipCopy
  Skip robocopy (e.g. files already copied); still validates layout is unnecessary here — use 05 only.

.EXAMPLE
  .\scripts\04-deploy-to-portable.ps1 -Stage D:\fcitx5-windows\stage -DeployDir C:\Fcitx5Portable
.EXAMPLE
  .\scripts\04-deploy-to-portable.ps1
  # Prompts for Stage; press Enter to use default ..\stage next to this repo.
#>
param(
  [string] $Stage,
  [string] $DeployDir = 'C:\Fcitx5Portable',
  [switch] $SkipCopy
)

$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Fcitx5-Ime.Common.ps1'
. $common

function Sync-FcitxPortableDataLayout {
  param([string] $DeployDir)

  $shareDir = Join-Path $DeployDir 'share'
  $dataDir = Join-Path $DeployDir 'data'

  if (-not (Test-Path -LiteralPath $shareDir)) {
    Write-Warning "share not found under $DeployDir; skipped syncing data."
    return
  }

  New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
  Write-Host "Syncing share -> data"
  $rc = Start-Process -FilePath 'robocopy.exe' -ArgumentList @(
    $shareDir, $dataDir, '/MIR', '/COPY:DAT',
    '/R:3', '/W:2', '/NFL', '/NDL', '/NJH', '/NJS'
  ) -Wait -PassThru
  if ($rc.ExitCode -gt 7) {
    throw "robocopy share -> data exit $($rc.ExitCode)"
  }
}

if (-not $SkipCopy) {
  if ([string]::IsNullOrWhiteSpace($Stage)) {
    $defaultStage = Join-Path (Split-Path -Parent $PSScriptRoot) 'stage'
    Write-Host @"
Stage = cmake install prefix (same tree as after 01-build / 02-build-deps: bin, lib, share, ...).
"@
    $Stage = Read-Host "Stage path [default: $defaultStage]"
    if ([string]::IsNullOrWhiteSpace($Stage)) {
      $Stage = $defaultStage
    }
  }
  if (-not (Test-Path -LiteralPath $Stage)) {
    Write-Error "Stage not found: $Stage"
  }
  $Stage = (Resolve-Path -LiteralPath $Stage).Path
  $DeployDir = $DeployDir.TrimEnd('\', '/')
  Write-Host "Robocopy: $Stage -> $DeployDir"
  try {
    Copy-FcitxImeStage -Stage $Stage -DeployDir $DeployDir
  }
  catch {
    Write-Error "Robocopy failed: $($_.Exception.Message)"
  }
}
else {
  $DeployDir = $DeployDir.TrimEnd('\', '/')
}

try {
  Sync-FcitxPortableDataLayout -DeployDir $DeployDir
}
catch {
  Write-Error "Syncing share -> data failed: $($_.Exception.Message)"
}

$binDir = Join-Path $DeployDir 'bin'
if (-not (Get-FcitxImeDll -BinDir $binDir)) {
  Write-Error "IME DLL not found under $binDir"
}
Write-Host "Deploy tree ready: $DeployDir (run 05-register-ime.ps1 to register TSF)"
