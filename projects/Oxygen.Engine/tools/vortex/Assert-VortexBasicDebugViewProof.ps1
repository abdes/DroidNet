<#
.SYNOPSIS
Asserts a VortexBasic deferred debug-view proof from the runtime log and RenderDoc report.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [string]$CaptureReportPath,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path
$captureReportFullPath = (Resolve-Path $CaptureReportPath).Path

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = "$captureReportFullPath.validation.txt"
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$expectedChecks = @(
  'stage9_scope_count_match'
  'debug_scope_count_match'
  'debug_draw_count_match'
  'stage12_scope_count_match'
  'compositing_draw_count_match'
  'phase_order_match'
  'debug_output_name_match'
  'debug_output_nonzero'
  'debug_output_grayscale_match'
  'final_present_nonzero'
)

$runtimeLogLines = Get-Content -LiteralPath $runtimeLogFullPath
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

$analysisResult = if ($captureReportMap.ContainsKey('analysis_result')) {
  $captureReportMap['analysis_result']
} else {
  ''
}
if ($analysisResult -ne 'success') {
  throw "Debug-view capture report did not report success: $captureReportFullPath"
}
$overallVerdict = if ($captureReportMap.ContainsKey('overall_verdict')) {
  $captureReportMap['overall_verdict']
} else {
  ''
}
if ($overallVerdict -ne 'pass') {
  throw "Debug-view capture report did not report pass: $captureReportFullPath"
}

foreach ($key in $expectedChecks) {
  $actualValue = if ($captureReportMap.ContainsKey($key)) {
    $captureReportMap[$key]
  } else {
    ''
  }
  if ($actualValue -ne 'true') {
    throw "Debug-view capture report check failed or missing: $key"
  }
}

$expectedMode = if ($captureReportMap.ContainsKey('expected_mode')) {
  $captureReportMap['expected_mode']
} else {
  ''
}
if ([string]::IsNullOrWhiteSpace($expectedMode)) {
  throw "Debug-view capture report is missing expected_mode: $captureReportFullPath"
}

$runtimeModeRegex = "Parsed Vortex shader debug mode = $([Regex]::Escape($expectedMode))"
$runtimeModeMatch = @(
  $runtimeLogLines | Select-String -Pattern $runtimeModeRegex
)
if ($runtimeModeMatch.Count -eq 0) {
  throw "Runtime log does not contain the expected debug mode '$expectedMode': $runtimeLogFullPath"
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortexbasic_debug_view_validation'
  "runtime_log_path=$runtimeLogFullPath"
  "capture_report_path=$captureReportFullPath"
  "expected_mode=$expectedMode"
  'runtime_mode_match=true'
)

foreach ($key in ($expectedChecks | Sort-Object)) {
  $reportLines += "$key=$($captureReportMap[$key])"
}
$reportLines += "overall_verdict=$($captureReportMap['overall_verdict'])"

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
