#Requires -Version 5.1
<#
.SYNOPSIS
  Uninstall Fcitx5 TSF IME: regsvr32 /u, then remove CLSID + CTF TIP registry trees; optionally delete deploy files.

.PARAMETER PurgeRegistryOnly
  Only delete registry keys (no DLL required). Use if IME files were removed manually.

.PARAMETER RemoveFiles
  After unregister, delete the entire DeployDir tree (requires confirmation unless -Force).

.PARAMETER PurgeHkcu
  Also remove HKCU\...\CTF\TIP\{CLSID} and HKCU\...\Classes\CLSID\{CLSID} if present
  (safe for current user; may fix stale language bar entries after uninstall).

.PARAMETER UICulture
  en | zh | auto

.EXAMPLE
  .\uninstall-fcitx5-ime.ps1 -DeployDir C:\Fcitx5Portable

.EXAMPLE
  .\uninstall-fcitx5-ime.ps1 -PurgeRegistryOnly

.EXAMPLE
  .\uninstall-fcitx5-ime.ps1 -DeployDir C:\Fcitx5Portable -PurgeHkcu
#>
param(
    [string] $DeployDir,
    [switch] $PurgeRegistryOnly,
    [switch] $PurgeHkcu,
    [switch] $RemoveFiles,
    [switch] $Force,
    [ValidateSet('auto', 'en', 'zh')]
    [string] $UICulture = 'auto'
)

$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Fcitx5-Ime.Common.ps1'
. $common

$lang = if ($UICulture -eq 'auto') { Resolve-FcitxImeLang } else { $UICulture }
$S = Get-FcitxImeStrings -Lang $lang

if ($PurgeRegistryOnly) {
    if (-not (Test-FcitxImeAdmin)) {
        Write-Warning $S.AdminWarn
    }
    Write-Host $S.PurgeReg
    try {
        Remove-FcitxImeRegistryTree
    } catch {
        Write-Warning "Registry: $($_.Exception.Message)"
    }
    Write-Host $S.PurgeDone
    if ($PurgeHkcu) {
        Write-Host $S.HkcuPurge
        try {
            Remove-FcitxImeHkcuResidualRegistry
        } catch {
            Write-Warning "HKCU: $($_.Exception.Message)"
        }
        Write-Host $S.HkcuDone
    }
    exit 0
}

if ([string]::IsNullOrWhiteSpace($DeployDir)) {
    Write-Error 'DeployDir is required unless -PurgeRegistryOnly is set.'
}

$DeployDir = $DeployDir.TrimEnd('\', '/')
$binDir = Join-Path $DeployDir 'bin'
$imeDll = Get-FcitxImeDll -BinDir $binDir

if (-not (Test-FcitxImeAdmin)) {
    Write-Warning $S.AdminWarn
}

if ($imeDll) {
    Write-Host ($S.Unregister + " $($imeDll.FullName)")
    $code = Invoke-FcitxImeUnregister -Dll $imeDll
    if ($code -ne 0) {
        Write-Warning "regsvr32 /u returned $code (continuing with registry purge)."
    }
} else {
    Write-Warning "$($S.ImeMissing) $binDir — $($S.PurgeReg)"
}

try {
    Remove-FcitxImeRegistryTree
} catch {
    Write-Warning "Registry purge: $($_.Exception.Message)"
}
Write-Host $S.PurgeDone

if ($PurgeHkcu) {
    Write-Host $S.HkcuPurge
    try {
        Remove-FcitxImeHkcuResidualRegistry
    } catch {
        Write-Warning "HKCU purge: $($_.Exception.Message)"
    }
    Write-Host $S.HkcuDone
}

if ($RemoveFiles) {
    if (-not (Test-Path -LiteralPath $DeployDir)) {
        Write-Host $S.DeployAbsent
        exit 0
    }
    if (-not $Force) {
        Write-Host ($S.ConfirmRemove)
        $ans = Read-Host
        if ($ans -notmatch '^(?i)y(es)?$') {
            Write-Host $S.RemoveCancelled
            exit 0
        }
    }
    Write-Host ($S.RemoveTree + " $DeployDir")
    Remove-Item -LiteralPath $DeployDir -Recurse -Force
}

exit 0
