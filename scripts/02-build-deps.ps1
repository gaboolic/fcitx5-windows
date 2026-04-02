#Requires -Version 5.1
<#
.SYNOPSIS
  Step 2 wrapper — run scripts/02-build-deps.sh in an MSYS2 CLANG64 environment (same toolchain as opening the CLANG64 shell).

.DESCRIPTION
  Sets MSYSTEM=CLANG64 and PATH so Ninja/cmake/clang from /clang64/bin are used. Environment variables you set in this PowerShell session (e.g. STAGE, SKIP_FCITX_WINDOWS, LIBIME_SRC) are inherited by bash.
  On MSYS2, 02-build-deps.sh applies patches/fcitx5-chinese-addons-msys2-clang-libcxx.patch to a clean fcitx5-chinese-addons tree (libc++/Windows); set CHINESE_ADDONS_SKIP_PATCH=1 to skip.
  It also applies patches/windows-cross-msys2-toolchain-arm64-llvm-windres.patch to the windows-cross submodule (fcitx-contrib @ 10ede74); set WINDOWS_CROSS_SKIP_PATCH=1 to skip.

.PARAMETER MsysRoot
  MSYS2 root (default C:\msys64).

.PARAMETER BashArgs
  Extra arguments forwarded to 02-build-deps.sh (they are appended to the first fcitx5-windows cmake when SKIP_FCITX_WINDOWS is unset).

.EXAMPLE
  .\scripts\02-build-deps.ps1
.EXAMPLE
  $env:SKIP_FCITX_WINDOWS = '1'; .\scripts\02-build-deps.ps1
#>
param(
    [string] $MsysRoot = 'C:\msys64',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $BashArgs = @()
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Split-Path $PSScriptRoot -Parent)).Path

$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
$cygpath = Join-Path $MsysRoot 'usr\bin\cygpath.exe'
foreach ($p in @($bash, $cygpath)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Error "Missing: $p — install MSYS2 CLANG64 toolchain or set -MsysRoot."
    }
}

$repoUnix = (& $cygpath -u $RepoRoot).Trim()

function Escape-BashSingleQuoted {
    param([string] $Value)
    $Value -replace "'", "'\''"
}
$qRepo = Escape-BashSingleQuoted $repoUnix
$argTail = ''
if ($BashArgs.Count -gt 0) {
    $argTail = ' ' + (($BashArgs | ForEach-Object { "'" + (Escape-BashSingleQuoted $_) + "'" }) -join ' ')
}

$cmd = @"
set -euo pipefail
export MSYSTEM=CLANG64
export PATH=/clang64/bin:/usr/bin:`$PATH
cd '$qRepo'
exec ./scripts/02-build-deps.sh$argTail
"@
$cmd = $cmd -replace "`r`n", "`n"

Write-Host "MSYS2 CLANG64: ./scripts/02-build-deps.sh$argTail"
& $bash -e -c $cmd
exit $LASTEXITCODE
