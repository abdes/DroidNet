# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

[CmdletBinding()]
param(
  [ValidateSet('clear', 'volume', 'local')]
  [string]$Fog = 'volume',

  [ValidateSet('Debug', 'Release')]
  [string]$Configuration,

  [Alias('h')][switch]$Help,

  [string]$BuildTree,

  [string]$Preset
)

if ($Help) { Get-Help $PSCommandPath -Detailed; return }

$ErrorActionPreference = 'Stop'
$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $engineRoot 'tools/cli/BuildSelection.ps1')
$selection = Resolve-OxygenBuildSelection -SourceRoot $engineRoot -BuildTree $BuildTree -Config $Configuration -Preset $Preset -RequiredExecutables 'Oxygen.Examples.MultiView.exe'
Write-OxygenBuildSelection $selection
$Configuration = $selection.Config
$executable = Join-Path $selection.BuildRoot "bin/$Configuration/Oxygen.Examples.MultiView.exe"
if (-not (Test-Path -LiteralPath $executable)) {
  throw "Build oxygen-examples-multiview in $Configuration before launching the visual check."
}

Push-Location -LiteralPath $engineRoot
try {
  Invoke-OxygenSelectedExecutable $selection $executable @('--exposure-proof', 'consumer-visual', '--visual-fog', $Fog, '--pip-wireframe', 'false', '--fps', '60', '-v=-1')
  if ($LASTEXITCODE -ne 0) {
    throw "The visual check exited with code $LASTEXITCODE."
  }
} finally {
  Pop-Location
}
