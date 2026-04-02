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

function Resolve-RepoPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }

  return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Invoke-StableTimingAnalysis {
  param(
    [Parameter(Mandatory = $true)]
    [string]$InvokeScript,

    [Parameter(Mandatory = $true)]
    [string]$CapturePath,

    [Parameter(Mandatory = $true)]
    [string]$UiScriptPath,

    [Parameter(Mandatory = $true)]
    [string]$PassName,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath
  )

  & $InvokeScript `
    -CapturePath $CapturePath `
    -UiScriptPath $UiScriptPath `
    -PassName $PassName `
    -ReportPath $ReportPath

  $durationLine = @(Select-String `
    -Path $ReportPath `
    -Pattern '^authoritative_scope_gpu_duration_ms=(.+)$')
  if ($durationLine.Count -ne 1) {
    throw "Timing report did not contain exactly one authoritative_scope_gpu_duration_ms entry: $ReportPath"
  }

  [void][double]::Parse(
    $durationLine[0].Matches[0].Groups[1].Value,
    [System.Globalization.CultureInfo]::InvariantCulture
  )
}

function Invoke-PythonCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptPath,

    [Parameter(Mandatory = $true)]
    [string[]]$Arguments
  )

  & python $ScriptPath @Arguments
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    throw "python exited with code $exitCode while running $ScriptPath"
  }

  $global:LASTEXITCODE = 0
}

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
$invokeScript = Join-Path $PSScriptRoot 'Invoke-RenderDocUiAnalysis.ps1'

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
  -PassName 'ConventionalShadowReceiverAnalysisPass' `
  -ReportPath "$outputStemFullPath.receiver_analysis_timing.txt"

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
