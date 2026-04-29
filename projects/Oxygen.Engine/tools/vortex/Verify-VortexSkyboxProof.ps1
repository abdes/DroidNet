<#
.SYNOPSIS
Validates a VTX-M08 visual SkySphere RenderDoc capture.
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
  $CaptureReportPath = "${captureBasePath}_vortex_skybox_report.txt"
}
$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexSkybox.py') `
  -PassName 'VortexSkyboxProof' `
  -ReportPath $captureReportFullPath `
  -AnalysisTimeoutSeconds $AnalysisTimeoutSeconds
if ($LASTEXITCODE -ne 0) {
  throw "RenderDoc UI skybox analysis failed with exit code $LASTEXITCODE"
}

$required = @(
  '^analysis_result=success$',
  '^stage15_sky_scope_count_match=true$',
  '^stage15_sky_draw_count_match=true$',
  '^stage15_atmosphere_scope_count_match=true$',
  '^atmosphere_enabled=0$',
  '^sky_sphere_source=0$',
  '^sky_sphere_enabled=1$',
  '^sky_sphere_cubemap_slot_valid=true$',
  '^sky_sphere_cubemap_slot_in_textures=true$',
  '^sky_background_overall_verdict=true$',
  '^sky_sphere_valid_for_cubemap=true$'
)

foreach ($pattern in $required) {
  $matches = @(Select-String -Path $captureReportFullPath -Pattern $pattern)
  if ($matches.Count -ne 1) {
    throw "Missing required skybox proof line matching '$pattern' in $captureReportFullPath"
  }
}

$nonBlackLine = @(Select-String -Path $captureReportFullPath -Pattern '^sky_sample_nonblack_count=(\d+)$')
if ($nonBlackLine.Count -ne 1) {
  throw "Missing sky_sample_nonblack_count in $captureReportFullPath"
}
$nonBlack = [int]$nonBlackLine[0].Matches[0].Groups[1].Value
if ($nonBlack -lt 1) {
  throw "Skybox proof expected at least one non-black top-of-frame sample"
}

$tonemapSkyLine = @(Select-String -Path $captureReportFullPath -Pattern '^stage22_tonemap_sky_nonblack_after_count=(\d+)$')
if ($tonemapSkyLine.Count -ne 1) {
  throw "Missing stage22_tonemap_sky_nonblack_after_count in $captureReportFullPath"
}
$tonemapSkyNonBlack = [int]$tonemapSkyLine[0].Matches[0].Groups[1].Value
if ($tonemapSkyNonBlack -lt 1) {
  throw "Skybox proof expected at least one sampled sky pixel to survive Stage 22 tonemap"
}

Write-Output "Skybox proof report: $captureReportFullPath"
