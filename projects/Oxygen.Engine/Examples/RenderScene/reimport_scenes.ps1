#requires -Version 7.4
<#
.SYNOPSIS
Reimports RenderScene models through native retained publication.
.DESCRIPTION
Each source has an authored record under Content/imports. Native publication
selects a validated immutable generation; failures retain the prior selection.
Existing readers keep their generation. Source files and demo settings are not
modified. Temporary recipes and reports are deleted on exit.
.PARAMETER SourceList
JSON matching reimport-sources.schema.json. Relative sources resolve beside it.
.PARAMETER ContentRoot
Authored Content directory; defaults to shared Examples/Content.
.PARAMETER Compression
BC7 (default), or uncompressed RGBA8 with sRGB colour and linear data textures.
.PARAMETER MipPolicy
Full (default), None or Max. Max requires MaxMipLevels.
.EXAMPLE
./reimport_scenes.ps1 -SourceList ./reimport-sources.local.json
.EXAMPLE
./reimport_scenes.ps1 -SourceList ./reimport-sources.local.json -WhatIf
#>
[CmdletBinding(DefaultParameterSetName = 'Import', SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Import', Position = 0)][string]$SourceList,

    [string]$ToolPath,

    [string]$InspectorPath,

    [string]$ContentRoot = (Join-Path $PSScriptRoot '../Content'),

    [ValidateSet('BC7', 'None')][string]$Compression = 'BC7',

    [ValidateSet('None', 'Full', 'Max')][string]$MipPolicy = 'Full',

    [ValidateRange(1, 255)][int]$MaxMipLevels,

    [ValidateSet('Fast', 'Default', 'High')][string]$BC7Quality = 'Default',

    [ValidateRange(1, 256)][int]$ThreadPoolSize = 8,

    [ValidateRange(1, 256)][int]$TextureWorkers = 2,

    [string]$BuildTree,

    [string]$Config,

    [string]$Preset,

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
$engineRoot = Get-AbsolutePath '../..' $PSScriptRoot
. (Join-Path $engineRoot 'tools/cli/BuildSelection.ps1')
$tools = Resolve-OxygenExecutables -SourceRoot $engineRoot -Targets @('oxygen-cooker-importtool', 'oxygen-cooker-inspector') -BuildTree $BuildTree -Config $Config -Preset $Preset -Overrides @{
    'oxygen-cooker-importtool' = $ToolPath
    'oxygen-cooker-inspector' = $InspectorPath
}
Write-OxygenExecutableSelection $tools
$ToolPath = $tools.Paths['oxygen-cooker-importtool']
$InspectorPath = $tools.Paths['oxygen-cooker-inspector']
$SourceList = Get-AbsolutePath $SourceList
foreach ($file in @($SourceList, $ToolPath, $InspectorPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file not found: $file" }
}
$sourceJson = Get-Content -LiteralPath $SourceList -Raw
if (-not (Test-Json -Json $sourceJson -SchemaFile (Join-Path $PSScriptRoot 'reimport-sources.schema.json'))) {
    throw 'Source list does not match reimport-sources.schema.json.'
}
$sources = ($sourceJson | ConvertFrom-Json -AsHashtable).sources
$ContentRoot = Get-AbsolutePath $ContentRoot
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

if (-not $PSCmdlet.ShouldProcess($ContentRoot, "Publish $(@($jobs).Count) retained model imports")) {
    return
}
$texture = @{
    mip_policy = $MipPolicy.ToLowerInvariant()
    mip_filter = 'kaiser'
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
        $recipePath = [IO.Path]::GetTempFileName()
        $temporaryFiles.Add($recipePath)
        $reportPath = [IO.Path]::GetTempFileName()
        $temporaryFiles.Add($reportPath)
        $recordPath = Join-Path $ContentRoot "imports/$($job.name).import.json"
        @{
            version = 1
            layout = @{ virtual_mount_root = '/.cooked' }
            defaults = @{ texture = $texture }
            jobs = @($job)
        } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $recipePath -Encoding utf8NoBOM
        Write-Host "Importing $($job.name)..."
        Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-importtool' -Arguments @(
            '--no-tui', '--thread-pool-size', "$ThreadPoolSize", '--concurrency', $concurrency,
            $job.type, '--recipe', $recipePath, '--record', $recordPath,
            '--content-root', $ContentRoot, '--report', $reportPath
        ) -CheckExitCode
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        $publishedRoot = $report.session.cooked_root
        Invoke-OxygenTool -Context $tools -Target 'oxygen-cooker-inspector' -Arguments @('validate', $publishedRoot) -CheckExitCode
        Write-Host "Record: $recordPath"
        Write-Host "Published: $publishedRoot"
    }
} finally {
    foreach ($path in $temporaryFiles) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
}
