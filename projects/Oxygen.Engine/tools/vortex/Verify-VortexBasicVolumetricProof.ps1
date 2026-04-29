<#
.SYNOPSIS
Runs focused VortexBasic volumetric-fog validation against existing artifacts.
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
  $captureBasePath = Join-Path `
    (Split-Path -Parent $captureFullPath) `
    ([System.IO.Path]::GetFileNameWithoutExtension($captureFullPath))
  $CaptureReportPath = "${captureBasePath}_vortexbasic_volumetric_report.txt"
}
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$CaptureReportPath.validation.txt"
}

$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)
$validationReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ValidationReportPath)

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexBasicVolumetric.py') `
  -PassName 'VortexBasicVolumetric' `
  -ReportPath $captureReportFullPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\vortex\Assert-VortexBasicVolumetricProof.ps1') `
  -RuntimeLogPath $runtimeLogFullPath `
  -CaptureReportPath $captureReportFullPath `
  -ReportPath $validationReportFullPath
exit $LASTEXITCODE
