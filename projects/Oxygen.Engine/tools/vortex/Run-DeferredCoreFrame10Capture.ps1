<#
.SYNOPSIS
Collects the Phase 03 deferred-core closeout proof inputs.

.DESCRIPTION
Phase 03 now closes with source/test/log-backed proof. RenderDoc runtime
validation is deferred until Phase 04 migrates Async and DemoShell to Vortex.
This script gathers the current closeout inputs and writes a manifest consumed
by Analyze-DeferredCoreCapture.ps1.
#>
[CmdletBinding()]
param(
  [Parameter()]
  [string]$Output = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

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
  $nativeCommandPreferenceAvailable = $null -ne (Get-Variable `
    -Name PSNativeCommandUseErrorActionPreference `
    -ErrorAction SilentlyContinue)
  $previousNativeCommandPreference = $false

  try {
    Push-Location $WorkingDirectory
    try {
      if ($nativeCommandPreferenceAvailable) {
        $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
      }
      & $FilePath @ArgumentList 1> $stdoutPath 2> $stderrPath
      $exitCode = $LASTEXITCODE
    } finally {
      if ($nativeCommandPreferenceAvailable) {
        $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
      }
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
    FilePath = $FilePath
    Arguments = @($ArgumentList)
    CommandLine = "`"$FilePath`" $commandText"
    WorkingDirectory = $WorkingDirectory
    LogPath = $LogPath
    ExitCode = $exitCode
    Started = $started
  }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $repoRoot 'out\build-ninja\analysis\vortex\deferred-core\frame10'
}

$outputDirectory = Resolve-RepoPath -RepoRoot $repoRoot -Path $Output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$buildLogPath = Join-Path $outputDirectory 'deferred-core-build.log'
$testLogPath = Join-Path $outputDirectory 'deferred-core-tests.log'
$tidyLogPath = Join-Path $outputDirectory 'deferred-core-tidy.log'
$inputsPath = Join-Path $outputDirectory 'deferred-core-frame10.inputs.json'

$buildResult = Invoke-LoggedCommand `
  -FilePath 'cmake' `
  -ArgumentList @(
    '--build',
    '--preset',
    'windows-debug',
    '--target',
    'oxygen-vortex',
    'oxygen-graphics-direct3d12',
    'Oxygen.Vortex.SceneRendererDeferredCore.Tests',
    'Oxygen.Vortex.SceneRendererPublication.Tests',
    '--parallel',
    '4'
  ) `
  -LogPath $buildLogPath `
  -WorkingDirectory $repoRoot

$testResult = Invoke-LoggedCommand `
  -FilePath 'ctest' `
  -ArgumentList @(
    '--test-dir',
    'out/build-ninja',
    '-C',
    'Debug',
    '--output-on-failure',
    '-R',
    '^Oxygen\.Vortex\.'
  ) `
  -LogPath $testLogPath `
  -WorkingDirectory $repoRoot

$tidyTargets = @(
  'src/Oxygen/Vortex/RenderContext.h',
  'src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h',
  'src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp',
  'src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h',
  'src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp',
  'src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h',
  'src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp',
  'src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h',
  'src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.cpp',
  'src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h',
  'src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp',
  'src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h',
  'src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp',
  'src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp',
  'src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp'
)

$tidyArguments = @(
  '-NoProfile',
  '-ExecutionPolicy',
  'Bypass',
  '-File',
  (Join-Path $repoRoot 'tools\cli\oxytidy.ps1')
)
$tidyArguments += $tidyTargets
$tidyArguments += @(
  '-IncludeTests',
  '-Configuration',
  'Debug',
  '-SummaryOnly'
)

$tidyResult = Invoke-LoggedCommand `
  -FilePath 'powershell' `
  -ArgumentList $tidyArguments `
  -LogPath $tidyLogPath `
  -WorkingDirectory $repoRoot

$manifest = [ordered]@{
  phase = '03-deferred-core'
  plan = '15'
  generated_at = (Get-Date -Format o)
  repo_root = $repoRoot
  renderdoc_runtime_validation = [ordered]@{
    deferred = $true
    deferred_to_phase = '04'
    reason = 'RenderDoc runtime validation is deferred until Async and DemoShell migrate to Vortex.'
  }
  steps = [ordered]@{
    build = [ordered]@{
      command = $buildResult.CommandLine
      exit_code = $buildResult.ExitCode
      log_path = $buildResult.LogPath
    }
    tests = [ordered]@{
      command = $testResult.CommandLine
      exit_code = $testResult.ExitCode
      log_path = $testResult.LogPath
    }
    tidy = [ordered]@{
      command = $tidyResult.CommandLine
      exit_code = $tidyResult.ExitCode
      log_path = $tidyResult.LogPath
    }
  }
  files = [ordered]@{
    scene_renderer = 'src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp'
    scene_textures = 'src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp'
    deferred_core_tests = 'src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp'
    publication_tests = 'src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp'
    validation_doc = '.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md'
    phase_plan = 'design/vortex/PLAN.md'
    implementation_status = 'design/vortex/IMPLEMENTATION-STATUS.md'
  }
}

$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $inputsPath -Encoding utf8

$global:LASTEXITCODE = 0
Write-Output "Inputs: $inputsPath"
