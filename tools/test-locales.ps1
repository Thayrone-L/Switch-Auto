[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$localeRoot = Join-Path $root 'resources\i18n'
$fallbackPath = Join-Path $localeRoot 'en-US\switch_auto.json'

function Get-LeafTranslations {
    param(
        [Parameter(Mandatory)] [object] $Value,
        [string] $Prefix = ''
    )

    $translations = @{}
    foreach ($property in $Value.PSObject.Properties) {
        $key = if ($Prefix) { "$Prefix/$($property.Name)" } else { $property.Name }
        if ($property.Value -is [string]) {
            if ([string]::IsNullOrWhiteSpace($property.Value)) {
                throw "Empty translation: $key"
            }
            $translations[$key] = $property.Value
        }
        elseif ($null -ne $property.Value) {
            $nested = Get-LeafTranslations -Value $property.Value -Prefix $key
            foreach ($entry in $nested.GetEnumerator()) {
                $translations[$entry.Key] = $entry.Value
            }
        }
    }
    return $translations
}

$fallbackJson = Get-Content -LiteralPath $fallbackPath -Raw -Encoding utf8 |
    ConvertFrom-Json
$fallback = Get-LeafTranslations -Value $fallbackJson
if ($fallback.Count -eq 0) { throw 'Fallback en-US has no translations.' }

Get-ChildItem -LiteralPath $localeRoot -Directory | ForEach-Object {
    $catalogPath = Join-Path $_.FullName 'switch_auto.json'
    if (-not (Test-Path -LiteralPath $catalogPath)) {
        throw "Missing catalog: $catalogPath"
    }
    $catalogJson = Get-Content -LiteralPath $catalogPath -Raw -Encoding utf8 |
        ConvertFrom-Json
    $catalog = Get-LeafTranslations -Value $catalogJson
    $missing = @($fallback.Keys | Where-Object { -not $catalog.ContainsKey($_) })
    if ($missing.Count -gt 0) {
        throw "$($_.Name) is missing keys: $($missing -join ', ')"
    }
}

Write-Output "LOCALE_TESTS_OK catalogs=$((Get-ChildItem -LiteralPath $localeRoot -Directory).Count) keys=$($fallback.Count)"
