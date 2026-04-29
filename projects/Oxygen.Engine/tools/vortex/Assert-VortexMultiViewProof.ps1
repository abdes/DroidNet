<#
.SYNOPSIS
Asserts VTX-M06A multi-view proof artifacts.
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
Assert-ReportValue -Report $captureReport -Key 'stage3_view_count_match' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'stage9_view_count_match' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'stage12_view_count_match' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'composition_view_count_match' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'distinct_composition_outputs' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'aux_consume_scope_match' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'aux_consume_work_match' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'stage9_before_aux_consume' -Expected 'true' -Label 'renderdoc'
Assert-ReportValue -Report $captureReport -Key 'aux_consume_before_composition' -Expected 'true' -Label 'renderdoc'

Assert-ReportValue -Report $allocationReport -Key 'overall_verdict' -Expected 'pass' -Label 'allocation-churn'
Assert-ReportValue -Report $allocationReport -Key 'run_frames_at_least_60' -Expected 'true' -Label 'allocation-churn'
Assert-ReportValue -Report $allocationReport -Key 'steady_state_window' -Expected 'true' -Label 'allocation-churn'
Assert-ReportValue -Report $allocationReport -Key 'steady_state_allocations_zero' -Expected 'true' -Label 'allocation-churn'

$proofLayoutMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Parsed proof layout option = true'
)
if ($proofLayoutMatches.Count -eq 0) {
  throw "Runtime logs do not prove that MultiView ran with --proof-layout true: $($runtimeLogFullPaths -join ', ')"
}

$auxDependencyMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Vortex\.AuxView\.Dependency .* valid=true'
)
if ($auxDependencyMatches.Count -eq 0) {
  throw "Runtime logs do not prove that a required auxiliary dependency resolved: $($runtimeLogFullPaths -join ', ')"
}

$auxExtractMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Vortex\.AuxView\.Extract '
)
if ($auxExtractMatches.Count -eq 0) {
  throw "Runtime logs do not prove that an auxiliary product was extracted: $($runtimeLogFullPaths -join ', ')"
}

$auxConsumeMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Vortex\.AuxView\.Consume '
)
if ($auxConsumeMatches.Count -eq 0) {
  throw "Runtime logs do not prove that an auxiliary product was consumed: $($runtimeLogFullPaths -join ', ')"
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortex_multiview_assert'
  "debug_layer_report=$debugLayerReportFullPath"
  "runtime_logs=$($runtimeLogFullPaths -join ';')"
  "capture_report=$captureReportFullPath"
  "allocation_report=$allocationReportFullPath"
  'overall_verdict=pass'
)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8
$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
