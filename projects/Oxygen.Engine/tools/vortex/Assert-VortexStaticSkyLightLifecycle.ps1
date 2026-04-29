<#
.SYNOPSIS
Asserts the VTX-M08 static SkyLight off/on lifecycle proof from a RenderScene log.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RuntimeLogPath,

  [Parameter(Mandatory = $true)]
  [int]$ToggleOffFrame,

  [Parameter(Mandatory = $true)]
  [int]$ToggleOnFrame,

  [Parameter()]
  [ValidateRange(0, 1000)]
  [int]$WarmupFramesAfterOn = 10,

  [Parameter()]
  [ValidateRange(1, 10000)]
  [int]$MinimumStableFrames = 60,

  [Parameter()]
  [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$logFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RuntimeLogPath)
if (-not (Test-Path -LiteralPath $logFullPath)) {
  throw "Runtime log not found: $RuntimeLogPath"
}

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = [System.IO.Path]::ChangeExtension($logFullPath, '.static-skylight-lifecycle.txt')
}
$reportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)

$lines = [System.IO.File]::ReadAllLines($logFullPath)

function Find-LineIndex {
  param([string]$Pattern)
  for ($i = 0; $i -lt $lines.Count; ++$i) {
    if ($lines[$i] -match $Pattern) {
      return $i
    }
  }
  return -1
}

$offPattern = "Static SkyLight lifecycle proof disabled SkyLight at frame $ToggleOffFrame"
$onPattern = "Static SkyLight lifecycle proof re-enabled SkyLight at frame $ToggleOnFrame"
$offIndex = Find-LineIndex -Pattern ([regex]::Escape($offPattern))
$onIndex = Find-LineIndex -Pattern ([regex]::Escape($onPattern))

$disabledStatusAfterOff = 0
$regeneratingAfterOn = 0
$validAfterOn = 0
$unexpectedUnavailableReasonAfterOn = 0
$stableChurnCount = 0
$stableChurnNonZero = 0
$warmupFrame = $ToggleOnFrame + $WarmupFramesAfterOn

for ($i = 0; $i -lt $lines.Count; ++$i) {
  $line = $lines[$i]
  if ($offIndex -ge 0 -and $i -gt $offIndex -and ($onIndex -lt 0 -or $i -lt $onIndex)) {
    if ($line -match 'sky_light_authored_enabled=false' -and $line -match 'sky_light_ibl_status=disabled') {
      ++$disabledStatusAfterOff
    }
  }
  if ($onIndex -ge 0 -and $i -gt $onIndex) {
    if ($line -match 'sky_light_ibl_status=regenerating-current-key') {
      ++$regeneratingAfterOn
    }
    if ($line -match 'sky_light_authored_enabled=true' -and $line -match 'sky_light_ibl_valid=true' -and $line -match 'sky_light_ibl_status=valid-current-key') {
      ++$validAfterOn
    }
    if ($line -match 'sky_light_ibl_unavailable_reason=' -and $line -notmatch 'sky_light_ibl_unavailable_reason=none') {
      ++$unexpectedUnavailableReasonAfterOn
    }
  }
  if ($line -match 'Vortex\.SceneTextureLeasePool\.Churn frame=(\d+).*allocations_delta=(-?\d+)') {
    $frame = [int]$Matches[1]
    $delta = [int]$Matches[2]
    if ($frame -ge $warmupFrame) {
      ++$stableChurnCount
      if ($delta -ne 0) {
        ++$stableChurnNonZero
      }
    }
  }
}

$overall = $true
$overall = $overall -and ($offIndex -ge 0)
$overall = $overall -and ($onIndex -gt $offIndex)
$overall = $overall -and ($disabledStatusAfterOff -gt 0)
$overall = $overall -and ($regeneratingAfterOn -gt 0)
$overall = $overall -and ($validAfterOn -ge $MinimumStableFrames)
$overall = $overall -and ($unexpectedUnavailableReasonAfterOn -eq 0)
$overall = $overall -and ($stableChurnCount -ge $MinimumStableFrames)
$overall = $overall -and ($stableChurnNonZero -eq 0)

$report = @(
  "overall_verdict=$($overall.ToString().ToLowerInvariant())",
  "toggle_off_frame=$ToggleOffFrame",
  "toggle_on_frame=$ToggleOnFrame",
  "toggle_off_line_found=$(($offIndex -ge 0).ToString().ToLowerInvariant())",
  "toggle_on_line_found=$(($onIndex -gt $offIndex).ToString().ToLowerInvariant())",
  "disabled_status_after_off_count=$disabledStatusAfterOff",
  "regenerating_after_on_count=$regeneratingAfterOn",
  "valid_current_key_after_on_count=$validAfterOn",
  "unexpected_unavailable_reason_after_on_count=$unexpectedUnavailableReasonAfterOn",
  "stable_churn_start_frame=$warmupFrame",
  "stable_churn_frame_count=$stableChurnCount",
  "stable_churn_nonzero_delta_count=$stableChurnNonZero"
)

[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($reportFullPath)) | Out-Null
[System.IO.File]::WriteAllLines($reportFullPath, $report)

if (-not $overall) {
  Get-Content -LiteralPath $reportFullPath
  throw "Static SkyLight lifecycle proof failed; see $reportFullPath"
}

Write-Output "Static SkyLight lifecycle report: $reportFullPath"
