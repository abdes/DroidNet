Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

$script:VortexProofRepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:RenderDocUiAnalysisScript = Join-Path `
  $script:VortexProofRepoRoot `
  'tools\shadows\Invoke-RenderDocUiAnalysis.ps1'

function Get-VortexProofRepoRoot {
  return $script:VortexProofRepoRoot
}

function Invoke-VortexProofStep {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,

    [Parameter()]
    [string[]]$ArgumentList = @(),

    [Parameter(Mandatory = $true)]
    [string]$Label
  )

  & $FilePath @ArgumentList
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    throw "$Label failed with exit code $exitCode."
  }

  $global:LASTEXITCODE = 0
}

function Invoke-VortexPowerShellProofScript {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptPath,

    [Parameter()]
    [string[]]$ArgumentList = @(),

    [Parameter(Mandatory = $true)]
    [string]$Label
  )

  Invoke-VortexProofStep `
    -FilePath 'powershell' `
    -ArgumentList (@('-NoProfile', '-File', $ScriptPath) + $ArgumentList) `
    -Label $Label
}

function Invoke-VortexRenderDocAnalysis {
  param(
    [Parameter(Mandatory = $true)]
    [string]$CapturePath,

    [Parameter(Mandatory = $true)]
    [string]$UiScriptPath,

    [Parameter(Mandatory = $true)]
    [string]$PassName,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath
  )

  Invoke-VortexPowerShellProofScript `
    -ScriptPath $script:RenderDocUiAnalysisScript `
    -ArgumentList @(
      '-CapturePath', $CapturePath,
      '-UiScriptPath', $UiScriptPath,
      '-PassName', $PassName,
      '-ReportPath', $ReportPath
    ) `
    -Label "RenderDoc analysis '$PassName'"
}

function Read-VortexProofReportMap {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $fullPath = (Resolve-Path $Path).Path
  $map = @{}
  foreach ($line in Get-Content -LiteralPath $fullPath) {
    if ($line -notmatch '=') {
      continue
    }

    $split = $line.Split('=', 2)
    if ($split.Count -ne 2) {
      continue
    }

    $map[$split[0]] = $split[1]
  }

  return $map
}

function Get-VortexProofReportValue {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Report,

    [Parameter(Mandatory = $true)]
    [string]$Key,

    [Parameter()]
    [string]$Default = ''
  )

  if ($Report.ContainsKey($Key)) {
    return $Report[$Key]
  }

  return $Default
}

function Assert-VortexProofReportStatus {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [Parameter()]
    [string]$VerdictKey = 'overall_verdict',

    [Parameter()]
    [string]$ExpectedVerdict = 'pass'
  )

  $report = Read-VortexProofReportMap -Path $ReportPath
  if ((Get-VortexProofReportValue -Report $report -Key 'analysis_result') -ne 'success') {
    throw "Report did not declare analysis_result=success: $ReportPath"
  }
  if (-not [string]::IsNullOrWhiteSpace($VerdictKey)) {
    $actualVerdict = Get-VortexProofReportValue -Report $report -Key $VerdictKey
    if ($actualVerdict -ne $ExpectedVerdict) {
      throw "Report verdict mismatch for ${VerdictKey}: expected ${ExpectedVerdict}, got '${actualVerdict}' in $ReportPath"
    }
  }
}

function Assert-VortexProofReportValues {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Report,

    [Parameter(Mandatory = $true)]
    [hashtable]$Expected,

    [Parameter(Mandatory = $true)]
    [string]$Label
  )

  foreach ($key in $Expected.Keys) {
    $actual = Get-VortexProofReportValue -Report $Report -Key $key
    if ($actual -ne $Expected[$key]) {
      throw "$Label check failed: $key expected '$($Expected[$key])', got '$actual'"
    }
  }
}
