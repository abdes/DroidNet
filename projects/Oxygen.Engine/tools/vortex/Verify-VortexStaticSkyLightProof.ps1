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
  [ValidateSet('ibl-only', 'direct-plus-ibl')]
  [string]$ProofMode = 'ibl-only',

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

$reportSuffix = if ($ProofMode -eq 'direct-plus-ibl') {
  '_vortex_static_skylight_direct_plus_ibl_report.txt'
} else {
  '_vortex_static_skylight_report.txt'
}

if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $captureBasePath = [System.IO.Path]::Combine(
    [System.IO.Path]::GetDirectoryName($captureFullPath),
    [System.IO.Path]::GetFileNameWithoutExtension($captureFullPath))
  $CaptureReportPath = "${captureBasePath}${reportSuffix}"
}
$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)

$previousProofMode = $env:VORTEX_STATIC_SKYLIGHT_PROOF_MODE
$env:VORTEX_STATIC_SKYLIGHT_PROOF_MODE = $ProofMode
& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexStaticSkyLight.py') `
  -PassName 'VortexStaticSkyLightProof' `
  -ReportPath $captureReportFullPath `
  -AnalysisTimeoutSeconds $AnalysisTimeoutSeconds
$env:VORTEX_STATIC_SKYLIGHT_PROOF_MODE = $previousProofMode
if ($LASTEXITCODE -ne 0) {
  throw "RenderDoc UI static SkyLight analysis failed with exit code $LASTEXITCODE"
}

$required = @(
  '^analysis_result=success$',
  "^static_skylight_proof_mode=$([regex]::Escape($ProofMode))$",
  '^stage12_static_skylight_scope_count=1$',
  '^stage12_static_skylight_draw_count=1$',
  '^stage12_point_scope_count=0$',
  '^stage12_spot_scope_count=0$',
  '^stage15_sky_scope_count=0$',
  '^sky_light_enabled=1$',
  '^sky_light_cubemap_slot_in_textures=true$',
  '^sky_light_diffuse_sh_slot_valid=true$',
  '^sky_light_prefilter_map_slot=4294967295$',
  '^static_skylight_pixel_history_verdict=true$'
)

if ($ProofMode -eq 'direct-plus-ibl') {
  $required += '^stage12_directional_scope_count=1$'
  $required += '^stage12_directional_draw_count=1$'
} else {
  $required += '^stage12_directional_scope_count=0$'
  $required += '^stage12_directional_draw_count=0$'
}

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

if ($ProofMode -eq 'direct-plus-ibl') {
  $bothLine = @(Select-String -Path $captureReportFullPath -Pattern '^direct_plus_ibl_both_history_count=(\d+)$')
  if ($bothLine.Count -ne 1) {
    throw "Missing direct_plus_ibl_both_history_count in $captureReportFullPath"
  }
  $both = [int]$bothLine[0].Matches[0].Groups[1].Value
  if ($both -lt 1) {
    throw "Direct-plus-IBL proof expected at least one pixel history touched by both directional and static SkyLight draws"
  }
}

Write-Output "Static SkyLight proof report: $captureReportFullPath"
