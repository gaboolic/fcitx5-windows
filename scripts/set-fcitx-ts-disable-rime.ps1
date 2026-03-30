#Requires -Version 5.1
<#
.SYNOPSIS
  Set or clear the user environment variable FCITX_TS_DISABLE_RIME (TSF loads with --disable=rime).

.DESCRIPTION
  Persists under HKCU\Environment. The TSF DLL also reads that key directly (not only getenv), so
  Notepad and other apps do not need Explorer to inherit the variable first. Rebuild and redeploy
  libfcitx5-x86_64.dll after updating the IME for that behavior. WM_SETTINGCHANGE is still sent for shells.

.PARAMETER Remove
  Remove FCITX_TS_DISABLE_RIME from the user environment (re-enable Rime in TSF hosts that are not QQ/Cursor/Code).

.EXAMPLE
  .\scripts\set-fcitx-ts-disable-rime.ps1
.EXAMPLE
  .\scripts\set-fcitx-ts-disable-rime.ps1 -Remove
#>
param(
    [switch] $Remove
)

$ErrorActionPreference = 'Stop'
$name = 'FCITX_TS_DISABLE_RIME'

if ($Remove) {
    [System.Environment]::SetEnvironmentVariable($name, $null, 'User')
    Write-Host "Removed user env: $name (restart apps using Fcitx5 TSF to pick up)."
} else {
    [System.Environment]::SetEnvironmentVariable($name, '1', 'User')
    Write-Host "Set user env: $name=1 (HKCU\Environment; TSF reads registry + getenv)."
    Write-Host "Rebuild/redeploy libfcitx5-x86_64.dll if your copy does not include registry fallback yet."
}

# WM_SETTINGCHANGE so Explorer / some shells refresh environment without full reboot
if (-not ('Win32.FcitxEnvNotify' -as [type])) {
    Add-Type -Namespace Win32 -Name FcitxEnvNotify -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
'@ -ErrorAction Stop
}
$HWND_BROADCAST = [IntPtr]0xffff
$WM_SETTINGCHANGE = 0x001A
[UIntPtr]$result = [UIntPtr]::Zero
$null = [Win32.FcitxEnvNotify]::SendMessageTimeout(
    $HWND_BROADCAST,
    $WM_SETTINGCHANGE,
    [UIntPtr]::Zero,
    'Environment',
    0,
    5000,
    [ref]$result)
