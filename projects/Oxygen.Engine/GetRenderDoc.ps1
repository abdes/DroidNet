# Downloads the RenderDoc in-application API header and installs it into the
# Oxygen source tree.
#
# -- Usage --
# `powershell -ExecutionPolicy Bypass -File GetRenderDoc.ps1`
# `powershell -ExecutionPolicy Bypass -File GetRenderDoc.ps1 -Tag v1.43`
# `powershell -ExecutionPolicy Bypass -File GetRenderDoc.ps1 -Latest`

param(
  [Parameter(Position = 0)]
  [string]$SaveFolder = "",
  [string]$Tag = "v1.43",
  [switch]$Latest
)

$ErrorActionPreference = "Stop"

function Resolve-LatestRenderDocTag {
  $release = Invoke-RestMethod -Uri "https://api.github.com/repos/baldurk/renderdoc/releases/latest" -Method Get
  if ([string]::IsNullOrWhiteSpace($release.tag_name)) {
    throw "Failed to resolve the latest RenderDoc release tag"
  }
  return $release.tag_name
}

function New-EmptyDirectory([string]$Path) {
  if (Test-Path -LiteralPath $Path) {
    Remove-Item -LiteralPath $Path -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

if ($Latest) {
  $Tag = Resolve-LatestRenderDocTag
}

if ([string]::IsNullOrWhiteSpace($SaveFolder)) {
  $SaveFolder = Join-Path $PSScriptRoot "src/Oxygen/Graphics/External/RenderDoc"
}

New-Item -ItemType Directory -Force -Path $SaveFolder | Out-Null

$tempDir = Join-Path $SaveFolder "_temp"
New-EmptyDirectory $tempDir

$targetPath = Join-Path $SaveFolder "renderdoc_app.h"
$tempPath = Join-Path $tempDir "renderdoc_app.h"
$downloadUrl = "https://raw.githubusercontent.com/baldurk/renderdoc/$Tag/renderdoc/api/app/renderdoc_app.h"

Write-Host "Downloading RenderDoc header from $downloadUrl"
Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $tempPath

$content = Get-Content -LiteralPath $tempPath -Raw
if ($content -notmatch "RENDERDOC_GetAPI" -or $content -notmatch "RENDERDOC_API_1_7_0") {
  throw "Downloaded file does not look like a valid renderdoc_app.h"
}

Move-Item -LiteralPath $tempPath -Destination $targetPath -Force
Remove-Item -LiteralPath $tempDir -Recurse -Force

Write-Host "Installed RenderDoc header $Tag to: $targetPath"
exit 0
