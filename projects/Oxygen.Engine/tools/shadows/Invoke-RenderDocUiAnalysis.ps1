<#
.SYNOPSIS
Runs one RenderDoc controller analyzer in the background and validates its report.
.DESCRIPTION
Uses the supported --python mode before qrenderdoc's main UI opens. A bootstrap
always signals SystemExit; shared analysis callbacks own pure replay handles.
No Qt/PySide calls or desktop interaction. Existing UiScriptPath callers retain
their controller/report callback contract. Direct manual --ui-python use remains
available; it no longer tries to close the user's UI.

Environment changes apply only to the child. The host returns zero for SystemExit
even on script failure, so successful final report and handle cleanup are required.
Python analyzer stdout/stderr are written directly to child-specific files by
the bootstrap. No output pipes or reader tasks are created. Native host failure
is reported through its exit status. Termination grace is at most five seconds.
Timeout cleanup is never counted as success.
.EXAMPLE
./tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath ./out/frame.rdc -UiScriptPath tools/vortex/DumpRenderDocActions.py -PassName Actions -ReportPath ./out/actions.txt
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$CapturePath,
  [Parameter(Mandatory = $true)][string]$UiScriptPath,
  [Parameter(Mandatory = $true)][string]$PassName,
  [Parameter(Mandatory = $true)][string]$ReportPath,
  [string]$ConfigRoot = '',
  [ValidateRange(1, 3600)][int]$AnalysisTimeoutSeconds = 180,
  [ValidateRange(1, 300)][int]$LockTimeoutSeconds = 10,
  [switch]$SkipLock,
  [string]$LaunchLogPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'PowerShellCommon.ps1')

function Enter-RenderDocAnalysisLock([int]$TimeoutSeconds) {
  $mutex = New-Object System.Threading.Mutex($false, 'Global\Oxygen.Engine.RenderDocUiAnalysis')
  try {
    try { $acquired = $mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds)) }
    catch [System.Threading.AbandonedMutexException] { $acquired = $true }
    if (-not $acquired) { throw "Timed out waiting for the RenderDoc analysis lock after $TimeoutSeconds seconds." }
    return $mutex
  } catch {
    $mutex.Dispose()
    throw
  }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$captureFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath
$analysisScriptPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $UiScriptPath
$reportFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ReportPath
$renderDocExe = 'C:\Program Files\RenderDoc\qrenderdoc.exe'
$bootstrapPath = Join-Path $PSScriptRoot 'RenderDocAnalysisBootstrap.py'
if (-not $LaunchLogPath) { $LaunchLogPath = "$reportFullPath.launch.log" }
$launchLog = Resolve-RepoPath -RepoRoot $repoRoot -Path $LaunchLogPath
foreach ($file in @($renderDocExe, $captureFullPath, $analysisScriptPath, $bootstrapPath)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file not found: $file" }
}
$protectedPaths = @($captureFullPath, $analysisScriptPath, $bootstrapPath, $renderDocExe)
$evidencePaths = @($reportFullPath, $launchLog, "$launchLog.stdout.log", "$launchLog.stderr.log")
if (@($evidencePaths | Sort-Object -Unique).Count -ne $evidencePaths.Count) {
  throw 'Report, launch log, stdout log, and stderr log paths must be distinct.'
}
foreach ($destination in $evidencePaths) {
  if ($destination -in $protectedPaths) { throw "Evidence output would replace an input: $destination" }
}
if (-not $ConfigRoot) { $ConfigRoot = Join-Path $repoRoot 'out/build-ninja/analysis/renderdoc-automation-config' }
$configParent = Resolve-RepoPath -RepoRoot $repoRoot -Path $ConfigRoot
$configSession = Join-Path $configParent ([Guid]::NewGuid().ToString('N'))
$appDataPath = Join-Path $configSession 'Roaming'
$localAppDataPath = Join-Path $configSession 'Local'
foreach ($directory in @($appDataPath, $localAppDataPath, (Split-Path -Parent $reportFullPath), (Split-Path -Parent $launchLog))) {
  $null = New-Item -ItemType Directory -Force -Path $directory
}

$analysisLock = $null
$process = $null
$started = $false
$terminationAttempted = $false
try {
  if (-not $SkipLock) { $analysisLock = Enter-RenderDocAnalysisLock $LockTimeoutSeconds }
  if (Test-Path -LiteralPath $reportFullPath) { Remove-Item -LiteralPath $reportFullPath -Force }
  $launchStartedUtc = [DateTime]::UtcNow
  @(
    "launch_started_utc=$($launchStartedUtc.ToString('o'))"
    'execution_mode=replay'
    "renderdoc_exe=$renderDocExe"
    "bootstrap=$bootstrapPath"
    "analysis_script=$analysisScriptPath"
    "capture=$captureFullPath"
    "report=$reportFullPath"
    "config_session=$configSession"
    "timeout_seconds=$AnalysisTimeoutSeconds"
  ) | Set-Content -LiteralPath $launchLog -Encoding utf8

  $startInfo = New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName = $renderDocExe
  # The only path argument is an existing .py file: it cannot contain a quote
  # or end in a backslash. Quote it for spaces without invoking a shell. This
  # also supports callers using Windows PowerShell/.NET Framework.
  $startInfo.Arguments = '--python "' + $bootstrapPath + '"'
  $startInfo.WorkingDirectory = $repoRoot
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  $startInfo.EnvironmentVariables['APPDATA'] = $appDataPath
  $startInfo.EnvironmentVariables['LOCALAPPDATA'] = $localAppDataPath
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_AUTOMATION_MODE'] = 'replay'
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_SCRIPT_PATH'] = $analysisScriptPath
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_CAPTURE_PATH'] = $captureFullPath
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_REPORT_PATH'] = $reportFullPath
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_PASS_NAME'] = $PassName
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_STDOUT_PATH'] = "$launchLog.stdout.log"
  $startInfo.EnvironmentVariables['OXYGEN_RENDERDOC_STDERR_PATH'] = "$launchLog.stderr.log"
  $process = New-Object System.Diagnostics.Process
  $process.StartInfo = $startInfo
  $started = $process.Start()
  if (-not $started) { throw 'Could not start RenderDoc replay host.' }
  "spawned_pid=$($process.Id)" | Add-Content -LiteralPath $launchLog -Encoding utf8
  $completed = $process.WaitForExit($AnalysisTimeoutSeconds * 1000)
  if (-not $completed) {
    'timed_out=true' | Add-Content -LiteralPath $launchLog -Encoding utf8
    $terminationAttempted = $true
    try { $process.Kill() } catch {
      "kill_error=$($_.Exception.Message)" | Add-Content -LiteralPath $launchLog -Encoding utf8
    }
    $terminated = $process.WaitForExit(5000)
    "exited_after_kill=$terminated" | Add-Content -LiteralPath $launchLog -Encoding utf8
  }
  if ($process.HasExited) {
    "final_exit_code=$($process.ExitCode)" | Add-Content -LiteralPath $launchLog -Encoding utf8
  }
  if (-not $completed) { throw "RenderDoc replay timed out after $AnalysisTimeoutSeconds seconds; no successful completion." }
  if ($process.ExitCode -ne 0) { throw "RenderDoc replay host exited $($process.ExitCode). See $launchLog" }
  if (-not (Test-Path -LiteralPath $reportFullPath -PathType Leaf)) { throw "Analyzer produced no report: $reportFullPath" }
  $reportItem = Get-Item -LiteralPath $reportFullPath
  if ($reportItem.Length -le 0 -or $reportItem.LastWriteTimeUtc -lt $launchStartedUtc) {
    throw "Analyzer report is empty or stale: $reportFullPath"
  }
  $lines = @(Get-Content -LiteralPath $reportFullPath)
  if (@($lines | Where-Object { $_ -eq 'analysis_result=success' }).Count -ne 1 -or
      $lines -contains 'analysis_result=exception' -or
      $lines -notcontains 'execution_mode=replay' -or
      $lines -notcontains 'replay_handles_shutdown=true' -or
      @($lines | Where-Object { $_ -match '^error=' }).Count -gt 0) {
    $preview = ($lines | Select-Object -First 40) -join [Environment]::NewLine
    throw "Analyzer did not complete successfully with replay handles closed:`n$preview"
  }
} finally {
  if ($process) {
    if ($started -and -not $process.HasExited -and -not $terminationAttempted) {
      $terminationAttempted = $true
      try { $process.Kill() } catch {
        "cleanup_kill_error=$($_.Exception.Message)" | Add-Content -LiteralPath $launchLog -Encoding utf8
      }
      $terminated = $process.WaitForExit(5000)
      "cleanup_process_exited=$terminated" | Add-Content -LiteralPath $launchLog -Encoding utf8
    }
    $process.Dispose()
  }
  if ($analysisLock) {
    try { $analysisLock.ReleaseMutex() } finally { $analysisLock.Dispose() }
  }
}
$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
