<#
.SYNOPSIS
Asserts VTX-M06B offscreen proof artifacts.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$DebugLayerReportPath,

  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter()]
  [string]$RuntimeErrorLogPath = '',

  [Parameter(Mandatory = $true)]
  [string]$CaptureReportPath,

  [Parameter(Mandatory = $true)]
  [string]$AllocationReportPath,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'VortexProofCommon.ps1')

function Assert-ReportValue {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Report,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string]$Expected,
    [Parameter(Mandatory = $true)][string]$Label
  )

  $actual = Get-VortexProofReportValue -Report $Report -Key $Key
  if ($actual -ne $Expected) {
    throw "$Label check failed: $Key expected '$Expected', got '$actual'"
  }
}

$debugLayerReportFullPath = (Resolve-Path $DebugLayerReportPath).Path
$runtimeLogFullPaths = @((Resolve-Path $RuntimeLogPath).Path)
if (-not [string]::IsNullOrWhiteSpace($RuntimeErrorLogPath)) {
  $runtimeLogFullPaths += (Resolve-Path $RuntimeErrorLogPath).Path
}
$captureReportFullPath = (Resolve-Path $CaptureReportPath).Path
$allocationReportFullPath = (Resolve-Path $AllocationReportPath).Path
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = "$captureReportFullPath.validation.txt"
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$debugReport = Read-VortexProofReportMap -Path $debugLayerReportFullPath
$captureReport = Read-VortexProofReportMap -Path $captureReportFullPath
$allocationReport = Read-VortexProofReportMap -Path $allocationReportFullPath
$runtimeLogLines = @(
  foreach ($path in $runtimeLogFullPaths) {
    Get-Content -LiteralPath $path
  }
)

Assert-ReportValue -Report $debugReport -Key 'overall_verdict' -Expected 'pass' -Label 'debug-layer'
Assert-ReportValue -Report $debugReport -Key 'runtime_exit_code' -Expected '0' -Label 'debug-layer'
Assert-ReportValue -Report $debugReport -Key 'd3d12_error_count' -Expected '0' -Label 'debug-layer'
Assert-ReportValue -Report $debugReport -Key 'dxgi_error_count' -Expected '0' -Label 'debug-layer'
Assert-ReportValue -Report $debugReport -Key 'blocking_warning_count' -Expected '0' -Label 'debug-layer'

Assert-ReportValue -Report $captureReport -Key 'overall_verdict' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'has_deferred_preview' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'has_forward_capture' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'has_preview_composite' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'has_capture_composite' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'preview_texture_found' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'preview_texture_rgb_nonzero' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'capture_texture_found' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'capture_texture_rgb_nonzero' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'forward_before_capture_composite' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'deferred_before_preview_composite' -Expected 'true' -Label 'renderdoc'

Assert-ReportValue -Report $allocationReport -Key 'overall_verdict' -Expected 'pass' -Label 'allocation-churn'
Assert-ReportValue -Report $allocationReport -Key 'run_frames_at_least_60' -Expected 'true' -Label 'allocation-churn'
Assert-ReportValue -Report $allocationReport -Key 'steady_state_window' -Expected 'true' -Label 'allocation-churn'
Assert-ReportValue -Report $allocationReport -Key 'steady_state_allocations_zero' -Expected 'true' -Label 'allocation-churn'

$layoutMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Parsed offscreen proof layout option = true'
)
if ($layoutMatches.Count -eq 0) {
  throw "Runtime logs do not prove that MultiView ran with --offscreen-proof-layout true: $($runtimeLogFullPaths -join ', ')"
}

$previewRenderMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Vortex\.OffscreenProof\.Render name=M06B\.OffscreenPreview\.Deferred'
)
if ($previewRenderMatches.Count -eq 0) {
  throw "Runtime logs do not prove deferred offscreen preview rendering: $($runtimeLogFullPaths -join ', ')"
}

$captureRenderMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Vortex\.OffscreenProof\.Render name=M06B\.OffscreenCapture\.Forward'
)
if ($captureRenderMatches.Count -eq 0) {
  throw "Runtime logs do not prove forward offscreen capture rendering: $($runtimeLogFullPaths -join ', ')"
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortex_offscreen_assert'
  "debug_layer_report=$debugLayerReportFullPath"
  "runtime_logs=$($runtimeLogFullPaths -join ';')"
  "capture_report=$captureReportFullPath"
  "allocation_report=$allocationReportFullPath"
  "deferred_preview_render_count=$($previewRenderMatches.Count)"
  "forward_capture_render_count=$($captureRenderMatches.Count)"
  'overall_verdict=pass'
)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8
$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
