[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$toolchainRoot = 'C:\devkitPro\msys2\opt\bin'
$compiler = Join-Path $toolchainRoot 'x86_64-w64-mingw32-g++.exe'
$cCompiler = Join-Path $toolchainRoot 'x86_64-w64-mingw32-gcc.exe'
$make = 'C:\devkitPro\msys2\usr\bin\make.exe'
$buildDirectory = Join-Path $root 'build\borealis-desktop'
$outputDirectory = Join-Path $root 'apps\simulator\build'
$output = Join-Path $outputDirectory 'switch-auto-simulator.exe'
$virtualDrive = 'S:'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Desktop compiler not found at $compiler"
}

if (Test-Path -LiteralPath "$virtualDrive\") {
    throw "Drive $virtualDrive is already in use."
}

try {
    & subst $virtualDrive $root
    if ($LASTEXITCODE -ne 0) { throw "Could not map $virtualDrive to the repository." }

    & cmake -S '/s' -B '/s/build/borealis-desktop' `
        -G 'Unix Makefiles' `
        -DPLATFORM_DESKTOP=ON `
        -DCMAKE_BUILD_TYPE=Release `
        '-DCMAKE_MAKE_PROGRAM=/usr/bin/make.exe' `
        '-DCMAKE_TOOLCHAIN_FILE=/s/cmake/mingw-toolchain.cmake'
    if ($LASTEXITCODE -ne 0) { throw "Simulator configuration failed." }

    & cmake --build '/s/build/borealis-desktop' --parallel
    if ($LASTEXITCODE -ne 0) { throw "Simulator build failed." }

    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildDirectory 'switch-auto.exe') -Destination $output -Force
    Copy-Item -LiteralPath (Join-Path $buildDirectory 'resources') -Destination $outputDirectory -Recurse -Force
    Write-Output $output
}
finally {
    & subst $virtualDrive /D 2>$null
}
