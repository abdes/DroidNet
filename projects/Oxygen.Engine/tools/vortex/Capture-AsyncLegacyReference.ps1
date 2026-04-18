<#
.SYNOPSIS
Captures the legacy/reference Async baseline for Phase 4 parity comparison.

.DESCRIPTION
Builds the Async example, runs it once with RenderDoc capture enabled, and
writes the reference artifacts (frame, depth, capture, behaviors) into the
specified output directory. These reference artifacts are consumed by
Run-AsyncRuntimeValidation.ps1 -ReferenceRoot to measure parity against a
real baseline instead of self-validating the current run.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Output,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Frame = 90,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$RunFrames = 94,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Fps = 30,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$BuildJobs = 4,

  [Parameter()]
  [string]$RenderDocLibrary = 'C:\Program Files\RenderDoc\renderdoc.dll'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')
. (Join-Path $PSScriptRoot '..\shadows\RenderSceneBenchmarkCommon.ps1')

function Quote-Argument {
  param([Parameter(Mandatory = $true)][string]$Value)

  if ($Value -notmatch '[\s"]') {
    return $Value
  }

  return '"' + ($Value -replace '"', '\"') + '"'
}

function Get-BlockingRuntimeLogMatches {
  param([Parameter(Mandatory = $true)][string[]]$Paths)

  $patterns = @(
    'CHECK FAILED',
    'handler threw:',
    '\bUnhandled exception\b',
    '\bassert(ion)? failed\b',
    '\bfatal\b',
    '\babort(ed|ing)?\b'
  )

  $matches = New-Object System.Collections.Generic.List[string]
  foreach ($path in $Paths) {
    if (-not (Test-Path -LiteralPath $path)) {
      continue
    }

    foreach ($line in Get-Content -LiteralPath $path) {
      foreach ($pattern in $patterns) {
        if ($line -match $pattern) {
          $matches.Add("$([System.IO.Path]::GetFileName($path)): $line")
          break
        }
      }
    }
  }

  return $matches
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$binaryDirectory = Join-Path $buildRoot 'bin\Debug'
$asyncExe = Join-Path $binaryDirectory 'Oxygen.Examples.Async.exe'
$renderDocLibraryPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $RenderDocLibrary
$outputDirectory = Resolve-RepoPath -RepoRoot $repoRoot -Path $Output
$captureStem = Join-Path $outputDirectory 'reference_renderdoc'
$finalCapturePath = Join-Path $outputDirectory 'reference_renderdoc.rdc'
$stdoutPath = Join-Path $outputDirectory 'reference.stdout.log'
$stderrPath = Join-Path $outputDirectory 'reference.stderr.log'
$buildLogPath = Join-Path $outputDirectory 'reference.build.log'
$captureFilter = 'reference_renderdoc*.rdc'

if (-not (Test-Path -LiteralPath $renderDocLibraryPath)) {
  throw "RenderDoc runtime library not found: $renderDocLibraryPath"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

# --- Build ---
$buildStdoutPath = "$buildLogPath.stdout.log"
$buildStderrPath = "$buildLogPath.stderr.log"
Remove-Item -LiteralPath $buildLogPath, $buildStdoutPath, $buildStderrPath -Force -ErrorAction SilentlyContinue

$buildArguments = @(
  '--build',
  $buildRoot,
  '--config',
  'Debug',
  '--target',
  'oxygen-vortex',
  'oxygen-graphics-direct3d12',
  'oxygen-examples-async',
  '--parallel',
  "$BuildJobs"
)
$buildArgumentString = (($buildArguments | ForEach-Object {
      Quote-Argument -Value $_
    }) -join ' ')
$buildProcess = Start-Process `
  -FilePath 'cmake' `
  -ArgumentList $buildArgumentString `
  -WorkingDirectory $repoRoot `
  -NoNewWindow `
  -Wait `
  -PassThru `
  -RedirectStandardOutput $buildStdoutPath `
  -RedirectStandardError $buildStderrPath

$buildLines = New-Object System.Collections.Generic.List[string]
$buildLines.Add("Command: `"cmake`" $buildArgumentString")
$buildLines.Add("ExitCode: $($buildProcess.ExitCode)")
if (Test-Path -LiteralPath $buildStdoutPath) {
  $buildLines.Add('--- stdout ---')
  foreach ($line in Get-Content -LiteralPath $buildStdoutPath) {
    $buildLines.Add($line)
  }
}
if (Test-Path -LiteralPath $buildStderrPath) {
  $buildLines.Add('--- stderr ---')
  foreach ($line in Get-Content -LiteralPath $buildStderrPath) {
    $buildLines.Add($line)
  }
}
Set-Content -LiteralPath $buildLogPath -Value $buildLines -Encoding ascii

if ($buildProcess.ExitCode -ne 0) {
  throw "Build failed. See log: $buildLogPath"
}
if (-not (Test-Path -LiteralPath $asyncExe)) {
  throw "Async executable not found after build: $asyncExe"
}

# --- Capture reference frame ---
Remove-Item -LiteralPath $stdoutPath, $stderrPath, $finalCapturePath -Force -ErrorAction SilentlyContinue
$captureSnapshotBefore = Get-CaptureSnapshot -Directory $outputDirectory -Filter $captureFilter

$appArguments = @(
  '--frames', "$RunFrames",
  '--fps', "$Fps",
  '--vsync', 'false',
  '--capture-provider', 'renderdoc',
  '--capture-load', 'path',
  '--capture-library', $renderDocLibraryPath,
  '--capture-output', $captureStem,
  '--capture-from-frame', "$Frame",
  '--capture-frame-count', '1'
)
$appArgumentString = (($appArguments | ForEach-Object {
      Quote-Argument -Value $_
    }) -join ' ')

$previousPath = $env:PATH
try {
  $env:PATH = "$binaryDirectory;$previousPath"
  $process = Start-Process `
    -FilePath $asyncExe `
    -ArgumentList $appArgumentString `
    -WorkingDirectory $repoRoot `
    -NoNewWindow `
    -Wait `
    -PassThru `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath
  $appExitCode = $process.ExitCode
} finally {
  $env:PATH = $previousPath
}

if ($appExitCode -ne 0) {
  throw "Async reference capture exited with code $appExitCode. See log: $stderrPath"
}

$blockingLogMatches = @(Get-BlockingRuntimeLogMatches -Paths @($stdoutPath, $stderrPath))
if ($blockingLogMatches.Count -gt 0) {
  $summary = ($blockingLogMatches | Select-Object -First 12) -join [Environment]::NewLine
  throw "Async reference runtime log contains blocking errors:`n$summary"
}

# --- Discover and stabilize capture ---
$captureDeadline = (Get-Date).AddSeconds(30)
$captures = @()
while ((Get-Date) -lt $captureDeadline) {
  $captures = @(Get-NewOrUpdatedCaptures `
      -Directory $outputDirectory `
      -Filter $captureFilter `
      -Before $captureSnapshotBefore)
  if ($captures.Count -eq 1) {
    break
  }
  Start-Sleep -Milliseconds 250
}

if ($captures.Count -ne 1) {
  throw "Expected exactly one new reference capture, found $($captures.Count). Check: $outputDirectory"
}

$capturePath = $captures[0].FullName
Wait-ForStableFile -Path $capturePath
Move-Item -LiteralPath $capturePath -Destination $finalCapturePath -Force

# --- Extract reference frame and depth via Verify in initialize mode ---
& powershell -NoProfile -File (Join-Path $PSScriptRoot 'Verify-AsyncRuntimeProof.ps1') `
  -CapturePath $finalCapturePath `
  -InitializeBaselineArtifacts `
  -BaselineFramePath (Join-Path $outputDirectory 'reference_frame10.png') `
  -BaselineDepthPath (Join-Path $outputDirectory 'reference_depth.png') `
  -BehaviorPath (Join-Path $outputDirectory 'reference_behaviors.md')
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

# --- Validate reference artifacts exist ---
$requiredArtifacts = @(
  (Join-Path $outputDirectory 'reference_frame10.png'),
  (Join-Path $outputDirectory 'reference_depth.png'),
  (Join-Path $outputDirectory 'reference_renderdoc.rdc'),
  (Join-Path $outputDirectory 'reference_behaviors.md')
)
foreach ($artifact in $requiredArtifacts) {
  if (-not (Test-Path -LiteralPath $artifact)) {
    throw "Missing required reference artifact: $artifact"
  }
}

Write-Host ''
Write-Host 'Reference capture complete.'
Write-Host "Build log: $buildLogPath"
Write-Host "Runtime stdout: $stdoutPath"
Write-Host "Runtime stderr: $stderrPath"
Write-Host "Capture: $finalCapturePath"
Write-Host "Reference frame: $(Join-Path $outputDirectory 'reference_frame10.png')"
Write-Host "Reference depth: $(Join-Path $outputDirectory 'reference_depth.png')"
Write-Host "Behaviors: $(Join-Path $outputDirectory 'reference_behaviors.md')"
