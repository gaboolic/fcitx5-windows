#Requires -Version 5.1
<#
.SYNOPSIS
  Install Fcitx5 TSF IME: robocopy stage -> deploy dir, then regsvr32 register.

.PARAMETER UICulture
  Message language: en | zh | auto (default: auto from system UI culture).

.EXAMPLE
  # Admin PowerShell
  .\install-fcitx5-ime.ps1 -Stage D:\fcitx5-windows\stage-pinyin -DeployDir C:\Fcitx5Portable
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $Stage,
    [Parameter(Mandatory = $true)]
    [string] $DeployDir,
    [switch] $SkipCopy,
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

if (-not $SkipCopy) {
    if (-not (Test-Path -LiteralPath $Stage)) {
        Write-Error ($S.StageMissing + " $Stage")
    }
    $Stage = (Resolve-Path -LiteralPath $Stage).Path
    $DeployDir = $DeployDir.TrimEnd('\', '/')
    Write-Host ($S.Robocopy + " $Stage -> $DeployDir")
    try {
        Copy-FcitxImeStage -Stage $Stage -DeployDir $DeployDir
    } catch {
        Write-Error "$($S.RobocopyFail) $($_.Exception.Message)"
    }
} else {
    $DeployDir = $DeployDir.TrimEnd('\', '/')
}

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

exit 0
