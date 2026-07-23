[CmdletBinding()]
param(
    [string]$Ip,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $PSScriptRoot 'devices.local.json'
$nxlink = 'C:\devkitPro\tools\bin\nxlink.exe'
$nro = Join-Path $root 'apps\switch-auto\switch-auto.nro'

if (-not (Test-Path -LiteralPath $nxlink)) {
    throw "nxlink não encontrado em $nxlink"
}
if (Test-Path -LiteralPath $configPath) {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if (-not $Ip) { $Ip = $config.switch.ip }
}
if (-not $Ip) {
    throw 'Informe -Ip ou configure tools/devices.local.json.'
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-switch.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Build do NRO falhou.' }
}
if (-not (Test-Path -LiteralPath $nro)) { throw "NRO não encontrado em $nro" }

Write-Output 'Abra o Homebrew Menu no Switch e pressione Y para ativar o NetLoader.'
Write-Output 'O nxlink manterá este terminal aberto para receber os logs do NRO.'
& $nxlink -a $Ip $nro
if ($LASTEXITCODE -ne 0) { throw 'Envio do NRO via nxlink falhou.' }
