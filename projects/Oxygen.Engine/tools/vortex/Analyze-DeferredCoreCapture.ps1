<#
.SYNOPSIS
Runs the Phase 03 deferred-core closeout analyzer.

.DESCRIPTION
Consumes the input manifest produced by Run-DeferredCoreFrame10Capture.ps1 and
emits a key=value report for the final Phase 03 ledger gate.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter(Mandatory = $true)]
  [string]$ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$inputFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath
$reportFullPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $ReportPath
$scriptFullPath = Resolve-RepoPath `
  -RepoRoot $repoRoot `
  -Path (Join-Path $PSScriptRoot 'AnalyzeDeferredCoreCapture.py')

Invoke-PythonCommand `
  -ScriptPath $scriptFullPath `
  -Arguments @('--input', $inputFullPath, '--report', $reportFullPath)

if (-not (Test-Path -LiteralPath $reportFullPath)) {
  throw "Deferred-core analysis report was not written: $reportFullPath"
}

$global:LASTEXITCODE = 0
Write-Output "Report: $reportFullPath"
