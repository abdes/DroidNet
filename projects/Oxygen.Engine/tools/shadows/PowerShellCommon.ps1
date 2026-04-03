Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-RepoPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $normalizedPath = $Path
  if ($null -eq $normalizedPath) {
    throw "Resolve-RepoPath received a null path."
  }

  $normalizedPath = $normalizedPath.Trim()
  $normalizedPath = $normalizedPath -replace "[\r\n]+", ''
  $normalizedPath = $normalizedPath.Trim('"''')

  if ([string]::IsNullOrWhiteSpace($normalizedPath)) {
    throw "Resolve-RepoPath received an empty path after normalization."
  }

  if ([System.IO.Path]::IsPathRooted($normalizedPath)) {
    return [System.IO.Path]::GetFullPath($normalizedPath)
  }

  return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $normalizedPath))
}

function Invoke-PythonCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptPath,

    [Parameter(Mandatory = $true)]
    [string[]]$Arguments
  )

  & python $ScriptPath @Arguments
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    throw "python exited with code $exitCode while running $ScriptPath"
  }

  $global:LASTEXITCODE = 0
}

function Assert-SingleNumericReportValue {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [Parameter(Mandatory = $true)]
    [string]$Pattern,

    [Parameter(Mandatory = $true)]
    [string]$Label
  )

  $lines = @(Select-String -Path $ReportPath -Pattern $Pattern)
  if ($lines.Count -ne 1) {
    throw "$Label did not contain exactly one matching numeric entry: $ReportPath"
  }

  [void][double]::Parse(
    $lines[0].Matches[0].Groups[1].Value,
    [System.Globalization.CultureInfo]::InvariantCulture
  )
}

function Invoke-StableTimingAnalysis {
  param(
    [Parameter(Mandatory = $true)]
    [string]$InvokeScript,

    [Parameter(Mandatory = $true)]
    [string]$CapturePath,

    [Parameter(Mandatory = $true)]
    [string]$UiScriptPath,

    [Parameter(Mandatory = $true)]
    [string]$PassName,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [Parameter()]
    [string]$SuccessPattern = '^authoritative_scope_gpu_duration_ms=(.+)$',

    [Parameter()]
    [string]$Label = 'Timing report'
  )

  & $InvokeScript `
    -CapturePath $CapturePath `
    -UiScriptPath $UiScriptPath `
    -PassName $PassName `
    -ReportPath $ReportPath

  Assert-SingleNumericReportValue `
    -ReportPath $ReportPath `
    -Pattern $SuccessPattern `
    -Label $Label
}
