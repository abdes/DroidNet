<#
.SYNOPSIS
Runs the scene ShaderPass binding analyzer for a flicker capture.

.DESCRIPTION
Mirrors the conventional-shadow analysis entrypoints under tools/csm by
invoking qrenderdoc through the shared UI-analysis wrapper and emitting the
report next to the chosen output stem.
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

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath

if (-not (Test-Path -LiteralPath $captureFullPath)) {
  throw "Capture not found: $captureFullPath"
}

if ([string]::IsNullOrWhiteSpace($OutputStem)) {
  $captureItem = Get-Item -LiteralPath $captureFullPath
  $OutputStem = [System.IO.Path]::Combine(
    $captureItem.DirectoryName,
    $captureItem.BaseName
  )
}

$outputStemFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $OutputStem
$invokeScript = Join-Path $repoRoot 'tools\csm\Invoke-RenderDocUiAnalysis.ps1'

& $invokeScript `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $PSScriptRoot 'AnalyzeRenderDocSceneShaderBindings.py') `
  -PassName 'ShaderPass' `
  -ReportPath "$outputStemFullPath.scene_shader_bindings.txt"

Write-Output "Flicker scene shader binding analysis complete for $captureFullPath"
