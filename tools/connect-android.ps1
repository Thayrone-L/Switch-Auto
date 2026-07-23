[CmdletBinding()]
param(
    [string]$PairEndpoint,
    [string]$PairCode,
    [string]$Endpoint
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $PSScriptRoot 'devices.local.json'
$adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'

if (-not (Test-Path -LiteralPath $adb)) {
    throw "ADB não encontrado em $adb"
}

if (Test-Path -LiteralPath $configPath) {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if (-not $Endpoint) { $Endpoint = $config.android.endpoint }
}

if ($PairEndpoint) {
    if (-not $PairCode) {
        $PairCode = Read-Host 'Código de pareamento exibido pelo Android'
    }
    & $adb pair $PairEndpoint $PairCode
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao parear o Android via ADB.' }
}

if (-not $Endpoint) {
    throw 'Informe -Endpoint IP:PORT ou configure tools/devices.local.json.'
}

& $adb connect $Endpoint
if ($LASTEXITCODE -ne 0) { throw 'Falha ao conectar ao Android via ADB.' }
& $adb devices -l
