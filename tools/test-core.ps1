[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = 'C:\devkitPro\msys2\opt\bin\x86_64-w64-mingw32-g++.exe'
$outputDirectory = Join-Path $root 'build\tests'
$output = Join-Path $outputDirectory 'core-tests.exe'

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$sources = @(
    (Join-Path $root 'core\source\canvas.cpp'),
    (Join-Path $root 'core\source\widget.cpp'),
    (Join-Path $root 'core\source\application.cpp'),
    (Join-Path $root 'tests\core_tests.cpp')
)

& $compiler @sources '-std=c++17' '-O2' '-Wall' '-Wextra' `
    '-I' (Join-Path $root 'core\include') '-o' $output '-static'
if ($LASTEXITCODE -ne 0) { throw "Core test build failed: $LASTEXITCODE" }

& $output
if ($LASTEXITCODE -ne 0) { throw "Core tests failed: $LASTEXITCODE" }

