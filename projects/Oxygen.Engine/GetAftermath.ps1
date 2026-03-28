# Downloads and installs the latest known Nsight Aftermath SDK package into a
# package layout used by CMake.
#
# By default, this script downloads from the NVIDIA SDK URL provided below.
# If download is blocked (for example secure portal/auth), it falls back to a
# local SDK installation when available.
#
# -- Usage --
# `GetAftermath.bat PathToFolder [SdkRoot] [DownloadUrl]`
# or
# `powershell -ExecutionPolicy Bypass -File GetAftermath.ps1 PathToFolder [SdkRoot] [DownloadUrl]`
#
# Where:
#  - PathToFolder: destination folder for copied package files.
#  - SdkRoot: optional explicit SDK root (skips download when provided).
#  - DownloadUrl: optional zip URL override.

param(
  [Parameter(Position = 0)]
  [string]$SaveFolder = "",

  [Parameter(Position = 1)]
  [string]$SdkRoot = "",

  [Parameter(Position = 2)]
  [string]$DownloadUrl = "https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2025_5_0/windows/NVIDIA_Nsight_Aftermath_SDK_2025.5.0.25317.zip"
)

if ([string]::IsNullOrWhiteSpace($SaveFolder)) {
  $SaveFolder = Join-Path $PSScriptRoot "packages/NsightAftermath"
}

$ErrorActionPreference = "Stop"

function New-EmptyDirectory([string]$Path) {
  if (Test-Path $Path) {
    Remove-Item -Recurse -Force -Path $Path
  }
  New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Resolve-SdkRoot([string]$ExplicitRoot) {
  if (-not [string]::IsNullOrWhiteSpace($ExplicitRoot)) {
    if (Test-Path $ExplicitRoot) {
      return (Get-Item $ExplicitRoot).FullName
    }
    throw "Provided SdkRoot does not exist: $ExplicitRoot"
  }

  $candidates = @(
    "C:\Program Files\NVIDIA Corporation\Nsight Aftermath SDK",
    "C:\Program Files\NVIDIA Corporation\NVIDIA Nsight Aftermath SDK",
    "C:\Program Files (x86)\NVIDIA Corporation\Nsight Aftermath SDK"
  )

  foreach ($path in $candidates) {
    if (Test-Path $path) {
      return (Get-Item $path).FullName
    }
  }

  return $null
}

function Find-FirstFile([string]$Root, [string[]]$Candidates) {
  foreach ($relative in $Candidates) {
    $full = Join-Path $Root $relative
    if (Test-Path $full) {
      return (Get-Item $full).FullName
    }
  }

  return $null
}

function Find-Recursive([string]$Root, [string]$Filter, [string]$PreferredRegex) {
  $files = Get-ChildItem -Path $Root -Recurse -Filter $Filter -File -ErrorAction SilentlyContinue
  if (-not $files -or $files.Count -eq 0) {
    return $null
  }

  $preferred = $files | Where-Object { $_.FullName -match $PreferredRegex } | Select-Object -First 1
  if ($preferred) {
    return $preferred.FullName
  }

  return ($files | Select-Object -First 1).FullName
}

function Install-FromSdkRoot([string]$Root, [string]$DestinationRoot) {
  Write-Host "Using Nsight Aftermath SDK root: $Root"

  $header = Find-FirstFile $Root @(
    "include\GFSDK_Aftermath_DX12.h",
    "Include\GFSDK_Aftermath_DX12.h",
    "include\GFSDK_Aftermath.h",
    "Include\GFSDK_Aftermath.h",
    "include\aftermath\GFSDK_Aftermath_DX12.h",
    "Include\Aftermath\GFSDK_Aftermath_DX12.h"
  )
  if (-not $header) {
    $header = Find-Recursive $Root "GFSDK_Aftermath*.h" "include|Include"
  }
  if (-not $header) {
    throw "GFSDK_Aftermath headers not found under SDK root: $Root"
  }

  $library = Find-FirstFile $Root @(
    "lib\x64\GFSDK_Aftermath_Lib.x64.lib",
    "Lib\x64\GFSDK_Aftermath_Lib.x64.lib",
    "lib\GFSDK_Aftermath_Lib.x64.lib",
    "Lib\GFSDK_Aftermath_Lib.x64.lib",
    "lib\x64\GFSDK_Aftermath_Lib.lib",
    "Lib\x64\GFSDK_Aftermath_Lib.lib"
  )
  if (-not $library) {
    $library = Find-Recursive $Root "GFSDK_Aftermath*.lib" "x64|64"
  }
  if (-not $library) {
    throw "GFSDK_Aftermath .lib not found under SDK root: $Root"
  }

  $dll = Find-FirstFile $Root @(
    "bin\x64\GFSDK_Aftermath_Lib.x64.dll",
    "Bin\x64\GFSDK_Aftermath_Lib.x64.dll",
    "bin\GFSDK_Aftermath_Lib.x64.dll",
    "Bin\GFSDK_Aftermath_Lib.x64.dll",
    "bin\x64\GFSDK_Aftermath_Lib.dll",
    "Bin\x64\GFSDK_Aftermath_Lib.dll",
    "lib\x64\GFSDK_Aftermath_Lib.x64.dll",
    "Lib\x64\GFSDK_Aftermath_Lib.x64.dll"
  )
  if (-not $dll) {
    $dll = Find-Recursive $Root "GFSDK_Aftermath*.dll" "x64|64"
  }
  if (-not $dll) {
    throw "GFSDK_Aftermath .dll not found under SDK root: $Root"
  }

  $includeOut = Join-Path $DestinationRoot "Include"
  $libOut = Join-Path $DestinationRoot "Lib\x64"
  $binOut = Join-Path $DestinationRoot "Bin\x64"

  New-Item -ItemType Directory -Force -Path $includeOut | Out-Null
  New-Item -ItemType Directory -Force -Path $libOut | Out-Null
  New-Item -ItemType Directory -Force -Path $binOut | Out-Null

  $headerDir = Split-Path -Path $header -Parent
  Copy-Item -Force -Recurse -Path (Join-Path $headerDir "GFSDK_Aftermath*.h") -Destination $includeOut
  Copy-Item -Force -Path $library -Destination (Join-Path $libOut ([System.IO.Path]::GetFileName($library)))
  Copy-Item -Force -Path $dll -Destination (Join-Path $binOut ([System.IO.Path]::GetFileName($dll)))

  Write-Host "Installed Nsight Aftermath package files to: $DestinationRoot"
  Write-Host "  Include: $includeOut"
  Write-Host "  Lib/x64 : $libOut"
  Write-Host "  Bin/x64 : $binOut"
}

function Get-ExtractedSdkRoot([string]$Url, [string]$WorkingRoot) {
  New-Item -ItemType Directory -Force -Path $WorkingRoot | Out-Null
  $zipPath = Join-Path $WorkingRoot "NsightAftermath.zip"
  $extractRoot = Join-Path $WorkingRoot "extract"
  New-EmptyDirectory $extractRoot

  Write-Host "Downloading Nsight Aftermath SDK from: $Url"
  Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $zipPath

  Write-Host "Extracting SDK archive..."
  Expand-Archive -Path $zipPath -DestinationPath $extractRoot -Force

  $header = Get-ChildItem -Path $extractRoot -Recurse -Filter "GFSDK_Aftermath*.h" -File -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $header) {
    throw "Downloaded archive did not contain GFSDK_Aftermath headers"
  }

  return $extractRoot
}

New-Item -ItemType Directory -Force -Path $SaveFolder | Out-Null

$resolvedSdkRoot = $null
$tempRoot = Join-Path $SaveFolder "_temp"

try {
  if (-not [string]::IsNullOrWhiteSpace($SdkRoot)) {
    $resolvedSdkRoot = Resolve-SdkRoot $SdkRoot
  } else {
    try {
      $resolvedSdkRoot = Get-ExtractedSdkRoot -Url $DownloadUrl -WorkingRoot $tempRoot
    } catch {
      Write-Warning "Download failed: $($_.Exception.Message)"
      Write-Host "Falling back to local Nsight Aftermath SDK detection..."
      $resolvedSdkRoot = Resolve-SdkRoot ""
      if (-not $resolvedSdkRoot) {
        throw "Failed to download Nsight Aftermath SDK and no local SDK installation was found. Download URL: $DownloadUrl"
      }
    }
  }

  Install-FromSdkRoot -Root $resolvedSdkRoot -DestinationRoot $SaveFolder
} finally {
  if (Test-Path $tempRoot) {
    Remove-Item -Recurse -Force -Path $tempRoot
  }
}

exit 0
