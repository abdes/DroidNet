#requires -Version 7.4
<#
.SYNOPSIS
Reimports original RenderScene models into the standard cooked root with recovery.
.DESCRIPTION
Uses the native ImportTool and Inspector from one build. Validates the source-list
schema and files, generates a persistent native manifest, runs native --dry-run,
preserves the previous root under the build tree, cooks directly into CookedRoot,
and checks reports/descriptors/texture policy. On failure the previous root is
restored; failed output and evidence remain under the build tree for diagnosis.

Close RenderScene and any other consumer of CookedRoot first. Relative source
paths resolve against SourceList. Other relative parameters resolve against the
working directory. The emitted virtual mount root is always /.cooked.
.PARAMETER SourceList
JSON matching reimport-sources.schema.json. Copy the example and set original paths.
.PARAMETER ToolPath
Native Oxygen.Cooker.ImportTool.exe. Defaults to this checkout's Ninja Debug build.
.PARAMETER InspectorPath
Native Oxygen.Cooker.Inspector.exe. Defaults to the ImportTool directory.
.PARAMETER CookedRoot
Live output root. Defaults to Examples/RenderScene/.cooked. Must have an existing parent.
.PARAMETER Compression
BC7 (default) or None. None emits RGBA8 sRGB color and linear RGBA8 data, not source-format preservation.
.PARAMETER MipPolicy
Full (default), None (base level only), or Max (limited full chain).
.PARAMETER MaxMipLevels
Required only with MipPolicy Max. Includes the base level; range 1..255.
.PARAMETER BC7Quality
Fast, Default, or High. Valid only with BC7 compression; Default is the default.
.PARAMETER ThreadPoolSize
Native import thread pool size; default 8.
.PARAMETER TextureWorkers
Native texture pipeline workers; default 2. Queue capacity is twice this value,
with a minimum of 4. Other pipelines have one worker and jobs remain sequential.
.EXAMPLE
./reimport_scenes.ps1 -SourceList ./reimport-sources.local.json
.EXAMPLE
./reimport_scenes.ps1 -SourceList ./reimport-sources.local.json -Compression None -MipPolicy Max -MaxMipLevels 5
.EXAMPLE
./reimport_scenes.ps1 -SourceList ./reimport-sources.local.json -WhatIf
.NOTES
Requires PowerShell 7.4+. Exits nonzero on preflight, native-tool, validation, or
publication failure. Backups, logs and result.json are under
out/build-ninja/renderscene-reimport/<unique-run>. -WhatIf performs read-only preflight and describes
the operation; it does not run native tools or create files. This script does
not validate rendered appearance or modify demo settings.
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory)][string]$SourceList,
    [string]$ToolPath,
    [string]$InspectorPath,
    [string]$CookedRoot = (Join-Path $PSScriptRoot '.cooked'),
    [ValidateSet('BC7', 'None')][string]$Compression = 'BC7',
    [ValidateSet('None', 'Full', 'Max')][string]$MipPolicy = 'Full',
    [ValidateRange(1, 255)][int]$MaxMipLevels,
    [ValidateSet('Fast', 'Default', 'High')][string]$BC7Quality = 'Default',
    [ValidateRange(1, 256)][int]$ThreadPoolSize = 8,
    [ValidateRange(1, 256)][int]$TextureWorkers = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Native failures are handled explicitly, with persistent logs and exact exit codes.
$PSNativeCommandUseErrorActionPreference = $false

function Get-AbsolutePath([string]$Path, [string]$Base = (Get-Location).Path) {
    return [IO.Path]::GetFullPath($Path, $Base).TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Test-Within([string]$Path, [string]$Root) {
    return $Path.Equals($Root, [StringComparison]::OrdinalIgnoreCase) -or
        $Path.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparseAncestor([string]$Path) {
    $cursor = $Path
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing a publication path through a junction/symlink: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Assert-Sibling([string]$Path, [string]$Parent) {
    if (-not [IO.Path]::GetDirectoryName($Path).Equals($Parent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Publication path escaped its checked parent: $Path"
    }
    Assert-NoReparseAncestor $Path
}

function Write-JsonFile($Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
}

function Invoke-NativeLogged([string]$Executable, [string[]]$Arguments, [string]$LogPath) {
    $stage = [IO.Path]::GetFileNameWithoutExtension($LogPath)
    Write-Host "Running ${stage}: $([IO.Path]::GetFileName($Executable)); log: $LogPath"
    & $Executable @Arguments *> $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($Executable)) exited $LASTEXITCODE. See $LogPath"
    }
    Write-Host "Completed $stage"
}

function Get-CheckedChild([string]$Root, [string]$RelativePath) {
    if ([IO.Path]::IsPathRooted($RelativePath)) { throw "Expected relative descriptor path: $RelativePath" }
    $path = Get-AbsolutePath $RelativePath $Root
    if (-not (Test-Within $path $Root) -or $path -eq $Root) {
        throw "Descriptor path escapes cooked root: $RelativePath"
    }
    return $path
}

$runDirectory = $null
$publicationLock = $null
$oldMoved = $false
$generationStarted = $false
$result = [ordered]@{ status = 'preflight'; published = $false }
try {
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
    if (-not $ToolPath) { $ToolPath = Join-Path $engineRoot 'out/build-ninja/bin/Debug/Oxygen.Cooker.ImportTool.exe' }
    $ToolPath = Get-AbsolutePath $ToolPath
    if (-not $InspectorPath) { $InspectorPath = Join-Path ([IO.Path]::GetDirectoryName($ToolPath)) 'Oxygen.Cooker.Inspector.exe' }
    $InspectorPath = Get-AbsolutePath $InspectorPath
    $SourceList = Get-AbsolutePath $SourceList
    foreach ($file in @($SourceList, $ToolPath, $InspectorPath)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file not found: $file" }
    }
    $sourceJson = Get-Content -LiteralPath $SourceList -Raw
    if (-not (Test-Json -Json $sourceJson -SchemaFile (Join-Path $PSScriptRoot 'reimport-sources.schema.json'))) {
        throw 'Source list does not match reimport-sources.schema.json.'
    }
    $sources = ($sourceJson | ConvertFrom-Json -AsHashtable).sources
    $CookedRoot = Get-AbsolutePath $CookedRoot
    $parent = [IO.Path]::GetDirectoryName($CookedRoot)
    if (-not $parent -or -not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw "CookedRoot must have an existing parent directory: $CookedRoot"
    }
    Assert-Sibling $CookedRoot $parent
    foreach ($protectedPath in @($engineRoot, $PSScriptRoot, $ToolPath, $InspectorPath, $SourceList)) {
        if (Test-Within $protectedPath $CookedRoot) { throw "CookedRoot contains protected input/tooling: $protectedPath" }
    }
    if ((Test-Path -LiteralPath $CookedRoot) -and -not (Test-Path -LiteralPath $CookedRoot -PathType Container)) {
        throw "CookedRoot is not a directory: $CookedRoot"
    }
    if (Get-Process -Name 'Oxygen.Examples.RenderScene' -ErrorAction SilentlyContinue) {
        throw 'Close RenderScene before refreshing its content.'
    }

    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $jobs = foreach ($source in $sources) {
        $original = Get-AbsolutePath $source.source ([IO.Path]::GetDirectoryName($SourceList))
        if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw "Original source is missing: $original" }
        if (Test-Within $original $CookedRoot) { throw "Original source is inside the output root: $original" }
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
    $runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $recoveryRoot = Join-Path $engineRoot 'out/build-ninja/renderscene-reimport'
    $pendingRunDirectory = Join-Path $recoveryRoot $runId
    $backup = Join-Path $pendingRunDirectory 'previous-cooked'
    Assert-NoReparseAncestor $pendingRunDirectory
    if (-not [IO.Path]::GetPathRoot($CookedRoot).Equals([IO.Path]::GetPathRoot($backup), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CookedRoot and the build-tree recovery directory must be on the same volume.'
    }
    if (-not $PSCmdlet.ShouldProcess($CookedRoot, "Reimport $($sources.Count) original(s), validate, preserve old generation at $backup, and publish")) {
        return
    }
    $null = New-Item -ItemType Directory -Path $recoveryRoot -Force
    $rootHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($CookedRoot.ToUpperInvariant())))
    $lockPath = Join-Path $recoveryRoot ($rootHash + '.lock')
    Assert-Sibling $lockPath $recoveryRoot
    $publicationLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $null = New-Item -ItemType Directory -Path $pendingRunDirectory
    $runDirectory = $pendingRunDirectory
    $result.cooked_root = $CookedRoot
    $result.backup_root = $backup
    $result.run_directory = $runDirectory
    $result.compression = $Compression
    $result.mip_policy = $MipPolicy
    $result.status = 'preparing'
    Copy-Item -LiteralPath $SourceList -Destination (Join-Path $runDirectory 'sources.json')
    Write-JsonFile @($jobs | ForEach-Object {
        @{ name = $_.name; source = $_.source; sha256 = (Get-FileHash -LiteralPath $_.source -Algorithm SHA256).Hash }
    }) (Join-Path $runDirectory 'source-hashes.json')
    Write-JsonFile @(@($ToolPath, $InspectorPath) | ForEach-Object {
        @{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    }) (Join-Path $runDirectory 'tool-hashes.json')

    $texture = @{
        mip_policy = $MipPolicy.ToLowerInvariant()
        mip_filter = 'kaiser'
        output_format = $(if ($Compression -eq 'BC7') { 'bc7_srgb' } else { 'rgba8_srgb' })
        data_format = $(if ($Compression -eq 'BC7') { 'bc7' } else { 'rgba8' })
        bc7_quality = $(if ($Compression -eq 'BC7') { $BC7Quality.ToLowerInvariant() } else { 'none' })
    }
    if ($MipPolicy -eq 'Max') { $texture.max_mips = $MaxMipLevels }
    $manifest = @{
        version = 1; output = $CookedRoot; thread_pool_size = $ThreadPoolSize; max_in_flight_jobs = 1
        layout = @{ virtual_mount_root = '/.cooked' }
        defaults = @{ texture = $texture }
        concurrency = @{
            texture = @{ workers = $TextureWorkers; queue_capacity = [Math]::Max(4, 2 * $TextureWorkers) }
            buffer = @{ workers = 1; queue_capacity = 4 }
            material = @{ workers = 1; queue_capacity = 4 }
            mesh_build = @{ workers = 1; queue_capacity = 4 }
            geometry = @{ workers = 1; queue_capacity = 4 }
            scene = @{ workers = 1; queue_capacity = 4 }
        }
        jobs = @($jobs)
    }
    $manifestPath = Join-Path $runDirectory 'manifest.json'
    $reportPath = Join-Path $runDirectory 'import-report.json'
    Write-JsonFile $manifest $manifestPath
    Invoke-NativeLogged $ToolPath @('--no-tui', 'batch', '--manifest', $manifestPath, '--dry-run', 'true') (Join-Path $runDirectory 'preflight.log')
    Assert-Sibling $CookedRoot $parent
    Assert-Sibling $backup $runDirectory
    if (Test-Path -LiteralPath $backup) { throw "Backup destination already exists: $backup" }
    if (Get-Process -Name 'Oxygen.Examples.RenderScene' -ErrorAction SilentlyContinue) { throw 'RenderScene started during preflight; close it before recooking.' }
    if (Test-Path -LiteralPath $CookedRoot) {
        [IO.Directory]::Move($CookedRoot, $backup)
        $oldMoved = $true
    }
    $generationStarted = $true
    $result.status = 'cooking'
    Invoke-NativeLogged $ToolPath @('--no-tui', 'batch', '--manifest', $manifestPath, '--report', $reportPath) (Join-Path $runDirectory 'import.log')
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json -AsHashtable
    if ($report.summary.jobs_total -ne $sources.Count -or $report.summary.jobs_succeeded -ne $sources.Count -or
        $report.summary.jobs_failed -ne 0 -or $report.summary.jobs_skipped -ne 0 -or $report.jobs.Count -ne $sources.Count) {
        throw 'Native report does not confirm success for every requested source.'
    }
    $expectedScenes = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($job in $report.jobs) {
        $sceneOutputs = @($job.outputs | Where-Object { [IO.Path]::GetExtension($_.path) -eq '.oscene' })
        if ($job.status -ne 'succeeded' -or $sceneOutputs.Count -ne 1 -or -not $expectedScenes.Add($sceneOutputs[0].path.Replace('\', '/'))) {
            throw 'Each source must produce one distinct successful scene output.'
        }
    }
    $result.status = 'validating'
    Invoke-NativeLogged $InspectorPath @('validate', $CookedRoot) (Join-Path $runDirectory 'validate.log')
    $indexLog = Join-Path $runDirectory 'index.log'
    Invoke-NativeLogged $InspectorPath @('index', $CookedRoot, '--assets', 'true', '--digests', 'true') $indexLog
    $indexedScenes = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $assetCount = 0
    $declaredAssets = -1
    foreach ($line in Get-Content -LiteralPath $indexLog) {
        if ($line -match '^Assets \((\d+)\)$') { $declaredAssets = [int]$Matches[1] }
        if (-not $line.StartsWith('- key=')) { continue }
        if ($line -notmatch "^- key='[^']+' type='(?<type>[^']+)'\(\d+\)' vpath='(?<virtual>[^']+)' desc='(?<path>[^']+)' desc_size=(?<size>\d+) desc_sha256=(?<hash>[A-Fa-f0-9]{64})$") {
            throw "Inspector asset row is unsupported or lacks a descriptor digest: $line"
        }
        $asset = $Matches.Clone()
        $descriptor = Get-CheckedChild $CookedRoot $asset.path
        if ($asset.virtual -cne ('/.cooked/' + $asset.path.Replace('\', '/')) -or
            (Get-Item -LiteralPath $descriptor).Length -ne [long]$asset.size -or
            (Get-FileHash -LiteralPath $descriptor -Algorithm SHA256).Hash -ne $asset.hash) {
            throw "Descriptor identity, size, or SHA256 mismatch: $($asset.path)"
        }
        if ($asset.type -eq 'Scene') { $null = $indexedScenes.Add($asset.path.Replace('\', '/')) }
        $assetCount++
    }
    if ($assetCount -le 0 -or $assetCount -ne $declaredAssets -or -not $expectedScenes.SetEquals($indexedScenes)) {
        throw 'Inspector index does not match the requested scene outputs.'
    }
    $textureCount = 0
    if (Test-Path -LiteralPath (Join-Path $CookedRoot 'Resources/textures.table')) {
        $textureLog = Join-Path $runDirectory 'textures.log'
        Invoke-NativeLogged $InspectorPath @('textures', $CookedRoot) $textureLog
        $declaredTextures = -1
        $textureRows = 0
        foreach ($line in Get-Content -LiteralPath $textureLog) {
            if ($line -match '^Dumping (\d+) textures in:') { $declaredTextures = [int]$Matches[1] }
            if ($line -notmatch '^\s*\d+\s+0x') { continue }
            if ($line -notmatch '^\s*(?<index>\d+)\s+0x[0-9A-Fa-f]+\s+(?<size>\d+)\s+(?<width>\d+)x(?<height>\d+)\s+(?<mips>\d+)\s+(?<layers>\d+)\s+(?<type>.+?)\s+(?<format>\S+)\s+0x[0-9A-Fa-f]+\s*$') {
                throw "Unsupported Inspector texture row: $line"
            }
            $textureRows++
            # Core/Meta/Data/ResourceIndex.inc reserves index 0 for fallback.
            # TextureEmitter::CreateFallbackPayload defines its fixed RGBA8
            # 1x1, one-layer, one-mip 2D payload independently of import policy.
            if ([int]$Matches.index -eq 0) {
                if ([long]$Matches.size -le 0 -or [int]$Matches.width -ne 1 -or
                    [int]$Matches.height -ne 1 -or [int]$Matches.mips -ne 1 -or
                    [int]$Matches.layers -ne 1 -or $Matches.type -ne '2D Texture' -or
                    $Matches.format -ne 'RGBA8_UNORM') {
                    throw "Texture fallback does not match the native resource contract: $line"
                }
                continue
            }
            if ([long]$Matches.size -eq 0) { continue }
            $extent = [Math]::Max([int]$Matches.width, [int]$Matches.height)
            if ($extent -lt 1) { throw 'Texture has an invalid extent.' }
            $fullMips = 1 + [int][Math]::Floor([Math]::Log2($extent))
            $expectedMips = switch ($MipPolicy) { 'None' { 1 } 'Full' { $fullMips } 'Max' { [Math]::Min($MaxMipLevels, $fullMips) } }
            $formats = if ($Compression -eq 'BC7') { @('BC7_UNORM', 'BC7_UNORM_SRGB') } else { @('RGBA8_UNORM', 'RGBA8_UNORM_SRGB') }
            if ([int]$Matches.mips -ne $expectedMips -or $Matches.format -notin $formats) {
                throw "Texture does not match requested compression/mip policy: $line"
            }
            $textureCount++
        }
        if ($textureRows -ne $declaredTextures) { throw 'Could not account for every Inspector texture row.' }
    }
    Write-JsonFile @((Get-ChildItem -LiteralPath $CookedRoot -Recurse -File -Force) | ForEach-Object {
        @{ path = [IO.Path]::GetRelativePath($CookedRoot, $_.FullName).Replace('\', '/'); size_bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    }) (Join-Path $runDirectory 'published-files.json')
    $result.assets = $assetCount
    $result.scenes = @($indexedScenes)
    $result.textures = $textureCount

    $result.status = 'published'
    $result.published = $true
    Write-Host "Published $($sources.Count) scenes to $CookedRoot"
    Write-Host "Previous generation: $(if ($oldMoved) { $backup } else { '(none)' })"
    Write-Host "Reports and hashes: $runDirectory"
} catch {
    $result.status = 'failed'
    $result.error = $_.Exception.Message
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    if ($runDirectory) { Write-Host "Preserved evidence: $runDirectory" }
    exit 1
} finally {
    if ($generationStarted -and -not $result.published) {
        try {
            Assert-Sibling $CookedRoot $parent
            Assert-Sibling $backup $runDirectory
            $failedRoot = Join-Path $runDirectory 'failed-cooked'
            Assert-Sibling $failedRoot $runDirectory
            if (Test-Path -LiteralPath $failedRoot) { throw "Recovery destination already exists: $failedRoot" }
            if (Test-Path -LiteralPath $CookedRoot) { [IO.Directory]::Move($CookedRoot, $failedRoot) }
            if ($oldMoved) { [IO.Directory]::Move($backup, $CookedRoot) }
            $result.status = 'failed_restored'
        } catch {
            $result.status = 'recovery_required'
            $result.recovery_error = $_.Exception.Message
            Write-Error -Message "Recovery failed; previous content remains at ${backup}: $($_.Exception.Message)" -ErrorAction Continue
        }
    }
    if ($publicationLock) { $publicationLock.Dispose() }
    if ($runDirectory) { Write-JsonFile $result (Join-Path $runDirectory 'result.json') }
}
