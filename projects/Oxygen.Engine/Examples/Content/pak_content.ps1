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
selects an available preset with a built PakTool, preferring Release, ordinary
builds, then Ninja. -BuildTree, -Config and -Preset constrain that selection.

.PARAMETER DiagnosticsFile
Optional explicit diagnostics report path. If omitted, no diagnostics report is
emitted.

.PARAMETER NoManifest
Suppress manifest emission for the build.

.PARAMETER BuildTree
Optional build tree name or path, resolved by the shared launcher.

.PARAMETER Config
Optional required build configuration.

.PARAMETER Preset
Optional exact CMake build preset.

.PARAMETER Help
Show usage, options and examples without selecting tools or packaging. Alias: -h.

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

    [switch]$NoManifest,

    [string]$BuildTree,

    [string]$Config,

    [string]$Preset,

    [Alias('h')][switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path, $PWD.Path)
}

$RepoRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName

. (Join-Path $RepoRoot 'tools/cli/BuildSelection.ps1')
$tools = Resolve-OxygenExecutables -SourceRoot $RepoRoot -Targets 'oxygen-cooker-paktool' -BuildTree $BuildTree -Config $Config -Preset $Preset -Overrides @{ 'oxygen-cooker-paktool' = $ToolPath }
Write-OxygenExecutableSelection $tools

$CookedRoot = Get-FullPath $CookedRoot
$OutputDir = Get-FullPath $OutputDir

if (-not (Test-Path $CookedRoot -PathType Container)) {
    throw "Cooked root not found: $CookedRoot"
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

Write-Host "Cooked root : $CookedRoot" -ForegroundColor DarkGray
Write-Host "Output dir  : $OutputDir" -ForegroundColor DarkGray
Write-Host "Base name   : $BaseName" -ForegroundColor DarkGray
Write-Host "Source key  : $SourceKey" -ForegroundColor DarkGray

if ($PSCmdlet.ShouldProcess($PakPath, "Build pak from loose-cooked root")) {
    if (-not (Test-Path -LiteralPath $OutputDir -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $OutputDir -Force
    }
    Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-paktool' -Arguments $Arguments
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
