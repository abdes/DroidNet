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

function Get-LastDirectionalSummary {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $matches = @(
    Select-String -Path $Path -Pattern 'directional light summary total=(\d+).*shadowed_total=(\d+).*shadowed_sun=(\d+)'
  )
  if ($matches.Count -eq 0) {
    return $null
  }

  $last = $matches[-1]
  return [pscustomobject]@{
    Total = [int]$last.Matches[0].Groups[1].Value
    ShadowedTotal = [int]$last.Matches[0].Groups[2].Value
    ShadowedSun = [int]$last.Matches[0].Groups[3].Value
    Line = $last.Line.Trim()
  }
}

function Get-LastTextureRepoint {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $matches = @(Select-String -Path $Path -Pattern 'TextureBinder\.cpp:1112\s+\|\s+Repointed descriptor')
  if ($matches.Count -eq 0) {
    return $null
  }

  $last = $matches[-1]
  return [pscustomobject]@{
    LineNumber = $last.LineNumber
    Line = $last.Line.Trim()
  }
}

function Get-LastSceneBuild {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $matches = @(Select-String -Path $Path -Pattern 'RenderScene: Scene build staged successfully')
  if ($matches.Count -eq 0) {
    return $null
  }

  $last = $matches[-1]
  return [pscustomobject]@{
    LineNumber = $last.LineNumber
    Line = $last.Line.Trim()
  }
}

function Get-CaptureRequest {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [int]$Frame
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $pattern = "RenderDoc configured frame capture requested for frame $Frame"
  $matches = @(Select-String -Path $Path -Pattern $pattern)
  if ($matches.Count -eq 0) {
    return $null
  }

  $last = $matches[-1]
  return [pscustomobject]@{
    LineNumber = $last.LineNumber
    Line = $last.Line.Trim()
  }
}

function Get-LogTimeOfDay {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Line
  )

  $match = [regex]::Match($Line, '^(?<time>\d{2}:\d{2}:\d{2}\.\d{3})')
  if (-not $match.Success) {
    return $null
  }

  return [timespan]::ParseExact($match.Groups['time'].Value, 'hh\:mm\:ss\.fff', $null)
}

function Get-FrameMarkerAfterLine {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [int]$LineNumber
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $matches = @(Select-String -Path $Path -Pattern 'Renderer: frame=Frame\(seq:(\d+)\)')
  foreach ($match in $matches) {
    if ($match.LineNumber -gt $LineNumber) {
      return [pscustomobject]@{
        LineNumber = $match.LineNumber
        Frame = [int]$match.Matches[0].Groups[1].Value
        Line = $match.Line.Trim()
      }
    }
  }

  return $null
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repoRoot 'out\build-ninja'
$renderSceneExe = Join-Path $buildRoot 'bin\Release\Oxygen.Examples.RenderScene.exe'
$settingsPath = Join-Path $repoRoot 'Examples\RenderScene\demo_settings.json'
$benchmarkSettingsPath = Join-Path $repoRoot 'Examples\RenderScene\demo_settings.benchmark.json'
$settingsBackupPath = Join-Path (Split-Path -Parent $settingsPath) 'demo_settings.sav'

if (-not (Test-Path -LiteralPath $renderSceneExe)) {
  throw "RenderScene Release executable not found: $renderSceneExe"
}
if (-not (Test-Path -LiteralPath $settingsPath)) {
  throw "RenderScene demo settings not found: $settingsPath"
}
if (-not (Test-Path -LiteralPath $benchmarkSettingsPath)) {
  throw "RenderScene benchmark demo settings not found: $benchmarkSettingsPath"
}
if (Test-Path -LiteralPath $settingsBackupPath) {
  throw "Refusing to overwrite existing settings backup: $settingsBackupPath"
}

$minimumRunFrames = $Frame + $Count + 120
if ($RunFrames -lt $minimumRunFrames) {
  $RunFrames = $minimumRunFrames
}

if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $buildRoot 'analysis\conventional_shadow_sponza_baseline\release_frame256_conventional'
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
  Copy-Item -LiteralPath $benchmarkSettingsPath -Destination $settingsPath

  $appliedSettings = Get-BenchmarkSettingsSummary -Path $settingsPath
  $benchmarkSettingsHash = (Get-FileHash -LiteralPath $benchmarkSettingsPath -Algorithm SHA256).Hash
  $appliedSettingsHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash

  if ($benchmarkSettingsHash -ne $appliedSettingsHash) {
    throw "Applied benchmark demo settings hash does not match saved benchmark settings: $benchmarkSettingsPath"
  }

  @(
    "Started: $(Get-Date -Format o)"
    "RenderScene executable: $renderSceneExe"
    "Target FPS: $Fps"
    "Frames requested: start=$Frame count=$Count total_run_frames=$RunFrames"
    "Capture output stem: $captureOutputTemplate"
    "Live settings path: $settingsPath"
    "Saved live settings backup: $settingsBackupPath"
    "Benchmark settings path: $benchmarkSettingsPath"
    "Original live settings SHA256: $liveSettingsHashBefore"
    "Benchmark settings SHA256: $benchmarkSettingsHash"
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

  $positiveSelection = @(Select-String -Path $stderrPath -Pattern 'activated scene .* selected scene directional .* as sun via ' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })
  $negativeSynthetic = @(Select-String -Path $stderrPath -Pattern 'activation rejected synthetic sun for scene ' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })
  $syntheticFallback = @(Select-String -Path $stderrPath -Pattern 'selected synthetic sun fallback' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })
  $syntheticOverride = @(Select-String -Path $stderrPath -Pattern 'using synthetic sun node ' |
    Where-Object { $_.LineNumber -gt $benchmarkWindowStart })

  if ($positiveSelection.Count -eq 0) {
    throw "Benchmark run did not log a positive scene-sun selection. See log: $benchmarkLogPath"
  }
  if ($negativeSynthetic.Count -eq 0) {
    throw "Benchmark run did not log synthetic rejection for the authoritative scene sun. See log: $benchmarkLogPath"
  }
  if ($syntheticFallback.Count -ne 0) {
    throw "Benchmark run unexpectedly activated a synthetic sun fallback. See log: $benchmarkLogPath"
  }
  if ($syntheticOverride.Count -ne 0) {
    throw "Benchmark run unexpectedly used a synthetic sun override. See log: $benchmarkLogPath"
  }

  $summary = Get-LastDirectionalSummary -Path $stderrPath
  if ($null -eq $summary) {
    throw "Benchmark run did not emit a directional light summary. See log: $benchmarkLogPath"
  }
  if ($summary.Total -ne 1 -or $summary.ShadowedTotal -ne 1 -or $summary.ShadowedSun -ne 1) {
    throw "Benchmark run ended with invalid directional light summary: $($summary.Line)"
  }

  "Validated scene-sun selection log: $($positiveSelection[-1].Line.Trim())" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  "Validated synthetic rejection log: $($negativeSynthetic[-1].Line.Trim())" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  "Validated directional summary: $($summary.Line)" |
    Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  $lastSceneBuild = Get-LastSceneBuild -Path $stderrPath
  if ($null -ne $lastSceneBuild) {
    "Benchmark scene build: $($lastSceneBuild.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  if ($benchmarkWindowStart -gt 0) {
    "Benchmark validation window starts at scene-build line: $benchmarkWindowStart" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  $lastRepoint = Get-LastTextureRepoint -Path $stderrPath
  if ($null -ne $lastRepoint) {
    "Last texture repoint observed: $($lastRepoint.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  $captureRequest = Get-CaptureRequest -Path $stderrPath -Frame $Frame
  if ($null -ne $captureRequest) {
    "Capture request observed: $($captureRequest.Line)" |
      Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
  }
  if ($null -ne $lastSceneBuild -and $null -ne $lastRepoint) {
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
  if ($null -ne $lastSceneBuild -and $null -ne $captureRequest) {
    $sceneBuildTime = Get-LogTimeOfDay -Line $lastSceneBuild.Line
    $captureRequestTime = Get-LogTimeOfDay -Line $captureRequest.Line
    if ($null -ne $sceneBuildTime -and $null -ne $captureRequestTime) {
      $delta = $captureRequestTime - $sceneBuildTime
      "Capture stabilization after scene build: $([math]::Round($delta.TotalSeconds, 3)) s" |
        Add-Content -LiteralPath $benchmarkLogPath -Encoding ascii
    }
  }
  if ($null -ne $lastRepoint -and $null -ne $captureRequest) {
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
  Write-Host "Directional summary: $($summary.Line)"
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
