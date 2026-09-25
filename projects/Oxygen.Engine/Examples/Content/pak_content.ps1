<#
.SYNOPSIS
Packages this Content directory's loose-cooked output with Oxygen PakTool.

.DESCRIPTION
This script packages `.cooked` into `pak`, both beside the script. An installed
SDK uses its bundled PakTool; a source checkout uses an existing build.

By default it emits:
- `<BaseName>.pak`
- `<BaseName>.catalog.json`
- `<BaseName>.manifest.json`

The default `SourceKey` is the stable UUIDv7 used for the showcase pak
line and should be reused for subsequent rebuilds and patches unless an
explicit replacement is intended.

.PARAMETER BaseName
Base filename for emitted artifacts in the pak output directory.

.PARAMETER CookedRoot
Loose-cooked root to package. Defaults to `.cooked` beside this script.

.PARAMETER OutputDir
Directory for published pak artifacts. Defaults to `pak` beside this script.

.PARAMETER ContentVersion
Pak content version passed to PakTool. Defaults to `1`.

.PARAMETER SourceKey
Canonical lowercase UUIDv7 source identity for the pak line.

.PARAMETER ToolPath
Optional explicit path to `Oxygen.Cooker.PakTool.exe`. If omitted, the script
uses the installed SDK's PakTool. In a source checkout it selects a built PakTool,
preferring Release, ordinary builds, then Ninja. Build-selection switches apply
only in a source checkout.

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

    [string]$CookedRoot,

    [string]$OutputDir,

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
if ([string]::IsNullOrWhiteSpace($CookedRoot)) { $CookedRoot = Join-Path $PSScriptRoot '.cooked' }
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $PSScriptRoot 'pak' }

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $PWD.Path $Path }))
}

$SdkSupport = Join-Path $PSScriptRoot '../Tools/BuildSelection.ps1'
if (Test-Path -LiteralPath $SdkSupport) {
    . $SdkSupport
    $RepoRoot = Get-OxygenSourceRoot
} else {
    $RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    . (Join-Path $RepoRoot 'tools/cli/BuildSelection.ps1')
}
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
