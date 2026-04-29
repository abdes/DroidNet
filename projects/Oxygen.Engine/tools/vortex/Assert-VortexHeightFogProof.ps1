<#
.SYNOPSIS
Asserts focused Vortex height-fog capture proof.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CaptureReportPath,

  [Parameter()]
  [string]$RuntimeLogPath = '',

  [Parameter()]
  [string]$ValidationReportPath = '',

  [Parameter()]
  [switch]$ExpectDisabled,

  [Parameter()]
  [switch]$SkipRuntimeCliCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-ReportMap {
  param([Parameter(Mandatory = $true)][string]$Path)
  $map = @{}
  foreach ($line in Get-Content -LiteralPath $Path) {
    if ([string]::IsNullOrWhiteSpace($line) -or -not $line.Contains('=')) {
      continue
    }
    $idx = $line.IndexOf('=')
    $map[$line.Substring(0, $idx)] = $line.Substring($idx + 1)
  }
  return $map
}

function Assert-Key {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Map,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string]$Expected
  )
  if (-not $Map.ContainsKey($Key)) {
    throw "Missing required report key '$Key'."
  }
  if ($Map[$Key] -ne $Expected) {
    throw "Report key '$Key' expected '$Expected' but was '$($Map[$Key])'."
  }
}

function Assert-PositiveFloat {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Map,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][double]$Threshold
  )
  if (-not $Map.ContainsKey($Key)) {
    throw "Missing required report key '$Key'."
  }
  $value = [double]::Parse($Map[$Key], [Globalization.CultureInfo]::InvariantCulture)
  if ($value -le $Threshold) {
    throw "Report key '$Key' expected > $Threshold but was $value."
  }
}

$captureReportFullPath = (Resolve-Path $CaptureReportPath).Path
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$captureReportFullPath.validation.txt"
}
$validationReportFullPath =
  $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ValidationReportPath)

$reportMap = Read-ReportMap -Path $captureReportFullPath
Assert-Key -Map $reportMap -Key 'analysis_result' -Expected 'success'
Assert-Key -Map $reportMap -Key 'analysis_profile' -Expected 'vortex_height_fog'

if ($ExpectDisabled) {
  Assert-Key -Map $reportMap -Key 'stage15_fog_scope_present' -Expected 'false'
  Assert-Key -Map $reportMap -Key 'stage15_fog_draw_valid' -Expected 'false'
} else {
  Assert-Key -Map $reportMap -Key 'stage15_fog_scope_present' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'stage15_fog_draw_valid' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'stage15_fog_environment_static_data_bound' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_fog_enabled_flag' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_enabled_flag' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_render_in_main_pass_flag' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_directional_inscattering_flag' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_primary_density_positive' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_any_density_positive' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_max_opacity_valid' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_min_transmittance_valid' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_cubemap_unusable' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'height_fog_scene_color_changed' -Expected 'true'
  Assert-PositiveFloat -Map $reportMap -Key 'height_fog_scene_color_delta_max' -Threshold 0.00001
}

$runtimeCliObserved = 'skipped'
if (-not [string]::IsNullOrWhiteSpace($RuntimeLogPath)) {
  $runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path
  $runtimeLines = Get-Content -LiteralPath $runtimeLogFullPath
  $runtimeExitMatches = @($runtimeLines | Select-String -Pattern '\bexit code:\s*0\b')
  if ($runtimeExitMatches.Count -eq 0) {
    throw "Runtime log does not prove exit code 0: $runtimeLogFullPath"
  }
  if (-not $SkipRuntimeCliCheck) {
    $expected = if ($ExpectDisabled) { 'false' } else { 'true' }
    $cliMatches = @(
      $runtimeLines | Select-String -Pattern "Parsed with-height-fog option = $expected"
    )
    if ($cliMatches.Count -eq 0) {
      throw "Runtime log does not prove Parsed with-height-fog option = $expected`: $runtimeLogFullPath"
    }
    $runtimeCliObserved = $expected
  }
}

$validationLines = @(
  'analysis_result=success'
  'analysis_profile=vortex_height_fog_validation'
  "capture_report_path=$captureReportFullPath"
  "expect_disabled=$($ExpectDisabled.IsPresent.ToString().ToLowerInvariant())"
  "runtime_cli_observed=$runtimeCliObserved"
  "stage15_fog_scope_count=$($reportMap['stage15_fog_scope_count'])"
  "stage15_fog_draw_count=$($reportMap['stage15_fog_draw_count'])"
  "height_fog_scene_color_delta_max=$($reportMap['height_fog_scene_color_delta_max'])"
  "height_fog_far_depth_delta_max=$($reportMap['height_fog_far_depth_delta_max'])"
  "height_fog_far_depth_sample_count=$($reportMap['height_fog_far_depth_sample_count'])"
  "overall_verdict=pass"
)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $validationReportFullPath) | Out-Null
Set-Content -LiteralPath $validationReportFullPath -Value $validationLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $validationReportFullPath"
