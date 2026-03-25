#Requires -Version 5.1
<#
.SYNOPSIS
  Configure + build fcitx5-windows (core + TSF IME) with MSYS2 CLANG64, then install and deploy to a portable root.

.DESCRIPTION
  Full tree on Windows is built with Clang + Ninja from MSYS2 CLANG64 (see scripts/build-msys-full.sh).
  This script prepends CLANG64 to PATH so the wrong ninja/windres (e.g. from other MSYS prefixes) does not break the toolchain.

.PARAMETER BuildDir
  CMake build directory (default: <repo>/build-portable).

.PARAMETER InstallPrefix
  CMAKE_INSTALL_PREFIX for cmake --install (use forward slashes, e.g. C:/Fcitx5Portable).

.PARAMETER DeployDir
  Portable root passed to deploy-build-to-portable.ps1 (default: C:\Fcitx5Portable).

.PARAMETER MsysRoot
  MSYS2 installation root (default: C:\msys64). Compilers and cmake come from <MsysRoot>\clang64\bin.

.PARAMETER BuildType
  CMAKE_BUILD_TYPE (default: Release).

.PARAMETER Jobs
  Parallel build jobs for ninja (default: 8).

.PARAMETER SkipInstall
  Only build; do not run cmake --install.

.PARAMETER SkipDeploy
  Do not run deploy-build-to-portable.ps1 after install (ignored if SkipInstall is set).

.PARAMETER SkipTest
  Do not run ctest after build.

.PARAMETER SkipRegsvr32
  Passed to deploy-build-to-portable.ps1: do not register the IME DLL.

.PARAMETER UnregisterFirst
  Passed to deploy-build-to-portable.ps1: unregister IME before copy if the DLL is locked.

.PARAMETER ConfigureOnly
  Only run cmake configure (no build).

.EXAMPLE
  .\scripts\build-portable.ps1

.EXAMPLE
  .\scripts\build-portable.ps1 -BuildDir D:\work\fcitx5-build -InstallPrefix C:/Fcitx5Portable -Jobs 16

.EXAMPLE
  .\scripts\build-portable.ps1 -UnregisterFirst
#>
param(
    [string] $BuildDir = '',
    [string] $InstallPrefix = 'C:/Fcitx5Portable',
    [string] $DeployDir = 'C:\Fcitx5Portable',
    [string] $MsysRoot = 'C:\msys64',
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string] $BuildType = 'Release',
    [int] $Jobs = 8,
    [switch] $SkipInstall,
    [switch] $SkipDeploy,
    [switch] $SkipTest,
    [switch] $SkipRegsvr32,
    [switch] $UnregisterFirst,
    [switch] $ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot 'build-portable'
}

$clang64Bin = Join-Path $MsysRoot 'clang64\bin'
$cmakeExe = Join-Path $clang64Bin 'cmake.exe'
$clangExe = Join-Path $clang64Bin 'clang.exe'
$clangxxExe = Join-Path $clang64Bin 'clang++.exe'

foreach ($p in @($cmakeExe, $clangExe, $clangxxExe)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Error "Missing: $p — install MSYS2 package mingw-w64-clang-x86_64-toolchain (CLANG64) or set -MsysRoot."
    }
}

$clang64Posix = ($MsysRoot + '\clang64') -replace '\\', '/'
$cCompiler = "$clang64Posix/bin/clang.exe"
$cxxCompiler = "$clang64Posix/bin/clang++.exe"

# Prefer CLANG64 tools first (cmake, ninja, ctest) and avoid a stale PATH breaking the generator.
$usrBin = Join-Path $MsysRoot 'usr\bin'
$env:PATH = "$clang64Bin;$usrBin;$env:PATH"

Write-Host "Repo:       $RepoRoot"
Write-Host "Build dir:  $BuildDir"
Write-Host "Build type: $BuildType"
Write-Host "Install:    $InstallPrefix"
Write-Host "CMake:      $cmakeExe"
Write-Host ''

$configureArgs = @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_INSTALL_PREFIX=$InstallPrefix",
    "-DCMAKE_C_COMPILER=$cCompiler",
    "-DCMAKE_CXX_COMPILER=$cxxCompiler",
    '-DFCITX5_WINDOWS_BUILD_WIN32_IME=ON'
)
& $cmakeExe @configureArgs

if ($ConfigureOnly) {
    Write-Host 'ConfigureOnly: done.'
    exit 0
}

& $cmakeExe --build $BuildDir -j $Jobs

if (-not $SkipTest) {
    Push-Location $BuildDir
    try {
        $ctest = Join-Path $clang64Bin 'ctest.exe'
        if (Test-Path -LiteralPath $ctest) {
            & $ctest --output-on-failure
        } else {
            Write-Warning "ctest.exe not found next to cmake; skip tests."
        }
    } finally {
        Pop-Location
    }
}

if (-not $SkipInstall) {
    & $cmakeExe --install $BuildDir
}

if (-not $SkipInstall -and -not $SkipDeploy) {
    $deploy = Join-Path $PSScriptRoot 'deploy-build-to-portable.ps1'
    if (-not (Test-Path -LiteralPath $deploy)) {
        Write-Error "Missing: $deploy"
    }
    $deployArgs = @{
        BuildDir      = $BuildDir
        DeployDir     = $DeployDir
        UnregisterFirst = $UnregisterFirst
    }
    if ($SkipRegsvr32) {
        $deployArgs['SkipRegsvr32'] = $true
    }
    & $deploy @deployArgs
}

Write-Host ''
Write-Host 'Done. Portable root:' $DeployDir.TrimEnd('\', '/')
if ($SkipInstall -or $SkipDeploy) {
    Write-Host '(Install/deploy skipped; run cmake --install and deploy-build-to-portable.ps1 if needed.)'
}
