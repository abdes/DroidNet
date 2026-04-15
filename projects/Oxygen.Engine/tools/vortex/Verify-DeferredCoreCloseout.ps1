<#
.SYNOPSIS
Runs the full Phase 03 deferred-core closeout gate.

.DESCRIPTION
This is the one-line operator surface for plan 03-15. It collects the proof
inputs, analyzes them, validates the positive report, proves the negative
assert path with a synthetic failing report, and confirms the ledger entry.
#>
[CmdletBinding()]
param(
  [Parameter()]
  [string]$Output = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($Output)) {
  $Output = Join-Path $repoRoot 'out\build-ninja\analysis\vortex\deferred-core\frame10'
}

$outputDirectory = Resolve-RepoPath -RepoRoot $repoRoot -Path $Output
$inputsPath = Join-Path $outputDirectory 'deferred-core-frame10.inputs.json'
$reportPath = Join-Path $outputDirectory 'deferred-core-frame10.report.txt'
$badReport = Join-Path $outputDirectory 'synthetic-fail.report.txt'
$ledgerPath = Resolve-RepoPath `
  -RepoRoot $repoRoot `
  -Path 'design/vortex/IMPLEMENTATION-STATUS.md'

& powershell -NoProfile -File `
  (Join-Path $PSScriptRoot 'Run-DeferredCoreFrame10Capture.ps1') `
  -Output $outputDirectory
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& powershell -NoProfile -File `
  (Join-Path $PSScriptRoot 'Analyze-DeferredCoreCapture.ps1') `
  -CapturePath $inputsPath `
  -ReportPath $reportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& powershell -NoProfile -File `
  (Join-Path $PSScriptRoot 'Assert-DeferredCoreCaptureReport.ps1') `
  -ReportPath $reportPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

@(
  'analysis_result=success'
  'stage_2_order=fail'
  'stage_3_order=pass'
  'stage_9_order=pass'
  'stage_12_order=pass'
  'gbuffer_contents=pass'
  'scene_color_lit=pass'
  'stencil_local_lights=pass'
) | Set-Content -LiteralPath $badReport -Encoding ascii

$nativeCommandPreferenceAvailable = $null -ne (Get-Variable `
  -Name PSNativeCommandUseErrorActionPreference `
  -ErrorAction SilentlyContinue)
$previousNativeCommandPreference = $false
if ($nativeCommandPreferenceAvailable) {
  $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
  $PSNativeCommandUseErrorActionPreference = $false
}
$assertExit = 1
try {
  try {
    & powershell -NoProfile -File `
      (Join-Path $PSScriptRoot 'Assert-DeferredCoreCaptureReport.ps1') `
      -ReportPath $badReport
    $assertExit = $LASTEXITCODE
  } catch {
    if ($LASTEXITCODE -ne 0) {
      $assertExit = $LASTEXITCODE
    } else {
      $assertExit = 1
    }
  }
} finally {
  if ($nativeCommandPreferenceAvailable) {
    $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
  }
  Remove-Item -LiteralPath $badReport -Force -ErrorAction SilentlyContinue
}

if ($assertExit -eq 0) {
  throw 'Synthetic failing report unexpectedly passed the deferred-core assert gate.'
}

$proofHeading = @(Select-String `
  -Path $ledgerPath `
  -Pattern '^### [0-9]{4}-[0-9]{2}-[0-9]{2} . Phase 3 deferred-core proof pack$')
if ($proofHeading.Count -eq 0) {
  throw "Proof-pack heading missing from ledger: $ledgerPath"
}

$deferralLine = @(Select-String `
  -Path $ledgerPath `
  -Pattern 'RenderDoc runtime validation deferred to Phase 04')
if ($deferralLine.Count -eq 0) {
  throw "RenderDoc deferral line missing from ledger: $ledgerPath"
}

$global:LASTEXITCODE = 0
Write-Output "Deferred-core closeout verified: $reportPath"
