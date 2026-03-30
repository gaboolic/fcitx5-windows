#Requires -Version 5.1
<#
.SYNOPSIS
  Compile Fcitx5Tsf.iss with Inno Setup 6 (requires ISCC.exe on PATH or default path).

.PARAMETER StageDir
  Root of the deploy tree (same layout as robocopy target: bin\, share\, lib\, etc.).

.PARAMETER IssPath
  Optional path to Fcitx5Tsf.iss (default: installer dir next to this script).

.EXAMPLE
  .\installer\build-installer.ps1 -StageDir D:\fcitx5-windows\stage
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $StageDir,
    [string] $IssPath = ''
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $StageDir)) {
    Write-Error "StageDir not found: $StageDir"
}
$StageDir = (Resolve-Path -LiteralPath $StageDir).Path

$bin1 = Join-Path $StageDir 'bin\libfcitx5-x86_64.dll'
$bin2 = Join-Path $StageDir 'bin\fcitx5-x86_64.dll'
if (-not ((Test-Path -LiteralPath $bin1) -or (Test-Path -LiteralPath $bin2))) {
    Write-Error "Stage bin\ must contain libfcitx5-x86_64.dll or fcitx5-x86_64.dll"
}

if ([string]::IsNullOrWhiteSpace($IssPath)) {
    $IssPath = Join-Path $PSScriptRoot 'Fcitx5Tsf.iss'
}
if (-not (Test-Path -LiteralPath $IssPath)) {
    Write-Error "ISS not found: $IssPath"
}

$candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
    'ISCC.exe'
)
$iscc = $null
foreach ($c in $candidates) {
    if ($c -eq 'ISCC.exe') {
        $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($cmd) { $iscc = $cmd.Path; break }
    } elseif (Test-Path -LiteralPath $c) {
        $iscc = $c
        break
    }
}
if (-not $iscc) {
    Write-Error @"
Inno Setup 6 compiler (ISCC.exe) not found.
Install: https://jrsoftware.org/isdl.php
Then re-run this script.
"@
}

$argStage = '/DStageDir=' + $StageDir
Write-Host "ISCC: $iscc"
Write-Host "Args: `"$IssPath`" $argStage"
$p = Start-Process -FilePath $iscc -ArgumentList @("`"$IssPath`"", $argStage) -Wait -PassThru -NoNewWindow
if ($p.ExitCode -ne 0) {
    Write-Error "ISCC failed with exit code $($p.ExitCode)"
}

$dist = Join-Path $PSScriptRoot 'dist'
Write-Host "Output: $(Join-Path $dist 'fcitx5-windows-setup.exe')"
exit 0
