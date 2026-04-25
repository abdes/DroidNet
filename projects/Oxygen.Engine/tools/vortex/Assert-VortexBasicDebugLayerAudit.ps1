<#
.SYNOPSIS
Asserts debugger-backed D3D12 debug-layer cleanliness for a Vortex runtime.

.DESCRIPTION
Consumes the WinDbg/CDB audit transcript generated from a no-capture Vortex run
with the D3D12 debug layer enabled. If application stdout/stderr is redirected
outside the debugger transcript, pass those files through `-RuntimeLogPath` so
the audit can also prove the runtime exit code.

The audit fails on:
- breakpoint exceptions
- any D3D12/DXGI errors
- any D3D12/DXGI warnings that are not explicitly triaged as accepted
- missing or nonzero runtime exit code

The normal shutdown `DXGI WARNING: Live IDXGIFactory ...` line is treated as an
accepted artifact for this audit surface and is reported explicitly rather than
failing the validation.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$DebuggerLogPath,

  [Parameter()]
  [string[]]$RuntimeLogPath = @(),

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Format-Bool {
  param(
    [Parameter(Mandatory = $true)]
    [bool]$Value
  )

  if ($Value) {
    return 'true'
  }

  return 'false'
}

$debuggerLogFullPath = (Resolve-Path $DebuggerLogPath).Path
$runtimeLogFullPaths = @(
  foreach ($path in $RuntimeLogPath) {
    if (-not [string]::IsNullOrWhiteSpace($path)) {
      (Resolve-Path $path).Path
    }
  }
)
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = "$debuggerLogFullPath.report.txt"
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$logLines = Get-Content -LiteralPath $debuggerLogFullPath
$runtimeLogLines = @()
foreach ($runtimeLogFullPath in $runtimeLogFullPaths) {
  $runtimeLogLines += Get-Content -LiteralPath $runtimeLogFullPath
}

$breakLines = @(
  $logLines | Select-String -Pattern 'Break instruction exception - code 80000003' | ForEach-Object { $_.Line } | Select-Object -Unique
)
$d3d12ErrorLines = @(
  $logLines | Select-String -Pattern '^D3D12 (ERROR|CORRUPTION):' | ForEach-Object { $_.Line } | Select-Object -Unique
)
$dxgiErrorLines = @(
  $logLines | Select-String -Pattern '^DXGI ERROR:' | ForEach-Object { $_.Line } | Select-Object -Unique
)
$warningLines = @(
  $logLines | Select-String -Pattern '^(D3D12|DXGI) WARNING:' | ForEach-Object { $_.Line } | Select-Object -Unique
)

$acceptedWarningRules = [ordered]@{
  'dxgi_live_factory_shutdown' = '^DXGI WARNING: Live IDXGIFactory at .* \[ STATE_CREATION WARNING #0: \]$'
}

$acceptedWarningCounts = @{}
foreach ($ruleName in $acceptedWarningRules.Keys) {
  $acceptedWarningCounts[$ruleName] = 0
}

$acceptedWarningLines = New-Object System.Collections.Generic.List[string]
$blockingWarningLines = New-Object System.Collections.Generic.List[string]
foreach ($warningLine in $warningLines) {
  $matchedRule = $null
  foreach ($rule in $acceptedWarningRules.GetEnumerator()) {
    if ($warningLine -match $rule.Value) {
      $matchedRule = $rule.Key
      break
    }
  }

  if ($null -ne $matchedRule) {
    $acceptedWarningCounts[$matchedRule] += 1
    $acceptedWarningLines.Add($warningLine)
    continue
  }

  $blockingWarningLines.Add($warningLine)
}

$exitCodeMatches = @(
  ($logLines + $runtimeLogLines) | Select-String -Pattern '\bexit code:\s*(\d+)'
)
$runtimeExitCode = $null
if ($exitCodeMatches.Count -gt 0) {
  $runtimeExitCode = [int]$exitCodeMatches[-1].Matches[0].Groups[1].Value
}

$failureReasons = New-Object System.Collections.Generic.List[string]
if ($null -eq $runtimeExitCode) {
  $failureReasons.Add('missing_runtime_exit_code')
} elseif ($runtimeExitCode -ne 0) {
  $failureReasons.Add("nonzero_runtime_exit_code:$runtimeExitCode")
}
if ($breakLines.Count -gt 0) {
  $failureReasons.Add("break_instruction_count:$($breakLines.Count)")
}
if ($d3d12ErrorLines.Count -gt 0) {
  $failureReasons.Add("d3d12_error_count:$($d3d12ErrorLines.Count)")
}
if ($dxgiErrorLines.Count -gt 0) {
  $failureReasons.Add("dxgi_error_count:$($dxgiErrorLines.Count)")
}
if ($blockingWarningLines.Count -gt 0) {
  $failureReasons.Add("blocking_warning_count:$($blockingWarningLines.Count)")
}

$overallVerdict = if ($failureReasons.Count -eq 0) { 'pass' } else { 'fail' }
$analysisResult = if ($failureReasons.Count -eq 0) { 'success' } else { 'failure' }

$reportLines = New-Object System.Collections.Generic.List[string]
$reportLines.Add("analysis_result=$analysisResult")
$reportLines.Add('analysis_profile=vortexbasic_debug_layer_audit')
$reportLines.Add("debugger_log_path=$debuggerLogFullPath")
foreach ($runtimeLogFullPath in $runtimeLogFullPaths) {
  $reportLines.Add("runtime_log_path=$runtimeLogFullPath")
}
$reportLines.Add("runtime_exit_code=$(if ($null -eq $runtimeExitCode) { '' } else { $runtimeExitCode })")
$reportLines.Add("debugger_break_detected=$(Format-Bool -Value ($breakLines.Count -gt 0))")
$reportLines.Add("break_instruction_count=$($breakLines.Count)")
$reportLines.Add("d3d12_error_count=$($d3d12ErrorLines.Count)")
$reportLines.Add("dxgi_error_count=$($dxgiErrorLines.Count)")
$reportLines.Add("blocking_warning_count=$($blockingWarningLines.Count)")
$reportLines.Add("accepted_warning_count=$($acceptedWarningLines.Count)")
foreach ($ruleName in $acceptedWarningRules.Keys) {
  $reportLines.Add("accepted_warning_rule_${ruleName}_count=$($acceptedWarningCounts[$ruleName])")
}
$reportLines.Add('accepted_warning_policy=The shutdown `DXGI WARNING: Live IDXGIFactory ...` line is documented as normal and accepted for this debugger-backed audit surface.')
$reportLines.Add("overall_verdict=$overallVerdict")
if ($failureReasons.Count -gt 0) {
  $reportLines.Add("failure_reasons=$($failureReasons -join ',')")
}
if ($breakLines.Count -gt 0) {
  $reportLines.Add("first_break_line=$($breakLines[0])")
}
if ($d3d12ErrorLines.Count -gt 0) {
  $reportLines.Add("first_d3d12_error_line=$($d3d12ErrorLines[0])")
}
if ($dxgiErrorLines.Count -gt 0) {
  $reportLines.Add("first_dxgi_error_line=$($dxgiErrorLines[0])")
}
if ($blockingWarningLines.Count -gt 0) {
  $reportLines.Add("first_blocking_warning_line=$($blockingWarningLines[0])")
}
if ($acceptedWarningLines.Count -gt 0) {
  $reportLines.Add("first_accepted_warning_line=$($acceptedWarningLines[0])")
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportFullPath) | Out-Null
Set-Content -LiteralPath $reportFullPath -Value $reportLines -Encoding utf8

if ($failureReasons.Count -ne 0) {
  throw "Debugger-backed D3D12 audit failed: $($failureReasons -join ', '). See report: $reportFullPath"
}

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
