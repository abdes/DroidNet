# Locates a local RenderDoc installation (default Windows paths first) and
# exports renderdoc_app.h/renderdoc.dll into a local package layout used by
# CMake. If the header is not available locally, it is fetched from the latest
# RenderDoc GitHub release tag.
#
# -- Usage --
# `GetRenderDoc.bat PathToFolder`
# or
# `powershell -ExecutionPolicy Bypass -File GetRenderDoc.ps1 PathToFolder`
#
# Where PathToFolder is folder where extracted files will be placed.

param(
  [Parameter(Position = 0)]
  [string]$SaveFolder = ""
)

if ([string]::IsNullOrWhiteSpace($SaveFolder)) {
  $SaveFolder = Join-Path $PSScriptRoot "packages/RenderDoc"
}

$ErrorActionPreference = "Stop"

function New-EmptyDirectory([string]$Path) {
  if (Test-Path $Path) {
    Remove-Item -Recurse -Force -Path $Path
  }
  New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Get-LatestRenderDocTag {
  $release = Invoke-RestMethod -Uri "https://api.github.com/repos/baldurk/renderdoc/releases/latest" -Method Get
  if (-not $release -or [string]::IsNullOrWhiteSpace($release.tag_name)) {
    throw "Failed to query RenderDoc releases from GitHub API"
  }
  return $release.tag_name
}

function Resolve-LocalRenderDocRoot {
  $candidates = @(
    "C:\Program Files\RenderDoc",
    "C:\Program Files (x86)\RenderDoc"
  )

  foreach ($path in $candidates) {
    if (Test-Path $path) {
      return $path
    }
  }

  return $null
}

function Find-RenderDocHeader([string]$Root) {
  if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path $Root)) {
    return $null
  }

  $direct = @(
    (Join-Path $Root "renderdoc_app.h"),
    (Join-Path $Root "include\renderdoc_app.h"),
    (Join-Path $Root "Include\renderdoc_app.h")
  )

  foreach ($candidate in $direct) {
    if (Test-Path $candidate) {
      return (Get-Item $candidate).FullName
    }
  }

  $found = Get-ChildItem -Path $Root -Recurse -Filter "renderdoc_app.h" -File -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($found) {
    return $found.FullName
  }

  return $null
}

function Find-RenderDocDll([string]$Root) {
  if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path $Root)) {
    return $null
  }

  $direct = @(
    (Join-Path $Root "renderdoc.dll"),
    (Join-Path $Root "x64\renderdoc.dll"),
    (Join-Path $Root "x86\renderdoc.dll")
  )

  foreach ($candidate in $direct) {
    if (Test-Path $candidate) {
      return (Get-Item $candidate).FullName
    }
  }

  $found = Get-ChildItem -Path $Root -Recurse -Filter "renderdoc.dll" -File -ErrorAction SilentlyContinue |
    Sort-Object -Property @{ Expression = { $_.FullName -notmatch "x64|64" } }, FullName |
    Select-Object -First 1
  if ($found) {
    return $found.FullName
  }

  return $null
}

function Download-HeaderFromTag([string]$Tag, [string]$DestinationPath) {
  $headerUrls = @(
    "https://raw.githubusercontent.com/baldurk/renderdoc/$Tag/renderdoc/api/app/renderdoc_app.h",
    "https://raw.githubusercontent.com/baldurk/renderdoc/$Tag/renderdoc_app.h"
  )

  foreach ($url in $headerUrls) {
    try {
      Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $DestinationPath
      if (Test-Path $DestinationPath) {
        return
      }
    } catch {
      # Try the next URL.
    }
  }

  $zipUrl = "https://github.com/baldurk/renderdoc/archive/refs/tags/$Tag.zip"
  $tempDir = Join-Path ([System.IO.Path]::GetDirectoryName($DestinationPath)) "_header_temp"
  New-EmptyDirectory $tempDir

  try {
    $zipPath = Join-Path $tempDir "renderdoc-$Tag.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $zipUrl -OutFile $zipPath
    Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force

    $header = Get-ChildItem -Path $tempDir -Recurse -Filter "renderdoc_app.h" -File | Select-Object -First 1
    if (-not $header) {
      throw "renderdoc_app.h not found in RenderDoc source archive for tag '$Tag'"
    }

    Copy-Item -Force -Path $header.FullName -Destination $DestinationPath
  } finally {
    if (Test-Path $tempDir) {
      Remove-Item -Recurse -Force -Path $tempDir
    }
  }
}

New-Item -ItemType Directory -Force -Path $SaveFolder | Out-Null

$includeOut = Join-Path $SaveFolder "Include"
$binOut = Join-Path $SaveFolder "Bin\\x64"

New-Item -ItemType Directory -Force -Path $includeOut | Out-Null
New-Item -ItemType Directory -Force -Path $binOut | Out-Null

$headerOut = Join-Path $includeOut "renderdoc_app.h"
$installRoot = Resolve-LocalRenderDocRoot

if ($installRoot) {
  Write-Host "Found local RenderDoc install: $installRoot"
} else {
  Write-Host "Local RenderDoc install not found in default Program Files paths"
}

$localHeader = Find-RenderDocHeader $installRoot
if ($localHeader) {
  Copy-Item -Force -Path $localHeader -Destination $headerOut
  Write-Host "Using local header: $localHeader"
} else {
  $tag = Get-LatestRenderDocTag
  Write-Host "Local header not found. Downloading renderdoc_app.h from release tag $tag"
  Download-HeaderFromTag -Tag $tag -DestinationPath $headerOut
}

if (-not (Test-Path $headerOut)) {
  throw "Failed to produce renderdoc_app.h in '$includeOut'"
}

$localDll = Find-RenderDocDll $installRoot
if ($localDll) {
  Copy-Item -Force -Path $localDll -Destination (Join-Path $binOut "renderdoc.dll")
  Write-Host "Using local DLL: $localDll"
} else {
  Write-Host "Warning: renderdoc.dll not found in default local install paths"
}

Write-Host "Installed RenderDoc package files to: $SaveFolder"
Write-Host "  Include: $includeOut"
if (Test-Path (Join-Path $binOut "renderdoc.dll")) {
  Write-Host "  Bin/x64 : $binOut"
} else {
  Write-Host "  Bin/x64 : renderdoc.dll was not found locally"
}

exit 0
