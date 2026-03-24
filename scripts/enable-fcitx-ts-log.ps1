#Requires -Version 5.1
<#
.SYNOPSIS
  Set user env vars so the TSF IME writes fcitx logs (restart apps after).

.PARAMETER LogPath
  Full path to log file (parent directory will be created if needed).

.EXAMPLE
  .\scripts\enable-fcitx-ts-log.ps1
.EXAMPLE
  .\scripts\enable-fcitx-ts-log.ps1 -LogPath "$env:USERPROFILE\Desktop\fcitx5-tsf.log"
#>
param(
    [string] $LogPath = (Join-Path $env:USERPROFILE 'Desktop\fcitx5-tsf.log')
)

$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $LogPath
if ($dir -and -not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

# Persist for new processes (setx truncates at 1024 chars; avoid long paths).
setx FCITX_TS_LOG $LogPath | Out-Null
# Optional: key_trace=5 only is lighter than *=5
setx FCITX_TS_LOG_RULE '*=5' | Out-Null

# Current PowerShell session (launch notepad from this window to test without re-login):
$env:FCITX_TS_LOG = $LogPath
$env:FCITX_TS_LOG_RULE = '*=5'

Write-Host "Set FCITX_TS_LOG=$LogPath and FCITX_TS_LOG_RULE=*=5 (user + this session)."
Write-Host "Log file path (open in editor after repro):"
Write-Host "  $LogPath"
Write-Host "Note: TSF runs inside ctfmon.exe. After setx, new log env applies only to NEW ctfmon."
Write-Host '  Easiest: sign out/in, or restart PC. To test quickly: run Notepad from THIS PowerShell (inherits $env:FCITX_TS_LOG).'
Write-Host "1) Close Notepad / apps using the IME."
Write-Host "2) Deploy libfcitx5-x86_64.dll if needed."
Write-Host "3) Start Notepad (from this window for session env, or re-login for setx)."
Write-Host "4) Reproduce typing; open the log file above."
