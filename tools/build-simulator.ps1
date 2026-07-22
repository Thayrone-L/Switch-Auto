[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = 'C:\devkitPro\msys2\opt\bin\x86_64-w64-mingw32-g++.exe'
$outputDirectory = Join-Path $root 'apps\simulator\build'
$output = Join-Path $outputDirectory 'switch-auto-simulator.exe'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Desktop compiler not found at $compiler"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$sources = @(
    (Join-Path $root 'core\source\canvas.cpp'),
    (Join-Path $root 'core\source\widget.cpp'),
    (Join-Path $root 'core\source\application.cpp'),
    (Join-Path $root 'apps\simulator\source\main.cpp')
)

& $compiler @sources '-std=c++17' '-O2' '-Wall' '-Wextra' '-municode' '-mwindows' `
    '-I' (Join-Path $root 'core\include') '-o' $output '-lgdi32' '-static'
if ($LASTEXITCODE -ne 0) {
    throw "Simulator build failed with exit code $LASTEXITCODE."
}

Write-Output $output

