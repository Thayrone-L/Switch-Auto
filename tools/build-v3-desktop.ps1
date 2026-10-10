[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sourceDirectory = Join-Path $repositoryRoot 'v3'
$buildDirectory = Join-Path $repositoryRoot 'build\v3-desktop'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio CMake not found at $cmake"
}

& $cmake -S $sourceDirectory -B $buildDirectory `
    -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'V3 desktop configuration failed.' }

# A single MSBuild worker is deliberate on this host: parallel project startup
# has intermittently stalled under the synchronized workspace.
& $cmake --build $buildDirectory --config Release --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'V3 desktop build failed.' }

& $ctest --test-dir $buildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'V3 desktop tests failed.' }
