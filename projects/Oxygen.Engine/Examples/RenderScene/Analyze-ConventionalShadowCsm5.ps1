[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$OutputStem = '',

  [Parameter()]
  [string]$BaselineStem = 'out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50'
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
$baselineStemFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $BaselineStem
$compareReportPath = "$outputStemFullPath.baseline_compare.txt"
$invokeScript = Join-Path $PSScriptRoot 'Invoke-RenderDocUiAnalysis.ps1'

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
