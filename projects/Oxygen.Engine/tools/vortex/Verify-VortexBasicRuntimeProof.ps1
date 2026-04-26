<#
.SYNOPSIS
Runs the VortexBasic runtime capture validation flow against existing artifacts.

.DESCRIPTION
This is a VortexBasic-specific wrapper. It delegates RenderDoc UI replay to the
shared wrapper under tools/shadows, then applies VortexBasic-specific runtime
assertions from Assert-VortexBasicRuntimeProof.ps1.

The full proof now also requires a debugger-backed D3D12 audit report from the
no-capture validation run.

It does not launch the app; it only validates an existing capture and log.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [string]$DebugLayerReportPath,

  [Parameter()]
  [string]$CaptureReportPath = '',

  [Parameter()]
  [string]$ProductsReportPath = '',

  [Parameter()]
  [string]$ValidationReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'VortexProofCommon.ps1')

$repoRoot = Get-VortexProofRepoRoot
$captureFullPath = (Resolve-Path $CapturePath).Path
$runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path
$debugLayerReportFullPath = (Resolve-Path $DebugLayerReportPath).Path

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

Invoke-VortexRenderDocAnalysis `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexBasicCapture.py') `
  -PassName 'VortexBasicRuntime' `
  -ReportPath $captureReportFullPath

Invoke-VortexRenderDocAnalysis `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexBasicProducts.py') `
  -PassName 'VortexBasicProducts' `
  -ReportPath $productsReportFullPath

Invoke-VortexPowerShellProofScript `
  -ScriptPath (Join-Path $repoRoot 'tools\vortex\Assert-VortexBasicRuntimeProof.ps1') `
  -ArgumentList @(
    '-DebugLayerReportPath', $debugLayerReportFullPath,
    '-RuntimeLogPath', $runtimeLogFullPath,
    '-CaptureReportPath', $captureReportFullPath,
    '-ProductsReportPath', $productsReportFullPath,
    '-ReportPath', $validationReportFullPath
  ) `
  -Label 'VortexBasic runtime proof assertion'
