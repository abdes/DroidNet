<#
.SYNOPSIS
Replays a CSM-2 capture and emits receiver-analysis comparison reports.

.DESCRIPTION
Runs the RenderDoc analyses required for the CSM-2 validation package and, when
configured, compares the output against the chosen baseline stem.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$OutputStem = '',

  [Parameter()]
  [string]$BaselineStem = ''
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
$compareReportPath = "$outputStemFullPath.baseline_compare.txt"
$invokeScript = Join-Path $PSScriptRoot '..\shadows\Invoke-RenderDocUiAnalysis.ps1'

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot '..\shadows\AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowRasterPass' `
  -ReportPath "$outputStemFullPath.shadow_timing.txt" `
  -Label 'CSM-2 shadow timing report'

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot '..\shadows\AnalyzeRenderDocPassTiming.py') `
  -PassName 'ShaderPass' `
  -ReportPath "$outputStemFullPath.shader_timing.txt" `
  -Label 'CSM-2 shader timing report'

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot '..\shadows\AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowReceiverAnalysisPass' `
  -ReportPath "$outputStemFullPath.receiver_analysis_timing.txt" `
  -Label 'CSM-2 receiver-analysis timing report'

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocConventionalReceiverAnalysis.py') `
  -PassName 'ConventionalShadowReceiverAnalysisPass' `
  -ReportPath "$outputStemFullPath.receiver_analysis_report.txt"

if (-not [string]::IsNullOrWhiteSpace($BaselineStem)) {
  Invoke-PythonCommand `
    -ScriptPath (Join-Path $PSScriptRoot 'CompareConventionalShadowBaseline.py') `
    -Arguments @(
      '--baseline-stem', (Resolve-RepoPath -RepoRoot $repoRoot -Path $BaselineStem),
      '--current-stem', $outputStemFullPath,
      '--output', $compareReportPath
    )
} elseif (Test-Path -LiteralPath $compareReportPath) {
  Remove-Item -LiteralPath $compareReportPath -Force
}

Write-Output "CSM-2 analysis complete for $captureFullPath"
