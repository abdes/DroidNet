<#
.SYNOPSIS
Runs the Async runtime capture validation flow against an existing RenderDoc capture.

.DESCRIPTION
Runs RenderDoc structural/product analysis for an existing Async Vortex
runtime capture, exports the inspected color/depth products, writes a behavior
summary, and gates the proof through Assert-AsyncRuntimeProof.ps1.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$CaptureReportPath = '',

  [Parameter()]
  [string]$ProductsReportPath = '',

  [Parameter()]
  [string]$ValidationReportPath = '',

  [Parameter()]
  [string]$ExportFramePath = '',

  [Parameter()]
  [string]$ExportDepthPath = '',

  [Parameter()]
  [string]$BehaviorPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'VortexProofCommon.ps1')

function Set-ScopedProcessEnvironmentVariable {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value,
    [Parameter(Mandatory = $true)][hashtable]$OriginalValues
  )

  if (-not $OriginalValues.ContainsKey($Name)) {
    $OriginalValues[$Name] = [System.Environment]::GetEnvironmentVariable($Name, 'Process')
  }
  [System.Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

function Restore-ScopedProcessEnvironmentVariables {
  param([Parameter(Mandatory = $true)][hashtable]$OriginalValues)

  foreach ($entry in $OriginalValues.GetEnumerator()) {
    [System.Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
  }
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

function Write-BehaviorSummary {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][hashtable]$CaptureReport,
    [Parameter(Mandatory = $true)][hashtable]$ProductsReport,
    [Parameter(Mandatory = $true)][string]$CaptureFullPath
  )

  $asyncMainPath = Join-Path $RepoRoot 'Examples\Async\MainModule.cpp'
  $asyncSettingsPath = Join-Path $RepoRoot 'Examples\Async\AsyncDemoSettingsService.cpp'

  $lines = @(
    '# Async Vortex Runtime Behaviors'
    ''
    "Capture: $CaptureFullPath"
    ''
    '## Runtime Evidence'
    "- atmosphere_enabled_main_view: $(Get-VortexProofReportValue -Report $ProductsReport -Key 'stage15_sky_scene_color_changed' -Default 'unknown')"
    "- spotlight_presence: $(Get-VortexProofReportValue -Report $ProductsReport -Key 'stage12_spot_scene_color_nonzero' -Default 'unknown')"
    "- directional_shadow_path: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage8_draw_count_present' -Default 'unknown')"
    "- post_processed_visible_output: $(Get-VortexProofReportValue -Report $ProductsReport -Key 'stage22_tonemap_output_nonzero' -Default 'unknown')"
    "- imgui_overlay_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'imgui_overlay_scope_present' -Default 'unknown')"
    "- imgui_overlay_blend_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'imgui_overlay_blend_scope_present' -Default 'unknown')"
    "- imgui_overlay_after_tonemap: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'imgui_overlay_after_tonemap' -Default 'unknown')"
    "- imgui_overlay_composited_on_scene: $(Get-VortexProofReportValue -Report $ProductsReport -Key 'imgui_overlay_composited_on_scene' -Default 'unknown')"
    "- final_present_nonzero: $(Get-VortexProofReportValue -Report $ProductsReport -Key 'final_present_nonzero' -Default 'unknown')"
    "- final_present_vs_tonemap_changed: $(Get-VortexProofReportValue -Report $ProductsReport -Key 'final_present_vs_tonemap_changed' -Default 'unknown')"
    ''
    '## Supplemental Source Audit (Non-Blocking)'
    "- main_view_with_atmosphere: $(if (Test-SourceContains -Path $asyncMainPath -Needle 'view_ctx.metadata.with_atmosphere = true;') { 'pass' } else { 'fail' })"
    "- spotlight_setup_present: $(if (Test-SourceContains -Path $asyncMainPath -Needle 'EnsureCameraSpotLight();') { 'pass' } else { 'fail' })"
    "- spotlight_node_present: $(if (Test-SourceContains -Path $asyncMainPath -Needle 'CameraSpotLight') { 'pass' } else { 'fail' })"
    "- spotlight_shadows_default_off: $(if (Test-SourceContains -Path $asyncSettingsPath -Needle 'return settings->GetBool(kSpotlightShadowsKey).value_or(false);') { 'pass' } else { 'fail' })"
    ''
    '## Structural Notes'
    "- stage3_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage3_scope_present' -Default 'unknown')"
    "- stage8_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage8_scope_present' -Default 'unknown')"
    "- stage12_directional_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage12_directional_scope_present' -Default 'unknown')"
    "- stage12_spot_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage12_spot_scope_present' -Default 'unknown')"
    "- stage15_atmosphere_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage15_atmosphere_scope_present' -Default 'unknown')"
    "- stage22_tonemap_scope_present: $(Get-VortexProofReportValue -Report $CaptureReport -Key 'stage22_tonemap_scope_present' -Default 'unknown')"
  )

  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
  Set-Content -LiteralPath $OutputPath -Value $lines -Encoding utf8
}

$repoRoot = Get-VortexProofRepoRoot
$captureFullPath = (Resolve-Path (Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath)).Path
$captureDirectory = Split-Path -Parent $captureFullPath

if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $CaptureReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath, "$([System.IO.Path]::GetExtension($captureFullPath))_async_capture_report.txt")
}
if ([string]::IsNullOrWhiteSpace($ProductsReportPath)) {
  $ProductsReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath, "$([System.IO.Path]::GetExtension($captureFullPath))_async_products_report.txt")
}
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$captureFullPath.validation.txt"
}
if ([string]::IsNullOrWhiteSpace($ExportFramePath)) {
  $ExportFramePath = Join-Path $captureDirectory 'async_frame.png'
}
if ([string]::IsNullOrWhiteSpace($ExportDepthPath)) {
  $ExportDepthPath = Join-Path $captureDirectory 'async_depth.png'
}
if ([string]::IsNullOrWhiteSpace($BehaviorPath)) {
  $BehaviorPath = Join-Path $captureDirectory 'async_behaviors.md'
}

$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)
$productsReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ProductsReportPath)
$validationReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ValidationReportPath)
$exportFrameFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExportFramePath)
$exportDepthFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExportDepthPath)
$behaviorFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BehaviorPath)

Invoke-VortexRenderDocAnalysis `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocAsyncCapture.py') `
  -PassName 'AsyncRuntimeCapture' `
  -ReportPath $captureReportFullPath

$environmentSnapshot = @{}
try {
  Set-ScopedProcessEnvironmentVariable `
    -Name 'OXYGEN_ASYNC_EXPORT_COLOR_PATH' `
    -Value $exportFrameFullPath `
    -OriginalValues $environmentSnapshot
  Set-ScopedProcessEnvironmentVariable `
    -Name 'OXYGEN_ASYNC_EXPORT_DEPTH_PATH' `
    -Value $exportDepthFullPath `
    -OriginalValues $environmentSnapshot

  Invoke-VortexRenderDocAnalysis `
    -CapturePath $captureFullPath `
    -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocAsyncProducts.py') `
    -PassName 'AsyncRuntimeProducts' `
    -ReportPath $productsReportFullPath
} finally {
  Restore-ScopedProcessEnvironmentVariables -OriginalValues $environmentSnapshot
}

$captureReportMap = Read-VortexProofReportMap -Path $captureReportFullPath
$productsReportMap = Read-VortexProofReportMap -Path $productsReportFullPath

Write-BehaviorSummary `
  -RepoRoot $repoRoot `
  -OutputPath $behaviorFullPath `
  -CaptureReport $captureReportMap `
  -ProductsReport $productsReportMap `
  -CaptureFullPath $captureFullPath

Invoke-VortexPowerShellProofScript `
  -ScriptPath (Join-Path $repoRoot 'tools\vortex\Assert-AsyncRuntimeProof.ps1') `
  -ArgumentList @(
    '-CaptureReportPath', $captureReportFullPath,
    '-ProductsReportPath', $productsReportFullPath,
    '-ExportFramePath', $exportFrameFullPath,
    '-ExportDepthPath', $exportDepthFullPath,
    '-ReportPath', $validationReportFullPath
  ) `
  -Label 'Async runtime proof assertion'
