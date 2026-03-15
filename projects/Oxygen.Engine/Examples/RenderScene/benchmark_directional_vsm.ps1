[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$baselineSettingsPath = Join-Path $PSScriptRoot 'demo_settings.directional_vsm_benchmark_baseline.json'
$liveSettingsPath = Join-Path $PSScriptRoot 'demo_settings.json'
$importToolExe = Join-Path $repoRoot 'out\build-vs\bin\Debug\Oxygen.Cooker.ImportTool.exe'
$inspectorExe = Join-Path $repoRoot 'out\build-vs\bin\Debug\Oxygen.Cooker.Inspector.exe'
$renderSceneExe = Join-Path $repoRoot 'out\build-vs\bin\Debug\Oxygen.Examples.RenderScene.exe'
$physicsDomainsManifestPath = Join-Path $repoRoot 'Examples\Content\scenes\physics_domains\import-manifest.json'
$contentCookedRoot = Join-Path $repoRoot 'Examples\Content\.cooked'
$contentIndexPath = Join-Path $contentCookedRoot 'container.index.bin'
$benchmarkSceneVirtualPath = '/.cooked/Scenes/physics_domains_vsm_benchmark.oscene'
$archiveDir = Join-Path $repoRoot 'out\build-vs\benchmarks\directional-vsm'
$latestLogPath = Join-Path $repoRoot 'out\build-vs\directional-vsm-benchmark-latest.log'
$latestJsonPath = Join-Path $repoRoot 'out\build-vs\directional-vsm-benchmark-latest.json'

if (-not (Test-Path -LiteralPath $baselineSettingsPath)) {
    throw "Missing benchmark baseline settings file: $baselineSettingsPath"
}
if (-not (Test-Path -LiteralPath $importToolExe)) {
    throw "Missing ImportTool executable: $importToolExe"
}
if (-not (Test-Path -LiteralPath $inspectorExe)) {
    throw "Missing Inspector executable: $inspectorExe"
}
if (-not (Test-Path -LiteralPath $renderSceneExe)) {
    throw "Missing RenderScene executable: $renderSceneExe"
}
if (-not (Test-Path -LiteralPath $physicsDomainsManifestPath)) {
    throw "Missing benchmark scene manifest: $physicsDomainsManifestPath"
}

New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
Copy-Item -LiteralPath $baselineSettingsPath -Destination $liveSettingsPath -Force

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$archivedCookLogPath = Join-Path $archiveDir "directional-vsm-benchmark-$timestamp.cook.log"
$archivedLogPath = Join-Path $archiveDir "directional-vsm-benchmark-$timestamp.log"
$args = @(
    '-v', '0',
    '--frames', '120',
    '--fps', '100',
    '--vsync', 'false',
    '--directional-shadows', 'virtual-only'
)

$previousNativeCommandPreference = $null
$hasNativeCommandPreference = $null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)
if ($hasNativeCommandPreference) {
    $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
}
$previousErrorActionPreference = $ErrorActionPreference
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$exitCode = -1
$cookExitCode = -1
$benchmarkSceneKey = $null

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter()]
        [string[]]$ArgumentList = @(),

        [Parameter()]
        [string]$OutputPath
    )

    $ErrorActionPreference = 'Continue'
    if ([string]::IsNullOrWhiteSpace($OutputPath)) {
        & $FilePath @ArgumentList
    } else {
        & $FilePath @ArgumentList *> $OutputPath
    }
    return $LASTEXITCODE
}

function Get-BenchmarkSceneKey {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InspectorPath,

        [Parameter(Mandatory = $true)]
        [string]$CookedRoot,

        [Parameter(Mandatory = $true)]
        [string]$SceneVirtualPath
    )

    $ErrorActionPreference = 'Continue'
    $output = & $InspectorPath index $CookedRoot --assets
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Inspector failed while resolving benchmark scene key (exit code $exitCode)"
    }

    foreach ($line in $output) {
        if ($line -notlike "*vpath='$SceneVirtualPath'*") {
            continue
        }
        if ($line -match "key='([^']+)'.*vpath='([^']+)'") {
            if ($matches[2] -eq $SceneVirtualPath) {
                return $matches[1]
            }
        }
    }

    throw "Benchmark scene '$SceneVirtualPath' was not found in cooked root '$CookedRoot'"
}

function Set-BenchmarkActiveSceneSelection {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SettingsPath,

        [Parameter(Mandatory = $true)]
        [string]$SceneKey,

        [Parameter(Mandatory = $true)]
        [string]$SceneVirtualPath,

        [Parameter(Mandatory = $true)]
        [string]$IndexPath
    )

    $settings = Get-Content -LiteralPath $SettingsPath -Raw | ConvertFrom-Json
    $settings.content.active_scene.key = $SceneKey
    $settings.content.active_scene.name = $SceneVirtualPath
    $settings.content.active_scene.source_is_pak = $false
    $settings.content.active_scene.source_path = $IndexPath
    $settings | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $SettingsPath -Encoding utf8
}

function Get-BenchmarkSettledStats {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath
    )

    if (-not (Test-Path -LiteralPath $LogPath)) {
        return $null
    }

    $requested = New-Object System.Collections.Generic.List[object]
    $scheduled = New-Object System.Collections.Generic.List[object]
    $raster = New-Object System.Collections.Generic.List[object]

    $pendingRaster = $null
    foreach ($line in (Get-Content -LiteralPath $LogPath)) {
        if ($line -match 'source_frame=(\d+) requested_pages=(\d+)') {
            $requested.Add([pscustomobject]@{
                source_frame = [int]$matches[1]
                value = [int]$matches[2]
            })
            continue
        }
        if ($line -match 'source_frame=(\d+) scheduled_pages=(\d+)') {
            $scheduled.Add([pscustomobject]@{
                source_frame = [int]$matches[1]
                value = [int]$matches[2]
            })
            continue
        }
        if ($line -match '(\d+) shadow-caster draw\(s\) for (\d+) pending raster virtual page\(s\).*\(rastered_pages=(\d+)') {
            $raster.Add([pscustomobject]@{
                shadow_draws = [int]$matches[1]
                indirect_draw_records = $null
                pending_raster_pages = [int]$matches[2]
                rastered_pages = [int]$matches[3]
            })
            $pendingRaster = $null
            continue
        }
        if ($line -match 'executed (\d+) indirect draw record\(s\) across (\d+) command submission\(s\) for (\d+) pending raster virtual page\(s\).*\(rastered_pages=(\d+)') {
            $raster.Add([pscustomobject]@{
                shadow_draws = $null
                indirect_draw_records = [int]$matches[1]
                cpu_draw_submissions = [int]$matches[2]
                pending_raster_pages = [int]$matches[3]
                rastered_pages = [int]$matches[4]
            })
            $pendingRaster = $null
            continue
        }
        if ($line -match '(\d+) shadow-caster draw\(s\) for (\d+) pending raster virtual page\(s\)') {
            $pendingRaster = [pscustomobject]@{
                shadow_draws = [int]$matches[1]
                indirect_draw_records = $null
                pending_raster_pages = [int]$matches[2]
            }
            continue
        }
        if ($line -match 'executed (\d+) indirect draw record\(s\) across (\d+) command submission\(s\) for (\d+) pending raster virtual page\(s\)') {
            $pendingRaster = [pscustomobject]@{
                shadow_draws = $null
                indirect_draw_records = [int]$matches[1]
                cpu_draw_submissions = [int]$matches[2]
                pending_raster_pages = [int]$matches[3]
            }
            continue
        }
        if ($pendingRaster -ne $null -and $line -match '\(rastered_pages=(\d+)') {
            $raster.Add([pscustomobject]@{
                shadow_draws = $pendingRaster.shadow_draws
                indirect_draw_records = $pendingRaster.indirect_draw_records
                pending_raster_pages = $pendingRaster.pending_raster_pages
                rastered_pages = [int]$matches[1]
            })
            $pendingRaster = $null
        }
    }

    $settledRequested = @($requested | Where-Object { $_.source_frame -ge 101 -and $_.source_frame -le 120 })
    $settledScheduled = @($scheduled | Where-Object { $_.source_frame -ge 101 -and $_.source_frame -le 120 })
    if ($settledRequested.Count -eq 0) {
        return $null
    }

    $tailCount = $settledRequested.Count
    $settledRaster = @($raster | Select-Object -Last $tailCount)

    $requestedAverage = if ($settledRequested.Count -gt 0) {
        ($settledRequested | Measure-Object -Property value -Average).Average
    } else { $null }
    $scheduledAverage = if ($settledScheduled.Count -gt 0) {
        ($settledScheduled | Measure-Object -Property value -Average).Average
    } elseif ($settledRaster.Count -gt 0) {
        # The live raster pass reports the dirty page queue that actually needs
        # redraw. When the explicit schedule sample is absent, use that same
        # pending-raster count as the scheduled work metric.
        ($settledRaster | Measure-Object -Property pending_raster_pages -Average).Average
    } else { $null }
    $settledShadowDrawSamples = @($settledRaster | Where-Object { $null -ne $_.shadow_draws })
    $shadowDrawAverage = if ($settledShadowDrawSamples.Count -gt 0) {
        ($settledShadowDrawSamples | Measure-Object -Property shadow_draws -Average).Average
    } else { $null }
    $settledIndirectDrawSamples = @($settledRaster | Where-Object { $null -ne $_.indirect_draw_records })
    $indirectDrawRecordAverage = if ($settledIndirectDrawSamples.Count -gt 0) {
        ($settledIndirectDrawSamples | Measure-Object -Property indirect_draw_records -Average).Average
    } else { $null }
    $pendingRasterPagesAverage = if ($settledRaster.Count -gt 0) {
        ($settledRaster | Measure-Object -Property pending_raster_pages -Average).Average
    } else { $null }
    $rasteredPagesAverage = if ($settledRaster.Count -gt 0) {
        ($settledRaster | Measure-Object -Property rastered_pages -Average).Average
    } else { $null }

    return [ordered]@{
        settled_source_frame_start = 101
        settled_source_frame_end = 120
        settled_source_frames_present = @($settledRequested | ForEach-Object { $_.source_frame })
        requested_pages_avg = if ($null -ne $requestedAverage) { [math]::Round($requestedAverage, 2) } else { $null }
        scheduled_pages_avg = if ($null -ne $scheduledAverage) { [math]::Round($scheduledAverage, 2) } else { $null }
        shadow_draws_avg = if ($null -ne $shadowDrawAverage) { [math]::Round($shadowDrawAverage, 2) } else { $null }
        indirect_draw_records_avg = if ($null -ne $indirectDrawRecordAverage) { [math]::Round($indirectDrawRecordAverage, 2) } else { $null }
        pending_raster_pages_avg = if ($null -ne $pendingRasterPagesAverage) { [math]::Round($pendingRasterPagesAverage, 2) } else { $null }
        rastered_pages_avg = if ($null -ne $rasteredPagesAverage) { [math]::Round($rasteredPagesAverage, 2) } else { $null }
        raster_sample_count = $settledRaster.Count
    }
}

try {
    $cookExitCode = Invoke-NativeCommand -FilePath $importToolExe -ArgumentList @(
        'batch',
        '--manifest',
        $physicsDomainsManifestPath,
        '--no-tui',
        '--max-in-flight-jobs',
        '1'
    ) -OutputPath $archivedCookLogPath
    if ($cookExitCode -ne 0) {
        throw "Benchmark scene cook failed with exit code $cookExitCode"
    }

    if (-not (Test-Path -LiteralPath $contentIndexPath)) {
        throw "Cooked content index not found after benchmark scene cook: $contentIndexPath"
    }

    $benchmarkSceneKey = Get-BenchmarkSceneKey -InspectorPath $inspectorExe `
        -CookedRoot $contentCookedRoot `
        -SceneVirtualPath $benchmarkSceneVirtualPath

    Set-BenchmarkActiveSceneSelection -SettingsPath $liveSettingsPath `
        -SceneKey $benchmarkSceneKey `
        -SceneVirtualPath $benchmarkSceneVirtualPath `
        -IndexPath $contentIndexPath

    $stopwatch.Restart()
    $exitCode = Invoke-NativeCommand -FilePath $renderSceneExe -ArgumentList $args -OutputPath $archivedLogPath
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
    if ($hasNativeCommandPreference) {
        $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
    }
    $stopwatch.Stop()
    Copy-Item -LiteralPath $baselineSettingsPath -Destination $liveSettingsPath -Force
}

Copy-Item -LiteralPath $archivedLogPath -Destination $latestLogPath -Force

$settledStats = Get-BenchmarkSettledStats -LogPath $archivedLogPath
$baselineSettingsHash = (Get-FileHash -LiteralPath $baselineSettingsPath -Algorithm SHA256).Hash
$restoredSettingsHash = (Get-FileHash -LiteralPath $liveSettingsPath -Algorithm SHA256).Hash

$result = [ordered]@{
    benchmark_command = "$renderSceneExe $($args -join ' ')"
    restored_settings_from = $baselineSettingsPath
    active_settings = $liveSettingsPath
    baseline_settings_sha256 = $baselineSettingsHash
    restored_settings_sha256 = $restoredSettingsHash
    cooked_manifest = $physicsDomainsManifestPath
    cooked_root = $contentCookedRoot
    benchmark_scene_virtual_path = $benchmarkSceneVirtualPath
    benchmark_scene_key = $benchmarkSceneKey
    cook_log = $archivedCookLogPath
    cook_exit_code = $cookExitCode
    archived_log = $archivedLogPath
    latest_log = $latestLogPath
    exit_code = $exitCode
    wall_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 0)
    approx_fps = [math]::Round((120.0 * 1000.0) / [math]::Max(1.0, $stopwatch.Elapsed.TotalMilliseconds), 2)
    settled_stats = $settledStats
}

$result | ConvertTo-Json | Set-Content -LiteralPath $latestJsonPath -Encoding utf8

if ($exitCode -ne 0) {
    throw "RenderScene benchmark failed with exit code $exitCode"
}

$result | ConvertTo-Json -Compress
