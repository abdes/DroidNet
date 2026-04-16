<#
.SYNOPSIS
Runs the one-command VortexBasic deferred debug-view validation flow.
#>
[CmdletBinding()]
param(
  [Parameter()]
  [string]$Output = '',

  [Parameter()]
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
  [string]$RenderDocLibrary = 'C:\Program Files\RenderDoc\renderdoc.dll'
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

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$binaryDirectory = Join-Path $buildRoot 'bin\Debug'
$vortexBasicExe = Join-Path $binaryDirectory 'Oxygen.Examples.VortexBasic.exe'
$verifyScript = Join-Path $PSScriptRoot 'Verify-VortexBasicDebugViewProof.ps1'
$renderDocLibraryPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $RenderDocLibrary

if (-not (Test-Path -LiteralPath $renderDocLibraryPath)) {
  throw "RenderDoc runtime library not found: $renderDocLibraryPath"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $buildRoot 'analysis\vortex\vortexbasic\runtime\phase3-debug-view-validation'
}

$outputStem = Resolve-RepoPath -RepoRoot $repoRoot -Path $Output
$outputDirectory = Split-Path -Parent $outputStem
$outputStemLeaf = Split-Path -Leaf $outputStem
$buildLogPath = "$outputStem.build.log"
$aggregateReportPath = "$outputStem.validation.txt"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

if ($RunFrames -lt ($Frame + 3)) {
  $RunFrames = $Frame + 3
}

$buildResult = Invoke-LoggedCommand `
  -FilePath 'cmake' `
  -ArgumentList @(
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
  ) `
  -LogPath $buildLogPath `
  -WorkingDirectory $repoRoot

if ($buildResult.ExitCode -ne 0) {
  throw "Build failed. See log: $($buildResult.LogPath)"
}

if (-not (Test-Path -LiteralPath $vortexBasicExe)) {
  throw "VortexBasic executable not found after build: $vortexBasicExe"
}

$modes = @(
  @{ Cli = 'base-color'; Token = 'basecolor'; Label = 'BaseColor' },
  @{ Cli = 'world-normals'; Token = 'worldnormals'; Label = 'WorldNormals' },
  @{ Cli = 'roughness'; Token = 'roughness'; Label = 'Roughness' },
  @{ Cli = 'metalness'; Token = 'metalness'; Label = 'Metalness' },
  @{ Cli = 'scene-depth-raw'; Token = 'scenedepthraw'; Label = 'SceneDepthRaw' },
  @{ Cli = 'scene-depth-linear'; Token = 'scenedepthlinear'; Label = 'SceneDepthLinear' }
)

$validationReports = New-Object System.Collections.Generic.List[string]

foreach ($mode in $modes) {
  $modeStem = Join-Path $outputDirectory "$outputStemLeaf-$($mode.Token)"
  $captureFilter = "$(Split-Path -Leaf $modeStem)*.rdc"
  $stdoutPath = "$modeStem.stdout.log"
  $stderrPath = "$modeStem.stderr.log"

  $captureSnapshotBefore = Get-CaptureSnapshot `
    -Directory $outputDirectory `
    -Filter $captureFilter

  Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

  $appArguments = @(
    '--frames', "$RunFrames",
    '--fps', "$Fps",
    '--shader-debug-mode', $mode.Cli,
    '--capture-provider', 'renderdoc',
    '--capture-load', 'path',
    '--capture-library', $renderDocLibraryPath,
    '--capture-output', $modeStem,
    '--capture-from-frame', "$Frame",
    '--capture-frame-count', '1'
  )

  $previousPath = $env:PATH
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
    throw "VortexBasic exited with code $appExitCode for mode $($mode.Label). See log: $stderrPath"
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
    throw "Expected exactly one new capture for mode $($mode.Label), found $($captures.Count). Check: $outputDirectory"
  }

  $capturePath = $captures[0].FullName
  Wait-ForStableFile -Path $capturePath

  $captureBasePath = Join-Path `
    (Split-Path -Parent $capturePath) `
    ([System.IO.Path]::GetFileNameWithoutExtension($capturePath))
  $captureReportPath = "${captureBasePath}_vortexbasic_debug_report.txt"
  $validationReportPath = "$modeStem.validation.txt"

  & powershell -NoProfile -File $verifyScript `
    -CapturePath $capturePath `
    -RuntimeLogPath $stderrPath `
    -CaptureReportPath $captureReportPath `
    -ValidationReportPath $validationReportPath
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  $validationReports.Add($validationReportPath)
}

$aggregateLines = New-Object System.Collections.Generic.List[string]
$aggregateLines.Add('analysis_result=success')
$aggregateLines.Add('analysis_profile=vortexbasic_debug_view_validation_suite')
$aggregateLines.Add("build_log_path=$buildLogPath")
foreach ($reportPath in $validationReports) {
  $aggregateLines.Add("mode_validation_report=$reportPath")
}
Set-Content -LiteralPath $aggregateReportPath -Value $aggregateLines -Encoding utf8

Write-Host "Build log: $buildLogPath"
Write-Host "Aggregate validation report: $aggregateReportPath"
foreach ($reportPath in $validationReports) {
  Write-Host "Mode validation report: $reportPath"
}
