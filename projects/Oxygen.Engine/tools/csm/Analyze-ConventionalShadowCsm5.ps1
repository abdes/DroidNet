<#
.SYNOPSIS
Replays a CSM-5 capture and emits counted-indirect raster validation reports.

.DESCRIPTION
Runs the RenderDoc timing and proof scripts needed for the CSM-5
counted-indirect raster workflow and writes the comparison report next to the
output stem.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$OutputStem = '',

  [Parameter()]
  [string]$BaselineStem = 'out/build-ninja/analysis/csm/csm2_validation/release_frame350_csm2_fps50'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath

if (-not (Test-Path -LiteralPath $captureFullPath)) {
  throw "Capture not found: $captureFullPath"
}

if ([string]::IsNullOrWhiteSpace($OutputStem)) {
  $captureItem = Get-Item -LiteralPath $captureFullPath
  $captureStem = $captureItem.BaseName -replace '_frame\d+$', ''
  $OutputStem = [System.IO.Path]::Combine($captureItem.DirectoryName, $captureStem)
}

$outputStemFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $OutputStem
$baselineStemFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $BaselineStem
$compareReportPath = "$outputStemFullPath.baseline_compare.txt"
$invokeScript = Join-Path $PSScriptRoot '..\shadows\Invoke-RenderDocUiAnalysis.ps1'

foreach ($requiredReport in @(
    "$baselineStemFullPath.shadow_timing.txt",
    "$baselineStemFullPath.shader_timing.txt",
    "$baselineStemFullPath.screen_hzb_timing.txt",
    "$baselineStemFullPath.receiver_analysis_timing.txt",
    "$baselineStemFullPath.receiver_analysis_report.txt",
    "$baselineStemFullPath.benchmark.log"
  )) {
  if (-not (Test-Path -LiteralPath $requiredReport)) {
    throw "Locked CSM-2 baseline artifact not found: $requiredReport"
  }
}

if (-not (Test-Path -LiteralPath "$outputStemFullPath.benchmark.log")) {
  throw "Current benchmark log not found: $outputStemFullPath.benchmark.log"
}

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowRasterPass' `
  -ReportPath "$outputStemFullPath.shadow_timing.txt"

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ShaderPass' `
  -ReportPath "$outputStemFullPath.shader_timing.txt"

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ScreenHzbBuildPass' `
  -ReportPath "$outputStemFullPath.screen_hzb_timing.txt"

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowReceiverAnalysisPass' `
  -ReportPath "$outputStemFullPath.receiver_analysis_timing.txt"

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowReceiverMaskPass' `
  -ReportPath "$outputStemFullPath.receiver_mask_timing.txt"

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowCasterCullingPass' `
  -ReportPath "$outputStemFullPath.caster_culling_timing.txt"

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocConventionalReceiverAnalysis.py') `
  -PassName 'ConventionalShadowReceiverAnalysisPass' `
  -ReportPath "$outputStemFullPath.receiver_analysis_report.txt"

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocConventionalReceiverMask.py') `
  -PassName 'ConventionalShadowReceiverMaskPass' `
  -ReportPath "$outputStemFullPath.receiver_mask_report.txt"

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocConventionalShadowCulling.py') `
  -PassName 'ConventionalShadowCasterCullingPass' `
  -ReportPath "$outputStemFullPath.caster_culling_report.txt"

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocConventionalShadowRasterIndirect.py') `
  -PassName 'ConventionalShadowRasterPass' `
  -ReportPath "$outputStemFullPath.raster_indirect_report.txt"

Invoke-PythonCommand `
  -ScriptPath (Join-Path $PSScriptRoot 'CompareConventionalShadowCsm5Baseline.py') `
  -Arguments @(
    '--baseline-stem', $baselineStemFullPath,
    '--current-stem', $outputStemFullPath,
    '--output', $compareReportPath
  )

Write-Output "CSM-5 analysis complete for $captureFullPath"
