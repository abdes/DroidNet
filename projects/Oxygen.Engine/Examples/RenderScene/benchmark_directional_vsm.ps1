[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$baselineSettingsPath = Join-Path $PSScriptRoot 'demo_settings.directional_vsm_benchmark_baseline.json'
$liveSettingsPath = Join-Path $PSScriptRoot 'demo_settings.json'
$renderSceneExe = Join-Path $repoRoot 'out\build-vs\bin\Debug\Oxygen.Examples.RenderScene.exe'
$archiveDir = Join-Path $repoRoot 'out\build-vs\benchmarks\directional-vsm'
$latestLogPath = Join-Path $repoRoot 'out\build-vs\directional-vsm-benchmark-latest.log'
$latestJsonPath = Join-Path $repoRoot 'out\build-vs\directional-vsm-benchmark-latest.json'

if (-not (Test-Path -LiteralPath $baselineSettingsPath)) {
    throw "Missing benchmark baseline settings file: $baselineSettingsPath"
}
if (-not (Test-Path -LiteralPath $renderSceneExe)) {
    throw "Missing RenderScene executable: $renderSceneExe"
}

New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
Copy-Item -LiteralPath $baselineSettingsPath -Destination $liveSettingsPath -Force

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$archivedLogPath = Join-Path $archiveDir "directional-vsm-benchmark-$timestamp.log"
$args = @(
    '-v', '0',
    '--frames', '120',
    '--fps', '100',
    '--vsync', 'false',
    '--directional-shadows', 'virtual-only'
)

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$exitCode = -1
$previousNativeCommandPreference = $null
$hasNativeCommandPreference = $null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)
if ($hasNativeCommandPreference) {
    $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
}
$previousErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & $renderSceneExe @args *> $archivedLogPath
    $exitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
    if ($hasNativeCommandPreference) {
        $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
    }
    $stopwatch.Stop()
    Copy-Item -LiteralPath $baselineSettingsPath -Destination $liveSettingsPath -Force
}

Copy-Item -LiteralPath $archivedLogPath -Destination $latestLogPath -Force

$result = [ordered]@{
    benchmark_command = "$renderSceneExe $($args -join ' ')"
    restored_settings_from = $baselineSettingsPath
    active_settings = $liveSettingsPath
    archived_log = $archivedLogPath
    latest_log = $latestLogPath
    exit_code = $exitCode
    wall_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 0)
}

$result | ConvertTo-Json | Set-Content -LiteralPath $latestJsonPath -Encoding utf8

if ($exitCode -ne 0) {
    throw "RenderScene benchmark failed with exit code $exitCode"
}

$result | ConvertTo-Json -Compress
