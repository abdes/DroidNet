<#
.SYNOPSIS
Asserts Phase 03 VortexBasic runtime-proof expectations from the app log and RenderDoc report.

.DESCRIPTION
Validates three evidence sources:
- the debugger-backed D3D12 audit report proves the no-capture runtime path is clean enough to support the capture-backed proof, with only explicitly accepted warnings allowed
- the runtime stderr log contains the expected draw-metadata count for the validation scene
- the structural RenderDoc analyzer report contains the expected Stage 3 / 9 / 12 / compositing shape
- the product RenderDoc analyzer report proves Stage 3, Stage 9 GBuffer + Velocity MRTs, point/spot/directional Stage 12 SceneColor accumulation, and final present are non-broken

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
  'compositing_draw_count_match' = 'true'
  'phase03_runtime_stage_order_match' = 'true'
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
  'stage9_has_expected_targets' = 'true'
  'stage9_gbuffer_base_color_nonzero' = 'true'
  'stage9_velocity_nonzero' = 'true'
  'stage12_spot_scene_color_nonzero' = 'true'
  'stage12_point_scene_color_nonzero' = 'true'
  'stage12_directional_scene_color_nonzero' = 'true'
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

foreach ($key in $expectedProductChecks.Keys) {
  $actualValue = if ($productsReportMap.ContainsKey($key)) { $productsReportMap[$key] } else { '' }
  if ($actualValue -ne $expectedProductChecks[$key]) {
    throw "Products report check failed or missing: $key (expected $($expectedProductChecks[$key]))"
  }
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
)

foreach ($key in ($expectedChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($captureReportMap[$key])"
}
foreach ($key in ($expectedDebugChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($debugLayerReportMap[$key])"
}
foreach ($key in ($expectedProductChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($productsReportMap[$key])"
}
foreach ($key in $diagnosticDebugKeys) {
  if ($debugLayerReportMap.ContainsKey($key)) {
    $reportLines += "$key=$($debugLayerReportMap[$key])"
  }
}
foreach ($key in $diagnosticProductKeys) {
  if ($productsReportMap.ContainsKey($key)) {
    $reportLines += "$key=$($productsReportMap[$key])"
  }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
