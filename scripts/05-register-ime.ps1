#Requires -Version 5.1
<#
.SYNOPSIS
  Step 5 — regsvr32 the Fcitx5 TSF DLL under a portable (or installed) root.

.DESCRIPTION
  Requires Administrator for HKCR registration in typical setups. Prepends DeployDir\bin to PATH for dependency DLLs during registration.

.PARAMETER DeployDir
  Root that contains bin\libfcitx5-x86_64.dll (or fcitx5-x86_64.dll).

.EXAMPLE
  .\scripts\05-register-ime.ps1 -DeployDir C:\Fcitx5Portable
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $DeployDir,
    [ValidateSet('auto', 'en', 'zh')]
    [string] $UICulture = 'auto'
)

$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Fcitx5-Ime.Common.ps1'
. $common

$lang = if ($UICulture -eq 'auto') { Resolve-FcitxImeLang } else { $UICulture }
$S = Get-FcitxImeStrings -Lang $lang

if (-not (Test-FcitxImeAdmin)) {
    Write-Warning $S.AdminWarn
}

$DeployDir = $DeployDir.TrimEnd('\', '/')
$binDir = Join-Path $DeployDir 'bin'
$imeDll = Get-FcitxImeDll -BinDir $binDir
if (-not $imeDll) {
    Write-Error "$($S.ImeMissing) $binDir"
}

$ico = Join-Path $binDir 'penguin.ico'
if (-not (Test-Path -LiteralPath $ico)) {
    Write-Warning $S.IcoWarn
}

Write-Host ($S.Register + " $($imeDll.FullName)")
try {
    Invoke-FcitxImeRegister -Dll $imeDll
} catch {
    Write-Error "$($S.RegsvrFail) $($_.Exception.Message)"
}

Write-Host ''
Write-Host ($S.DeployRoot + " $DeployDir")
Write-Host $S.NextSettings
Write-Host $S.TestNotepad
Write-Host $S.ProfileHint
