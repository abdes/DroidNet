<#
.SYNOPSIS
Asserts VTX-M08 skybox steady-state scene-texture allocation churn.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string[]]$RuntimeLogPath,

  [Parameter()]
  [ValidateRange(1, 1000)]
  [int]$RunFrames = 65,

  [Parameter()]
  [ValidateRange(0, 100)]
  [int]$WarmupFrames = 5,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = Join-Path $repoRoot `
    'out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox.allocation-churn.txt'
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$runtimeLogFullPaths = @(
  foreach ($path in $RuntimeLogPath) {
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path)
    if (-not (Test-Path -LiteralPath $resolved)) {
      throw "Runtime log not found: $path"
    }
    $resolved
  }
)

$churnMatches = @(
  foreach ($path in $runtimeLogFullPaths) {
    Select-String `
      -Path $path `
      -Pattern 'Vortex\.SceneTextureLeasePool\.Churn frame=(\d+) scene_views=(\d+) allocations_before=(\d+) allocations_after=(\d+) allocations_delta=(\d+) live_leases=(\d+)'
  }
)

$steadyStateAllocationsAfterWarmup = 0
$steadyStateFrameCount = 0
foreach ($match in ($churnMatches | Select-Object -Skip $WarmupFrames)) {
  $steadyStateFrameCount += 1
  $steadyStateAllocationsAfterWarmup += [int]$match.Matches[0].Groups[5].Value
}

$runFramesAtLeast60 = $RunFrames -ge 60
$hasSteadyStateWindow = $steadyStateFrameCount -ge 60
$steadyStateAllocationsZero = $steadyStateAllocationsAfterWarmup -eq 0
$overallVerdict = $runFramesAtLeast60 `
  -and $hasSteadyStateWindow `
  -and $steadyStateAllocationsZero

$reportLines = @(
  'analysis_result=success'
  'analysis_profile=vortex_skybox_allocation_churn'
  "runtime_logs=$($runtimeLogFullPaths -join ';')"
  "run_frames=$RunFrames"
  "run_frames_at_least_60=$(if ($runFramesAtLeast60) { 'true' } else { 'false' })"
  "warmup_frames=$WarmupFrames"
  "telemetry_frame_count=$($churnMatches.Count)"
  "steady_state_frame_count=$steadyStateFrameCount"
  "steady_state_window=$(if ($hasSteadyStateWindow) { 'true' } else { 'false' })"
  "steady_state_allocations_after_warmup=$steadyStateAllocationsAfterWarmup"
  "steady_state_allocations_zero=$(if ($steadyStateAllocationsZero) { 'true' } else { 'false' })"
  "overall_verdict=$(if ($overallVerdict) { 'pass' } else { 'fail' })"
)

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

if (-not $overallVerdict) {
  throw "Skybox allocation churn proof failed. See $reportFullPath"
}

$global:LASTEXITCODE = 0
Write-Output "Skybox allocation-churn report: $reportFullPath"
