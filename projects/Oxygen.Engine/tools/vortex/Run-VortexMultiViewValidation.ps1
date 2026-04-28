<#
.SYNOPSIS
Runs the VTX-M06A MultiView runtime validation flow.
#>
[CmdletBinding()]
param(
  [Parameter()]
  [string]$Output = '',

  [Parameter()]
  [ValidateRange(0, [int]::MaxValue)]
  [int]$Frame = 5,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$RunFrames = 65,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Fps = 30,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$BuildJobs = 4,

  [Parameter()]
  [string]$RenderDocLibrary = 'C:\Program Files\RenderDoc\renderdoc.dll',

  [Parameter()]
  [string]$DebuggerPath = '',

  [Parameter()]
  [switch]$AuxProofLayout
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'VortexProofCommon.ps1')
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')
. (Join-Path $PSScriptRoot '..\shadows\RenderSceneBenchmarkCommon.ps1')

function Quote-Argument {
  param([Parameter(Mandatory = $true)][string]$Value)
  if ($Value -notmatch '[\s"]') {
    return $Value
  }
  return '"' + ($Value -replace '"', '\"') + '"'
}

function Invoke-LoggedProcess {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [Parameter(Mandatory = $true)][string[]]$ArgumentList,
    [Parameter(Mandatory = $true)][string]$WorkingDirectory,
    [Parameter(Mandatory = $true)][string]$StdoutPath,
    [Parameter(Mandatory = $true)][string]$StderrPath
  )

  Remove-Item -LiteralPath $StdoutPath, $StderrPath -Force -ErrorAction SilentlyContinue
  $argumentString = (($ArgumentList | ForEach-Object { Quote-Argument -Value $_ }) -join ' ')
  $process = Start-Process `
    -FilePath $FilePath `
    -ArgumentList $argumentString `
    -WorkingDirectory $WorkingDirectory `
    -NoNewWindow `
    -Wait `
    -PassThru `
    -RedirectStandardOutput $StdoutPath `
    -RedirectStandardError $StderrPath
  return $process.ExitCode
}

function Resolve-DebuggerToolPath {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter()][string]$DebuggerPath = ''
  )

  if (-not [string]::IsNullOrWhiteSpace($DebuggerPath)) {
    $resolved = Resolve-RepoPath -RepoRoot $RepoRoot -Path $DebuggerPath
    if (-not (Test-Path -LiteralPath $resolved)) {
      throw "Debugger tool not found: $resolved"
    }
    return $resolved
  }

  $command = Get-Command 'cdb.exe' -ErrorAction SilentlyContinue
  if ($null -ne $command) {
    return $command.Source
  }

  foreach ($candidate in @(
      'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe',
      'C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe'
    )) {
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  throw 'Could not locate cdb.exe. Install the Windows debugger tools or pass -DebuggerPath.'
}

$repoRoot = Get-VortexProofRepoRoot
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$binaryDirectory = Join-Path $buildRoot 'bin\Debug'
$multiViewExe = Join-Path $binaryDirectory 'Oxygen.Examples.MultiView.exe'
$debugAuditAssertScript = Join-Path $PSScriptRoot 'Assert-VortexBasicDebugLayerAudit.ps1'
$assertScript = Join-Path $PSScriptRoot 'Assert-VortexMultiViewProof.ps1'
$analysisScript = Join-Path $PSScriptRoot 'AnalyzeRenderDocVortexMultiView.py'
$debuggerTool = Resolve-DebuggerToolPath -RepoRoot $repoRoot -DebuggerPath $DebuggerPath
$renderDocLibraryPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $RenderDocLibrary

if (-not (Test-Path -LiteralPath $renderDocLibraryPath)) {
  throw "RenderDoc runtime library not found: $renderDocLibraryPath"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
  $leaf = if ($AuxProofLayout) { 'multiview-aux-proof' } else { 'multiview-proof' }
  $Output = Join-Path $buildRoot "analysis\vortex\m06a-multiview\$leaf"
}

$outputStem = Resolve-RepoPath -RepoRoot $repoRoot -Path $Output
$outputDirectory = Split-Path -Parent $outputStem
$outputStemLeaf = Split-Path -Leaf $outputStem
$captureFilter = "$outputStemLeaf*.rdc"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

if ($RunFrames -lt ($Frame + 3)) {
  $RunFrames = $Frame + 3
}

$buildStdoutPath = "$outputStem.build.stdout.log"
$buildStderrPath = "$outputStem.build.stderr.log"
$debugLogPath = "$outputStem.debug-layer.cdb.log"
$debugStdoutPath = "$outputStem.debug-layer.stdout.log"
$debugStderrPath = "$outputStem.debug-layer.stderr.log"
$debugTranscriptPath = "$outputStem.debug-layer.transcript.log"
$debugCommandsPath = "$outputStem.debug-layer.commands.txt"
$debugReportPath = "$outputStem.debug-layer.report.txt"
$runtimeStdoutPath = "$outputStem.stdout.log"
$runtimeStderrPath = "$outputStem.stderr.log"
$captureReportPath = "$outputStem.renderdoc.txt"
$allocationReportPath = "$outputStem.allocation-churn.txt"
$validationReportPath = "$outputStem.validation.txt"

$buildArgs = @(
  '--build', $buildRoot,
  '--config', 'Debug',
  '--target', 'oxygen-vortex', 'oxygen-graphics-direct3d12', 'oxygen-examples-multiview',
  '--parallel', "$BuildJobs"
)
$buildExitCode = Invoke-LoggedProcess `
  -FilePath 'cmake' `
  -ArgumentList $buildArgs `
  -WorkingDirectory $repoRoot `
  -StdoutPath $buildStdoutPath `
  -StderrPath $buildStderrPath
if ($buildExitCode -ne 0) {
  throw "Build failed with exit code $buildExitCode. See $buildStdoutPath and $buildStderrPath"
}
if (-not (Test-Path -LiteralPath $multiViewExe)) {
  throw "MultiView executable not found after build: $multiViewExe"
}

Set-Content -LiteralPath $debugCommandsPath -Value @('g', 'q') -Encoding ascii
$debugArgs = @(
  '-G', '-g',
  '-logo', $debugLogPath,
  '-cf', $debugCommandsPath,
  $multiViewExe,
  '--frames', "$RunFrames",
  '--fps', "$Fps",
  '--proof-layout', 'true',
  '--capture-provider', 'off',
  '--debug-layer', 'true'
)
if ($AuxProofLayout) {
  $debugArgs += @('--aux-proof-layout', 'true')
}

$previousPath = $env:PATH
try {
  $env:PATH = "$binaryDirectory;$previousPath"
  $debugExitCode = Invoke-LoggedProcess `
    -FilePath $debuggerTool `
    -ArgumentList $debugArgs `
    -WorkingDirectory $repoRoot `
    -StdoutPath $debugStdoutPath `
    -StderrPath $debugStderrPath
} finally {
  $env:PATH = $previousPath
}
if ($debugExitCode -ne 0) {
  throw "Debugger-backed audit exited with code $debugExitCode. See $debugLogPath"
}

$debugTranscriptLines = New-Object System.Collections.Generic.List[string]
foreach ($path in @($debugLogPath, $debugStdoutPath, $debugStderrPath)) {
  if (Test-Path -LiteralPath $path) {
    $debugTranscriptLines.Add("--- $path ---")
    foreach ($line in Get-Content -LiteralPath $path) {
      $debugTranscriptLines.Add($line)
    }
  }
}
Set-Content -LiteralPath $debugTranscriptPath -Value $debugTranscriptLines -Encoding utf8

& powershell -NoProfile -File $debugAuditAssertScript `
  -DebuggerLogPath $debugTranscriptPath `
  -ReportPath $debugReportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$captureSnapshotBefore = Get-CaptureSnapshot -Directory $outputDirectory -Filter $captureFilter
$appArgs = @(
  '--frames', "$RunFrames",
  '--fps', "$Fps",
  '--proof-layout', 'true',
  '--capture-provider', 'renderdoc',
  '--capture-load', 'path',
  '--capture-library', $renderDocLibraryPath,
  '--capture-output', $outputStem,
  '--capture-from-frame', "$Frame",
  '--capture-frame-count', '1'
)
if ($AuxProofLayout) {
  $appArgs += @('--aux-proof-layout', 'true')
}
try {
  $env:PATH = "$binaryDirectory;$previousPath"
  $runtimeExitCode = Invoke-LoggedProcess `
    -FilePath $multiViewExe `
    -ArgumentList $appArgs `
    -WorkingDirectory $repoRoot `
    -StdoutPath $runtimeStdoutPath `
    -StderrPath $runtimeStderrPath
} finally {
  $env:PATH = $previousPath
}
if ($runtimeExitCode -ne 0) {
  throw "MultiView capture run exited with code $runtimeExitCode. See $runtimeStdoutPath and $runtimeStderrPath"
}

$newCaptures = @(
  Get-NewOrUpdatedCaptures `
    -Directory $outputDirectory `
    -Filter $captureFilter `
    -Before $captureSnapshotBefore
)
if ($newCaptures.Count -eq 0) {
  throw "No new RenderDoc capture found in $outputDirectory with filter $captureFilter"
}
$capturePath = $newCaptures[-1].FullName
Wait-ForStableFile -Path $capturePath

Invoke-VortexRenderDocAnalysis `
  -CapturePath $capturePath `
  -UiScriptPath $analysisScript `
  -PassName 'VortexMultiViewProof' `
  -ReportPath $captureReportPath

$runFramesAtLeast60 = $RunFrames -ge 60
$allocationWarmupFrames = 5
$churnMatches = @(
  Select-String `
    -Path $runtimeStderrPath `
    -Pattern 'Vortex\.SceneTextureLeasePool\.Churn frame=(\d+) scene_views=(\d+) allocations_before=(\d+) allocations_after=(\d+) allocations_delta=(\d+) live_leases=(\d+)'
)
$steadyStateAllocationsAfterWarmup = 0
$steadyStateFrameCount = 0
foreach ($match in ($churnMatches | Select-Object -Skip $allocationWarmupFrames)) {
  $steadyStateFrameCount += 1
  $steadyStateAllocationsAfterWarmup += [int]$match.Matches[0].Groups[5].Value
}
$hasSteadyStateWindow = $steadyStateFrameCount -gt 0
$steadyStateAllocationsZero = $steadyStateAllocationsAfterWarmup -eq 0
$allocationReportLines = @(
  'analysis_result=success'
  'analysis_profile=vortex_multiview_allocation_churn'
  "run_frames=$RunFrames"
  "run_frames_at_least_60=$(if ($runFramesAtLeast60) { 'true' } else { 'false' })"
  "warmup_frames=$allocationWarmupFrames"
  "telemetry_frame_count=$($churnMatches.Count)"
  "steady_state_frame_count=$steadyStateFrameCount"
  "steady_state_window=$(if ($hasSteadyStateWindow) { 'true' } else { 'false' })"
  "steady_state_allocations_after_warmup=$steadyStateAllocationsAfterWarmup"
  "steady_state_allocations_zero=$(if ($steadyStateAllocationsZero) { 'true' } else { 'false' })"
  "overall_verdict=$(if ($runFramesAtLeast60 -and $hasSteadyStateWindow -and $steadyStateAllocationsZero) { 'pass' } else { 'fail' })"
)
Set-Content -LiteralPath $allocationReportPath -Value $allocationReportLines -Encoding utf8

& powershell -NoProfile -File $assertScript `
  -DebugLayerReportPath $debugReportPath `
  -RuntimeLogPath $runtimeStdoutPath `
  -RuntimeErrorLogPath $runtimeStderrPath `
  -CaptureReportPath $captureReportPath `
  -AllocationReportPath $allocationReportPath `
  -ReportPath $validationReportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Output "VTX-M06A MultiView validation passed."
Write-Output "Capture: $capturePath"
Write-Output "Report: $validationReportPath"
