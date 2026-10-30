#requires -Version 7.4
<#
.SYNOPSIS
Cooks examples of the examples project for RenderScene with Oxygen ImportTool.

.DESCRIPTION
Runs one ImportTool batch with the manifests of the selected examples
(Examples/Showcase/Manifests/<Name>.import-manifest.json), so the mount table is
checked once and a source several examples share is cooked once. The output is
RenderScene's own cooked root, .cooked/Content beside these tools; nothing is
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
manifest an example, every example with scenes has its folder and scene files,
every identity is its source's mount path with the cooked extension (a model's
is /Content/Models/<path under SourceMedia/DCC>), and every identity a manifest
references is cooked by that manifest, so each example cooks alone.

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

# The cooked identity of a source: its mount path with the cooked extension.
$cookedExtensions = [ordered]@{
    '.omat.json' = '.omat'; '.ogeo.json' = '.ogeo'; '.otex.json' = '.otex'
    '.scene.json' = '.oscene'; '.physics-material.json' = '.opmat'
    '.shape.json' = '.ocshape'; '.input.json' = '.input'; '.lua' = '.oscript'
}
function Get-ExpectedIdentity([string]$Source) {
    foreach ($extension in $cookedExtensions.Keys) {
        if ($Source.EndsWith($extension, [StringComparison]::Ordinal)) {
            return $Source.Substring(0, $Source.Length - $extension.Length) + $cookedExtensions[$extension]
        }
    }
    return $null
}

function Get-Strings($Value) {
    if ($Value -is [string]) { return $Value }
    if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        foreach ($item in $Value) { Get-Strings $item }
        return
    }
    if ($Value -is [pscustomobject]) {
        foreach ($property in $Value.PSObject.Properties) { Get-Strings $property.Value }
    }
}

function Test-ExamplesProject {
    $problems = [Collections.Generic.List[string]]::new()
    $catalog = Get-ExamplesCatalog
    $names = @($catalog | ForEach-Object name)
    foreach ($manifest in Get-ChildItem -LiteralPath (Join-Path $ExamplesRoot 'Manifests') -Filter '*.import-manifest.json') {
        $exampleName = $manifest.Name -replace '\.import-manifest\.json$', ''
        if ($names -cnotcontains $exampleName) { $problems.Add("$($manifest.Name) has no example in Examples.json") }
    }
    foreach ($example in $catalog) {
        $manifestPath = Get-ExampleManifestPath $example.name
        if (-not (Test-Path -LiteralPath $manifestPath)) {
            $problems.Add("$($example.name): no manifest $manifestPath")
            continue
        }
        foreach ($scene in @($example.scenes)) {
            $file = Join-Path $ExamplesRoot "Content/Scenes/$($example.name)/$scene.scene.json"
            if (-not (Test-Path -LiteralPath $file)) { $problems.Add("$($example.name): no scene $file") }
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        # The project is the editor's: a manifest names no output folder, the
        # cooking tool gives it one.
        if ($manifest.PSObject.Properties['output']) {
            $problems.Add("$($example.name): its manifest names an output folder; the tool that cooks it chooses one")
        }
        $produced = [Collections.Generic.HashSet[string]]::new()
        $referenced = [Collections.Generic.HashSet[string]]::new()
        foreach ($job in @($manifest.jobs)) {
            $source = $job.PSObject.Properties['source']?.Value
            $identity = $job.PSObject.Properties['virtual_path']?.Value
            if ($job.type -eq 'model') {
                # A model's record lives at its identity in the Content mount
                # and names its source relative to itself.
                $recordFile = Join-Path $ExamplesRoot ($identity.Substring(1) + '.import.json')
                if (-not (Test-Path -LiteralPath $recordFile)) {
                    $problems.Add("$($example.name): the model $identity has no record $recordFile")
                    continue
                }
                $record = Get-Content -LiteralPath $recordFile -Raw | ConvertFrom-Json
                $sourceFile = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $recordFile) $record.source))
                $sourcePath = '/' + [IO.Path]::GetRelativePath($ExamplesRoot, $sourceFile).Replace('\', '/')
                $dcc = '/Content/SourceMedia/DCC/'
                $expected = if ($sourcePath.StartsWith($dcc)) { '/Content/Models/' + $sourcePath.Substring($dcc.Length) } else { $null }
                if ($identity -cne $expected) {
                    $problems.Add("$($example.name): the model of $sourcePath is $identity, not $expected")
                }
                [void]$produced.Add($identity)
                continue
            }
            if ($identity) {
                [void]$produced.Add($identity)
                $expected = Get-ExpectedIdentity $source
                if ($identity -cne $expected) {
                    $problems.Add("$($example.name): $source cooks to $identity, not $expected")
                }
            }
            foreach ($text in Get-Strings ($job | Select-Object -Property * -ExcludeProperty id, source, virtual_path, depends_on)) {
                if ($text.StartsWith('/Content/')) { [void]$referenced.Add($text) }
            }
            if ($source -and $source.EndsWith('.json')) {
                $document = Get-Content -LiteralPath (Join-Path $ExamplesRoot $source.Substring(1)) -Raw | ConvertFrom-Json
                if ($job.type -eq 'buffer-container') {
                    foreach ($buffer in @($document.buffers)) { [void]$produced.Add($buffer.virtual_path) }
                }
                foreach ($text in Get-Strings $document) {
                    if ($text.StartsWith('/Content/') -and -not $text.StartsWith('/Content/SourceMedia/')) {
                        [void]$referenced.Add($text)
                    }
                }
            }
        }
        foreach ($reference in $referenced) {
            # A container's products live under its identity; a scene's
            # physics companion beside the scene.
            $owner = $produced | Where-Object { $reference -ceq $_ -or $reference.StartsWith("$_/") } | Select-Object -First 1
            if (-not $owner -and $reference.EndsWith('.opscene')) {
                $owner = $produced | Where-Object { $_ -ceq ($reference -replace '\.opscene$', '.oscene') } | Select-Object -First 1
            }
            if (-not $owner) { $problems.Add("$($example.name): references $reference, which its manifest does not cook") }
        }
    }
    return $problems
}

if ($Check) {
    $problems = @(Test-ExamplesProject)
    foreach ($problem in $problems) { Write-Host $problem -ForegroundColor Red }
    if ($problems.Count -gt 0) { exit 1 }
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
