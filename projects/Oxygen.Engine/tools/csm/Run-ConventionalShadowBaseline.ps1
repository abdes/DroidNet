<#
.SYNOPSIS
Captures a steady-state conventional-shadow baseline from RenderScene.

.DESCRIPTION
Launches the RenderScene example in conventional-shadow mode, requests a
RenderDoc frame capture, and writes the capture plus benchmark logs under the
requested output stem.
#>
[CmdletBinding()]
param(
  [Parameter()]
  [Alias('CaptureFromFrame')]
  [ValidateRange(0, [int]::MaxValue)]
  [int]$Frame = 256,

  [Parameter()]
  [Alias('CaptureFrameCount')]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Count = 1,

  [Parameter()]
  [Alias('CaptureOutputTemplate')]
  [string]$Output = '',

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$RunFrames = 320,

  [Parameter()]
  [ValidateRange(1, [int]::MaxValue)]
  [int]$Fps = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\RenderSceneBenchmarkCommon.ps1')

function Get-BenchmarkSettingsSummary {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $settings = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  $activeScene = $settings.content.active_scene
  $env = $settings.env
  $envSettings = $env.settings
  $sun = $env.sun

  return [pscustomobject]@{
    ActiveSceneName = $activeScene.name
    ActiveSceneKey = $activeScene.key
    SunSource = $sun.source
    CustomStatePresent = $envSettings.custom_state_present
    EnvironmentPresetIndex = $settings.environment_preset_index
  }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$renderSceneExe = Join-Path $buildRoot 'bin\Release\Oxygen.Examples.RenderScene.exe'
$settingsPath = Join-Path $repoRoot 'Examples\RenderScene\demo_settings.json'
$baselineSettingsPath = Join-Path $PSScriptRoot 'demo_settings.json'
$settingsBackupPath = Join-Path (Split-Path -Parent $settingsPath) 'demo_settings.sav'

if (-not (Test-Path -LiteralPath $renderSceneExe)) {
  throw "RenderScene Release executable not found: $renderSceneExe"
}
if (-not (Test-Path -LiteralPath $settingsPath)) {
  throw "RenderScene demo settings not found: $settingsPath"
}
if (-not (Test-Path -LiteralPath $baselineSettingsPath)) {
  throw "CSM baseline demo settings not found: $baselineSettingsPath"
}
if (Test-Path -LiteralPath $settingsBackupPath) {
  throw "Refusing to overwrite existing settings backup: $settingsBackupPath"
}

$minimumRunFrames = $Frame + $Count + 120
if ($RunFrames -lt $minimumRunFrames) {
  $RunFrames = $minimumRunFrames
}

if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $buildRoot 'analysis\csm\baseline_capture\release_frame256_conventional'
}

$captureOutputTemplate = [System.IO.Path]::GetFullPath($Output)
$captureOutputDirectory = Split-Path -Parent $captureOutputTemplate
$captureOutputStem = Split-Path -Leaf $captureOutputTemplate
$captureFilter = "$captureOutputStem*.rdc"
$benchmarkLogPath = "$captureOutputTemplate.benchmark.log"
$stdoutPath = "$captureOutputTemplate.stdout.log"
$stderrPath = "$captureOutputTemplate.stderr.log"

New-Item -ItemType Directory -Force -Path $captureOutputDirectory | Out-Null
$captureSnapshotBefore = Get-CaptureSnapshot -Directory $captureOutputDirectory -Filter $captureFilter
$liveSettingsHashBefore = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash

try {
  Move-Item -LiteralPath $settingsPath -Destination $settingsBackupPath
  Copy-Item -LiteralPath $baselineSettingsPath -Destination $settingsPath

  $appliedSettings = Get-BenchmarkSettingsSummary -Path $settingsPath
  $baselineSettingsHash = (Get-FileHash -LiteralPath $baselineSettingsPath -Algorithm SHA256).Hash
  $appliedSettingsHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash

  if ($baselineSettingsHash -ne $appliedSettingsHash) {
    throw "Applied benchmark demo settings hash does not match baseline settings: $baselineSettingsPath"
  }

  @(
    "Started: $(Get-Date -Format o)"
    "RenderScene executable: $renderSceneExe"
    "Target FPS: $Fps"
    "Frames requested: start=$Frame count=$Count total_run_frames=$RunFrames"
    "Capture output stem: $captureOutputTemplate"
    "Live settings path: $settingsPath"
    "Saved live settings backup: $settingsBackupPath"
    "Baseline settings path: $baselineSettingsPath"
    "Original live settings SHA256: $liveSettingsHashBefore"
    "Baseline settings SHA256: $baselineSettingsHash"
    "Applied settings SHA256: $appliedSettingsHash"
    "Active scene key: $($appliedSettings.ActiveSceneKey)"
    "Active scene name: $($appliedSettings.ActiveSceneName)"
    "Benchmark sun source: $($appliedSettings.SunSource)"
    "Environment preset index: $($appliedSettings.EnvironmentPresetIndex)"
  ) | Set-Content -LiteralPath $benchmarkLogPath -Encoding ascii

  if (Test-Path -LiteralPath $stdoutPath) {
    Remove-Item -LiteralPath $stdoutPath -Force
  }
  if (Test-Path -LiteralPath $stderrPath) {
    Remove-Item -LiteralPath $stderrPath -Force
  }

  $renderSceneArgs = @(
    '-v=0'
    '--frames', $RunFrames
    '--fps', $Fps
    '--vsync', 'false'
    '--directional-shadows', 'conventional'
    '--capture-provider', 'renderdoc'
    '--capture-load', 'search'
    '--capture-output', $captureOutputTemplate
    '--capture-from-frame', $Frame
    '--capture-frame-count', $Count
  )

  "Command: `"$renderSceneExe`" $($renderSceneArgs -join ' ')" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii

  $process = Start-Process `
    -FilePath $renderSceneExe `
    -ArgumentList $renderSceneArgs `
    -WorkingDirectory $repoRoot `
    -NoNewWindow `
    -Wait `
    -PassThru `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath

  if (Test-Path -LiteralPath $stdoutPath) {
    Get-Content -LiteralPath $stdoutPath |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  if (Test-Path -LiteralPath $stderrPath) {
    Get-Content -LiteralPath $stderrPath |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }

  "Finished: $(Get-Date -Format o)" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  "ExitCode: $($process.ExitCode)" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii

  if ($process.ExitCode -ne 0) {
    throw "RenderScene exited with code $($process.ExitCode). See log: $benchmarkLogPath"
  }

  $sceneBuilds = @(Select-String -Path $stderrPath -Pattern 'RenderScene: Scene build staged successfully')
  $benchmarkWindowStart = 0
  if ($sceneBuilds.Count -gt 0) {
    $benchmarkWindowStart = $sceneBuilds[-1].LineNumber
  }

  $positiveSelection = @(Select-String -Path $stderrPath -Pattern 'activated scene .* selected scene directional .* as resolved primary sun' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })
  $activeSceneSun = @(Select-String -Path $stderrPath -Pattern 'using scene directional .* as sun \(source=scene, casts_shadows=true, environment_contribution=true\)' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })
  $sceneSummary = @(Select-String -Path $stderrPath -Pattern 'SceneLoader: Scene summary: .*directional_lights=1')
  $syntheticFallback = @(Select-String -Path $stderrPath -Pattern 'selected synthetic sun fallback' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })
  $syntheticOverride = @(Select-String -Path $stderrPath -Pattern 'using synthetic sun node ' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })

  if ($positiveSelection.Count -eq 0) {
    throw "Benchmark run did not log a positive scene-sun selection. See log: $benchmarkLogPath"
  }
  if ($activeSceneSun.Count -eq 0) {
    throw "Benchmark run did not log an active scene sun with shadow casting enabled. See log: $benchmarkLogPath"
  }
  if ($sceneSummary.Count -eq 0) {
    throw "Benchmark run did not log a scene summary with one directional light. See log: $benchmarkLogPath"
  }
  if ($syntheticFallback.Count -ne 0) {
    throw "Benchmark run unexpectedly activated a synthetic sun fallback. See log: $benchmarkLogPath"
  }
  if ($syntheticOverride.Count -ne 0) {
    throw "Benchmark run unexpectedly used a synthetic sun override. See log: $benchmarkLogPath"
  }

  $summary = Get-LastDirectionalSummary -Path $stderrPath
  $hasDirectionalSummary = $null -ne $summary `
    -and $summary.PSObject.Properties.Match('Line').Count -gt 0
  if ($hasDirectionalSummary -and ($summary.Total -ne 1 -or $summary.ShadowedTotal -ne 1 -or $summary.ShadowedSun -ne 1)) {
    throw "Benchmark run ended with invalid directional light summary: $($summary.Line)"
  }

  "Validated scene-sun selection log: $($positiveSelection[-1].Line.Trim())" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  "Validated active scene-sun log: $($activeSceneSun[-1].Line.Trim())" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  "Validated scene light summary: $($sceneSummary[-1].Line.Trim())" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  if ($hasDirectionalSummary) {
    "Validated directional summary: $($summary.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  $lastSceneBuild = Get-LastSceneBuild -Path $stderrPath
  $hasLastSceneBuild = $null -ne $lastSceneBuild `
    -and $lastSceneBuild.PSObject.Properties.Match('Line').Count -gt 0 `
    -and $lastSceneBuild.PSObject.Properties.Match('LineNumber').Count -gt 0
  if ($hasLastSceneBuild) {
    "Benchmark scene build: $($lastSceneBuild.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  if ($benchmarkWindowStart -gt 0) {
    "Benchmark validation window starts at scene-build line: $benchmarkWindowStart" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  $lastRepoint = Get-LastTextureRepoint -Path $stderrPath
  $hasLastRepoint = $null -ne $lastRepoint `
    -and $lastRepoint.PSObject.Properties.Match('Line').Count -gt 0 `
    -and $lastRepoint.PSObject.Properties.Match('LineNumber').Count -gt 0
  if ($hasLastRepoint) {
    "Last texture repoint observed: $($lastRepoint.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  $captureRequest = Get-CaptureRequest -Path $stderrPath -Frame $Frame
  $hasCaptureRequest = $null -ne $captureRequest `
    -and $captureRequest.PSObject.Properties.Match('Line').Count -gt 0 `
    -and $captureRequest.PSObject.Properties.Match('LineNumber').Count -gt 0
  if ($hasCaptureRequest) {
    "Capture request observed: $($captureRequest.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  if ($hasLastSceneBuild -and $hasLastRepoint) {
    $sceneBuildFrame = Get-FrameMarkerAfterLine -Path $stderrPath -LineNumber $lastSceneBuild.LineNumber
    $lastRepointFrame = Get-FrameMarkerAfterLine -Path $stderrPath -LineNumber $lastRepoint.LineNumber
    if ($null -ne $sceneBuildFrame) {
      "Benchmark scene build frame: $($sceneBuildFrame.Frame)" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
    if ($null -ne $lastRepointFrame) {
      "Last texture repoint frame: $($lastRepointFrame.Frame)" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
    if ($null -ne $sceneBuildFrame) {
      "Capture stabilization after scene build: $($Frame - $sceneBuildFrame.Frame) frame(s)" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
    if ($null -ne $lastRepointFrame) {
      "Capture stabilization after last repoint: $($Frame - $lastRepointFrame.Frame) frame(s)" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
  }
  if ($hasLastSceneBuild -and $hasCaptureRequest) {
    $sceneBuildTime = Get-LogTimeOfDay -Line $lastSceneBuild.Line
    $captureRequestTime = Get-LogTimeOfDay -Line $captureRequest.Line
    if ($null -ne $sceneBuildTime -and $null -ne $captureRequestTime) {
      $delta = $captureRequestTime - $sceneBuildTime
      "Capture stabilization after scene build: $([math]::Round($delta.TotalSeconds, 3)) s" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
  }
  if ($hasLastRepoint -and $hasCaptureRequest) {
    $lastRepointTime = Get-LogTimeOfDay -Line $lastRepoint.Line
    $captureRequestTime = Get-LogTimeOfDay -Line $captureRequest.Line
    if ($null -ne $lastRepointTime -and $null -ne $captureRequestTime) {
      $delta = $captureRequestTime - $lastRepointTime
      "Capture stabilization after last repoint: $([math]::Round($delta.TotalSeconds, 3)) s" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
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
    throw "Expected $Count capture(s), found $($captures.Count). See log: $benchmarkLogPath"
  }

  foreach ($capture in $captures) {
    Wait-ForStableFile -Path $capture.FullName
  }

  Write-Host "RenderScene executable: $renderSceneExe"
  Write-Host "Target FPS: $Fps"
  Write-Host "Frames requested: start=$Frame count=$Count total_run_frames=$RunFrames"
  Write-Host "Settings restore file: $settingsBackupPath"
  Write-Host "Benchmark log: $benchmarkLogPath"
  if ($hasDirectionalSummary) {
    Write-Host "Directional summary: $($summary.Line)"
  }
  Write-Host "Capture files:"
  foreach ($capture in $captures) {
    Write-Host "  $($capture.FullName)"
  }
}
finally {
  if (Test-Path -LiteralPath $settingsBackupPath) {
    if (Test-Path -LiteralPath $settingsPath) {
      Remove-Item -LiteralPath $settingsPath -Force
    }

    Move-Item -LiteralPath $settingsBackupPath -Destination $settingsPath -Force

    $restoredSettingsHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash
    if ($restoredSettingsHash -ne $liveSettingsHashBefore) {
      throw "Restored demo settings hash mismatch after benchmark run. expected=$liveSettingsHashBefore actual=$restoredSettingsHash"
    }

    if (Test-Path -LiteralPath $benchmarkLogPath) {
      "Restored demo settings: $settingsPath" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
      "Restored demo settings SHA256: $restoredSettingsHash" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
  }
}
