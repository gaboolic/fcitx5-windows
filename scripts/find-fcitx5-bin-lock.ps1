#Requires -Version 5.1
<#
.SYNOPSIS
  List processes that load DLLs/exe from the portable deploy bin (common cause of robocopy ERROR 32 during 04-deploy-to-portable.ps1).

.PARAMETER DeployDir
  Same root as 04-deploy (default C:\Fcitx5Portable).

.PARAMETER Stop
  End processes that hold DLLs under DeployDir\bin (or fcitx5 modules). Excludes the current PowerShell PID.

.PARAMETER NoExplorer
  With -Stop, do not stop explorer.exe (desktop may still lock IME DLLs; deploy can keep failing until you sign out or restart explorer manually).

.EXAMPLE
  .\scripts\find-fcitx5-bin-lock.ps1
.EXAMPLE
  .\scripts\find-fcitx5-bin-lock.ps1 -DeployDir D:\Fcitx5Portable
.EXAMPLE
  .\scripts\find-fcitx5-bin-lock.ps1 -Stop
.EXAMPLE
  .\scripts\find-fcitx5-bin-lock.ps1 -Stop -NoExplorer
#>
param(
    [string] $DeployDir = 'C:\Fcitx5Portable',
    [switch] $Stop,
    [switch] $NoExplorer
)

$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Fcitx5-Ime.Common.ps1'
. $common

Write-Host "Scanning for processes using: $DeployDir\bin (and fcitx5*x86_64*.dll anywhere)..."
Write-Host ""
$rows = Get-FcitxImeProcessesLockingBin -DeployDir $DeployDir
if (-not $rows -or $rows.Count -eq 0) {
    Write-Host "No matching process found. If deploy still fails:"
    Write-Host "  - Run PowerShell as Administrator and retry this script (some processes are not enumerable)."
    Write-Host "  - Or use Sysinternals Handle: handle.exe -a -p fcitx5"
    exit 0
}
$rows | Format-Table -AutoSize ProcessId, ProcessName, Kind, Path
Write-Host ""

if ($Stop) {
    $pids = $rows | Select-Object -ExpandProperty ProcessId -Unique | Sort-Object
    $myPid = $PID
    $stopped = New-Object System.Collections.Generic.List[string]
    $skipped = New-Object System.Collections.Generic.List[string]
    foreach ($procId in $pids) {
        if ($procId -eq $myPid) {
            $skipped.Add("PID $procId (current shell)")
            continue
        }
        try {
            $p = Get-Process -Id $procId -ErrorAction Stop
        }
        catch {
            $skipped.Add("PID $procId (already exited)")
            continue
        }
        if ($NoExplorer -and $p.ProcessName -eq 'explorer') {
            $skipped.Add("PID $procId explorer (NoExplorer)")
            continue
        }
        try {
            Stop-Process -Id $procId -Force -ErrorAction Stop
            $stopped.Add("$($p.ProcessName) (PID $procId)")
        }
        catch {
            Write-Warning "Could not stop PID ${procId} $($p.ProcessName): $_"
        }
    }
    if ($stopped.Count -gt 0) {
        Write-Host "Stopped:"
        $stopped | ForEach-Object { Write-Host "  $_" }
    }
    if ($skipped.Count -gt 0) {
        Write-Host "Skipped:"
        $skipped | ForEach-Object { Write-Host "  $_" }
    }
    if ($NoExplorer -and ($rows | Where-Object { $_.ProcessName -eq 'explorer' })) {
        Write-Host ""
        Write-Host "Note: explorer.exe was not stopped; if robocopy still reports ERROR 32, run again without -NoExplorer or restart Explorer from Task Manager."
    }
    Write-Host ""
    Write-Host "You can rerun deploy (e.g. 04-deploy-to-portable.ps1) now."
    exit 0
}

Write-Host "To free files: close these apps, end Fcitx5ImePipeServer if listed, switch Windows input method away from Fcitx5, or sign out."
Write-Host "Or run: .\scripts\find-fcitx5-bin-lock.ps1 -DeployDir `"$DeployDir`" -Stop"
