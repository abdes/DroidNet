<#
.SYNOPSIS
Runs focused Vortex height-fog validation against an existing RenderDoc capture.
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
  [switch]$SkipRuntimeCliCheck
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
  $CaptureReportPath = "${captureBasePath}_vortex_height_fog_report.txt"
}
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$CaptureReportPath.validation.txt"
}

$analysisScript = Join-Path $PSScriptRoot 'AnalyzeRenderDocVortexHeightFog.py'
$invokeScript = Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1'
$assertScript = Join-Path $PSScriptRoot 'Assert-VortexHeightFogProof.ps1'

powershell -NoProfile -File $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath $analysisScript `
  -PassName VortexHeightFogProof `
  -ReportPath $CaptureReportPath

$assertArgs = @(
  '-NoProfile',
  '-File', $assertScript,
  '-CaptureReportPath', $CaptureReportPath,
  '-ValidationReportPath', $ValidationReportPath
)
if (-not [string]::IsNullOrWhiteSpace($RuntimeLogPath)) {
  $assertArgs += @('-RuntimeLogPath', $RuntimeLogPath)
}
if ($ExpectDisabled) {
  $assertArgs += '-ExpectDisabled'
}
if ($SkipRuntimeCliCheck) {
  $assertArgs += '-SkipRuntimeCliCheck'
}

powershell @assertArgs

$global:LASTEXITCODE = 0
Write-Output "Capture report: $CaptureReportPath"
Write-Output "Validation report: $ValidationReportPath"
