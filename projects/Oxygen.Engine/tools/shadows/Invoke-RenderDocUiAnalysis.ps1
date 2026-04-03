<#
.SYNOPSIS
Runs one RenderDoc UI analysis script against a capture and validates its report.

.DESCRIPTION
Launches qrenderdoc with a UI-python script, serializes concurrent analysis
through a process-wide mutex, and fails if the generated report is missing,
stale, empty, or reports an exception.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter(Mandatory = $true)]
  [string]$UiScriptPath,

  [Parameter(Mandatory = $true)]
  [string]$PassName,

  [Parameter(Mandatory = $true)]
  [string]$ReportPath,

  [Parameter()]
  [string]$ConfigRoot = '',

  [Parameter()]
  [ValidateRange(1, 3600)]
  [int]$AnalysisTimeoutSeconds = 180,

  [Parameter()]
  [ValidateRange(1, 300)]
  [int]$LockTimeoutSeconds = 10,

  [Parameter()]
  [switch]$SkipLock,

  [Parameter()]
  [string]$LaunchLogPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'PowerShellCommon.ps1')
. (Join-Path $PSScriptRoot 'RenderSceneBenchmarkCommon.ps1')

function Set-ScopedProcessEnvironmentVariable {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$Value,

    [Parameter(Mandatory = $true)]
    [hashtable]$OriginalValues
  )

  if (-not $OriginalValues.ContainsKey($Name)) {
    $OriginalValues[$Name] = [System.Environment]::GetEnvironmentVariable($Name, 'Process')
  }

  [System.Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

function Restore-ScopedProcessEnvironmentVariables {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$OriginalValues
  )

  foreach ($entry in $OriginalValues.GetEnumerator()) {
    [System.Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
  }
}

function Enter-RenderDocAnalysisLock {
  param(
    [Parameter()]
    [int]$TimeoutSeconds = 10
  )

  $mutex = New-Object System.Threading.Mutex($false, 'Global\Oxygen.Engine.RenderDocUiAnalysis')
  $lockAcquired = $false

  try {
    try {
      $lockAcquired = $mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds))
    } catch [System.Threading.AbandonedMutexException] {
      $lockAcquired = $true
    }

    if (-not $lockAcquired) {
      throw "Timed out waiting for the RenderDoc analysis lock after $TimeoutSeconds second(s)."
    }

    return [pscustomobject]@{
      Mutex = $mutex
      LockAcquired = $true
    }
  } catch {
    $mutex.Dispose()
    throw
  }
}

function Exit-RenderDocAnalysisLock {
  param(
    [Parameter(Mandatory = $true)]
    $Lock
  )

  if ($null -eq $Lock) {
    return
  }

  try {
    if ($Lock.LockAcquired) {
      $Lock.Mutex.ReleaseMutex() | Out-Null
    }
  } finally {
    $Lock.Mutex.Dispose()
  }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath
$uiScriptFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $UiScriptPath
$reportFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ReportPath
$renderDocExe = 'C:\Program Files\RenderDoc\qrenderdoc.exe'
if ([string]::IsNullOrWhiteSpace($LaunchLogPath)) {
  $LaunchLogPath = "$reportFullPath.launch.log"
}
$launchLogFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $LaunchLogPath

if (-not (Test-Path -LiteralPath $renderDocExe)) {
  throw "qrenderdoc.exe not found: $renderDocExe"
}
if (-not (Test-Path -LiteralPath $captureFullPath)) {
  throw "Capture not found: $captureFullPath"
}
if (-not (Test-Path -LiteralPath $uiScriptFullPath)) {
  throw "RenderDoc UI script not found: $uiScriptFullPath"
}

if ([string]::IsNullOrWhiteSpace($ConfigRoot)) {
  $ConfigRoot = Join-Path $repoRoot 'out\build-ninja\analysis\csm\renderdoc-automation-config'
}

$configRootFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ConfigRoot
$appDataPath = Join-Path $configRootFullPath 'Roaming'
$localAppDataPath = Join-Path $configRootFullPath 'Local'
$reportDirectory = Split-Path -Parent $reportFullPath
$launchLogDirectory = Split-Path -Parent $launchLogFullPath

New-Item -ItemType Directory -Force -Path $appDataPath | Out-Null
New-Item -ItemType Directory -Force -Path $localAppDataPath | Out-Null
New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $launchLogDirectory | Out-Null

$environmentSnapshot = @{}
$analysisLock = $null

try {
  if (Test-Path -LiteralPath $reportFullPath) {
    Remove-Item -LiteralPath $reportFullPath -Force
  }
  if (Test-Path -LiteralPath $launchLogFullPath) {
    Remove-Item -LiteralPath $launchLogFullPath -Force
  }

  Set-ScopedProcessEnvironmentVariable `
    -Name 'APPDATA' `
    -Value $appDataPath `
    -OriginalValues $environmentSnapshot
  Set-ScopedProcessEnvironmentVariable `
    -Name 'LOCALAPPDATA' `
    -Value $localAppDataPath `
    -OriginalValues $environmentSnapshot
  Set-ScopedProcessEnvironmentVariable `
    -Name 'OXYGEN_RENDERDOC_PASS_NAME' `
    -Value $PassName `
    -OriginalValues $environmentSnapshot
  Set-ScopedProcessEnvironmentVariable `
    -Name 'OXYGEN_RENDERDOC_REPORT_PATH' `
    -Value $reportFullPath `
    -OriginalValues $environmentSnapshot

  $launchStartedUtc = [datetime]::UtcNow
  @(
    "launch_started_utc=$($launchStartedUtc.ToString('o'))"
    "renderdoc_exe=$renderDocExe"
    "ui_script=$uiScriptFullPath"
    "capture=$captureFullPath"
    "report=$reportFullPath"
    "skip_lock=$($SkipLock.IsPresent)"
    "lock_timeout_seconds=$LockTimeoutSeconds"
    "timeout_seconds=$AnalysisTimeoutSeconds"
  ) | Set-Content -LiteralPath $launchLogFullPath -Encoding ascii

  if ($SkipLock) {
    "lock_skipped=true" |
      Add-Content -LiteralPath $launchLogFullPath -Encoding ascii
  } else {
    "waiting_for_lock=true" |
      Add-Content -LiteralPath $launchLogFullPath -Encoding ascii
    $analysisLock = Enter-RenderDocAnalysisLock -TimeoutSeconds $LockTimeoutSeconds
    "lock_acquired=true" |
      Add-Content -LiteralPath $launchLogFullPath -Encoding ascii
  }

  $process = Start-Process `
    -FilePath $renderDocExe `
    -ArgumentList @('--ui-python', $uiScriptFullPath, $captureFullPath) `
    -WorkingDirectory $repoRoot `
    -PassThru

  "spawned_pid=$($process.Id)" |
    Add-Content -LiteralPath $launchLogFullPath -Encoding ascii

  Start-Sleep -Milliseconds 500
  if ($process.HasExited) {
    $process.Refresh()
    "exited_immediately=true exit_code=$($process.ExitCode)" |
      Add-Content -LiteralPath $launchLogFullPath -Encoding ascii
  } else {
    "running_after_spawn=true" |
      Add-Content -LiteralPath $launchLogFullPath -Encoding ascii
  }

  $completed = $true
  try {
    Wait-Process -Id $process.Id -Timeout $AnalysisTimeoutSeconds -ErrorAction Stop
  } catch {
    $completed = $false
  }

  if (-not $completed) {
    "timed_out=true" |
      Add-Content -LiteralPath $launchLogFullPath -Encoding ascii
    try {
      if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      }
    } finally {
      throw "qrenderdoc.exe timed out after $AnalysisTimeoutSeconds second(s) for $uiScriptFullPath"
    }
  }

  $process.Refresh()
  "final_exit_code=$($process.ExitCode)" |
    Add-Content -LiteralPath $launchLogFullPath -Encoding ascii

  if ($process.ExitCode -ne 0) {
    throw "qrenderdoc.exe exited with code $($process.ExitCode) for $uiScriptFullPath"
  }

  if (-not (Test-Path -LiteralPath $reportFullPath)) {
    throw "RenderDoc UI analysis did not produce the expected report: $reportFullPath"
  }

  Wait-ForStableFile -Path $reportFullPath

  $reportItem = Get-Item -LiteralPath $reportFullPath
  if ($reportItem.Length -le 0) {
    throw "RenderDoc UI analysis produced an empty report: $reportFullPath"
  }
  if ($reportItem.LastWriteTimeUtc -lt $launchStartedUtc) {
    throw "RenderDoc UI analysis did not update the report during this launch: $reportFullPath"
  }

  $exceptionLines = @(Select-String -Path $reportFullPath -Pattern '^analysis_result=exception$')
  if ($exceptionLines.Count -gt 0) {
    $reportPreview = (Get-Content -LiteralPath $reportFullPath -TotalCount 40) -join [Environment]::NewLine
    throw "RenderDoc UI analysis reported an exception:`n$reportPreview"
  }

  $successLines = @(Select-String -Path $reportFullPath -Pattern '^analysis_result=success$')
  if ($successLines.Count -ne 1) {
    $reportPreview = (Get-Content -LiteralPath $reportFullPath -TotalCount 40) -join [Environment]::NewLine
    throw "RenderDoc UI analysis did not report explicit success:`n$reportPreview"
  }

  $errorLines = @(Select-String -Path $reportFullPath -Pattern '^error=')
  if ($errorLines.Count -gt 0) {
    $reportPreview = (Get-Content -LiteralPath $reportFullPath -TotalCount 40) -join [Environment]::NewLine
    throw "RenderDoc UI analysis reported an error:`n$reportPreview"
  }
} finally {
  Restore-ScopedProcessEnvironmentVariables -OriginalValues $environmentSnapshot
  if ($null -ne $analysisLock) {
    Exit-RenderDocAnalysisLock -Lock $analysisLock
  }
}

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
