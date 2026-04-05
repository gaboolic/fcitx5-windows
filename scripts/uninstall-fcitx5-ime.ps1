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

function Remove-FcitxPathTree {
    param([string] $Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) {
        return $true
    }
    try {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
        return $true
    }
    catch {
        Write-Warning "Remove failed: $Path ($($_.Exception.Message))"
        return $false
    }
}

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
    $libDir = Join-Path $DeployDir 'lib'
    Write-Host "Stopping processes that still use $binDir ..."
    $stopInfo = Stop-FcitxImeProcessesLockingBin -DeployDir $DeployDir
    foreach ($line in $stopInfo.Stopped) {
        Write-Host "  stopped: $line"
    }
    foreach ($line in $stopInfo.Skipped) {
        Write-Host "  skipped: $line"
    }

    $allRemoved = $true
    foreach ($path in @($binDir, $libDir, $DeployDir)) {
        if (-not (Remove-FcitxPathTree -Path $path)) {
            $allRemoved = $false
        }
    }

    if (-not $allRemoved) {
        Write-Host 'Retrying removal after stopping explorer-hosted locks ...'
        $stopInfo = Stop-FcitxImeProcessesLockingBin -DeployDir $DeployDir -IncludeExplorer
        foreach ($line in $stopInfo.Stopped) {
            Write-Host "  stopped: $line"
        }
        foreach ($path in @($binDir, $libDir, $DeployDir)) {
            Remove-FcitxPathTree -Path $path | Out-Null
        }
    }

    $residual = @($binDir, $libDir, $DeployDir) | Where-Object {
        Test-Path -LiteralPath $_
    }
    if ($residual.Count -gt 0) {
        Write-Warning ("Residual paths remain after uninstall: " + ($residual -join ', '))
    }
}

exit 0
