<#
.SYNOPSIS
Compares paired VortexBasic volumetric-fog reports for term-isolated proof.

.DESCRIPTION
This VTX-M04D.4 helper consumes two focused volumetric RenderDoc reports plus
their runtime logs. It verifies that the requested volumetric term is enabled in
the first run, disabled in the second run, and produces a measurable delta in
the integrated-light-scattering probe statistics.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('combined', 'local-fog', 'sky-light', 'directional-shadow')]
  [string]$Term,

  [Parameter(Mandatory = $true)]
  [string]$EnabledRuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [string]$EnabledCaptureReportPath,

  [Parameter(Mandatory = $true)]
  [string]$DisabledRuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [string]$DisabledCaptureReportPath,

  [Parameter()]
  [double]$MinRgbSumDelta = 0.001,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-KeyValueReport {
  param([Parameter(Mandatory = $true)][string]$Path)

  $resolved = (Resolve-Path $Path).Path
  $map = @{}
  foreach ($line in Get-Content -LiteralPath $resolved) {
    if ($line -notmatch '=') {
      continue
    }
    $split = $line.Split('=', 2)
    if ($split.Count -eq 2) {
      $map[$split[0]] = $split[1]
    }
  }
  return $map
}

function Assert-MapValue {
  param(
    [Parameter(Mandatory = $true)]$Map,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string]$Expected,
    [Parameter(Mandatory = $true)][string]$Label
  )

  $actual = if ($Map.ContainsKey($Key)) { $Map[$Key] } else { '' }
  if ($actual -ne $Expected) {
    throw "$Label report check failed: $Key expected '$Expected', got '$actual'"
  }
}

function Assert-LogPattern {
  param(
    [Parameter(Mandatory = $true)][string[]]$Lines,
    [Parameter(Mandatory = $true)][string]$Pattern,
    [Parameter(Mandatory = $true)][string]$Label
  )

  if (@($Lines | Select-String -Pattern $Pattern).Count -eq 0) {
    throw "$Label runtime log missing pattern: $Pattern"
  }
}

$enabledRuntimeLogFullPath = (Resolve-Path $EnabledRuntimeLogPath).Path
$disabledRuntimeLogFullPath = (Resolve-Path $DisabledRuntimeLogPath).Path
$enabledCaptureReportFullPath = (Resolve-Path $EnabledCaptureReportPath).Path
$disabledCaptureReportFullPath = (Resolve-Path $DisabledCaptureReportPath).Path
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = "$enabledCaptureReportFullPath.$Term.compare.txt"
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$enabledMap = Read-KeyValueReport -Path $enabledCaptureReportFullPath
$disabledMap = Read-KeyValueReport -Path $disabledCaptureReportFullPath

foreach ($label in @('enabled', 'disabled')) {
  $map = if ($label -eq 'enabled') { $enabledMap } else { $disabledMap }
  Assert-MapValue -Map $map -Key 'analysis_result' -Expected 'success' -Label $label
  Assert-MapValue -Map $map -Key 'overall_verdict' -Expected 'pass' -Label $label
  Assert-MapValue -Map $map -Key 'integrated_light_scattering_volume_sampled' -Expected 'true' -Label $label
  Assert-MapValue -Map $map -Key 'integrated_light_scattering_volume_rgb_nonzero' -Expected 'true' -Label $label
}

$enabledProbeSum = [double]$enabledMap['integrated_light_scattering_probe_rgb_sum']
$disabledProbeSum = [double]$disabledMap['integrated_light_scattering_probe_rgb_sum']
$rgbSumIncrease = $enabledProbeSum - $disabledProbeSum
$rgbSumDecrease = $disabledProbeSum - $enabledProbeSum
$requiredDelta = if ($Term -eq 'directional-shadow') { $rgbSumDecrease } else { $rgbSumIncrease }
$deltaLabel = if ($Term -eq 'directional-shadow') { 'decrease' } else { 'increase' }
if ($requiredDelta -lt $MinRgbSumDelta) {
  throw "Integrated-scattering probe RGB sum $deltaLabel too small for $Term proof: $requiredDelta < $MinRgbSumDelta"
}

$enabledLogLines = Get-Content -LiteralPath $enabledRuntimeLogFullPath
$disabledLogLines = Get-Content -LiteralPath $disabledRuntimeLogFullPath

if ($Term -eq 'combined' -or $Term -eq 'local-fog') {
  Assert-LogPattern -Lines $enabledLogLines -Pattern 'volumetric_fog_local_fog_injection_executed=true' -Label 'enabled'
  Assert-LogPattern -Lines $disabledLogLines -Pattern 'volumetric_fog_local_fog_injection_executed=false' -Label 'disabled'
}
if ($Term -eq 'combined' -or $Term -eq 'sky-light') {
  Assert-LogPattern -Lines $enabledLogLines -Pattern 'volumetric_fog_sky_light_injection_executed=true' -Label 'enabled'
  Assert-LogPattern -Lines $disabledLogLines -Pattern 'volumetric_fog_sky_light_injection_executed=false' -Label 'disabled'
}
if ($Term -eq 'directional-shadow') {
  Assert-LogPattern -Lines $enabledLogLines -Pattern 'volumetric_fog_directional_shadowed_light_requested=true' -Label 'enabled'
  Assert-LogPattern -Lines $disabledLogLines -Pattern 'volumetric_fog_directional_shadowed_light_requested=false' -Label 'disabled'
}

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortexbasic_volumetric_term_comparison'
  "term=$Term"
  "enabled_runtime_log_path=$enabledRuntimeLogFullPath"
  "enabled_capture_report_path=$enabledCaptureReportFullPath"
  "disabled_runtime_log_path=$disabledRuntimeLogFullPath"
  "disabled_capture_report_path=$disabledCaptureReportFullPath"
  "enabled_probe_rgb_sum=$enabledProbeSum"
  "disabled_probe_rgb_sum=$disabledProbeSum"
  "probe_rgb_sum_delta=$rgbSumIncrease"
  "probe_rgb_sum_increase=$rgbSumIncrease"
  "probe_rgb_sum_decrease=$rgbSumDecrease"
  "min_probe_rgb_sum_delta=$MinRgbSumDelta"
)

foreach ($key in @(
    'integrated_light_scattering_volume_dims',
    'integrated_light_scattering_probe_count',
    'integrated_light_scattering_probe_avg',
    'integrated_light_scattering_probe_max',
    'integrated_light_scattering_volume_min',
    'integrated_light_scattering_volume_max'
  )) {
  if ($enabledMap.ContainsKey($key)) {
    $reportLines += "enabled_$key=$($enabledMap[$key])"
  }
  if ($disabledMap.ContainsKey($key)) {
    $reportLines += "disabled_$key=$($disabledMap[$key])"
  }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
