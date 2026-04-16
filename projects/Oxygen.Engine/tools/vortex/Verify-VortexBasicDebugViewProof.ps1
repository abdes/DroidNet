<#
.SYNOPSIS
Runs the VortexBasic deferred debug-view validation flow against existing artifacts.
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
  [string]$ValidationReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = (Resolve-Path $CapturePath).Path
$runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path

if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $CaptureReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath,
    "$([System.IO.Path]::GetExtension($captureFullPath))_vortexbasic_debug_report.txt")
}
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$CaptureReportPath.validation.txt"
}

$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)
$validationReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ValidationReportPath)

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexBasicDebugCapture.py') `
  -PassName 'VortexBasicDebugView' `
  -ReportPath $captureReportFullPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\vortex\Assert-VortexBasicDebugViewProof.ps1') `
  -RuntimeLogPath $runtimeLogFullPath `
  -CaptureReportPath $captureReportFullPath `
  -ReportPath $validationReportFullPath
exit $LASTEXITCODE
