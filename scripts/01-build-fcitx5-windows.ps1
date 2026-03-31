#Requires -Version 5.1
<#
.SYNOPSIS
  Step 1 — Configure, build, and install fcitx5-windows (core + TSF IME) with MSYS2 CLANG64 into a stage prefix.

.DESCRIPTION
  Default install prefix is <repo>/stage (same tree used by 02-build-deps.sh). Does not copy to C:\Fcitx5Portable or register IME (use 04 / 05 or install-fcitx5-ime.ps1).

.PARAMETER Stage
  CMAKE_INSTALL_PREFIX (forward slashes recommended, e.g. D:/path/to/fcitx5-windows/stage).

.PARAMETER BuildDir
  CMake build directory (default: <repo>/build-portable).

.PARAMETER MsysRoot
  MSYS2 root (default C:\msys64).

.EXAMPLE
  .\scripts\01-build-fcitx5-windows.ps1
.EXAMPLE
  .\scripts\01-build-fcitx5-windows.ps1 -Stage D:/fcitx5-windows/stage -Jobs 16
#>
param(
    [string] $BuildDir = '',
    [string] $Stage = '',
    [string] $MsysRoot = 'C:\msys64',
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string] $BuildType = 'Release',
    [int] $Jobs = 8,
    [switch] $SkipInstall,
    [switch] $SkipTest,
    [switch] $ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot 'build-portable'
}
if (-not $Stage) {
    $Stage = Join-Path $RepoRoot 'stage'
}
$installPrefix = [System.IO.Path]::GetFullPath($Stage) -replace '\\', '/'

$clang64Bin = Join-Path $MsysRoot 'clang64\bin'
$cmakeExe = Join-Path $clang64Bin 'cmake.exe'
$clangExe = Join-Path $clang64Bin 'clang.exe'
$clangxxExe = Join-Path $clang64Bin 'clang++.exe'

foreach ($p in @($cmakeExe, $clangExe, $clangxxExe)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Error "Missing: $p — install MSYS2 CLANG64 toolchain or set -MsysRoot."
    }
}

$clang64Posix = ($MsysRoot + '\clang64') -replace '\\', '/'
$cCompiler = "$clang64Posix/bin/clang.exe"
$cxxCompiler = "$clang64Posix/bin/clang++.exe"

$usrBin = Join-Path $MsysRoot 'usr\bin'
$env:PATH = "$clang64Bin;$usrBin;$env:PATH"

Write-Host "Repo:       $RepoRoot"
Write-Host "Build dir:  $BuildDir"
Write-Host "Stage:      $installPrefix"
Write-Host "Build type: $BuildType"
Write-Host "CMake:      $cmakeExe"
Write-Host ''

$configureArgs = @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_INSTALL_PREFIX=$installPrefix",
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
            Write-Warning "ctest.exe not found; skip tests."
        }
    } finally {
        Pop-Location
    }
}

if (-not $SkipInstall) {
    & $cmakeExe --install $BuildDir
    $prefixPath = $installPrefix -replace '/', [System.IO.Path]::DirectorySeparatorChar
    $shareFcitx5 = Join-Path $prefixPath 'share\fcitx5'
    if (-not (Test-Path -LiteralPath $shareFcitx5)) {
        New-Item -ItemType Directory -Path $shareFcitx5 -Force | Out-Null
    }
    foreach ($name in @('profile.pinyin.example', 'profile.pinyin-only.example', 'profile.windows.example')) {
        $src = Join-Path $RepoRoot "contrib\fcitx5\$name"
        if (Test-Path -LiteralPath $src) {
            Copy-Item -LiteralPath $src -Destination (Join-Path $shareFcitx5 $name) -Force
        }
    }
}

Write-Host ''
Write-Host 'Done. Next: MSYS bash — ./scripts/02-build-deps.sh (or set SKIP_FCITX_WINDOWS=1 if stage already has this install).'
