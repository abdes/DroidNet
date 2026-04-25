<#
.SYNOPSIS
Runs the full one-command VortexBasic runtime validation flow.

.DESCRIPTION
Builds the required Vortex targets in the standard out/build-ninja tree,
first runs VortexBasic once under the WinDbg/CDB debugger with capture
disabled to audit the D3D12 debug-layer surface, then launches VortexBasic once
with RenderDoc capture enabled, discovers the newly produced capture, and
validates both the debugger-backed audit and the capture-backed proof through
the existing Verify-VortexBasicRuntimeProof.ps1 wrapper.
#>
[CmdletBinding()]
param(
  [Parameter()]
  [Alias('CaptureOutputTemplate')]
  [string]$Output = '',

  [Parameter()]
  [Alias('CaptureFromFrame')]
  [ValidateRange(0, [int]::MaxValue)]
  [int]$Frame = 3,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$RunFrames = 6,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Fps = 30,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$BuildJobs = 4,

  [Parameter()]
  [string]$RenderDocLibrary = 'C:\Program Files\RenderDoc\renderdoc.dll',

  [Parameter()]
  [string]$DebuggerPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')
. (Join-Path $PSScriptRoot '..\shadows\RenderSceneBenchmarkCommon.ps1')

function Invoke-LoggedCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,

    [Parameter(Mandatory = $true)]
    [string[]]$ArgumentList,

    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [Parameter(Mandatory = $true)]
    [string]$WorkingDirectory
  )

  $stdoutPath = "$LogPath.stdout.log"
  $stderrPath = "$LogPath.stderr.log"
  Remove-Item -LiteralPath $LogPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

  $started = Get-Date -Format o
  $exitCode = 9009
  $exceptionText = $null

  try {
    Push-Location $WorkingDirectory
    try {
      & $FilePath @ArgumentList 1> $stdoutPath 2> $stderrPath
      $exitCode = $LASTEXITCODE
    } finally {
      Pop-Location
    }
  } catch {
    $exceptionText = $_.Exception.Message
  }

  $lines = New-Object System.Collections.Generic.List[string]
  $commandText = $ArgumentList -join ' '
  $lines.Add("Started: $started")
  $lines.Add("WorkingDirectory: $WorkingDirectory")
  $lines.Add("Command: `"$FilePath`" $commandText")
  if ($exceptionText) {
    $lines.Add("Exception: $exceptionText")
  }
  if (Test-Path -LiteralPath $stdoutPath) {
    $lines.Add('--- stdout ---')
    foreach ($line in Get-Content -LiteralPath $stdoutPath) {
      $lines.Add($line)
    }
  }
  if (Test-Path -LiteralPath $stderrPath) {
    $lines.Add('--- stderr ---')
    foreach ($line in Get-Content -LiteralPath $stderrPath) {
      $lines.Add($line)
    }
  }
  $lines.Add("ExitCode: $exitCode")
  Set-Content -LiteralPath $LogPath -Value $lines -Encoding ascii

  return [pscustomobject]@{
    ExitCode = $exitCode
    LogPath = $LogPath
  }
}

function Quote-Argument {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Value
  )

  if ($Value -notmatch '[\s"]') {
    return $Value
  }

  return '"' + ($Value -replace '"', '\"') + '"'
}

function Resolve-DebuggerToolPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter()]
    [string]$DebuggerPath = ''
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

  $fallbacks = @(
    'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe',
    'C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe'
  )
  foreach ($candidate in $fallbacks) {
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  throw 'Could not locate cdb.exe. Install the Windows debugger tools or pass -DebuggerPath.'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$binaryDirectory = Join-Path $buildRoot 'bin\Debug'
$vortexBasicExe = Join-Path $binaryDirectory 'Oxygen.Examples.VortexBasic.exe'
$verifyScript = Join-Path $PSScriptRoot 'Verify-VortexBasicRuntimeProof.ps1'
$debugAuditAssertScript = Join-Path $PSScriptRoot 'Assert-VortexBasicDebugLayerAudit.ps1'
$debuggerTool = Resolve-DebuggerToolPath -RepoRoot $repoRoot -DebuggerPath $DebuggerPath
$renderDocLibraryPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $RenderDocLibrary

if (-not (Test-Path -LiteralPath $renderDocLibraryPath)) {
  throw "RenderDoc runtime library not found: $renderDocLibraryPath"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $buildRoot 'analysis\vortex\vortexbasic\runtime\phase3-runtime-validation'
}

$outputStem = Resolve-RepoPath -RepoRoot $repoRoot -Path $Output
$outputDirectory = Split-Path -Parent $outputStem
$outputStemLeaf = Split-Path -Leaf $outputStem
$captureFilter = "$outputStemLeaf*.rdc"
$buildLogPath = "$outputStem.build.log"
$debugAuditLogPath = "$outputStem.debug-layer.cdb.log"
$debugAuditStdoutPath = "$outputStem.debug-layer.cdb.stdout.log"
$debugAuditStderrPath = "$outputStem.debug-layer.cdb.stderr.log"
$debugAuditTranscriptPath = "$outputStem.debug-layer.transcript.log"
$debugAuditCommandsPath = "$outputStem.debug-layer.cdb.commands.txt"
$debugAuditReportPath = "$outputStem.debug-layer.report.txt"
$stdoutPath = "$outputStem.stdout.log"
$stderrPath = "$outputStem.stderr.log"
$validationReportPath = "$outputStem.validation.txt"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

if ($RunFrames -lt ($Frame + 3)) {
  $RunFrames = $Frame + 3
}

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
  'oxygen-examples-vortexbasic',
  '--parallel',
  "$BuildJobs"
)
$buildArgumentString = (($buildArguments | ForEach-Object {
      Quote-Argument -Value $_
    }) -join ' ')
$buildStarted = Get-Date -Format o
$buildProcess = Start-Process `
  -FilePath 'cmake' `
  -ArgumentList $buildArgumentString `
  -WorkingDirectory $repoRoot `
  -NoNewWindow `
  -Wait `
  -PassThru `
  -RedirectStandardOutput $buildStdoutPath `
  -RedirectStandardError $buildStderrPath
$buildExitCode = $buildProcess.ExitCode

$buildLogLines = New-Object System.Collections.Generic.List[string]
$buildLogLines.Add("Started: $buildStarted")
$buildLogLines.Add("WorkingDirectory: $repoRoot")
$buildLogLines.Add("Command: `"cmake`" $buildArgumentString")
if (Test-Path -LiteralPath $buildStdoutPath) {
  $buildLogLines.Add('--- stdout ---')
  foreach ($line in Get-Content -LiteralPath $buildStdoutPath) {
    $buildLogLines.Add($line)
  }
}
if (Test-Path -LiteralPath $buildStderrPath) {
  $buildLogLines.Add('--- stderr ---')
  foreach ($line in Get-Content -LiteralPath $buildStderrPath) {
    $buildLogLines.Add($line)
  }
}
$buildLogLines.Add("ExitCode: $buildExitCode")
Set-Content -LiteralPath $buildLogPath -Value $buildLogLines -Encoding ascii

if ($buildExitCode -ne 0) {
  throw "Build failed. See log: $buildLogPath"
}

if (-not (Test-Path -LiteralPath $vortexBasicExe)) {
  throw "VortexBasic executable not found after build: $vortexBasicExe"
}

Remove-Item -LiteralPath $debugAuditLogPath, $debugAuditStdoutPath, $debugAuditStderrPath, $debugAuditTranscriptPath, $debugAuditCommandsPath, $debugAuditReportPath -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath $debugAuditCommandsPath -Value @(
    'g'
    'q'
  ) -Encoding ascii

$debugAuditArguments = @(
  '-G',
  '-g',
  '-logo', $debugAuditLogPath,
  '-cf', $debugAuditCommandsPath,
  $vortexBasicExe,
  '--frames', "$RunFrames",
  '--fps', "$Fps",
  '--with-atmosphere',
  '--with-height-fog',
  '--with-local-fog',
  '--with-volumetric-fog',
  '--debug-layer', 'true',
  '--capture-provider', 'off'
)

$previousPath = $env:PATH
$debugAuditArgumentString = (($debugAuditArguments | ForEach-Object {
      Quote-Argument -Value $_
    }) -join ' ')
try {
  $env:PATH = "$binaryDirectory;$previousPath"
  $debugAuditProcess = Start-Process `
    -FilePath $debuggerTool `
    -ArgumentList $debugAuditArgumentString `
    -WorkingDirectory $repoRoot `
    -NoNewWindow `
    -Wait `
    -PassThru `
    -RedirectStandardOutput $debugAuditStdoutPath `
    -RedirectStandardError $debugAuditStderrPath
  $debugAuditExitCode = $debugAuditProcess.ExitCode
} finally {
  $env:PATH = $previousPath
}

if ($debugAuditExitCode -ne 0) {
  throw "Debugger-backed audit process exited with code $debugAuditExitCode. See log: $debugAuditLogPath"
}

$debugAuditTranscriptLines = New-Object System.Collections.Generic.List[string]
if (Test-Path -LiteralPath $debugAuditLogPath) {
  $debugAuditTranscriptLines.Add('--- debugger log ---')
  foreach ($line in Get-Content -LiteralPath $debugAuditLogPath) {
    $debugAuditTranscriptLines.Add($line)
  }
}
if (Test-Path -LiteralPath $debugAuditStdoutPath) {
  $debugAuditTranscriptLines.Add('--- debugger stdout ---')
  foreach ($line in Get-Content -LiteralPath $debugAuditStdoutPath) {
    $debugAuditTranscriptLines.Add($line)
  }
}
if (Test-Path -LiteralPath $debugAuditStderrPath) {
  $debugAuditTranscriptLines.Add('--- debugger stderr ---')
  foreach ($line in Get-Content -LiteralPath $debugAuditStderrPath) {
    $debugAuditTranscriptLines.Add($line)
  }
}
Set-Content -LiteralPath $debugAuditTranscriptPath -Value $debugAuditTranscriptLines -Encoding utf8

& powershell -NoProfile -File $debugAuditAssertScript `
  -DebuggerLogPath $debugAuditTranscriptPath `
  -ReportPath $debugAuditReportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$captureSnapshotBefore = Get-CaptureSnapshot `
  -Directory $outputDirectory `
  -Filter $captureFilter

Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

$appArguments = @(
  '--frames', "$RunFrames",
  '--fps', "$Fps",
  '--with-atmosphere',
  '--with-height-fog',
  '--with-local-fog',
  '--with-volumetric-fog',
  '--capture-provider', 'renderdoc',
  '--capture-load', 'path',
  '--capture-library', $renderDocLibraryPath,
  '--capture-output', $outputStem,
  '--capture-from-frame', "$Frame",
  '--capture-frame-count', '1'
)
$appArgumentString = (($appArguments | ForEach-Object {
      Quote-Argument -Value $_
    }) -join ' ')
try {
  $env:PATH = "$binaryDirectory;$previousPath"
  $process = Start-Process `
    -FilePath $vortexBasicExe `
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
  throw "VortexBasic exited with code $appExitCode. See log: $stderrPath"
}

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
  throw "Expected exactly one new capture, found $($captures.Count). Check: $outputDirectory"
}

$capturePath = $captures[0].FullName
Wait-ForStableFile -Path $capturePath

$captureBasePath = Join-Path `
  (Split-Path -Parent $capturePath) `
  ([System.IO.Path]::GetFileNameWithoutExtension($capturePath))
$captureReportPath = "${captureBasePath}_vortexbasic_capture_report.txt"
$productsReportPath = "${captureBasePath}_products_report.txt"

& powershell -NoProfile -File $verifyScript `
  -CapturePath $capturePath `
  -DebugLayerReportPath $debugAuditReportPath `
  -RuntimeLogPath $stderrPath `
  -CaptureReportPath $captureReportPath `
  -ProductsReportPath $productsReportPath `
  -ValidationReportPath $validationReportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Host "Build log: $buildLogPath"
Write-Host "Debugger audit log: $debugAuditLogPath"
Write-Host "Debugger audit stdout: $debugAuditStdoutPath"
Write-Host "Debugger audit stderr: $debugAuditStderrPath"
Write-Host "Debugger audit transcript: $debugAuditTranscriptPath"
Write-Host "Debugger audit report: $debugAuditReportPath"
Write-Host "Runtime stdout: $stdoutPath"
Write-Host "Runtime stderr: $stderrPath"
Write-Host "Capture: $capturePath"
Write-Host "Capture report: $captureReportPath"
Write-Host "Products report: $productsReportPath"
Write-Host "Validation report: $validationReportPath"
