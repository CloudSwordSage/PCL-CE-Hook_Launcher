[CmdletBinding()]
param(
    [ValidateSet('win-x64', 'win-arm64')]
    [string]$Runtime = 'win-x64'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'RegionPolicy.cs'))) {
    & (Join-Path $PSScriptRoot 'prepare.ps1')
}

$project = Join-Path $PSScriptRoot 'PclCeRegionProbe.csproj'
$output = Join-Path $PSScriptRoot "publish/$Runtime"
& dotnet publish $project -c Release -r $Runtime --self-contained false `
    -p:PublishSingleFile=true -p:PublishReadyToRun=false `
    -p:PublishTrimmed=false -o $output
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed ($LASTEXITCODE)"
}

Write-Host "Executable: $(Join-Path $output 'PclCeRegionProbe.exe')"
