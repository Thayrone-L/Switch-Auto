[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$gxx = 'C:\devkitPro\devkitA64\bin\aarch64-none-elf-g++.exe'
if (-not (Test-Path -LiteralPath $gxx)) { throw "devkitA64 g++ not found at $gxx" }
$v3 = Join-Path $repositoryRoot 'v3'
$sources = Get-ChildItem -LiteralPath (Join-Path $v3 'tests') -Recurse -Filter '*.cpp'
$failed = 0
foreach ($source in $sources) {
    & $gxx -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror `
        -I $v3 $source.FullName
    if ($LASTEXITCODE -ne 0) { $failed++ ; Write-Output "FAIL $($source.FullName)" }
    else { Write-Output "OK   $($source.FullName)" }
}
if ($failed -ne 0) { throw "$failed file(s) failed GCC syntax check." }
