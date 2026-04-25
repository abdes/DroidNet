<#
.SYNOPSIS
Runs the Async runtime capture validation flow against an existing RenderDoc capture.

.DESCRIPTION
When -ReferenceRoot is provided, baseline frame/depth are loaded from the
external reference directory for parity comparison. When -InitializeBaselineArtifacts
is set, the current capture exports are written as the baseline (used by
Capture-AsyncLegacyReference.ps1 to seed the reference).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$CapturePath,

  [Parameter()]
  [string]$CaptureReportPath = '',

  [Parameter()]
  [string]$ProductsReportPath = '',

  [Parameter()]
  [string]$ValidationReportPath = '',

  [Parameter()]
  [string]$BaselineFramePath = '',

  [Parameter()]
  [string]$BaselineDepthPath = '',

  [Parameter()]
  [string]$BehaviorPath = '',

  [Parameter()]
  [string]$ReferenceRoot = '',

  [Parameter()]
  [switch]$InitializeBaselineArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\shadows\PowerShellCommon.ps1')

function Set-ScopedProcessEnvironmentVariable {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value,
    [Parameter(Mandatory = $true)][hashtable]$OriginalValues
  )

  if (-not $OriginalValues.ContainsKey($Name)) {
    $OriginalValues[$Name] = [System.Environment]::GetEnvironmentVariable($Name, 'Process')
  }
  [System.Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

function Restore-ScopedProcessEnvironmentVariables {
  param([Parameter(Mandatory = $true)][hashtable]$OriginalValues)

  foreach ($entry in $OriginalValues.GetEnumerator()) {
    [System.Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
  }
}

function Read-ReportMap {
  param([Parameter(Mandatory = $true)][string]$Path)

  $map = @{}
  foreach ($line in Get-Content -LiteralPath $Path) {
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

function Test-SourceContains {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Needle
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $false
  }
  $content = Get-Content -LiteralPath $Path -Raw
  return $content.Contains($Needle)
}

function Get-ReportValue {
  param(
    [Parameter(Mandatory = $true)][hashtable]$Report,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter()][string]$Default = 'unknown'
  )

  if ($Report.ContainsKey($Key)) {
    return $Report[$Key]
  }
  return $Default
}

function Write-BehaviorSummary {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][hashtable]$CaptureReport,
    [Parameter(Mandatory = $true)][hashtable]$ProductsReport,
    [Parameter(Mandatory = $true)][string]$CaptureFullPath
  )

  $asyncMainPath = Join-Path $RepoRoot 'Examples\Async\MainModule.cpp'
  $asyncSettingsPath = Join-Path $RepoRoot 'Examples\Async\AsyncDemoSettingsService.cpp'

  $lines = @(
    '# Async Baseline Behaviors'
    ''
    "Capture: $CaptureFullPath"
    ''
    '## Runtime Evidence'
    "- atmosphere_enabled_main_view: $(Get-ReportValue -Report $ProductsReport -Key 'stage15_sky_scene_color_changed')"
    "- spotlight_presence: $(Get-ReportValue -Report $ProductsReport -Key 'stage12_spot_scene_color_nonzero')"
    "- directional_shadow_path: $(Get-ReportValue -Report $CaptureReport -Key 'stage8_draw_count_present')"
    "- post_processed_visible_output: $(Get-ReportValue -Report $ProductsReport -Key 'stage22_tonemap_output_nonzero')"
    "- imgui_overlay_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'imgui_overlay_scope_present')"
    "- imgui_overlay_blend_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'imgui_overlay_blend_scope_present')"
    "- imgui_overlay_after_tonemap: $(Get-ReportValue -Report $CaptureReport -Key 'imgui_overlay_after_tonemap')"
    "- imgui_overlay_composited_on_scene: $(Get-ReportValue -Report $ProductsReport -Key 'imgui_overlay_composited_on_scene')"
    "- final_present_nonzero: $(Get-ReportValue -Report $ProductsReport -Key 'final_present_nonzero')"
    "- final_present_vs_tonemap_changed: $(Get-ReportValue -Report $ProductsReport -Key 'final_present_vs_tonemap_changed')"
    ''
    '## Supplemental Source Audit (Non-Blocking)'
    "- main_view_with_atmosphere: $(if (Test-SourceContains -Path $asyncMainPath -Needle 'view_ctx.metadata.with_atmosphere = true;') { 'pass' } else { 'fail' })"
    "- spotlight_setup_present: $(if (Test-SourceContains -Path $asyncMainPath -Needle 'EnsureCameraSpotLight();') { 'pass' } else { 'fail' })"
    "- spotlight_node_present: $(if (Test-SourceContains -Path $asyncMainPath -Needle 'CameraSpotLight') { 'pass' } else { 'fail' })"
    "- spotlight_shadows_default_off: $(if (Test-SourceContains -Path $asyncSettingsPath -Needle 'return settings->GetBool(kSpotlightShadowsKey).value_or(false);') { 'pass' } else { 'fail' })"
    ''
    '## Structural Notes'
    "- stage3_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'stage3_scope_present')"
    "- stage8_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'stage8_scope_present')"
    "- stage12_directional_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'stage12_directional_scope_present')"
    "- stage12_spot_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'stage12_spot_scope_present')"
    "- stage15_atmosphere_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'stage15_atmosphere_scope_present')"
    "- stage22_tonemap_scope_present: $(Get-ReportValue -Report $CaptureReport -Key 'stage22_tonemap_scope_present')"
  )

  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
  Set-Content -LiteralPath $OutputPath -Value $lines -Encoding utf8
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$captureFullPath = (Resolve-Path (Resolve-RepoPath -RepoRoot $repoRoot -Path $CapturePath)).Path
$captureDirectory = Split-Path -Parent $captureFullPath

if ([string]::IsNullOrWhiteSpace($CaptureReportPath)) {
  $CaptureReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath, "$([System.IO.Path]::GetExtension($captureFullPath))_async_capture_report.txt")
}
if ([string]::IsNullOrWhiteSpace($ProductsReportPath)) {
  $ProductsReportPath = [System.IO.Path]::ChangeExtension(
    $captureFullPath, "$([System.IO.Path]::GetExtension($captureFullPath))_async_products_report.txt")
}
if ([string]::IsNullOrWhiteSpace($ValidationReportPath)) {
  $ValidationReportPath = "$captureFullPath.validation.txt"
}
# --- Resolve baseline frame/depth paths ---
# When a reference root is provided, load the baseline from the external
# reference directory. Otherwise fall back to the capture directory.
if (-not [string]::IsNullOrWhiteSpace($ReferenceRoot)) {
  $resolvedReferenceRoot = Resolve-RepoPath -RepoRoot $repoRoot -Path $ReferenceRoot
  if (-not (Test-Path -LiteralPath $resolvedReferenceRoot)) {
    throw "Reference root not found: $resolvedReferenceRoot"
  }
  if ([string]::IsNullOrWhiteSpace($BaselineFramePath)) {
    $BaselineFramePath = Join-Path $resolvedReferenceRoot 'reference_frame10.png'
  }
  if ([string]::IsNullOrWhiteSpace($BaselineDepthPath)) {
    $BaselineDepthPath = Join-Path $resolvedReferenceRoot 'reference_depth.png'
  }
} else {
  if ([string]::IsNullOrWhiteSpace($BaselineFramePath)) {
    $BaselineFramePath = Join-Path $captureDirectory 'baseline_frame.png'
  }
  if ([string]::IsNullOrWhiteSpace($BaselineDepthPath)) {
    $BaselineDepthPath = Join-Path $captureDirectory 'baseline_depth.png'
  }
}
if ([string]::IsNullOrWhiteSpace($BehaviorPath)) {
  $BehaviorPath = Join-Path $captureDirectory 'baseline_behaviors.md'
}

$captureReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CaptureReportPath)
$productsReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ProductsReportPath)
$validationReportFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ValidationReportPath)
$baselineFrameFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BaselineFramePath)
$baselineDepthFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BaselineDepthPath)
$behaviorFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BehaviorPath)

$comparisonFrameFullPath = if ($InitializeBaselineArtifacts) {
  $baselineFrameFullPath
} elseif (-not [string]::IsNullOrWhiteSpace($ReferenceRoot)) {
  Join-Path $captureDirectory 'current_frame.verify.png'
} else {
  Join-Path $captureDirectory 'baseline_frame.verify.png'
}
$comparisonDepthFullPath = if ($InitializeBaselineArtifacts) {
  $baselineDepthFullPath
} elseif (-not [string]::IsNullOrWhiteSpace($ReferenceRoot)) {
  Join-Path $captureDirectory 'current_depth.verify.png'
} else {
  Join-Path $captureDirectory 'baseline_depth.verify.png'
}

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
  -CapturePath $captureFullPath `
  -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocAsyncCapture.py') `
  -PassName 'AsyncRuntimeCapture' `
  -ReportPath $captureReportFullPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$environmentSnapshot = @{}
try {
  Set-ScopedProcessEnvironmentVariable `
    -Name 'OXYGEN_ASYNC_EXPORT_COLOR_PATH' `
    -Value $comparisonFrameFullPath `
    -OriginalValues $environmentSnapshot
  Set-ScopedProcessEnvironmentVariable `
    -Name 'OXYGEN_ASYNC_EXPORT_DEPTH_PATH' `
    -Value $comparisonDepthFullPath `
    -OriginalValues $environmentSnapshot

  & powershell -NoProfile -File (Join-Path $repoRoot 'tools\shadows\Invoke-RenderDocUiAnalysis.ps1') `
    -CapturePath $captureFullPath `
    -UiScriptPath (Join-Path $repoRoot 'tools\vortex\AnalyzeRenderDocAsyncProducts.py') `
    -PassName 'AsyncRuntimeProducts' `
    -ReportPath $productsReportFullPath
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Restore-ScopedProcessEnvironmentVariables -OriginalValues $environmentSnapshot
}

$captureReportMap = Read-ReportMap -Path $captureReportFullPath
$productsReportMap = Read-ReportMap -Path $productsReportFullPath

Write-BehaviorSummary `
  -RepoRoot $repoRoot `
  -OutputPath $behaviorFullPath `
  -CaptureReport $captureReportMap `
  -ProductsReport $productsReportMap `
  -CaptureFullPath $captureFullPath

& powershell -NoProfile -File (Join-Path $repoRoot 'tools\vortex\Assert-AsyncRuntimeProof.ps1') `
  -CaptureReportPath $captureReportFullPath `
  -ProductsReportPath $productsReportFullPath `
  -BaselineFramePath $baselineFrameFullPath `
  -ComparisonFramePath $comparisonFrameFullPath `
  -BaselineDepthPath $baselineDepthFullPath `
  -ComparisonDepthPath $comparisonDepthFullPath `
  -ReportPath $validationReportFullPath
exit $LASTEXITCODE
