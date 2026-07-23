[CmdletBinding()]
param(
    [string]$Endpoint,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $PSScriptRoot 'devices.local.json'
$androidRoot = Join-Path $root 'apps\android-companion'
$apk = Join-Path $androidRoot 'app\build\outputs\apk\debug\app-debug.apk'
$adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'

if (-not (Test-Path -LiteralPath $adb)) {
    throw "ADB não encontrado em $adb"
}
if (Test-Path -LiteralPath $configPath) {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if (-not $Endpoint) { $Endpoint = $config.android.endpoint }
}
if (-not $Endpoint) {
    throw 'Informe -Endpoint IP:PORT ou configure tools/devices.local.json.'
}

& $adb connect $Endpoint
if ($LASTEXITCODE -ne 0) { throw 'Android indisponível via ADB.' }

if (-not $SkipBuild) {
    $env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
    $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
    $env:Path = "$env:JAVA_HOME\bin;$env:Path"
    & (Join-Path $androidRoot 'gradlew.bat') -p $androidRoot assembleDebug
    if ($LASTEXITCODE -ne 0) { throw 'Build do APK falhou.' }
}
if (-not (Test-Path -LiteralPath $apk)) { throw "APK não encontrado em $apk" }

& $adb -s $Endpoint install -r $apk
if ($LASTEXITCODE -ne 0) { throw 'Instalação do APK falhou.' }
Write-Output "APK instalado em $Endpoint"
