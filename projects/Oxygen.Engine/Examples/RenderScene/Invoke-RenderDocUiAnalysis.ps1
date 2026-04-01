[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter(Mandatory = $true)]
  [string]$UiScriptPath,

  [Parameter(Mandatory = $true)]
  [string]$PassName,

  [Parameter(Mandatory = $true)]
  [string]$ReportPath,

  [Parameter()]
  [string]$ConfigRoot = ''
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
$uiScriptFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $UiScriptPath
$reportFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ReportPath
$renderDocExe = 'C:\Program Files\RenderDoc\qrenderdoc.exe'

if (-not (Test-Path -LiteralPath $renderDocExe)) {
  throw "qrenderdoc.exe not found: $renderDocExe"
}
if (-not (Test-Path -LiteralPath $captureFullPath)) {
  throw "Capture not found: $captureFullPath"
}
if (-not (Test-Path -LiteralPath $uiScriptFullPath)) {
  throw "RenderDoc UI script not found: $uiScriptFullPath"
}

if ([string]::IsNullOrWhiteSpace($ConfigRoot)) {
  $ConfigRoot = Join-Path $repoRoot 'out\build-ninja\analysis\renderdoc-automation-config'
}

$configRootFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ConfigRoot
$appDataPath = Join-Path $configRootFullPath 'Roaming'
$localAppDataPath = Join-Path $configRootFullPath 'Local'

New-Item -ItemType Directory -Force -Path $appDataPath | Out-Null
New-Item -ItemType Directory -Force -Path $localAppDataPath | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null

$env:APPDATA = $appDataPath
$env:LOCALAPPDATA = $localAppDataPath
$env:OXYGEN_RENDERDOC_PASS_NAME = $PassName
$env:OXYGEN_RENDERDOC_REPORT_PATH = $reportFullPath

& $renderDocExe --ui-python $uiScriptFullPath $captureFullPath
$exitCode = 0
if (Test-Path variable:LASTEXITCODE) {
  $exitCode = $LASTEXITCODE
}
if ($exitCode -ne 0) {
  throw "RenderDoc UI analysis failed with exit code $exitCode"
}

if (-not (Test-Path -LiteralPath $reportFullPath)) {
  throw "RenderDoc UI analysis did not produce the expected report: $reportFullPath"
}

$reportItem = Get-Item -LiteralPath $reportFullPath
if ($reportItem.Length -le 0) {
  throw "RenderDoc UI analysis produced an empty report: $reportFullPath"
}

Write-Output "Report: $reportFullPath"
