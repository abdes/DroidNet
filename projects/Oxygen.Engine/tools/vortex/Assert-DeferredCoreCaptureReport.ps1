<#
.SYNOPSIS
Asserts the final Phase 03 deferred-core closeout report.

.DESCRIPTION
Validates the required proof keys, then records either a proof-pack success
entry or an explicit missing delta in design/vortex/IMPLEMENTATION-STATUS.md.
RenderDoc runtime validation remains explicitly deferred to Phase 04.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

function Get-ReportEntries {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $entries = @{}
  foreach ($line in Get-Content -LiteralPath $Path) {
    if ($line -match '^(?<key>[^=]+)=(?<value>.*)$') {
      $entries[$matches['key']] = $matches['value']
    }
  }
  return $entries
}

function Update-DocumentationSyncLogEntry {
  param(
    [Parameter(Mandatory = $true)]
    [string]$LedgerPath,

    [Parameter(Mandatory = $true)]
    [string]$Heading,

    [Parameter(Mandatory = $true)]
    [string[]]$BodyLines
  )

  $content = Get-Content -LiteralPath $LedgerPath -Raw
  $entryPattern = '(?ms)^' + [regex]::Escape($Heading) + '\r?\n.*?(?=^### |\z)'
  $content = [regex]::Replace($content, $entryPattern, '')
  $entry = $Heading + "`r`n`r`n" + ($BodyLines -join "`r`n") + "`r`n`r`n"
  $content = [regex]::Replace(
    $content,
    '(?m)^## Documentation Sync Log\s*',
    "## Documentation Sync Log`r`n`r`n$entry",
    1
  )
  Set-Content -LiteralPath $LedgerPath -Value $content -Encoding utf8
}

function Get-EntryValue {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Entries,

    [Parameter(Mandatory = $true)]
    [string]$Key
  )

  if ($Entries.ContainsKey($Key)) {
    return $Entries[$Key]
  }

  return $null
}

$requiredKeys = @(
  'stage_2_order',
  'stage_3_order',
  'stage_9_order',
  'stage_12_order',
  'gbuffer_contents',
  'scene_color_lit',
  'stencil_local_lights'
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$reportFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ReportPath
$ledgerPath = Resolve-RepoPath `
  -RepoRoot $repoRoot `
  -Path 'design/vortex/IMPLEMENTATION-STATUS.md'

if (-not (Test-Path -LiteralPath $reportFullPath)) {
  throw "Deferred-core report not found: $reportFullPath"
}

$entries = Get-ReportEntries -Path $reportFullPath
$failures = New-Object System.Collections.Generic.List[string]
$analysisResult = Get-EntryValue -Entries $entries -Key 'analysis_result'

if ($analysisResult -ne 'success') {
  if ([string]::IsNullOrWhiteSpace($analysisResult)) {
    $analysisResult = '<missing>'
  }
  $failures.Add("analysis_result=$analysisResult") | Out-Null
}

foreach ($key in $requiredKeys) {
  if (-not $entries.ContainsKey($key)) {
    $failures.Add("$key=<missing>") | Out-Null
    continue
  }

  $value = $entries[$key]
  if ($value -in @('fail', 'inconclusive')) {
    $failures.Add("$key=$value") | Out-Null
  }
}

$isSyntheticReport = ([System.IO.Path]::GetFileName($reportFullPath) -like 'synthetic-*')
$today = Get-Date -Format 'yyyy-MM-dd'

if (-not $isSyntheticReport) {
  if ($failures.Count -eq 0) {
    Update-DocumentationSyncLogEntry `
      -LedgerPath $ledgerPath `
      -Heading "### $today — Phase 3 deferred-core proof pack" `
      -BodyLines @(
        '- Changed files this session:',
        '  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`',
        '  - `tools/vortex/AnalyzeDeferredCoreCapture.py`',
        '  - `tools/vortex/Analyze-DeferredCoreCapture.ps1`',
        '  - `tools/vortex/Assert-DeferredCoreCaptureReport.ps1`',
        '  - `design/vortex/IMPLEMENTATION-STATUS.md`',
        '- Commands used for verification:',
        '  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`',
        '  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`',
        '  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`',
        '- Result:',
        '  - Phase 3 closeout is proven by source/test/log-backed analysis of the current deferred-core tree.',
        '  - RenderDoc runtime validation deferred to Phase 04 when Async and DemoShell migrate to Vortex.',
        "  - Analyzer report: $reportFullPath",
        '- Code / validation delta:',
        '  - The proof pack now closes Stage 2/3/9/12 ordering, GBuffer publication, SceneColor accumulation, and stencil-bounded local-light behavior without claiming a runtime capture that does not exist yet.',
        '- Remaining blocker:',
        '  - Phase 04 must migrate Async and DemoShell to Vortex before the first truthful frame-10 RenderDoc runtime capture is claimed.'
      )
  } else {
    Update-DocumentationSyncLogEntry `
      -LedgerPath $ledgerPath `
      -Heading "### $today — Phase 3 deferred-core closeout blocked" `
      -BodyLines @(
        '- Commands used for verification:',
        '  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`',
        '  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`',
        '  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`',
        '- Result:',
        '  - The Phase 03 closeout gate did not pass.',
        "  - Report: $reportFullPath",
        "- Missing delta: $($failures -join ', ')",
        '- RenderDoc runtime validation remains deferred to Phase 04 and is not part of this failure.'
      )
  }
}

if ($failures.Count -gt 0) {
  Write-Output ("Deferred-core closeout report failed: " + ($failures -join ', '))
  exit 1
}

$global:LASTEXITCODE = 0
Write-Output "Validated report: $reportFullPath"
