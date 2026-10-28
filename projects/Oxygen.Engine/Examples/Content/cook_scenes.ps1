#requires -Version 7.4
<#
.SYNOPSIS
Cooks one or all shipped scene manifests with Oxygen ImportTool.

.DESCRIPTION
This script finds and runs the Oxygen.Cooker.ImportTool against scene 'import-manifest.json' files.
It can target a specific scene by folder name, or all scenes found in the 'scenes' subdirectory.
In an installed SDK, it uses the importer in that SDK's bin directory.
In a source checkout, it selects a configured build tree from CMake presets,
preferring Release, ordinary builds, then Ninja, and builds the importer
target there first, so a stale importer is rebuilt before it cooks anything.
No reconfiguration is run.
The native ImportTool validates manifests and descriptors against its current schemas.
The -All scope is the authored scene manifests and their dependencies; unreferenced
raw FBX models and standalone images are not imported, and PAKs are not rebuilt.

.PARAMETER Scene
The name of the scene folder to cook (e.g. "sdk-materials"). Required if -All is not specified.

.PARAMETER All
Switch to cook all scene folders that contain an import-manifest.json.

.PARAMETER Preset
Source checkouts only: exact CMake build preset.

.PARAMETER BuildTree
Source checkouts only: build tree name or path.

.PARAMETER Config
Source checkouts only: required configuration.

.PARAMETER NoTUI
Switch to disable the Text User Interface of the ImportTool (useful for CI or plain logs).

.PARAMETER ToolPath
Optional explicit importer executable; otherwise use the SDK or source-checkout selection.

.PARAMETER ContentRoot
Content directory containing scenes/<name>/import-manifest.json. Defaults to this script directory.

.PARAMETER EnginePak
Engine content pak that resolves /Engine asset references. Defaults to the
installed SDK's share/oxygen/engine.pak, or in a source checkout to the pak the
selected build tree produces (built with the importer). Required with -ToolPath
outside an SDK.

.PARAMETER Help
Show usage, options and examples without selecting tools or cooking. Alias: -h.

.EXAMPLE
.\cook_scenes.ps1 -Scene sdk-materials

.EXAMPLE
.\cook_scenes.ps1 -All -NoTUI
#>

[CmdletBinding(DefaultParameterSetName = 'Single')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Single', Position = 0)]
    [string]$Scene,

    [Parameter(Mandatory = $true, ParameterSetName = 'All')]
    [switch]$All,

    [Parameter(Mandatory = $false)]
    [string]$Preset,

    [switch]$NoTUI,

    [string]$ToolPath,

    [string]$BuildTree,

    [string]$Config,

    [string]$ContentRoot,

    [string]$EnginePak,

    [Parameter(Mandatory = $true, ParameterSetName = 'Help')]
    [Alias('h')][switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

$ErrorActionPreference = "Stop"

# Paths
if ([string]::IsNullOrWhiteSpace($ContentRoot)) { $ContentRoot = $PSScriptRoot }
$ContentDir = [IO.Path]::GetFullPath($ContentRoot)
$ScenesDir = Join-Path $ContentDir "scenes"

# Ensure scenes directory exists
if (-not (Test-Path $ScenesDir)) {
    Write-Error "Scenes directory not found: $ScenesDir"
    exit 1
}

$SdkSupport = Join-Path $PSScriptRoot '../Tools/BuildSelection.ps1'
if (Test-Path -LiteralPath $SdkSupport) {
    . $SdkSupport
    $RepoRoot = Get-OxygenSourceRoot
} else {
    $RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    . (Join-Path $RepoRoot 'tools/cli/BuildSelection.ps1')
}
# An explicit tool or an installed SDK is used as is; a source checkout
# builds the importer so cooking never runs a stale tool.
$installedSdk = Test-Path -LiteralPath $SdkSupport
if ($ToolPath -or $installedSdk) {
    $tools = Resolve-OxygenExecutables -SourceRoot $RepoRoot -Targets 'oxygen-cooker-importtool' -BuildTree $BuildTree -Config $Config -Preset $Preset -Overrides @{ 'oxygen-cooker-importtool' = $ToolPath }
} else {
    $tools = Build-OxygenExecutables -SourceRoot $RepoRoot -Targets 'oxygen-cooker-importtool' -Prerequisites 'oxygen-engine-content' -BuildTree $BuildTree -Config $Config -Preset $Preset
}
Write-OxygenExecutableSelection $tools

$EnginePak = Get-OxygenEngineContentPak -SourceRoot $RepoRoot -Selection $tools.Selection -Path $EnginePak
Write-Host "Using engine content pak: $EnginePak"

# Collect scenes to process
$ScenesToCook = @()

if ($All) {
    $AllDirs = Get-ChildItem -Path $ScenesDir -Directory | Sort-Object Name
    foreach ($dir in $AllDirs) {
        $ManifestPath = Join-Path $dir.FullName "import-manifest.json"
        if (Test-Path $ManifestPath) {
            $ScenesToCook += $dir
        }
    }

    if ($ScenesToCook.Count -eq 0) {
        Write-Host "No scenes found with an import-manifest.json file in $ScenesDir" -ForegroundColor Yellow
        exit 0
    }
} else {
    $TargetSceneDir = Join-Path $ScenesDir $Scene
    if (-not (Test-Path $TargetSceneDir -PathType Container)) {
        Write-Error "Scene directory not found: $TargetSceneDir"
        exit 1
    }

    $ManifestPath = Join-Path $TargetSceneDir "import-manifest.json"
    if (-not (Test-Path $ManifestPath)) {
        Write-Error "No import-manifest.json found in scene directory: $TargetSceneDir"
        exit 1
    }

    $ScenesToCook = @(Get-Item $TargetSceneDir)
}

# Process each scene
$TotalCount = $ScenesToCook.Count
$SuccessCount = 0
$FailedCount = 0

foreach ($SceneDir in $ScenesToCook) {
    $ManifestPath = Join-Path $SceneDir.FullName "import-manifest.json"

    Write-Host ""
    Write-Host "=======================================================" -ForegroundColor Cyan
    Write-Host "Cooking Scene: $($SceneDir.Name)" -ForegroundColor Cyan
    Write-Host "=======================================================" -ForegroundColor Cyan

    # Global ImportTool options precede the batch subcommand.
    $ArgsList = @()
    if ($NoTUI) { $ArgsList += '--no-tui' }
    $ReportPath = [IO.Path]::GetTempFileName()
    $ArgsList += @('batch', '--manifest', $ManifestPath, '--cooked-context-root', $EnginePak, '--report', $ReportPath)
    try {
        Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-importtool' -Arguments $ArgsList
        $ExitCode = $LASTEXITCODE
        # The importer reports what it left out or repaired only in the report.
        if ((Get-Item -LiteralPath $ReportPath).Length -gt 0) {
            $Report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
            foreach ($Job in @($Report.jobs)) {
                foreach ($Diagnostic in @($Job.diagnostics | Where-Object { $_ -and $_.severity -ne 'info' })) {
                    # The report omits an empty object path.
                    $ObjectPath = $Diagnostic.PSObject.Properties['object_path']
                    $Where = if ($ObjectPath) { " ($($ObjectPath.Value))" } else { '' }
                    Write-Warning "job $($Job.index) [$($Diagnostic.code)] $($Diagnostic.message)$Where"
                }
            }
        }
    } finally {
        Remove-Item -LiteralPath $ReportPath -Force -ErrorAction SilentlyContinue
    }

    if ($ExitCode -ne 0) {
        Write-Host "FAILED: Cooking scene $($SceneDir.Name) exited with code $ExitCode" -ForegroundColor Red
        $FailedCount++
    } else {
        Write-Host "SUCCESS: $($SceneDir.Name) cooked successfully." -ForegroundColor Green
        $SuccessCount++
    }
}

Write-Host "`nSummary: Cooked $TotalCount scene(s) ($SuccessCount succeeded, $FailedCount failed)." -ForegroundColor $(if ($FailedCount -gt 0) { "Red" } else { "Green" })

if ($FailedCount -gt 0) {
    exit 1
} else {
    exit 0
}
