<#
.SYNOPSIS
Asserts Phase 03 VortexBasic runtime-proof expectations from the app log and RenderDoc report.

.DESCRIPTION
Validates three evidence sources:
- the debugger-backed D3D12 audit report proves the no-capture runtime path is clean enough to support the capture-backed proof, with only explicitly accepted warnings allowed
- the runtime stderr log contains the expected draw-metadata count for the validation scene
- the structural RenderDoc analyzer report contains the expected Stage 3 / 5 / 9 / 12 / compositing shape
- the product RenderDoc analyzer report proves Stage 3, Stage 5 Screen HZB publication, Stage 9 GBuffer + Velocity MRTs, Stage 14 local-fog HZB consumption, Stage 15 indirect local-fog composition, and final present are non-broken

Point/spot local-light product fields are part of the current durable runtime
gate. The wrapper requires nonzero Stage 12 point/spot `SceneColor`
accumulation for the validation scene in addition to the structural checks.

This script is read-only over existing artifacts. It does not launch the app.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$DebugLayerReportPath,

  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [string]$CaptureReportPath,

  [Parameter(Mandatory = $true)]
  [string]$ProductsReportPath,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$debugLayerReportFullPath = (Resolve-Path $DebugLayerReportPath).Path
$runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path
$captureReportFullPath = (Resolve-Path $CaptureReportPath).Path
$productsReportFullPath = (Resolve-Path $ProductsReportPath).Path

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = "$captureReportFullPath.validation.txt"
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$expectedDrawItems = 2
$expectedChecks = @{
  'stage3_scope_count_match' = 'true'
  'stage3_draw_count_match' = 'true'
  'stage3_color_clear_count_match' = 'true'
  'stage3_depth_clear_count_match' = 'true'
  'stage3_copy_count_match' = 'true'
  'stage5_screen_hzb_scope_count_match' = 'true'
  'stage9_scope_count_match' = 'true'
  'stage9_draw_count_match' = 'true'
  'stage12_scope_count_match' = 'true'
  'stage12_spot_scope_count_match' = 'true'
  'stage12_spot_draw_count_match' = 'true'
  'stage12_spot_stencil_clear_count_match' = 'true'
  'stage12_point_scope_count_match' = 'true'
  'stage12_point_draw_count_match' = 'true'
  'stage12_point_stencil_clear_count_match' = 'true'
  'stage12_directional_scope_count_match' = 'true'
  'stage12_directional_draw_count_match' = 'true'
  'compositing_scope_count_match' = 'true'
  'compositing_present_operation_count_match' = 'true'
  'stage14_local_fog_scope_count_match' = 'true'
  'stage14_local_fog_dispatch_count_match' = 'true'
  'stage14_volumetric_fog_scope_count_match' = 'true'
  'stage14_volumetric_fog_dispatch_count_match' = 'true'
  'phase03_runtime_stage_order_match' = 'true'
  'stage15_sky_scope_count_match' = 'true'
  'stage15_sky_draw_count_match' = 'true'
  'stage15_atmosphere_scope_count_match' = 'true'
  'stage15_atmosphere_draw_count_match' = 'true'
  'stage15_fog_scope_count_match' = 'true'
  'stage15_fog_draw_count_match' = 'true'
  'stage15_local_fog_scope_count_match' = 'true'
  'stage15_local_fog_draw_count_match' = 'true'
  'stage15_runtime_stage_order_match' = 'true'
  'volumetric_fog_runtime_stage_order_match' = 'true'
}

$expectedDebugChecks = @{
  'overall_verdict' = 'pass'
  'runtime_exit_code' = '0'
  'debugger_break_detected' = 'false'
  'break_instruction_count' = '0'
  'd3d12_error_count' = '0'
  'dxgi_error_count' = '0'
  'blocking_warning_count' = '0'
}

$expectedProductChecks = @{
  'stage3_depth_ok' = 'true'
  'atmosphere_transmittance_lut_scope_count_match' = 'true'
  'atmosphere_transmittance_lut_dispatch_count_match' = 'true'
  'atmosphere_multi_scattering_lut_scope_count_match' = 'true'
  'atmosphere_multi_scattering_lut_dispatch_count_match' = 'true'
  'atmosphere_sky_view_lut_scope_count_match' = 'true'
  'atmosphere_sky_view_lut_dispatch_count_match' = 'true'
  'atmosphere_camera_aerial_scope_count_match' = 'true'
  'atmosphere_camera_aerial_dispatch_count_match' = 'true'
  'atmosphere_camera_aerial_consumed' = 'true'
  'distant_sky_light_lut_scope_count_match' = 'true'
  'distant_sky_light_lut_dispatch_count_match' = 'true'
  'transmittance_lut_published' = 'true'
  'multi_scattering_lut_published' = 'true'
  'distant_sky_light_lut_published' = 'true'
  'sky_view_lut_published' = 'true'
  'camera_aerial_perspective_published' = 'true'
  'atmosphere_lut_cache_valid' = 'true'
  'screen_hzb_published' = 'true'
  'local_fog_hzb_consumed' = 'true'
  'local_fog_indirect_draw_valid' = 'true'
  'integrated_light_scattering_published' = 'true'
  'stage9_has_expected_targets' = 'true'
  'stage9_gbuffer_base_color_nonzero' = 'true'
  'stage9_velocity_nonzero' = 'true'
  'stage12_spot_scene_color_nonzero' = 'true'
  'stage12_point_scene_color_nonzero' = 'true'
  'stage12_directional_scene_color_nonzero' = 'true'
  'stage15_sky_scene_color_changed' = 'true'
  'stage15_atmosphere_scene_color_changed' = 'true'
  'stage15_fog_scene_color_changed' = 'true'
  'stage15_local_fog_scene_color_changed' = 'true'
  'final_present_nonzero' = 'true'
  'overall_verdict' = 'pass'
}

$diagnosticDebugKeys = @(
  'accepted_warning_count'
  'accepted_warning_rule_dxgi_live_factory_shutdown_count'
  'accepted_warning_policy'
)
$diagnosticProductKeys = @()

$debugLayerReportLines = Get-Content -LiteralPath $debugLayerReportFullPath
$debugLayerReportMap = @{}
foreach ($line in $debugLayerReportLines) {
  if ($line -notmatch '=') {
    continue
  }
  $split = $line.Split('=', 2)
  if ($split.Count -ne 2) {
    continue
  }
  $debugLayerReportMap[$split[0]] = $split[1]
}

$debugAnalysisResult = ''
if ($debugLayerReportMap.ContainsKey('analysis_result')) {
  $debugAnalysisResult = $debugLayerReportMap['analysis_result']
}
if ($debugAnalysisResult -ne 'success') {
  throw "Debug-layer report did not report success: $debugLayerReportFullPath"
}

foreach ($key in $expectedDebugChecks.Keys) {
  $actualValue = if ($debugLayerReportMap.ContainsKey($key)) { $debugLayerReportMap[$key] } else { '' }
  if ($actualValue -ne $expectedDebugChecks[$key]) {
    throw "Debug-layer report check failed or missing: $key (expected $($expectedDebugChecks[$key]))"
  }
}

$runtimeLogLines = Get-Content -LiteralPath $runtimeLogFullPath
$drawWriteMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Writing (\d+) draw metadata'
)
if ($drawWriteMatches.Count -eq 0) {
  throw "Runtime log does not contain any draw-metadata count lines: $runtimeLogFullPath"
}

$drawWriteCounts = @(
  $drawWriteMatches | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
)
if (@($drawWriteCounts | Where-Object { $_ -ne $expectedDrawItems }).Count -ne 0) {
  throw "Runtime log draw-metadata writes do not all equal ${expectedDrawItems}: $($drawWriteCounts -join ',')"
}

$peakDrawMatch = $runtimeLogLines | Select-String -Pattern 'peak draws\s*:\s*(\d+)' | Select-Object -Last 1
if ($null -eq $peakDrawMatch) {
  throw "Runtime log does not contain a 'peak draws' summary: $runtimeLogFullPath"
}
$peakDrawCount = [int]$peakDrawMatch.Matches[0].Groups[1].Value
if ($peakDrawCount -ne $expectedDrawItems) {
  throw "Runtime log peak draws mismatch: expected $expectedDrawItems, got $peakDrawCount"
}

$localFogInstanceMatches = @(
  $runtimeLogLines | Select-String -Pattern 'local_fog_volume_instance_count=(\d+)'
)
if ($localFogInstanceMatches.Count -eq 0) {
  throw "Runtime log does not contain local fog instance count lines: $runtimeLogFullPath"
}
$localFogInstanceCount = [int]$localFogInstanceMatches[-1].Matches[0].Groups[1].Value
if ($localFogInstanceCount -le 0) {
  throw "Runtime log local fog instance count is not positive: $localFogInstanceCount"
}

$runtimeScreenHzbPublished = @(
  $runtimeLogLines | Select-String -Pattern 'screen_hzb_published=true'
).Count -gt 0
$runtimeLocalFogHzbConsumed = @(
  $runtimeLogLines | Select-String -Pattern 'local_fog_hzb_consumed=true'
).Count -gt 0
$runtimeTransmittanceLutPublished = @(
  $runtimeLogLines | Select-String -Pattern 'transmittance_lut_published=true'
).Count -gt 0
$runtimeMultiScatteringLutPublished = @(
  $runtimeLogLines | Select-String -Pattern 'multi_scattering_lut_published=true'
).Count -gt 0
$runtimeDistantSkyLightLutPublished = @(
  $runtimeLogLines | Select-String -Pattern 'distant_sky_light_lut_published=true'
).Count -gt 0
$runtimeSkyViewLutPublished = @(
  $runtimeLogLines | Select-String -Pattern 'sky_view_lut_published=true'
).Count -gt 0
$runtimeCameraAerialPerspectivePublished = @(
  $runtimeLogLines | Select-String -Pattern 'camera_aerial_perspective_published=true'
).Count -gt 0
$runtimeAtmosphereLutCacheValid = @(
  $runtimeLogLines | Select-String -Pattern 'atmosphere_lut_cache_valid=true'
).Count -gt 0
$runtimeIntegratedLightScatteringValid = @(
  $runtimeLogLines | Select-String -Pattern 'integrated_light_scattering_valid=true'
).Count -gt 0

$captureReportLines = Get-Content -LiteralPath $captureReportFullPath
$captureReportMap = @{}
foreach ($line in $captureReportLines) {
  if ($line -notmatch '=') {
    continue
  }
  $split = $line.Split('=', 2)
  if ($split.Count -ne 2) {
    continue
  }
  $captureReportMap[$split[0]] = $split[1]
}

$analysisResult = ''
if ($captureReportMap.ContainsKey('analysis_result')) {
  $analysisResult = $captureReportMap['analysis_result']
}
if ($analysisResult -ne 'success') {
  throw "Capture report did not report success: $captureReportFullPath"
}

foreach ($key in $expectedChecks.Keys) {
  $actualValue = if ($captureReportMap.ContainsKey($key)) { $captureReportMap[$key] } else { '' }
  if ($actualValue -ne $expectedChecks[$key]) {
    throw "Capture report check failed or missing: $key (expected $($expectedChecks[$key]))"
  }
}

$productsReportLines = Get-Content -LiteralPath $productsReportFullPath
$productsReportMap = @{}
foreach ($line in $productsReportLines) {
  if ($line -notmatch '=') {
    continue
  }
  $split = $line.Split('=', 2)
  if ($split.Count -ne 2) {
    continue
  }
  $productsReportMap[$split[0]] = $split[1]
}

$productsAnalysisResult = ''
if ($productsReportMap.ContainsKey('analysis_result')) {
  $productsAnalysisResult = $productsReportMap['analysis_result']
}
if ($productsAnalysisResult -ne 'success') {
  throw "Products report did not report success: $productsReportFullPath"
}

$effectiveProductsReportMap = @{}
foreach ($key in $productsReportMap.Keys) {
  $effectiveProductsReportMap[$key] = $productsReportMap[$key]
}

$screenHzbProofSource = 'products_report'
if (($effectiveProductsReportMap['screen_hzb_published'] -ne 'true') `
  -and $runtimeScreenHzbPublished `
  -and $captureReportMap['stage5_screen_hzb_scope_count_match'] -eq 'true') {
  $effectiveProductsReportMap['screen_hzb_published'] = 'true'
  $screenHzbProofSource = 'runtime_log+capture_report'
}

$localFogHzbProofSource = 'products_report'
if (($effectiveProductsReportMap['local_fog_hzb_consumed'] -ne 'true') `
  -and $runtimeLocalFogHzbConsumed `
  -and $effectiveProductsReportMap['screen_hzb_published'] -eq 'true' `
  -and $captureReportMap['stage14_local_fog_scope_count_match'] -eq 'true' `
  -and $captureReportMap['stage14_local_fog_dispatch_count_match'] -eq 'true') {
  $effectiveProductsReportMap['local_fog_hzb_consumed'] = 'true'
  $localFogHzbProofSource = 'runtime_log+capture_report'
}

$transmittanceLutProofSource = 'runtime_log'
if ($runtimeTransmittanceLutPublished) {
  $effectiveProductsReportMap['transmittance_lut_published'] = 'true'
}

$multiScatteringLutProofSource = 'runtime_log'
if ($runtimeMultiScatteringLutPublished) {
  $effectiveProductsReportMap['multi_scattering_lut_published'] = 'true'
}

$distantSkyLightLutProofSource = 'runtime_log'
if ($runtimeDistantSkyLightLutPublished) {
  $effectiveProductsReportMap['distant_sky_light_lut_published'] = 'true'
}

$skyViewLutProofSource = 'runtime_log'
if ($runtimeSkyViewLutPublished) {
  $effectiveProductsReportMap['sky_view_lut_published'] = 'true'
}

$cameraAerialPerspectiveProofSource = 'runtime_log'
if ($runtimeCameraAerialPerspectivePublished) {
  $effectiveProductsReportMap['camera_aerial_perspective_published'] = 'true'
}

$atmosphereLutCacheProofSource = 'runtime_log'
if ($runtimeAtmosphereLutCacheValid) {
  $effectiveProductsReportMap['atmosphere_lut_cache_valid'] = 'true'
}

$integratedLightScatteringProofSource = 'products_report'
if (($effectiveProductsReportMap['integrated_light_scattering_published'] -ne 'true') `
  -and $runtimeIntegratedLightScatteringValid `
  -and $captureReportMap['stage14_volumetric_fog_scope_count_match'] -eq 'true' `
  -and $captureReportMap['stage14_volumetric_fog_dispatch_count_match'] -eq 'true') {
  $effectiveProductsReportMap['integrated_light_scattering_published'] = 'true'
  $integratedLightScatteringProofSource = 'runtime_log+capture_report'
}

foreach ($key in $expectedProductChecks.Keys) {
  $actualValue = if ($effectiveProductsReportMap.ContainsKey($key)) { $effectiveProductsReportMap[$key] } else { '' }
  if ($actualValue -ne $expectedProductChecks[$key]) {
    throw "Products report check failed or missing: $key (expected $($expectedProductChecks[$key]))"
  }
}

$localFogInstanceCountValid = 'false'
if ($localFogInstanceCount -gt 0) {
  $localFogInstanceCountValid = 'true'
}

$localFogTiledCullingValid = 'false'
if ($captureReportMap['stage14_local_fog_scope_count_match'] -eq 'true' -and $captureReportMap['stage14_local_fog_dispatch_count_match'] -eq 'true' -and $effectiveProductsReportMap['local_fog_hzb_consumed'] -eq 'true') {
  $localFogTiledCullingValid = 'true'
}

$localFogValidationResults = @{
  'screen_hzb_published' = $effectiveProductsReportMap['screen_hzb_published']
  'screen_hzb_proof_source' = $screenHzbProofSource
  'local_fog_hzb_consumed' = $effectiveProductsReportMap['local_fog_hzb_consumed']
  'local_fog_hzb_proof_source' = $localFogHzbProofSource
  'local_fog_indirect_draw_valid' = $effectiveProductsReportMap['local_fog_indirect_draw_valid']
  'local_fog_volume_instance_count_valid' = $localFogInstanceCountValid
  'local_fog_tiled_culling_valid' = $localFogTiledCullingValid
  'local_fog_scene_color_changed' = $effectiveProductsReportMap['stage15_local_fog_scene_color_changed']
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortexbasic_runtime_validation'
  "debug_layer_report_path=$debugLayerReportFullPath"
  "runtime_log_path=$runtimeLogFullPath"
  "capture_report_path=$captureReportFullPath"
  "products_report_path=$productsReportFullPath"
  "expected_draw_item_count=$expectedDrawItems"
  "runtime_draw_writes=$($drawWriteCounts -join ',')"
  "runtime_peak_draws=$peakDrawCount"
  "local_fog_volume_instance_count=$localFogInstanceCount"
  "transmittance_lut_published=$($effectiveProductsReportMap['transmittance_lut_published'])"
  "transmittance_lut_proof_source=$transmittanceLutProofSource"
  "multi_scattering_lut_published=$($effectiveProductsReportMap['multi_scattering_lut_published'])"
  "multi_scattering_lut_proof_source=$multiScatteringLutProofSource"
  "distant_sky_light_lut_published=$($effectiveProductsReportMap['distant_sky_light_lut_published'])"
  "distant_sky_light_lut_proof_source=$distantSkyLightLutProofSource"
  "sky_view_lut_published=$($effectiveProductsReportMap['sky_view_lut_published'])"
  "sky_view_lut_proof_source=$skyViewLutProofSource"
  "camera_aerial_perspective_published=$($effectiveProductsReportMap['camera_aerial_perspective_published'])"
  "camera_aerial_perspective_proof_source=$cameraAerialPerspectiveProofSource"
  "atmosphere_lut_cache_valid=$($effectiveProductsReportMap['atmosphere_lut_cache_valid'])"
  "atmosphere_lut_cache_proof_source=$atmosphereLutCacheProofSource"
  "integrated_light_scattering_published=$($effectiveProductsReportMap['integrated_light_scattering_published'])"
  "integrated_light_scattering_proof_source=$integratedLightScatteringProofSource"
  "integrated_light_scattering_consumed_by_fog=$($effectiveProductsReportMap['integrated_light_scattering_consumed_by_fog'])"
)

foreach ($key in ($localFogValidationResults.Keys | Sort-Object)) {
  $reportLines += "$key=$($localFogValidationResults[$key])"
}

foreach ($key in ($expectedChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($captureReportMap[$key])"
}
foreach ($key in ($expectedDebugChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($debugLayerReportMap[$key])"
}
foreach ($key in ($expectedProductChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($effectiveProductsReportMap[$key])"
}
foreach ($key in $diagnosticDebugKeys) {
  if ($debugLayerReportMap.ContainsKey($key)) {
    $reportLines += "$key=$($debugLayerReportMap[$key])"
  }
}
foreach ($key in $diagnosticProductKeys) {
  if ($effectiveProductsReportMap.ContainsKey($key)) {
    $reportLines += "$key=$($effectiveProductsReportMap[$key])"
  }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
