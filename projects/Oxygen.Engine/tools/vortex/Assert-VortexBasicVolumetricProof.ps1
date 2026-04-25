<#
.SYNOPSIS
Asserts focused VortexBasic volumetric-fog capture proof.

.DESCRIPTION
This is a narrow VTX-M04D.4 proof gate. It validates that a VortexBasic
runtime log reports a valid integrated-scattering product and that the
RenderDoc report proves the Stage-14 volumetric pass wrote an
IntegratedLightScattering resource before Stage-15 fog draws.
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

$runtimeLogLines = Get-Content -LiteralPath $runtimeLogFullPath
$runtimeIntegratedValid = @(
  $runtimeLogLines | Select-String -Pattern 'integrated_light_scattering_valid=true'
).Count -gt 0
$runtimeVolumetricExecuted = @(
  $runtimeLogLines | Select-String -Pattern 'volumetric_fog_executed=true'
).Count -gt 0
if (-not $runtimeIntegratedValid) {
  throw "Runtime log does not prove integrated_light_scattering_valid=true: $runtimeLogFullPath"
}
if (-not $runtimeVolumetricExecuted) {
  throw "Runtime log does not prove volumetric_fog_executed=true: $runtimeLogFullPath"
}

$captureReportLines = Get-Content -LiteralPath $captureReportFullPath
$captureReportMap = @{}
foreach ($line in $captureReportLines) {
  if ($line -notmatch '=') {
    continue
  }
  $split = $line.Split('=', 2)
  if ($split.Count -eq 2) {
    $captureReportMap[$split[0]] = $split[1]
  }
}

$expectedChecks = @{
  'analysis_result' = 'success'
  'stage14_volumetric_fog_scope_present' = 'true'
  'stage14_volumetric_fog_dispatch_valid' = 'true'
  'stage15_fog_scope_present' = 'true'
  'stage15_fog_draw_valid' = 'true'
  'volumetric_fog_before_stage15_fog' = 'true'
  'integrated_light_scattering_written' = 'true'
  'stage15_fog_environment_static_data_bound' = 'true'
  'stage15_fog_integrated_light_scattering_srv_valid' = 'true'
  'stage15_fog_volumetric_enabled_flag' = 'true'
  'stage15_fog_integrated_scattering_valid_flag' = 'true'
  'stage15_fog_volumetric_grid_valid' = 'true'
  'overall_verdict' = 'pass'
}

foreach ($key in $expectedChecks.Keys) {
  $actualValue = if ($captureReportMap.ContainsKey($key)) { $captureReportMap[$key] } else { '' }
  if ($actualValue -ne $expectedChecks[$key]) {
    throw "Volumetric capture report check failed or missing: $key (expected $($expectedChecks[$key]), got '$actualValue')"
  }
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortexbasic_volumetric_validation'
  "runtime_log_path=$runtimeLogFullPath"
  "capture_report_path=$captureReportFullPath"
  'runtime_integrated_light_scattering_valid=true'
  'runtime_volumetric_fog_executed=true'
)
foreach ($key in ($expectedChecks.Keys | Sort-Object)) {
  $reportLines += "$key=$($captureReportMap[$key])"
}
foreach ($key in @(
    'stage14_volumetric_fog_scope_count',
    'stage14_volumetric_fog_dispatch_count',
    'stage15_fog_scope_count',
    'stage15_fog_draw_count',
    'integrated_light_scattering_written_resource',
    'integrated_light_scattering_consumed_resource',
    'integrated_light_scattering_consumed_by_fog',
    'stage15_fog_environment_static_resource',
    'stage15_fog_environment_static_byte_size',
    'stage15_fog_integrated_light_scattering_srv',
    'stage15_fog_volumetric_flags',
    'stage15_fog_volumetric_grid'
  )) {
  if ($captureReportMap.ContainsKey($key)) {
    $reportLines += "$key=$($captureReportMap[$key])"
  }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
