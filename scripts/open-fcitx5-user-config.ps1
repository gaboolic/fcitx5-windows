#Requires -Version 5.0
<#
.SYNOPSIS
  Open the Fcitx5 per-user config directory (Windows layout).

.DESCRIPTION
  Opens %AppData%\Roaming\Fcitx5 in Explorer. Optional -EditProfile /
  -EditGlobalConfig launch Notepad on the usual fcitx5 paths (create parent dirs
  if missing). TSF IME reads the same files as a desktop Fcitx5 install.

  Global hotkeys relevant to TSF (see fcitx GlobalConfig):
  - TriggerKeys / EnumerateForwardKeys / AltTriggerKeys (default includes Shift_L)
  - EnumerateGroupForwardKeys (default Super+space)

.PARAMETER EditProfile
  Open profile (first existing: profile, conf\fcitx5\profile) in notepad.exe.

.PARAMETER EditGlobalConfig
  Open conf\fcitx5\config (global options incl. DefaultPageSize) in notepad.exe.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\open-fcitx5-user-config.ps1
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\open-fcitx5-user-config.ps1 -EditProfile -EditGlobalConfig
#>
param(
    [switch] $EditProfile,
    [switch] $EditGlobalConfig
)

$ErrorActionPreference = "Stop"
$configRoot = Join-Path $env:APPDATA "Fcitx5"
if (-not (Test-Path -LiteralPath $configRoot)) {
    New-Item -ItemType Directory -Path $configRoot | Out-Null
}
Start-Process explorer.exe -ArgumentList $configRoot

function Open-FirstExistingOrNew {
    param(
        [string[]] $Candidates
    )
    foreach ($rel in $Candidates) {
        $full = Join-Path $configRoot $rel
        if (Test-Path -LiteralPath $full) {
            return $full
        }
    }
    $first = Join-Path $configRoot $Candidates[0]
    $parent = Split-Path -Parent $first
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    return $first
}

if ($EditProfile) {
    $pf = Open-FirstExistingOrNew @('profile', 'conf\fcitx5\profile')
    Start-Process notepad.exe -ArgumentList "`"$pf`""
}
if ($EditGlobalConfig) {
    $gc = Open-FirstExistingOrNew @('conf\fcitx5\config')
    Start-Process notepad.exe -ArgumentList "`"$gc`""
}
