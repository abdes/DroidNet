<#
.SYNOPSIS
Validates an existing VortexBasic translucency RenderDoc capture and logs.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [string]$DebugLayerReportPath,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'VortexProofCommon.ps1')

$repoRoot = Get-VortexProofRepoRoot
$captureFullPath = (Resolve-Path $CapturePath).Path
$runtimeLogFullPath = (Resolve-Path $RuntimeLogPath).Path
$debugLayerReportFullPath = (Resolve-Path $DebugLayerReportPath).Path

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath,
    "$([System.IO.Path]::GetExtension($captureFullPath))_vortex_translucency_report.txt")
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

Invoke-VortexRenderDocAnalysis `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocVortexTranslucency.py') `
  -PassName 'VortexTranslucency' `
  -ReportPath $reportFullPath

$debugLayerReportMap = @{}
foreach ($line in Get-Content -LiteralPath $debugLayerReportFullPath) {
  if ($line -notmatch '=') {
    continue
  }
  $split = $line.Split('=', 2)
  if ($split.Count -eq 2) {
    $debugLayerReportMap[$split[0]] = $split[1]
  }
}

$expectedDebugChecks = @{
  'analysis_result' = 'success'
  'overall_verdict' = 'pass'
  'runtime_exit_code' = '0'
  'debugger_break_detected' = 'false'
  'd3d12_error_count' = '0'
  'dxgi_error_count' = '0'
  'blocking_warning_count' = '0'
}
foreach ($key in $expectedDebugChecks.Keys) {
  $actual = if ($debugLayerReportMap.ContainsKey($key)) {
    $debugLayerReportMap[$key]
  } else {
    ''
  }
  if ($actual -ne $expectedDebugChecks[$key]) {
    throw "Debug-layer report check failed: $key expected=$($expectedDebugChecks[$key]) actual=$actual"
  }
}

$runtimeLogLines = Get-Content -LiteralPath $runtimeLogFullPath
if (@($runtimeLogLines | Select-String -Pattern "Parsed with-translucency option = true").Count -eq 0) {
  throw "Runtime log does not prove --with-translucency true: $runtimeLogFullPath"
}

$drawWriteMatches = @(
  $runtimeLogLines | Select-String -Pattern 'Writing (\d+) draw metadata'
)
if ($drawWriteMatches.Count -eq 0) {
  throw "Runtime log does not contain draw metadata writes: $runtimeLogFullPath"
}
$drawWriteCounts = @(
  $drawWriteMatches | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
)
if (@($drawWriteCounts | Where-Object { $_ -ne 4 }).Count -ne 0) {
  throw "Runtime log draw metadata writes are not all 4: $($drawWriteCounts -join ',')"
}

Add-Content -LiteralPath $reportFullPath -Value @(
  'runtime_log_translucency_enabled=true'
  'runtime_log_draw_metadata_count=4'
  'debug_layer_verdict=pass'
)
