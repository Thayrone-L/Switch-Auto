[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$projectRelativePath = 'apps\switch-auto'
$devkitProPath = 'C:\devkitPro'
$makePath = Join-Path $devkitProPath 'msys2\usr\bin\make.exe'
$virtualDrive = 'S:'

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

    $env:DEVKITPRO = $devkitProPath.Replace('\', '/')
    $env:DEVKITA64 = (Join-Path $devkitProPath 'devkitA64').Replace('\', '/')
    $makeProjectPath = "$virtualDrive/$($projectRelativePath.Replace('\', '/'))"

    & $makePath -C $makeProjectPath
    if ($LASTEXITCODE -ne 0) {
        throw "Switch build failed with exit code $LASTEXITCODE."
    }
}
finally {
    & subst $virtualDrive /D 2>$null
}
