#Requires -Version 5.1
<#
.SYNOPSIS
  Copy freshly built IME + Fcitx5Core/Utils DLLs from a CMake build tree to the portable deploy dir (e.g. C:\Fcitx5Portable\bin).

.PARAMETER BuildDir
  Path to the CMake build directory (contains win32\dll and bin).

.PARAMETER DeployDir
  Portable root (DLLs go to DeployDir\bin).

.PARAMETER SkipRegsvr32
  Do not run regsvr32 /s after copy (default: run register so TIP stays valid).

.PARAMETER UnregisterFirst
  Before copying, run regsvr32 /u /s on DeployDir\bin\libfcitx5-x86_64.dll if it exists, then wait briefly.
  Use this when Copy-Item fails with "being used by another process" (IME DLL still loaded).

.EXAMPLE
  .\scripts\deploy-build-to-portable.ps1
.EXAMPLE
  .\scripts\deploy-build-to-portable.ps1 -BuildDir D:\vscode\fcitx_projs\fcitx5-windows\build-portable -DeployDir C:\Fcitx5Portable
.EXAMPLE
  .\scripts\deploy-build-to-portable.ps1 -DeployDir C:\Fcitx5Portable -UnregisterFirst
#>
param(
    [string] $BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build-portable'),
    [string] $DeployDir = 'C:\Fcitx5Portable',
    [switch] $SkipRegsvr32,
    [switch] $UnregisterFirst
)

$ErrorActionPreference = 'Stop'
$DeployDir = $DeployDir.TrimEnd('\', '/')
$bin = Join-Path $DeployDir 'bin'

$imeDll = Join-Path $BuildDir 'win32\dll\libfcitx5-x86_64.dll'
$core = Join-Path $BuildDir 'bin\libFcitx5Core.dll'
$utils = Join-Path $BuildDir 'bin\libFcitx5Utils.dll'
$config = Join-Path $BuildDir 'bin\libFcitx5Config.dll'

foreach ($p in @($BuildDir, $imeDll, $core, $utils)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Error "Missing: $p (configure and build first, or pass -BuildDir)"
    }
}

if (-not (Test-Path -LiteralPath $bin)) {
    New-Item -ItemType Directory -Path $bin -Force | Out-Null
}

$copyList = @(
    @{ Src = $imeDll;  Name = 'libfcitx5-x86_64.dll' }
    @{ Src = $core;   Name = 'libFcitx5Core.dll' }
    @{ Src = $utils;  Name = 'libFcitx5Utils.dll' }
)
if (Test-Path -LiteralPath $config) {
    $copyList += @{ Src = $config; Name = 'libFcitx5Config.dll' }
}

$penguin = Join-Path $BuildDir 'win32\dll\penguin.ico'
if (Test-Path -LiteralPath $penguin) {
    $copyList += @{ Src = $penguin; Name = 'penguin.ico' }
} else {
    Write-Warning "Optional missing: $penguin (TSF IconFile / file icon fallback; embed may still work)."
}

$imeInBin = Join-Path $bin 'libfcitx5-x86_64.dll'
if ($UnregisterFirst -and (Test-Path -LiteralPath $imeInBin)) {
    Write-Host "regsvr32 /u /s $imeInBin (release IME DLL for overwrite)"
    Start-Process -FilePath regsvr32.exe -ArgumentList @('/u', '/s', $imeInBin) -Wait
    Start-Sleep -Seconds 2
}

Write-Host "Deploy: $BuildDir -> $bin"
foreach ($item in $copyList) {
    $dest = Join-Path $bin $item.Name
    Write-Host "  $($item.Name)"
    $copied = $false
    for ($i = 0; $i -lt 5; $i++) {
        try {
            Copy-Item -LiteralPath $item.Src -Destination $dest -Force -ErrorAction Stop
            $copied = $true
            break
        } catch {
            if ($i -eq 4) { throw }
            Write-Warning "Copy retry $($i + 1)/5: $($_.Exception.Message)"
            Start-Sleep -Seconds 2
        }
    }
}

# $imeInBin already defined above for UnregisterFirst

if (-not $SkipRegsvr32) {
    Write-Host "regsvr32 /s $imeInBin"
    Start-Process -FilePath regsvr32.exe -ArgumentList @('/s', $imeInBin) -Wait
}

Write-Host 'Done. Switch to Fcitx5 in Win+Space and test in Notepad.'
