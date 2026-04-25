<#
.SYNOPSIS
Asserts Async runtime-proof expectations from RenderDoc reports and exported images.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CaptureReportPath,

  [Parameter(Mandatory = $true)]
  [string]$ProductsReportPath,

  [Parameter(Mandatory = $true)]
  [string]$BaselineFramePath,

  [Parameter(Mandatory = $true)]
  [string]$ComparisonFramePath,

  [Parameter(Mandatory = $true)]
  [string]$BaselineDepthPath,

  [Parameter(Mandatory = $true)]
  [string]$ComparisonDepthPath,

  [Parameter()]
  [double]$MinPsnrDb = 40.0,

  [Parameter()]
  [double]$MaxDepthError = 0.001,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-ReportMap {
  param([Parameter(Mandatory = $true)][string]$Path)

  $fullPath = (Resolve-Path $Path).Path
  $map = @{}
  foreach ($line in Get-Content -LiteralPath $fullPath) {
    if ($line -notmatch '=') {
      continue
    }
    $split = $line.Split('=', 2)
    if ($split.Count -ne 2) {
      continue
    }
    $map[$split[0]] = $split[1]
  }
  return $map
}

function Get-ReportValue {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Report,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter()][string]$Default = ''
  )

  if ($Report.ContainsKey($Key)) {
    return $Report[$Key]
  }
  return $Default
}

function Invoke-ImageMetric {
  param(
    [Parameter(Mandatory = $true)][ValidateSet('color', 'depth')] [string]$Kind,
    [Parameter(Mandatory = $true)][string]$BaselinePath,
    [Parameter(Mandatory = $true)][string]$ComparisonPath
  )

  $script = @'
import json
import math
import sys
from pathlib import Path
from PIL import Image

kind = sys.argv[1]
baseline = Path(sys.argv[2]).resolve()
comparison = Path(sys.argv[3]).resolve()

if not baseline.exists():
    raise SystemExit(f"Missing image: {baseline}")
if not comparison.exists():
    raise SystemExit(f"Missing image: {comparison}")

if kind == "color":
    left = Image.open(baseline).convert("RGB")
    right = Image.open(comparison).convert("RGB")
else:
    left = Image.open(baseline).convert("L")
    right = Image.open(comparison).convert("L")

if left.size != right.size:
    raise SystemExit(f"Image size mismatch: {left.size} vs {right.size}")

left_pixels = list(left.getdata())
right_pixels = list(right.getdata())

if kind == "color":
    squared_error = 0.0
    sample_count = 0
    for lp, rp in zip(left_pixels, right_pixels):
        for lv, rv in zip(lp, rp):
            diff = float(lv) - float(rv)
            squared_error += diff * diff
            sample_count += 1
    mse = squared_error / float(sample_count)
    psnr = 999.0 if mse == 0.0 else 20.0 * math.log10(255.0) - 10.0 * math.log10(mse)
    payload = {"psnr_db": psnr}
else:
    max_error = 0.0
    for lv, rv in zip(left_pixels, right_pixels):
        diff = abs(float(lv) - float(rv)) / 255.0
        if diff > max_error:
            max_error = diff
    payload = {"max_error": max_error}

print(json.dumps(payload))
'@

  $output = @"
$script
"@ | python - $Kind $BaselinePath $ComparisonPath
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to compute image metric for $Kind"
  }

  return $output | ConvertFrom-Json
}

function Test-SourceContains {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Needle
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $false
  }
  $content = Get-Content -LiteralPath $Path -Raw
  return $content.Contains($Needle)
}

$captureReportFullPath = (Resolve-Path $CaptureReportPath).Path
$productsReportFullPath = (Resolve-Path $ProductsReportPath).Path
$baselineFrameFullPath = (Resolve-Path $BaselineFramePath).Path
$comparisonFrameFullPath = (Resolve-Path $ComparisonFramePath).Path
$baselineDepthFullPath = (Resolve-Path $BaselineDepthPath).Path
$comparisonDepthFullPath = (Resolve-Path $ComparisonDepthPath).Path

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $captureReportStem = $captureReportFullPath -replace '_async_capture_report\.txt$', ''
  $ReportPath = "$captureReportStem.validation.txt"
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$captureReportMap = Read-ReportMap -Path $captureReportFullPath
$productsReportMap = Read-ReportMap -Path $productsReportFullPath
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sceneRendererPath = Join-Path $repoRoot 'src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp'
$stage22InputBundleMatchesContract = (
  (Test-SourceContains -Path $sceneRendererPath -Needle 'active_view->composite_source != nullptr') -and
  (Test-SourceContains -Path $sceneRendererPath -Needle 'SceneRenderer Stage 22 requires a SceneRenderer-supplied post target') -and
  (Test-SourceContains -Path $sceneRendererPath -Needle 'const auto scene_signal_srv = ShaderVisibleIndex { RegisterSceneTextureView(') -and
  (Test-SourceContains -Path $sceneRendererPath -Needle 'const auto scene_depth_srv = ShaderVisibleIndex { RegisterSceneTextureView(')
)

foreach ($required in @('analysis_result', 'overall_verdict')) {
  if (-not $captureReportMap.ContainsKey($required)) {
    throw "Missing capture report key: $required"
  }
  if (-not $productsReportMap.ContainsKey($required)) {
    throw "Missing products report key: $required"
  }
}

if ($captureReportMap['analysis_result'] -ne 'success') {
  throw "Async capture report did not report success: $captureReportFullPath"
}
if ($productsReportMap['analysis_result'] -ne 'success') {
  throw "Async products report did not report success: $productsReportFullPath"
}
if ($captureReportMap['overall_verdict'] -ne 'pass') {
  throw "Async capture report verdict was not pass: $captureReportFullPath"
}
if ($productsReportMap['overall_verdict'] -ne 'pass') {
  throw "Async products report verdict was not pass: $productsReportFullPath"
}

$requiredCaptureChecks = @(
  'stage3_scope_present',
  'stage8_scope_present',
  'stage12_scope_present',
  'stage12_directional_scope_present',
  'stage12_spot_scope_present',
  'stage15_sky_scope_present',
  'stage15_atmosphere_scope_present',
  'stage15_fog_scope_present',
  'stage22_tonemap_scope_present',
  'imgui_overlay_scope_present',
  'imgui_overlay_blend_scope_present',
  'imgui_overlay_after_tonemap',
  'compositing_scope_present',
  'async_runtime_stage_order_valid'
)
foreach ($key in $requiredCaptureChecks) {
  if ((Get-ReportValue -Report $captureReportMap -Key $key) -ne 'true') {
    throw "Async capture report check failed or missing: $key"
  }
}

$requiredProductChecks = @(
  'stage3_scene_depth_nonzero',
  'stage8_shadow_depth_nonzero',
  'stage12_directional_scene_color_nonzero',
  'stage12_spot_scene_color_nonzero',
  'atmosphere_sky_view_lut_scope_count_match',
  'atmosphere_sky_view_lut_dispatch_count_match',
  'atmosphere_camera_aerial_scope_count_match',
  'atmosphere_camera_aerial_dispatch_count_match',
  'atmosphere_camera_aerial_consumed',
  'stage15_sky_scene_color_changed',
  'stage15_atmosphere_scene_color_changed',
  'stage15_fog_scene_color_changed',
  'stage15_async_scene_color_changed',
  'stage15_far_background_mask_valid',
  'stage15_sky_quality_ok',
  'stage22_tonemap_output_nonzero',
  'stage22_exposure_clipping_ratio_ok',
  'final_present_vs_tonemap_changed',
  'imgui_overlay_composited_on_scene',
  'final_present_nonzero',
  'exported_color_exists',
  'exported_depth_exists'
)
foreach ($key in $requiredProductChecks) {
  if ((Get-ReportValue -Report $productsReportMap -Key $key) -ne 'true') {
    throw "Async products report check failed or missing: $key"
  }
}
if (-not $stage22InputBundleMatchesContract) {
  throw 'Async Stage 22 input-bundle contract invariant failed'
}

$colorMetrics = Invoke-ImageMetric `
  -Kind color `
  -BaselinePath $baselineFrameFullPath `
  -ComparisonPath $comparisonFrameFullPath
$depthMetrics = Invoke-ImageMetric `
  -Kind depth `
  -BaselinePath $baselineDepthFullPath `
  -ComparisonPath $comparisonDepthFullPath

$psnrDb = [double]$colorMetrics.psnr_db
$depthMaxError = [double]$depthMetrics.max_error

if ($psnrDb -lt $MinPsnrDb) {
  throw "Async frame PSNR below threshold: $psnrDb < $MinPsnrDb"
}
if ($depthMaxError -gt $MaxDepthError) {
  throw "Async depth max error above threshold: $depthMaxError > $MaxDepthError"
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=async_runtime_validation'
  "capture_report_path=$captureReportFullPath"
  "products_report_path=$productsReportFullPath"
  "baseline_frame_path=$baselineFrameFullPath"
  "comparison_frame_path=$comparisonFrameFullPath"
  "baseline_depth_path=$baselineDepthFullPath"
  "comparison_depth_path=$comparisonDepthFullPath"
  "min_psnr_db=$MinPsnrDb"
  "max_depth_error_allowed=$MaxDepthError"
  "visual_psnr_db={0:F6}" -f $psnrDb
  "depth_max_error={0:F6}" -f $depthMaxError
  "visual_psnr_pass=true"
  "depth_max_error_pass=true"
  "stage22_input_bundle_matches_contract=$($stage22InputBundleMatchesContract.ToString().ToLowerInvariant())"
  'overall_verdict=pass'
)

foreach ($key in ($requiredCaptureChecks | Sort-Object)) {
  $reportLines += "$key=$($captureReportMap[$key])"
}
foreach ($key in ($requiredProductChecks | Sort-Object)) {
  $reportLines += "$key=$($productsReportMap[$key])"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
