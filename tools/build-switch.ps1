[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$devkitProPath = 'C:\devkitPro'
$makePath = Join-Path $devkitProPath 'msys2\usr\bin\make.exe'
$virtualDrive = 'S:'
$buildDirectory = Join-Path $repositoryRoot 'build\borealis-switch'
$nroOutput = Join-Path $repositoryRoot 'apps\switch-auto\switch-auto.nro'

if (-not (Test-Path -LiteralPath $makePath)) {
    throw "devkitPro make not found at $makePath"
}

if (Test-Path -LiteralPath "$virtualDrive\") {
    throw "Drive $virtualDrive is already in use."
}

try {
    & subst $virtualDrive $repositoryRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Could not map $virtualDrive to the repository."
    }

    $env:DEVKITPRO = '/opt/devkitpro'
    $env:DEVKITA64 = '/opt/devkitpro/devkitA64'

    # devkitA64 emits Windows drive letters in dependency files. GNU Make cannot
    # safely reuse those files through subst, so the Switch tree is disposable.
    if (Test-Path -LiteralPath $buildDirectory) {
        $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
        $expectedBuild = [System.IO.Path]::GetFullPath(
            (Join-Path $repositoryRoot 'build\borealis-switch'))
        if (-not $resolvedBuild.Equals($expectedBuild, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected Switch build directory: $resolvedBuild"
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }

    & cmake -S '/s' -B '/s/build/borealis-switch' `
        -G 'Unix Makefiles' `
        -DPLATFORM_SWITCH=ON `
        -DCMAKE_BUILD_TYPE=Release `
        '-DCMAKE_MAKE_PROGRAM=/usr/bin/make.exe'
    if ($LASTEXITCODE -ne 0) { throw "Switch configuration failed." }

    & cmake --build '/s/build/borealis-switch' --target switch-auto.nro --parallel -- -s
    if ($LASTEXITCODE -ne 0) { throw "Switch build failed." }

    Copy-Item -LiteralPath (Join-Path $buildDirectory 'switch-auto.nro') -Destination $nroOutput -Force
    Write-Output $nroOutput
}
finally {
    & subst $virtualDrive /D 2>$null
}
