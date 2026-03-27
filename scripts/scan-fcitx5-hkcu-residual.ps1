#Requires -Version 5.1
<#
.SYNOPSIS
  List per-user (HKCU) registry keys that may still reference Fcitx5 TSF after machine uninstall.

.DESCRIPTION
  Windows may keep caches under HKCU\Software\Microsoft\CTF\TIP\{CLSID} and
  HKCU\Software\Classes\CLSID\{CLSID}. Machine unregister clears HKCR/HKLM;
  these HKCU trees are optional to remove (see uninstall -PurgeHkcu).

.PARAMETER FailIfFound
  Exit with code 1 if any residual path exists (for CI / automation).

.EXAMPLE
  .\scripts\scan-fcitx5-hkcu-residual.ps1
.EXAMPLE
  .\scripts\scan-fcitx5-hkcu-residual.ps1 -FailIfFound
#>
param(
    [switch] $FailIfFound
)

$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Fcitx5-Ime.Common.ps1'
. $common

$found = @(Get-FcitxImeHkcuResidualRegistryPaths)
if ($found.Count -eq 0) {
    Write-Host 'HKCU: no Fcitx5 TSF residual keys (TIP/CLSID) found.'
    exit 0
}

foreach ($p in $found) {
    Write-Host "HKCU residual: $p"
}
if ($FailIfFound) {
    exit 1
}
exit 0
