<#
.SYNOPSIS
Builds a pak from the Examples/Content loose-cooked root using Oxygen.Cooker.PakTool.

.DESCRIPTION
This script packages `Examples/Content/.cooked` into `Examples/Content/pak`
using the native `Oxygen.Cooker.PakTool` in the current repo workspace.

By default it emits:
- `<BaseName>.pak`
- `<BaseName>.catalog.json`
- `<BaseName>.manifest.json`

The default `SourceKey` is the stable UUIDv7 used for the Examples/Content pak
line and should be reused for subsequent rebuilds and patches unless an
explicit replacement is intended.

.PARAMETER BaseName
Base filename for emitted artifacts in the pak output directory.

.PARAMETER CookedRoot
Loose-cooked root to package. Defaults to `Examples/Content/.cooked`.

.PARAMETER OutputDir
Directory for published pak artifacts. Defaults to `Examples/Content/pak`.

.PARAMETER ContentVersion
Pak content version passed to PakTool. Defaults to `1`.

.PARAMETER SourceKey
Canonical lowercase UUIDv7 source identity for the pak line.

.PARAMETER ToolPath
Optional explicit path to `Oxygen.Cooker.PakTool.exe`. If omitted, the script
uses `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe`.

.PARAMETER DiagnosticsFile
Optional explicit diagnostics report path. If omitted, no diagnostics report is
emitted.

.PARAMETER NoManifest
Suppress manifest emission for the build.

.EXAMPLE
.\pak_content.ps1

.EXAMPLE
.\pak_content.ps1 -BaseName all-base

.EXAMPLE
.\pak_content.ps1 -BaseName all-base -DiagnosticsFile .\pak\all-base.report.json
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$BaseName = "all",

    [string]$CookedRoot = (Join-Path $PSScriptRoot ".cooked"),

    [string]$OutputDir = (Join-Path $PSScriptRoot "pak"),

    [ValidateRange(0, 65535)]
    [int]$ContentVersion = 1,

    [string]$SourceKey = "018f8f8f-1234-7abc-8def-0123456789ab",

    [string]$ToolPath,

    [string]$DiagnosticsFile,

    [switch]$NoManifest
)

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

$RepoRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName

if ([string]::IsNullOrWhiteSpace($ToolPath)) {
    $ToolPath = Join-Path $RepoRoot "out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe"
}

$ToolPath = Get-FullPath $ToolPath
$CookedRoot = Get-FullPath $CookedRoot
$OutputDir = Get-FullPath $OutputDir

if (-not (Test-Path $ToolPath -PathType Leaf)) {
    throw "PakTool not found: $ToolPath"
}

if (-not (Test-Path $CookedRoot -PathType Container)) {
    throw "Cooked root not found: $CookedRoot"
}

if (-not (Test-Path $OutputDir -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $OutputDir -Force
}

$PakPath = Join-Path $OutputDir ($BaseName + ".pak")
$CatalogPath = Join-Path $OutputDir ($BaseName + ".catalog.json")
$ManifestPath = Join-Path $OutputDir ($BaseName + ".manifest.json")

$Arguments = @(
    "build",
    "--loose-source", $CookedRoot,
    "--out", $PakPath,
    "--catalog-out", $CatalogPath,
    "--content-version", $ContentVersion.ToString(),
    "--source-key", $SourceKey
)

if (-not $NoManifest) {
    $Arguments += @("--manifest-out", $ManifestPath)
}

if (-not [string]::IsNullOrWhiteSpace($DiagnosticsFile)) {
    $DiagnosticsFile = Get-FullPath $DiagnosticsFile
    $Arguments += @("--diagnostics-file", $DiagnosticsFile)
}

Write-Host "Using PakTool: $ToolPath" -ForegroundColor DarkGray
Write-Host "Cooked root : $CookedRoot" -ForegroundColor DarkGray
Write-Host "Output dir  : $OutputDir" -ForegroundColor DarkGray
Write-Host "Base name   : $BaseName" -ForegroundColor DarkGray
Write-Host "Source key  : $SourceKey" -ForegroundColor DarkGray

if ($PSCmdlet.ShouldProcess($PakPath, "Build pak from loose-cooked root")) {
    & $ToolPath @Arguments
    $ExitCode = $LASTEXITCODE

    if ($ExitCode -ne 0) {
        exit $ExitCode
    }

    Write-Host ""
    Write-Host "Published artifacts:" -ForegroundColor Green
    Write-Host "  pak     : $PakPath" -ForegroundColor Green
    Write-Host "  catalog : $CatalogPath" -ForegroundColor Green
    if (-not $NoManifest) {
        Write-Host "  manifest: $ManifestPath" -ForegroundColor Green
    }
    if (-not [string]::IsNullOrWhiteSpace($DiagnosticsFile)) {
        Write-Host "  report  : $DiagnosticsFile" -ForegroundColor Green
    }
}
