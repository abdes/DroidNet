#requires -Version 7.4
<#
.SYNOPSIS
Packages the cooked examples into RenderScene's .build/pak/examples.pak.

.DESCRIPTION
Builds one pak with Oxygen PakTool from what Tools/cook cooked (RenderScene's
.cooked/Content), with its catalog and manifest beside it. The models an
example's manifest names are in that cooked root. The pak's source key is the
examples project's Id (Project.oxy), and its external scripts resolve through
the project's source mounts.

In an installed SDK, it uses the packager in that SDK. In a source checkout, it
selects a configured build tree from CMake presets, preferring Release,
ordinary builds, then Ninja, and builds the packager there first.

.PARAMETER ContentVersion
The pak's content version.

.PARAMETER EnginePak
Engine content pak the examples depend on. Defaults to the installed SDK's
engine.pak, or in a source checkout to the pak the selected build tree
produces.

.PARAMETER DiagnosticsFile
Writes PakTool's diagnostics report to this file.

.PARAMETER NoManifest
Skips the pak manifest.

.EXAMPLE
Tools/pak
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateRange(0, 65535)][int]$ContentVersion = 1,
    [string]$ToolPath,
    [string]$EnginePak,
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

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'internal/ExamplesTools.ps1')

if (-not (Test-Path -LiteralPath (Join-Path $ExamplesCookedRoot 'container.index.bin') -PathType Leaf)) {
    throw "Nothing is cooked in $ExamplesCookedRoot; run Tools/cook first."
}
$tools = Select-ExamplesTools -Targets 'oxygen-cooker-paktool' -Prerequisites 'oxygen-engine-content' -Overrides @{ 'oxygen-cooker-paktool' = $ToolPath } -BuildTree $BuildTree -Config $Config -Preset $Preset
$EnginePak = Get-OxygenEngineContentPak -SourceRoot $ExamplesEngineRoot -Selection $tools.Selection -Path $EnginePak

$outputDir = $ExamplesPakDirectory
$pakPath = Join-Path $outputDir 'examples.pak'
$catalogPath = Join-Path $outputDir 'examples.catalog.json'
$manifestPath = Join-Path $outputDir 'examples.manifest.json'
$arguments = @(
    'build',
    '--loose-source', $ExamplesCookedRoot,
    '--dependency-pak', $EnginePak,
    '--root', $ExamplesRoot
) + (Get-ExamplesMountArguments) + @(
    '--out', $pakPath,
    '--catalog-out', $catalogPath,
    '--content-version', "$ContentVersion",
    '--source-key', $ExamplesProject.Id
)
if (-not $NoManifest) { $arguments += @('--manifest-out', $manifestPath) }
if ($DiagnosticsFile) { $arguments += @('--diagnostics-file', [IO.Path]::GetFullPath($DiagnosticsFile)) }

Write-Host "Cooked root : $ExamplesCookedRoot" -ForegroundColor DarkGray
Write-Host "Depends on  : $EnginePak" -ForegroundColor DarkGray
if ($PSCmdlet.ShouldProcess($pakPath, 'Build the examples pak')) {
    $null = New-Item -ItemType Directory -Path $outputDir -Force
    Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-paktool' -Arguments $arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "Published: $pakPath" -ForegroundColor Green
}
