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
  [string]$ConfigRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-RepoPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }

  return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

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

function Acquire-RenderDocAnalysisLock {
  param(
    [Parameter()]
    [int]$TimeoutMinutes = 60
  )

  $mutex = New-Object System.Threading.Mutex($false, 'Global\Oxygen.Engine.RenderDocUiAnalysis')
  $lockAcquired = $false

  try {
    try {
      $lockAcquired = $mutex.WaitOne([TimeSpan]::FromMinutes($TimeoutMinutes))
    } catch [System.Threading.AbandonedMutexException] {
      $lockAcquired = $true
    }

    if (-not $lockAcquired) {
      throw "Timed out waiting for the RenderDoc analysis lock after $TimeoutMinutes minute(s)."
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

function Release-RenderDocAnalysisLock {
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
  $ConfigRoot = Join-Path $repoRoot 'out\build-ninja\analysis\renderdoc-automation-config'
}

$configRootFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ConfigRoot
$appDataPath = Join-Path $configRootFullPath 'Roaming'
$localAppDataPath = Join-Path $configRootFullPath 'Local'
$reportDirectory = Split-Path -Parent $reportFullPath

New-Item -ItemType Directory -Force -Path $appDataPath | Out-Null
New-Item -ItemType Directory -Force -Path $localAppDataPath | Out-Null
New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null

$environmentSnapshot = @{}
$analysisLock = $null

try {
  $analysisLock = Acquire-RenderDocAnalysisLock

  if (Test-Path -LiteralPath $reportFullPath) {
    Remove-Item -LiteralPath $reportFullPath -Force
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
  $process = Start-Process `
    -FilePath $renderDocExe `
    -ArgumentList @('--ui-python', $uiScriptFullPath, $captureFullPath) `
    -WorkingDirectory $repoRoot `
    -Wait `
    -PassThru

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
  Release-RenderDocAnalysisLock -Lock $analysisLock
}

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
