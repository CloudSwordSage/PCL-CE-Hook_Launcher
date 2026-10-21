# Download the original, unmodified PCL-CE v2.15.1 policy source.
# Optionally supply -LocalPath if you have already checked out the repo.
[CmdletBinding()]
param(
    [string]$LocalPath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot 'RegionPolicy.cs'

if ((Test-Path -LiteralPath $target) -and -not $Force) {
    Write-Host "Using existing policy source: $target"
    return
}

if ($LocalPath) {
    Copy-Item -LiteralPath $LocalPath -Destination $target -Force
} else {
    $uri = 'https://raw.githubusercontent.com/PCL-Community/PCL-CE/v2.15.1/PCL.Core/App/Region/RegionPolicy.cs'
    Invoke-WebRequest -Uri $uri -OutFile $target -ErrorAction Stop
}

$source = Get-Content -LiteralPath $target -Raw -Encoding UTF8
if (-not $source.Contains('namespace PCL.Core.App.Region;') -or
    -not $source.Contains('public static RegionAssessment Evaluate(RegionSignals signals)') -or
    -not $source.Contains('public static RegionSignals ReadWindowsSignals()')) {
    Remove-Item -LiteralPath $target -Force
    throw 'RegionPolicy.cs did not contain the expected v2.15.1 API.'
}

Write-Host "Downloaded PCL-CE region policy: $target"
Get-FileHash -LiteralPath $target -Algorithm SHA256 | Format-Table -AutoSize
