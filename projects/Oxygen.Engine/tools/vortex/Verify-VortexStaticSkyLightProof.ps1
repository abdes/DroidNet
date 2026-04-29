<#
.SYNOPSIS
Validates a VTX-M08 deferred static SkyLight diffuse RenderDoc capture.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$CaptureReportPath = '',

  [Parameter()]
  [ValidateRange(1, 3600)]
  [int]$AnalysisTimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CapturePath)
if (-not (Test-Path -LiteralPath $captureFullPath)) {
  $captureFullPath = Join-Path $repoRoot $CapturePath
}
if (-not (Test-Path -LiteralPath $captureFullPath)) {
  throw "Capture not found: $CapturePath"
}

if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $captureBasePath = [System.IO.Path]::Combine(
    [System.IO.Path]::GetDirectoryName($captureFullPath),
    [System.IO.Path]::GetFileNameWithoutExtension($captureFullPath))
  $CaptureReportPath = "${captureBasePath}_vortex_static_skylight_report.txt"
}
$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexStaticSkyLight.py') `
  -PassName 'VortexStaticSkyLightProof' `
  -ReportPath $captureReportFullPath `
  -AnalysisTimeoutSeconds $AnalysisTimeoutSeconds
if ($LASTEXITCODE -ne 0) {
  throw "RenderDoc UI static SkyLight analysis failed with exit code $LASTEXITCODE"
}

$required = @(
  '^analysis_result=success$',
  '^stage12_static_skylight_scope_count=1$',
  '^stage12_static_skylight_draw_count=1$',
  '^stage12_directional_scope_count=0$',
  '^stage12_point_scope_count=0$',
  '^stage12_spot_scope_count=0$',
  '^stage15_sky_scope_count=0$',
  '^sky_light_enabled=1$',
  '^sky_light_cubemap_slot_in_textures=true$',
  '^sky_light_diffuse_sh_slot_valid=true$',
  '^sky_light_prefilter_map_slot=4294967295$',
  '^static_skylight_pixel_history_verdict=true$'
)

foreach ($pattern in $required) {
  $matches = @(Select-String -Path $captureReportFullPath -Pattern $pattern)
  if ($matches.Count -ne 1) {
    throw "Missing required static SkyLight proof line matching '$pattern' in $captureReportFullPath"
  }
}

$nonBlackLine = @(Select-String -Path $captureReportFullPath -Pattern '^static_skylight_nonblack_after_count=(\d+)$')
if ($nonBlackLine.Count -ne 1) {
  throw "Missing static_skylight_nonblack_after_count in $captureReportFullPath"
}
$nonBlack = [int]$nonBlackLine[0].Matches[0].Groups[1].Value
if ($nonBlack -lt 1) {
  throw "Static SkyLight proof expected at least one non-black sampled pixel"
}

Write-Output "Static SkyLight proof report: $captureReportFullPath"
