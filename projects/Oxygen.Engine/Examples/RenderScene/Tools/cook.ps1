#requires -Version 7.4
<#
.SYNOPSIS
Cooks examples of the examples project for RenderScene with Oxygen ImportTool.

.DESCRIPTION
Runs one ImportTool batch with the manifests of the selected examples
(Examples/Showcase/Manifests/<Name>.import-manifest.json), so the mount table is
checked once and a source several examples share is cooked once. The output is
RenderScene's own cooked root, .cooked/Showcase beside these tools; nothing is
written into the examples project, which the editor owns.

In an installed SDK, it uses the importer in that SDK. In a source checkout, it
selects a configured build tree from CMake presets, preferring Release,
ordinary builds, then Ninja, and builds the importer there first. No
reconfiguration is run.

.PARAMETER Name
The examples to cook, as Examples.json names them (BottleOnBox).

.PARAMETER Tag
Cooks the examples with this tag (showcase).

.PARAMETER All
Cooks every example of Examples.json.

.PARAMETER List
Lists the examples with their tags and descriptions, then exits.

.PARAMETER Check
Checks the project without cooking: every example has its manifest and every
manifest an example, and every example with scenes has its scene files. Then
ImportTool's source analysis (analyze-sources) reads every manifest and the
sources it names, and reports what the import rules refuse, as a cook would.

.PARAMETER NoTUI
Disables the ImportTool text interface (for CI or plain logs).

.PARAMETER Quiet
Prints only failures and the cook's result, not each job's progress (for
builds).

.PARAMETER EnginePak
Engine content pak that resolves /Engine references. Defaults to the installed
SDK's engine.pak, or in a source checkout to the pak the selected build tree
produces.

.EXAMPLE
Tools/cook BottleOnBox VsmTwoCubes

.EXAMPLE
Tools/cook -Tag showcase -NoTUI

.EXAMPLE
Tools/cook -Check
#>

[CmdletBinding(DefaultParameterSetName = 'Name')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Name', Position = 0, ValueFromRemainingArguments)]
    [string[]]$Name,

    [Parameter(Mandatory, ParameterSetName = 'Tag')][string]$Tag,

    [Parameter(Mandatory, ParameterSetName = 'All')][switch]$All,

    [Parameter(Mandatory, ParameterSetName = 'List')][switch]$List,

    [Parameter(Mandatory, ParameterSetName = 'Check')][switch]$Check,

    [switch]$NoTUI,
    [switch]$Quiet,
    [string]$Preset,
    [string]$BuildTree,
    [string]$Config,
    [string]$ToolPath,
    [string]$EnginePak,

    [Parameter(Mandatory, ParameterSetName = 'Help')]
    [Alias('h')][switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'internal/ExamplesTools.ps1')

if ($List) {
    foreach ($example in Get-ExamplesCatalog) {
        Write-Host ('{0,-28} {1,-32} {2}' -f $example.name, ($example.tags -join ','), $example.description)
    }
    return
}

# What the import rules do not cover: the catalog and the manifests are one
# set, and a scene the catalog plays exists.
function Test-ExamplesCatalog {
    $problems = [Collections.Generic.List[string]]::new()
    $catalog = Get-ExamplesCatalog
    $names = @($catalog | ForEach-Object name)
    foreach ($manifest in Get-ChildItem -LiteralPath (Join-Path $ExamplesRoot 'Manifests') -Filter '*.import-manifest.json') {
        $exampleName = $manifest.Name -replace '\.import-manifest\.json$', ''
        if ($names -cnotcontains $exampleName) { $problems.Add("$($manifest.Name) has no example in Examples.json") }
    }
    $mount = (Get-ExamplesSourceMounts)['Showcase']
    foreach ($example in $catalog) {
        $manifestPath = Get-ExampleManifestPath $example.name
        if (-not (Test-Path -LiteralPath $manifestPath)) { $problems.Add("$($example.name): no manifest $manifestPath") }
        foreach ($scene in @($example.scenes)) {
            $file = Join-Path $mount "Scenes/$($example.name)/$scene.scene.json"
            if (-not (Test-Path -LiteralPath $file)) { $problems.Add("$($example.name): no scene $file") }
        }
    }
    return $problems
}

if ($Check) {
    $problems = @(Test-ExamplesCatalog)
    foreach ($problem in $problems) { Write-Host $problem -ForegroundColor Red }
    if ($problems.Count -gt 0) { exit 1 }
    $tools = Select-ExamplesTools -Targets 'oxygen-cooker-importtool' -Overrides @{ 'oxygen-cooker-importtool' = $ToolPath } -BuildTree $BuildTree -Config $Config -Preset $Preset
    $failed = @()
    foreach ($example in Get-ExamplesCatalog) {
        $reportPath = [IO.Path]::GetTempFileName()
        try {
            $arguments = @('--no-tui', 'analyze-sources', '--manifest', (Get-ExampleManifestPath $example.name), '--report', $reportPath)
            Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-importtool' -Arguments $arguments
            $exitCode = $LASTEXITCODE
            Write-ImportReportDiagnostics $reportPath
        } finally {
            Remove-Item -LiteralPath $reportPath -Force -ErrorAction SilentlyContinue
        }
        if ($exitCode -ne 0) { $failed += $example.name }
    }
    if ($failed.Count -gt 0) {
        Write-Host "FAILED: the source analysis failed for $($failed -join ', ')" -ForegroundColor Red
        exit 1
    }
    Write-Host 'The examples project is consistent.' -ForegroundColor Green
    return
}

$examples = @(Select-Examples -Name $Name -Tag $Tag -All:$All)
$tools = Select-ExamplesTools -Targets 'oxygen-cooker-importtool' -Prerequisites 'oxygen-engine-content' -Overrides @{ 'oxygen-cooker-importtool' = $ToolPath } -BuildTree $BuildTree -Config $Config -Preset $Preset
$EnginePak = Get-OxygenEngineContentPak -SourceRoot $ExamplesEngineRoot -Selection $tools.Selection -Path $EnginePak
Write-Host "Engine content pak: $EnginePak"
Write-Host "Cooking: $(($examples | ForEach-Object name) -join ', ')"

# Global ImportTool options precede the batch subcommand.
$arguments = @()
if ($NoTUI) { $arguments += '--no-tui' }
if ($Quiet) { $arguments += '--quiet' }
# The cooked root replaces every job's own: the manifests name none.
$arguments += @('--cooked-root', $ExamplesCookedRoot, 'batch')
foreach ($example in $examples) { $arguments += @('--manifest', (Get-ExampleManifestPath $example.name)) }
$reportPath = [IO.Path]::GetTempFileName()
$arguments += @('--cooked-context-root', $EnginePak, '--report', $reportPath)
try {
    Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-importtool' -Arguments $arguments
    $exitCode = $LASTEXITCODE
    Write-ImportReportDiagnostics $reportPath
} finally {
    Remove-Item -LiteralPath $reportPath -Force -ErrorAction SilentlyContinue
}
if ($exitCode -ne 0) {
    Write-Host "FAILED: the cook exited with code $exitCode" -ForegroundColor Red
    exit $exitCode
}
Write-Host "Cooked $($examples.Count) example(s) into $ExamplesCookedRoot" -ForegroundColor Green
