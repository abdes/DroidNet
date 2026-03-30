[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [Alias('CaptureFromFrame')]
  [ValidateRange(0, [int]::MaxValue)]
  [int]$Frame,

  [Parameter()]
  [Alias('CaptureFrameCount')]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Count = 1,

  [Parameter()]
  [Alias('CaptureOutputTemplate')]
  [string]$Output = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Wait-ForStableFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [int]$TimeoutSeconds = 30,
    [int]$PollMilliseconds = 250,
    [int]$StableSamplesRequired = 3
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $lastLength = -1L
  $lastWriteTimeUtc = [datetime]::MinValue
  $stableSamples = 0

  while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $Path) {
      $item = Get-Item -LiteralPath $Path
      if ($item.Length -eq $lastLength -and $item.LastWriteTimeUtc -eq $lastWriteTimeUtc) {
        $stableSamples++
      } else {
        $lastLength = $item.Length
        $lastWriteTimeUtc = $item.LastWriteTimeUtc
        $stableSamples = 1
      }

      if ($stableSamples -ge $StableSamplesRequired) {
        return
      }
    }

    Start-Sleep -Milliseconds $PollMilliseconds
  }

  throw "Timed out waiting for file to stabilize: $Path"
}

function Get-CaptureSnapshot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Directory,

    [Parameter(Mandatory = $true)]
    [string]$Filter
  )

  $snapshot = @{}
  if (-not (Test-Path -LiteralPath $Directory)) {
    return $snapshot
  }

  Get-ChildItem -LiteralPath $Directory -Filter $Filter -File | ForEach-Object {
    $snapshot[$_.FullName] = @{
      Length = $_.Length
      LastWriteTimeUtc = $_.LastWriteTimeUtc
    }
  }

  return $snapshot
}

function Get-NewOrUpdatedCaptures {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Directory,

    [Parameter(Mandatory = $true)]
    [string]$Filter,

    [Parameter(Mandatory = $true)]
    [hashtable]$Before
  )

  if (-not (Test-Path -LiteralPath $Directory)) {
    return @()
  }

  $captures = Get-ChildItem -LiteralPath $Directory -Filter $Filter -File
  return @(
    $captures | Where-Object {
      $previous = $Before[$_.FullName]
      if ($null -eq $previous) {
        return $true
      }

      return $_.Length -ne $previous.Length -or $_.LastWriteTimeUtc -ne $previous.LastWriteTimeUtc
    } | Sort-Object @{
      Expression = {
        if ($_.BaseName -match '_frame(\d+)$') {
          return [int]$matches[1]
        }

        return [int]::MaxValue
      }
    }, Name
  )
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$renderSceneExe = Join-Path $buildRoot 'bin\Debug\Oxygen.Examples.RenderScene.exe'

if (-not (Test-Path -LiteralPath $renderSceneExe)) {
  throw "RenderScene executable not found: $renderSceneExe"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $buildRoot 'analysis\renderdoc_capture\render_scene_capture'
}

$captureOutputTemplate = [System.IO.Path]::GetFullPath($Output)
$captureOutputDirectory = Split-Path -Parent $captureOutputTemplate
$captureOutputStem = Split-Path -Leaf $captureOutputTemplate
$captureFilter = "$captureOutputStem*.rdc"
$logPath = "$captureOutputTemplate.run.log"

New-Item -ItemType Directory -Force -Path $captureOutputDirectory | Out-Null

$captureSnapshotBefore = Get-CaptureSnapshot -Directory $captureOutputDirectory -Filter $captureFilter

$lastCaptureFrame = $Frame + $Count - 1
$runFrames = [Math]::Max(30, $lastCaptureFrame + 10)

$renderSceneArgs = @(
  '-v=-1'
  '--frames', $runFrames
  '--fps', '0'
  '--directional-shadows', 'vsm'
  '--capture-provider', 'renderdoc'
  '--capture-load', 'search'
  '--capture-output', $captureOutputTemplate
  '--capture-from-frame', $Frame
  '--capture-frame-count', $Count
)

"Command: `"$renderSceneExe`" $($renderSceneArgs -join ' ')" | Set-Content -Path $logPath -Encoding ascii
"Started: $(Get-Date -Format o)" | Add-Content -Path $logPath -Encoding ascii

$stdoutPath = "$captureOutputTemplate.stdout.log"
$stderrPath = "$captureOutputTemplate.stderr.log"

if (Test-Path -LiteralPath $stdoutPath) {
  Remove-Item -LiteralPath $stdoutPath -Force
}

if (Test-Path -LiteralPath $stderrPath) {
  Remove-Item -LiteralPath $stderrPath -Force
}

$process = Start-Process `
  -FilePath $renderSceneExe `
  -ArgumentList $renderSceneArgs `
  -WorkingDirectory $repoRoot `
  -NoNewWindow `
  -Wait `
  -PassThru `
  -RedirectStandardOutput $stdoutPath `
  -RedirectStandardError $stderrPath

$exitCode = $process.ExitCode

if (Test-Path -LiteralPath $stdoutPath) {
  Get-Content -Path $stdoutPath | Add-Content -Path $logPath -Encoding ascii
  Remove-Item -LiteralPath $stdoutPath -Force
}

if (Test-Path -LiteralPath $stderrPath) {
  Get-Content -Path $stderrPath | Add-Content -Path $logPath -Encoding ascii
  Remove-Item -LiteralPath $stderrPath -Force
}

"Finished: $(Get-Date -Format o)" | Add-Content -Path $logPath -Encoding ascii
"ExitCode: $exitCode" | Add-Content -Path $logPath -Encoding ascii

if ($exitCode -ne 0) {
  throw "RenderScene exited with code $exitCode. See log: $logPath"
}

$captureDeadline = (Get-Date).AddSeconds(30)
$captures = @()
while ((Get-Date) -lt $captureDeadline) {
  $captures = @(Get-NewOrUpdatedCaptures `
    -Directory $captureOutputDirectory `
    -Filter $captureFilter `
    -Before $captureSnapshotBefore)

  if ($captures.Count -ge $Count) {
    break
  }

  Start-Sleep -Milliseconds 250
}

if ($captures.Count -ne $Count) {
  throw "Expected $Count capture(s), found $($captures.Count). See log: $logPath"
}

foreach ($capture in $captures) {
  Wait-ForStableFile -Path $capture.FullName
}

Write-Host "RenderScene executable: $renderSceneExe"
Write-Host "Frames requested: start=$Frame count=$Count total_run_frames=$runFrames"
Write-Host "Capture output stem: $captureOutputTemplate"
Write-Host "Log path: $logPath"
Write-Host "Capture files:"
foreach ($capture in $captures) {
  Write-Host "  $($capture.FullName)"
}
