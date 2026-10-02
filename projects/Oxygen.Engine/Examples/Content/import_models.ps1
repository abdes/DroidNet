#requires -Version 7.4
<#
.SYNOPSIS
Imports or reimports Content models through native retained publication.
.DESCRIPTION
Each source has an authored record under imports. Native publication
selects a validated immutable generation; failures retain the prior selection.
Source checkouts build a matched Release importer/inspector pair before importing.
An installed SDK uses its bundled tools. -ToolPath uses an explicitly supplied
importer and its sibling inspector without building; the caller owns freshness.
Existing readers keep their generation. Source files and demo settings are not
modified. Temporary recipes and reports are deleted on exit.
.PARAMETER SourceList
Apply sources/settings from a list matching import-sources.schema.json. Relative
sources resolve beside the list. -Record and -All replay saved settings unchanged.
.PARAMETER Record
Replay one retained .import.json record without changing its saved recipe.
.PARAMETER All
Replay every imports/*.import.json record under ContentRoot in name order.
.PARAMETER Reclaim
With -Record or -All, reclaim unused generations only, without reimporting.
.PARAMETER ContentRoot
Authored Content directory; defaults to this script's directory.
.PARAMETER ToolPath
Explicit current importer; its sibling inspector must be from the same build.
.PARAMETER Compression
BC7 (default), or uncompressed RGBA8 with sRGB colour and linear data textures.
.PARAMETER MipPolicy
Full (default), None or Max. Max requires MaxMipLevels.
.EXAMPLE
./import_models.ps1 -SourceList ./import-sources.local.json
.EXAMPLE
./import_models.ps1 -All -WhatIf
.EXAMPLE
./import_models.ps1 -All -Reclaim
#>
[CmdletBinding(DefaultParameterSetName = 'Import', SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Import', Position = 0)][string]$SourceList,

    [Parameter(Mandatory, ParameterSetName = 'Record')][string]$Record,

    [Parameter(Mandatory, ParameterSetName = 'All')][switch]$All,

    [Parameter(ParameterSetName = 'Record')]
    [Parameter(ParameterSetName = 'All')][switch]$Reclaim,

    [string]$ContentRoot = $PSScriptRoot,

    [Parameter(ParameterSetName = 'Import')]
    [ValidateSet('BC7', 'None')][string]$Compression = 'BC7',

    [Parameter(ParameterSetName = 'Import')]
    [ValidateSet('None', 'Full', 'Max')][string]$MipPolicy = 'Full',

    [Parameter(ParameterSetName = 'Import')]
    [ValidateRange(1, 255)][int]$MaxMipLevels,

    [Parameter(ParameterSetName = 'Import')]
    [ValidateSet('Fast', 'Default', 'High')][string]$BC7Quality = 'Fast',

    [Parameter(ParameterSetName = 'Import')]
    [ValidateSet('Box', 'Kaiser')][string]$MipFilter = 'Box',

    [ValidateRange(1, 256)][int]$ThreadPoolSize = [Math]::Clamp([Environment]::ProcessorCount, 2, 16),

    [ValidateRange(1, 256)][int]$TextureWorkers = [Math]::Clamp($ThreadPoolSize - 2, 1, 8),

    [string]$BuildTree,

    [ValidateSet('Release')][string]$Config = 'Release',

    [string]$Preset,

    [string]$ToolPath,

    [Parameter(Mandatory, ParameterSetName = 'Help')]
    [Alias('h')][switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Native failures propagate without replacing the previous selected generation.
$PSNativeCommandUseErrorActionPreference = $false

function Get-AbsolutePath([string]$Path, [string]$Base = (Get-Location).Path) {
    return [IO.Path]::GetFullPath($Path, $Base).TrimEnd([IO.Path]::DirectorySeparatorChar)
}

if (($MipPolicy -eq 'Max') -ne $PSBoundParameters.ContainsKey('MaxMipLevels')) {
    throw '-MaxMipLevels is required exactly when -MipPolicy Max is selected.'
}
if ($Compression -eq 'None' -and $PSBoundParameters.ContainsKey('BC7Quality')) {
    throw '-BC7Quality cannot be used with -Compression None.'
}
if ($TextureWorkers -gt $ThreadPoolSize) {
    throw '-TextureWorkers must not exceed -ThreadPoolSize.'
}
$sdkSupport = Join-Path $PSScriptRoot '../Tools/BuildSelection.ps1'
$installedSdk = Test-Path -LiteralPath $sdkSupport
if ($installedSdk) {
    . $sdkSupport
    $engineRoot = Get-OxygenSourceRoot
} else {
    $engineRoot = Get-AbsolutePath '../..' $PSScriptRoot
    . (Join-Path $engineRoot 'tools/cli/BuildSelection.ps1')
}
$ContentRoot = Get-AbsolutePath $ContentRoot
if ($SourceList) {
    $SourceList = Get-AbsolutePath $SourceList
    if (-not (Test-Path -LiteralPath $SourceList -PathType Leaf)) { throw "Source list not found: $SourceList" }
    $sourceJson = Get-Content -LiteralPath $SourceList -Raw
    if (-not (Test-Json -Json $sourceJson -SchemaFile (Join-Path $PSScriptRoot 'import-sources.schema.json'))) {
        throw 'Source list does not match import-sources.schema.json.'
    }
    $sources = ($sourceJson | ConvertFrom-Json -AsHashtable).sources
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $jobs = foreach ($source in $sources) {
        $original = Get-AbsolutePath $source.source ([IO.Path]::GetDirectoryName($SourceList))
        if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw "Original source is missing: $original" }
        if (-not $names.Add($source.name) -or -not $paths.Add($original)) { throw "Duplicate source name/path: $($source.name)" }
        $type = switch ([IO.Path]::GetExtension($original).ToLowerInvariant()) {
            '.fbx' { 'fbx' }
            '.glb' { 'gltf' }
            '.gltf' { 'gltf' }
            default { throw "Expected original FBX, GLB, or glTF: $original" }
        }
        @{
            id = $source.name; name = $source.name; type = $type; source = $original
            content_policy = 'default'
            content_flags = @{ textures = $true; materials = $true; geometry = $true; scene = $true }
            content_hashing = $true; unit_policy = 'normalize'; bake_transforms = $true
            normals_policy = 'generate'; tangents_policy = 'generate'
            node_pruning = 'keep'; naming_policy = 'normalize'
        }
    }
} else {
    $recordPaths = if ($All) {
        @(Get-ChildItem -LiteralPath (Join-Path $ContentRoot 'imports') -Filter '*.import.json' -File | Sort-Object Name | ForEach-Object { $_.FullName })
    } else {
        @(Get-AbsolutePath $Record)
    }
    if (@($recordPaths).Count -eq 0) { throw "No retained records under $ContentRoot/imports." }
    $recordSchema = if ($installedSdk) {
        Join-Path $PSScriptRoot '../schemas/oxygen.retained-model-import.schema.json'
    } else {
        Join-Path $engineRoot 'src/Oxygen/Cooker/Import/Schemas/oxygen.retained-model-import.schema.json'
    }
    $jobs = foreach ($recordPath in $recordPaths) {
        $recordJson = Get-Content -LiteralPath $recordPath -Raw
        if (-not (Test-Json -Json $recordJson -SchemaFile $recordSchema)) {
            throw "Invalid retained record: $recordPath"
        }
        $saved = $recordJson | ConvertFrom-Json -AsHashtable
        if ($saved.recipe.jobs.Count -ne 1 -or $saved.recipe.jobs[0].type -notin @('fbx', 'gltf')) {
            throw "Expected one FBX or glTF job in retained record: $recordPath"
        }
        @{ name = [IO.Path]::GetFileName($recordPath); type = $saved.recipe.jobs[0].type; record = $recordPath }
    }
}

$action = if ($Reclaim) { 'Reclaim unused generations for' } else { 'Publish' }
$target = if ($Record) { Get-AbsolutePath $Record } else { $ContentRoot }
if (-not $PSCmdlet.ShouldProcess($target, "$action $(@($jobs).Count) retained model imports")) {
    return
}
$targets = @('oxygen-cooker-importtool', 'oxygen-cooker-inspector')
if ($ToolPath -or $installedSdk) {
    if ($BuildTree -or $Preset -or $PSBoundParameters.ContainsKey('Config')) {
        throw 'Build selection options cannot be combined with -ToolPath or an installed SDK.'
    }
    $tools = Resolve-OxygenExecutables -SourceRoot $engineRoot -Targets $targets -Overrides @{ 'oxygen-cooker-importtool' = $ToolPath }
} else {
    $tools = Build-OxygenExecutables -SourceRoot $engineRoot -Targets $targets -BuildTree $BuildTree -Config $Config -Preset $Preset
}
Write-OxygenExecutableSelection $tools
$texture = @{
    mip_policy = $MipPolicy.ToLowerInvariant()
    mip_filter = $MipFilter.ToLowerInvariant()
    output_format = $(if ($Compression -eq 'BC7') { 'bc7_srgb' } else { 'rgba8_srgb' })
    data_format = $(if ($Compression -eq 'BC7') { 'bc7' } else { 'rgba8' })
    bc7_quality = $(if ($Compression -eq 'BC7') { $BC7Quality.ToLowerInvariant() } else { 'none' })
}
if ($MipPolicy -eq 'Max') { $texture.max_mips = $MaxMipLevels }

$queueCapacity = [Math]::Max(4, 2 * $TextureWorkers)
$concurrency = "t:${TextureWorkers}/${queueCapacity},b:1/4,m:1/4,h:1/4,g:1/4,s:1/4"
$temporaryFiles = [Collections.Generic.List[string]]::new()
try {
    foreach ($job in $jobs) {
        if ($Reclaim) {
            Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-importtool' -Arguments @('--no-tui', 'reclaim', '--record', $job.record) -CheckExitCode
            continue
        }
        $reportPath = [IO.Path]::GetTempFileName()
        $temporaryFiles.Add($reportPath)
        $recordPath = if ($SourceList) { Join-Path $ContentRoot "imports/$($job.name).import.json" } else { $job.record }
        $recipeArguments = @()
        if ($SourceList) {
            $recipePath = [IO.Path]::GetTempFileName()
            $temporaryFiles.Add($recipePath)
            @{
                version = 1
                layout = @{ virtual_mount_root = '/.cooked' }
                defaults = @{ texture = $texture }
                jobs = @($job)
            } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $recipePath -Encoding utf8NoBOM
            $recipeArguments = @('--recipe', $recipePath, '--content-root', $ContentRoot)
        }
        Write-Host "Importing $($job.name)..."
        $importArguments = @(
            '--no-tui', '--thread-pool-size', "$ThreadPoolSize", '--concurrency', $concurrency,
            $job.type, '--record', $recordPath, '--report', $reportPath
        ) + $recipeArguments
        Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-importtool' -Arguments $importArguments -CheckExitCode
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        $publishedRoot = $report.session.cooked_root
        $inventoryPath = [IO.Path]::GetTempFileName()
        $temporaryFiles.Add($inventoryPath)
        Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-inspector' -Arguments @('inventory', $publishedRoot, '--output', $inventoryPath) -CheckExitCode
        $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
        if (@($inventory.issues).Count -ne 0) { throw "Cooked inventory contains integrity failures: $publishedRoot" }
        Write-Host "Record: $recordPath"
        Write-Host "Published: $publishedRoot"
    }
} finally {
    foreach ($path in $temporaryFiles) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
}
