<#
.SYNOPSIS
Asserts focused Vortex aerial-perspective capture proof.
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
  [double]$ExpectedStrength = -1.0,

  [Parameter()]
  [double]$ExpectedStartDepthMeters = -1.0
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

function Read-Float {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Map,
    [Parameter(Mandatory = $true)][string]$Key
  )
  if (-not $Map.ContainsKey($Key)) {
    throw "Missing required report key '$Key'."
  }
  return [double]::Parse($Map[$Key], [Globalization.CultureInfo]::InvariantCulture)
}

function Assert-PositiveFloat {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Map,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][double]$Threshold
  )
  $value = Read-Float -Map $Map -Key $Key
  if ($value -le $Threshold) {
    throw "Report key '$Key' expected > $Threshold but was $value."
  }
}

function Assert-MaxFloat {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Map,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][double]$Threshold
  )
  $value = Read-Float -Map $Map -Key $Key
  if ($value -gt $Threshold) {
    throw "Report key '$Key' expected <= $Threshold but was $value."
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
Assert-Key -Map $reportMap -Key 'analysis_profile' -Expected 'vortex_aerial_perspective'
Assert-Key -Map $reportMap -Key 'camera_aerial_scope_present' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_dispatch_valid' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_before_stage15_atmosphere' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_written' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_volume_sampled' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_volume_rgb_nonzero' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_volume_alpha_valid' -Expected 'true'
Assert-Key -Map $reportMap -Key 'camera_aerial_volume_transmittance_varies' -Expected 'true'
Assert-Key -Map $reportMap -Key 'stage15_atmosphere_scope_present' -Expected 'true'
Assert-Key -Map $reportMap -Key 'stage15_atmosphere_draw_valid' -Expected 'true'
Assert-Key -Map $reportMap -Key 'stage15_environment_view_data_bound' -Expected 'true'
Assert-Key -Map $reportMap -Key 'stage15_environment_static_data_bound' -Expected 'true'
Assert-Key -Map $reportMap -Key 'static_atmosphere_enabled' -Expected 'true'
Assert-Key -Map $reportMap -Key 'static_atmosphere_camera_volume_srv_valid' -Expected 'true'
Assert-Key -Map $reportMap -Key 'aerial_perspective_main_pass_enabled' -Expected 'true'
Assert-PositiveFloat -Map $reportMap -Key 'camera_aerial_probe_rgb_sum' -Threshold 0.00001

if ($ExpectedStrength -ge 0.0) {
  $actualStrength = Read-Float -Map $reportMap -Key 'aerial_scattering_strength'
  if ([Math]::Abs($actualStrength - $ExpectedStrength) -gt 0.0001) {
    throw "aerial_scattering_strength expected $ExpectedStrength but was $actualStrength."
  }
}
if ($ExpectedStartDepthMeters -ge 0.0) {
  $actualStartDepthKm = Read-Float -Map $reportMap -Key 'aerial_perspective_start_depth_km'
  $expectedStartDepthKm = $ExpectedStartDepthMeters * 0.001
  if ([Math]::Abs($actualStartDepthKm - $expectedStartDepthKm) -gt 0.0001) {
    throw "aerial_perspective_start_depth_km expected $expectedStartDepthKm but was $actualStartDepthKm."
  }
}

if ($ExpectDisabled) {
  Assert-Key -Map $reportMap -Key 'aerial_perspective_scene_color_changed' -Expected 'false'
  Assert-MaxFloat -Map $reportMap -Key 'stage15_atmosphere_scene_color_delta_max' -Threshold 0.00001
} else {
  Assert-Key -Map $reportMap -Key 'camera_aerial_consumed_by_atmosphere' -Expected 'true'
  Assert-Key -Map $reportMap -Key 'aerial_perspective_scene_color_changed' -Expected 'true'
  Assert-PositiveFloat -Map $reportMap -Key 'stage15_atmosphere_scene_color_delta_max' -Threshold 0.00001
}

$runtimeCliObserved = 'skipped'
if (-not [string]::IsNullOrWhiteSpace($RuntimeLogPath)) {
  $runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path
  $runtimeLines = Get-Content -LiteralPath $runtimeLogFullPath
  $runtimeExitMatches = @($runtimeLines | Select-String -Pattern '\bexit code:\s*0\b')
  if ($runtimeExitMatches.Count -eq 0) {
    throw "Runtime log does not prove exit code 0: $runtimeLogFullPath"
  }
  if ($ExpectedStrength -ge 0.0) {
    $escapedStrength = [regex]::Escape(
      $ExpectedStrength.ToString([Globalization.CultureInfo]::InvariantCulture))
    $cliMatches = @(
      $runtimeLines |
        Select-String -Pattern "Parsed aerial-scattering-strength option = $escapedStrength"
    )
    if ($cliMatches.Count -eq 0) {
      throw "Runtime log does not prove aerial-scattering-strength $ExpectedStrength`: $runtimeLogFullPath"
    }
    $runtimeCliObserved = $ExpectedStrength.ToString(
      [Globalization.CultureInfo]::InvariantCulture)
  }
  if ($ExpectedStartDepthMeters -ge 0.0) {
    $escapedStartDepth = [regex]::Escape(
      $ExpectedStartDepthMeters.ToString([Globalization.CultureInfo]::InvariantCulture))
    $startMatches = @(
      $runtimeLines |
        Select-String -Pattern "Parsed aerial-start-depth option = $escapedStartDepth"
    )
    if ($startMatches.Count -eq 0) {
      throw "Runtime log does not prove aerial-start-depth $ExpectedStartDepthMeters`: $runtimeLogFullPath"
    }
  }
}

$validationLines = @(
  'analysis_result=success'
  'analysis_profile=vortex_aerial_perspective_validation'
  "capture_report_path=$captureReportFullPath"
  "expect_disabled=$($ExpectDisabled.IsPresent.ToString().ToLowerInvariant())"
  "runtime_cli_observed=$runtimeCliObserved"
  "camera_aerial_volume_dims=$($reportMap['camera_aerial_volume_dims'])"
  "camera_aerial_probe_rgb_sum=$($reportMap['camera_aerial_probe_rgb_sum'])"
  "aerial_scattering_strength=$($reportMap['aerial_scattering_strength'])"
  "aerial_perspective_start_depth_km=$($reportMap['aerial_perspective_start_depth_km'])"
  "stage15_atmosphere_scene_color_delta_max=$($reportMap['stage15_atmosphere_scene_color_delta_max'])"
  "overall_verdict=pass"
)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $validationReportFullPath) | Out-Null
Set-Content -LiteralPath $validationReportFullPath -Value $validationLines -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Report: $validationReportFullPath"
