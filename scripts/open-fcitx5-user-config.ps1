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

.PARAMETER LaunchSettingsGui
  Run fcitx5-config-win32.exe if found (same GUI as bin\ next to TSF IME). Tries
  $env:FCITX5_BIN, $env:FCITX5_HOME\bin, PATH.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\open-fcitx5-user-config.ps1
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\open-fcitx5-user-config.ps1 -EditProfile -EditGlobalConfig
.EXAMPLE
  $env:FCITX5_BIN = 'C:\Fcitx5Portable\bin'; .\scripts\open-fcitx5-user-config.ps1 -LaunchSettingsGui
#>
param(
    [switch] $EditProfile,
    [switch] $EditGlobalConfig,
    [switch] $LaunchSettingsGui
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

if ($LaunchSettingsGui) {
    $name = 'fcitx5-config-win32.exe'
    $dirs = @(
        $env:FCITX5_BIN,
        $(if ($env:FCITX5_HOME) { Join-Path $env:FCITX5_HOME 'bin' })
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $started = $false
    foreach ($d in $dirs) {
        $exe = Join-Path $d $name
        if (Test-Path -LiteralPath $exe) {
            Start-Process -FilePath $exe
            $started = $true
            break
        }
    }
    if (-not $started) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            Start-Process -FilePath $cmd.Source
            $started = $true
        }
    }
    if (-not $started) {
        Write-Warning "fcitx5-config-win32.exe not found. Set FCITX5_BIN to your install ...\bin (portable layout), or add bin to PATH."
    }
}
