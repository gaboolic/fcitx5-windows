#Requires -Version 5.1
<#
.SYNOPSIS
  Back-compat wrapper: install or uninstall Fcitx5 TSF IME (delegates to install-fcitx5-ime.ps1 / uninstall-fcitx5-ime.ps1).

.PARAMETER Stage
  Source prefix (required for install unless -SkipCopy).

.PARAMETER Unregister
  Uninstall mode: regsvr32 /u + registry purge.

.PARAMETER RemoveFiles
  With -Unregister: also delete DeployDir after unregister (-Force skips prompt).

.EXAMPLE
  .\deploy-ime-stage.ps1 -Stage D:\stage-pinyin -DeployDir C:\Fcitx5Portable
  .\deploy-ime-stage.ps1 -DeployDir C:\Fcitx5Portable -Unregister
  .\deploy-ime-stage.ps1 -DeployDir C:\Fcitx5Portable -Unregister -RemoveFiles -Force
#>
param(
    [string] $Stage,
    [Parameter(Mandatory = $true)]
    [string] $DeployDir,
    [switch] $Unregister,
    [switch] $SkipCopy,
    [switch] $RemoveFiles,
    [switch] $Force,
    [ValidateSet('auto', 'en', 'zh')]
    [string] $UICulture = 'auto'
)

$ErrorActionPreference = 'Stop'
$DeployDir = $DeployDir.TrimEnd('\', '/')

if ($Unregister) {
    $uArgs = @{ DeployDir = $DeployDir; UICulture = $UICulture }
    if ($RemoveFiles) { $uArgs.RemoveFiles = $true }
    if ($Force) { $uArgs.Force = $true }
    & "$PSScriptRoot\uninstall-fcitx5-ime.ps1" @uArgs
    exit $LASTEXITCODE
}

if ([string]::IsNullOrWhiteSpace($Stage)) {
    Write-Error 'Stage is required when installing (omit only with -Unregister).'
}

$iArgs = @{ Stage = $Stage; DeployDir = $DeployDir; UICulture = $UICulture }
if ($SkipCopy) { $iArgs.SkipCopy = $true }
& "$PSScriptRoot\install-fcitx5-ime.ps1" @iArgs
exit 0
