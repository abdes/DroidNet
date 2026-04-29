<#
.SYNOPSIS
Runs focused Vortex aerial-perspective validation against a RenderDoc capture.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$RuntimeLogPath = '',

  [Parameter()]
  [string]$CaptureReportPath = '',

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

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CapturePath)
if (-not [System.IO.Path]::IsPathRooted($captureFullPath)) {
  $captureFullPath = Join-Path $repoRoot $CapturePath
}
$captureBasePath = [System.IO.Path]::Combine(
  [System.IO.Path]::GetDirectoryName($captureFullPath),
  [System.IO.Path]::GetFileNameWithoutExtension($captureFullPath))
if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $CaptureReportPath = "${captureBasePath}_vortex_aerial_perspective_report.txt"
}
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$CaptureReportPath.validation.txt"
}

$analysisScript = Join-Path $PSScriptRoot 'AnalyzeRenderDocVortexAerialPerspective.py'
$invokeScript = Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1'
$assertScript = Join-Path $PSScriptRoot 'Assert-VortexAerialPerspectiveProof.ps1'

powershell -NoProfile -File $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath $analysisScript `
  -PassName VortexAerialPerspectiveProof `
  -ReportPath $CaptureReportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$assertArgs = @(
  '-NoProfile',
  '-File', $assertScript,
  '-CaptureReportPath', $CaptureReportPath,
  '-ValidationReportPath', $ValidationReportPath,
  '-ExpectedStrength', $ExpectedStrength,
  '-ExpectedStartDepthMeters', $ExpectedStartDepthMeters
)
if (-not [string]::IsNullOrWhiteSpace($RuntimeLogPath)) {
  $assertArgs += @('-RuntimeLogPath', $RuntimeLogPath)
}
if ($ExpectDisabled) {
  $assertArgs += '-ExpectDisabled'
}

powershell @assertArgs
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$global:LASTEXITCODE = 0
Write-Output "Capture report: $CaptureReportPath"
Write-Output "Validation report: $ValidationReportPath"
