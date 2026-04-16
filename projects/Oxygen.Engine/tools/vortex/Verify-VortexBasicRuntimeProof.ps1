<#
.SYNOPSIS
Runs the VortexBasic runtime capture validation flow against existing artifacts.

.DESCRIPTION
This is a VortexBasic-specific wrapper. It delegates RenderDoc UI replay to the
shared wrapper under tools/shadows, then applies VortexBasic-specific runtime
assertions from Assert-VortexBasicRuntimeProof.ps1.

It does not launch the app; it only validates an existing capture and log.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter()]
  [string]$CaptureReportPath = '',

  [Parameter()]
  [string]$ProductsReportPath = '',

  [Parameter()]
  [string]$ValidationReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = (Resolve-Path $CapturePath).Path
$runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path

if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $captureReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath,
    "$([System.IO.Path]::GetExtension($captureFullPath))_vortexbasic_capture_report.txt")
}

if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$captureReportPath.validation.txt"
}
if ([string]::IsNullOrWhiteSpace($ProductsReportPath)) {
  $ProductsReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath,
    "$([System.IO.Path]::GetExtension($captureFullPath))_products_report.txt")
}

$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($captureReportPath)
$productsReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ProductsReportPath)
$validationReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ValidationReportPath)

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexBasicCapture.py') `
  -PassName 'VortexBasicRuntime' `
  -ReportPath $captureReportFullPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexBasicProducts.py') `
  -PassName 'VortexBasicProducts' `
  -ReportPath $productsReportFullPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\vortex\Assert-VortexBasicRuntimeProof.ps1') `
  -RuntimeLogPath $runtimeLogFullPath `
  -CaptureReportPath $captureReportFullPath `
  -ProductsReportPath $productsReportFullPath `
  -ReportPath $validationReportFullPath
exit $LASTEXITCODE
