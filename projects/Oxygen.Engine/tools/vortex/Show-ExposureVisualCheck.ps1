# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

[CmdletBinding()]
param(
  [ValidateSet('clear', 'volume', 'local')]
  [string]$Fog = 'volume',
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$engineRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$executable = Join-Path $engineRoot "out/build-ninja/bin/$Configuration/Oxygen.Examples.MultiView.exe"
if (-not (Test-Path -LiteralPath $executable)) {
  throw "Build oxygen-examples-multiview in $Configuration before launching the visual check."
}

Push-Location -LiteralPath $engineRoot
try {
  & $executable --exposure-proof consumer-visual --visual-fog $Fog --pip-wireframe false --fps 60 -v=-1
  if ($LASTEXITCODE -ne 0) {
    throw "The visual check exited with code $LASTEXITCODE."
  }
} finally {
  Pop-Location
}
