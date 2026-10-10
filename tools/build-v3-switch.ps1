[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$devkitProPath = 'C:\devkitPro'
$makePath = Join-Path $devkitProPath 'msys2\usr\bin\make.exe'
$buildDirectory = Join-Path $repositoryRoot 'build\v3-switch'
$outputDirectory = Join-Path $repositoryRoot 'v3\out'
$output = Join-Path $outputDirectory 'switch-auto-v3.nro'
$virtualDrive = 'S:'

if (-not (Test-Path -LiteralPath $makePath)) {
    throw "devkitPro make not found at $makePath"
}

if (Test-Path -LiteralPath "$virtualDrive\") {
    throw "Drive $virtualDrive is already in use."
}

# Never leave an older deployable artifact after a failed build.
if (Test-Path -LiteralPath $output) {
    Remove-Item -LiteralPath $output -Force
}

try {
    & subst $virtualDrive $repositoryRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not map $virtualDrive to the repository." }

    $env:DEVKITPRO = '/opt/devkitpro'
    $env:DEVKITA64 = '/opt/devkitpro/devkitA64'

    & cmake -S '/s/v3' -B '/s/build/v3-switch' `
        -G 'Unix Makefiles' `
        -DCMAKE_BUILD_TYPE=Release `
        '-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake' `
        '-DCMAKE_MAKE_PROGRAM=/usr/bin/make.exe'
    if ($LASTEXITCODE -ne 0) { throw 'V3 Switch configuration failed.' }

    & cmake --build '/s/build/v3-switch' --target switch-auto-v3-nro --parallel -- -s
    if ($LASTEXITCODE -ne 0) { throw 'V3 Switch build failed.' }

    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildDirectory 'switch-auto-v3.nro') `
        -Destination $output -Force
    Write-Output $output
}
finally {
    & subst $virtualDrive /D 2>$null
}
