<#
.SYNOPSIS
Replays a conventional-shadow baseline capture and emits the baseline reports.

.DESCRIPTION
Runs the baseline timing and pass-focus RenderDoc analyses for an existing
conventional-shadow capture and writes the reports next to the chosen output
stem.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$OutputStem = ''
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
$invokeScript = Join-Path $PSScriptRoot '..\shadows\Invoke-RenderDocUiAnalysis.ps1'

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot '..\shadows\AnalyzeRenderDocPassTiming.py') `
  -PassName 'Vortex.Stage8.ShadowDepths' `
  -ReportPath "$outputStemFullPath.shadow_timing.txt" `
  -SuccessPattern '^total_gpu_duration_ms=(.+)$' `
  -Label 'Conventional shadow timing report'

Invoke-StableTimingAnalysis `
  -InvokeScript $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot '..\shadows\AnalyzeRenderDocPassTiming.py') `
  -PassName 'Vortex.Stage9.BasePass.MainPass' `
  -ReportPath "$outputStemFullPath.shader_timing.txt" `
  -SuccessPattern '^total_gpu_duration_ms=(.+)$' `
  -Label 'Conventional shader timing report'

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot '..\shadows\AnalyzeRenderDocPassFocus.py') `
  -PassName 'Vortex.Stage8.ShadowDepths' `
  -ReportPath "$outputStemFullPath.shadow_parent_focus.txt"

Write-Output "Baseline analysis complete for $captureFullPath"
