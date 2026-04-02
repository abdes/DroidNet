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

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassFocus.py') `
  -PassName 'ConventionalShadowRasterPass' `
  -ReportPath "$outputStemFullPath.shadow_parent_focus.txt"

Write-Output "Baseline analysis complete for $captureFullPath"
