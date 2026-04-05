#Requires -Version 5.1
<#
.SYNOPSIS
  Step 3 — Compile Inno Setup installer from a complete stage directory (fcitx5-windows-setup.exe).

.DESCRIPTION
  End users who install the generated EXE get the same files + IME registration as running 04-deploy-to-portable.ps1 and 05-register-ime.ps1 locally (Inno runs regsvr32 during install).

.PARAMETER StageDir
  Root with bin\, lib\, share\ (e.g. <repo>/stage after 02-build-deps.sh).

.EXAMPLE
  .\scripts\03-build-installer.ps1 -StageDir D:\fcitx5-windows\stage
#>
param(
  [Parameter(Mandatory = $true)]
  [string] $StageDir = 'D:\vscode\fcitx_projs\fcitx5-windows\stage'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$installer = Join-Path $repo 'installer\build-installer.ps1'
if (-not (Test-Path -LiteralPath $installer)) {
  Write-Error "Missing: $installer"
}
& $installer -StageDir $StageDir
