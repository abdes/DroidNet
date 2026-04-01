[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$OutputStem = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $CapturePath))

if (-not (Test-Path -LiteralPath $captureFullPath)) {
  throw "Capture not found: $captureFullPath"
}

if ([string]::IsNullOrWhiteSpace($OutputStem)) {
  $captureItem = Get-Item -LiteralPath $captureFullPath
  $OutputStem = [System.IO.Path]::Combine($captureItem.DirectoryName, $captureItem.BaseName)
}

$outputStemFullPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputStem))
$invokeScript = Join-Path $PSScriptRoot 'Invoke-RenderDocUiAnalysis.ps1'

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocPassTiming.py') `
  -PassName 'ConventionalShadowRasterPass' `
  -ReportPath "$outputStemFullPath.shadow_timing.txt"

& $invokeScript `
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
