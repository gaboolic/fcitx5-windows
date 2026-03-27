#Requires -Version 5.1
<#
.SYNOPSIS
  Syntax-check all *.ps1 files in this directory (no execution).
#>
$ErrorActionPreference = 'Stop'
$failed = $false
$dirs = @(
    $PSScriptRoot,
    (Join-Path $PSScriptRoot '..\installer')
)
foreach ($d in $dirs) {
    if (-not (Test-Path -LiteralPath $d)) {
        continue
    }
    Get-ChildItem -LiteralPath $d -Filter *.ps1 -File | ForEach-Object {
        $tokens = $null
        $errs = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile(
            $_.FullName, [ref]$tokens, [ref]$errs)
        if ($errs) {
            Write-Host "Parse errors in $($_.FullName):"
            $errs | ForEach-Object { Write-Host $_.ToString() }
            $failed = $true
        }
    }
}
if ($failed) {
    exit 1
}
Write-Host "OK: PowerShell parse check (scripts/ and installer/)"
