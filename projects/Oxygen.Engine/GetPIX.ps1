# Provisions PIX dependencies for this repo.
#
# Responsibilities:
# - provision or validate WinPixEventRuntime for build-time PIX marker support;
# - discover the installed PIX tool and capture DLLs for runtime GPU capture.
#
# This is the canonical PIX setup entry point for the repo.

param(
  [Parameter(Position = 0)]
  [string]$SaveFolder = "",
  [string]$WinPixEventSourceRoot = "",
  [string]$WinPixEventPackagePath = "",
  [string]$PixRoot = "",
  [switch]$SkipDownloadIfPresent
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SaveFolder)) {
  $SaveFolder = Join-Path $PSScriptRoot "packages/WinPixEventRuntime"
}

function Test-WinPixEventLayoutComplete([string]$Root) {
  if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path $Root)) {
    return $false
  }

  return (Test-Path (Join-Path $Root "Include\pix3.h")) `
    -and (Test-Path (Join-Path $Root "Lib\x64\WinPixEventRuntime.lib")) `
    -and (Test-Path (Join-Path $Root "Bin\x64\WinPixEventRuntime.dll"))
}

function Get-LatestNuGetVersion([string]$PackageId) {
  $url = "https://api.nuget.org/v3-flatcontainer/$PackageId/index.json"
  $response = Invoke-RestMethod -UseBasicParsing -Uri $url
  if (-not $response.versions -or $response.versions.Count -eq 0) {
    throw "Failed to query NuGet versions for package '$PackageId'"
  }
  return $response.versions[-1]
}

function New-EmptyDirectory([string]$Path) {
  if (Test-Path $Path) {
    Remove-Item -Recurse -Force -Path $Path
  }
  New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Copy-WinPixEventLayout([string]$SourceRoot, [string]$DestinationRoot) {
  if (-not (Test-WinPixEventLayoutComplete $SourceRoot)) {
    throw "WinPixEventRuntime layout is incomplete: '$SourceRoot'"
  }

  $includeOut = Join-Path $DestinationRoot "Include"
  $libOut = Join-Path $DestinationRoot "Lib\x64"
  $binOut = Join-Path $DestinationRoot "Bin\x64"

  New-Item -ItemType Directory -Force -Path $includeOut | Out-Null
  New-Item -ItemType Directory -Force -Path $libOut | Out-Null
  New-Item -ItemType Directory -Force -Path $binOut | Out-Null

  Copy-Item -Force -Recurse -Path (Join-Path $SourceRoot "Include\*") -Destination $includeOut
  Copy-Item -Force -Path (Join-Path $SourceRoot "Lib\x64\WinPixEventRuntime.lib") -Destination $libOut
  Copy-Item -Force -Path (Join-Path $SourceRoot "Bin\x64\WinPixEventRuntime.dll") -Destination $binOut
}

function Install-WinPixEventFromExtractedRoot([string]$ExtractedRoot, [string]$DestinationRoot) {
  $pix3 = Get-ChildItem -Path $ExtractedRoot -Recurse -Filter "pix3.h" -File | Select-Object -First 1
  if (-not $pix3) {
    throw "pix3.h not found inside extracted package"
  }

  $libCandidates = Get-ChildItem -Path $ExtractedRoot -Recurse -Filter "WinPixEventRuntime.lib" -File
  if (-not $libCandidates -or $libCandidates.Count -eq 0) {
    throw "WinPixEventRuntime.lib not found inside extracted package"
  }

  $dllCandidates = Get-ChildItem -Path $ExtractedRoot -Recurse -Filter "WinPixEventRuntime.dll" -File
  if (-not $dllCandidates -or $dllCandidates.Count -eq 0) {
    throw "WinPixEventRuntime.dll not found inside extracted package"
  }

  $lib = $libCandidates | Sort-Object -Property @{ Expression = { $_.FullName -notmatch "x64" } }, FullName | Select-Object -First 1
  $dll = $dllCandidates | Sort-Object -Property @{ Expression = { $_.FullName -notmatch "x64" } }, FullName | Select-Object -First 1

  $includeOut = Join-Path $DestinationRoot "Include"
  $libOut = Join-Path $DestinationRoot "Lib\x64"
  $binOut = Join-Path $DestinationRoot "Bin\x64"

  New-Item -ItemType Directory -Force -Path $includeOut | Out-Null
  New-Item -ItemType Directory -Force -Path $libOut | Out-Null
  New-Item -ItemType Directory -Force -Path $binOut | Out-Null

  Copy-Item -Force -Path (Join-Path $pix3.Directory.FullName "*") -Destination $includeOut
  Copy-Item -Force -Path $lib.FullName -Destination $libOut
  Copy-Item -Force -Path $dll.FullName -Destination $binOut
}

function Provision-WinPixEventRuntime() {
  New-Item -ItemType Directory -Force -Path $SaveFolder | Out-Null

  if ($SkipDownloadIfPresent -and (Test-WinPixEventLayoutComplete $SaveFolder)) {
    Write-Host "Using existing WinPixEventRuntime layout: $SaveFolder"
    return
  }

  if (-not [string]::IsNullOrWhiteSpace($WinPixEventSourceRoot)) {
    Copy-WinPixEventLayout -SourceRoot $WinPixEventSourceRoot -DestinationRoot $SaveFolder
    Write-Host "Installed WinPixEventRuntime from existing layout: $WinPixEventSourceRoot"
    Write-Host "  Destination: $SaveFolder"
    return
  }

  if (-not [string]::IsNullOrWhiteSpace($WinPixEventPackagePath)) {
    if (-not (Test-Path $WinPixEventPackagePath)) {
      throw "Specified WinPixEventRuntime package path does not exist: '$WinPixEventPackagePath'"
    }

    $tempDir = Join-Path $SaveFolder "_temp"
    New-EmptyDirectory $tempDir

    try {
      Expand-Archive -Path $WinPixEventPackagePath -DestinationPath $tempDir -Force
      Install-WinPixEventFromExtractedRoot -ExtractedRoot $tempDir -DestinationRoot $SaveFolder
    } finally {
      if (Test-Path $tempDir) {
        Remove-Item -Recurse -Force -Path $tempDir
      }
    }

    Write-Host "Installed WinPixEventRuntime from package: $WinPixEventPackagePath"
    Write-Host "  Destination: $SaveFolder"
    return
  }

  $packageId = "WinPixEventRuntime"
  $version = Get-LatestNuGetVersion $packageId
  $nupkgUrl = "https://api.nuget.org/v3-flatcontainer/$packageId/$version/$packageId.$version.nupkg"
  $nupkgPath = Join-Path $SaveFolder "$packageId.$version.nupkg"
  $tempDir = Join-Path $SaveFolder "_temp"

  New-EmptyDirectory $tempDir
  Invoke-WebRequest -UseBasicParsing -Uri $nupkgUrl -OutFile $nupkgPath

  try {
    Expand-Archive -Path $nupkgPath -DestinationPath $tempDir -Force
    Install-WinPixEventFromExtractedRoot -ExtractedRoot $tempDir -DestinationRoot $SaveFolder
  } finally {
    if (Test-Path $tempDir) {
      Remove-Item -Recurse -Force -Path $tempDir
    }
    if (Test-Path $nupkgPath) {
      Remove-Item -Force -Path $nupkgPath
    }
  }

  Write-Host "Installed WinPixEventRuntime to: $SaveFolder"
  Write-Host "  Include: $(Join-Path $SaveFolder 'Include')"
  Write-Host "  Lib/x64 : $(Join-Path $SaveFolder 'Lib\x64')"
  Write-Host "  Bin/x64 : $(Join-Path $SaveFolder 'Bin\x64')"
}

function Convert-ToVersionKey([string]$Name) {
  $parts = $Name -split "[^0-9]+"
  $numbers = @()
  foreach ($part in $parts) {
    if ([string]::IsNullOrWhiteSpace($part)) {
      continue
    }
    $numbers += [int]$part
  }
  while ($numbers.Count -lt 4) {
    $numbers += 0
  }

  return [PSCustomObject]@{
    A = $numbers[0]
    B = $numbers[1]
    C = $numbers[2]
    D = $numbers[3]
  }
}

function Resolve-PixInstall([string]$ExplicitRoot) {
  $candidateRoots = @()
  if (-not [string]::IsNullOrWhiteSpace($ExplicitRoot)) {
    $candidateRoots += $ExplicitRoot
  }
  $candidateRoots += @(
    "C:\Program Files\Microsoft PIX",
    "C:\Program Files (x86)\Microsoft PIX"
  )

  $installCandidates = @()
  foreach ($root in $candidateRoots) {
    if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path $root)) {
      continue
    }

    $gpuDllDirect = Join-Path $root "WinPixGpuCapturer.dll"
    if (Test-Path $gpuDllDirect) {
      $installCandidates += (Get-Item $root)
      continue
    }

    $subdirs = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue
    foreach ($subdir in $subdirs) {
      if (Test-Path (Join-Path $subdir.FullName "WinPixGpuCapturer.dll")) {
        $installCandidates += $subdir
      }
    }
  }

  if (-not $installCandidates -or $installCandidates.Count -eq 0) {
    return $null
  }

  $latest = $installCandidates |
    Sort-Object -Property `
      @{ Expression = { (Convert-ToVersionKey $_.Name).A } }, `
      @{ Expression = { (Convert-ToVersionKey $_.Name).B } }, `
      @{ Expression = { (Convert-ToVersionKey $_.Name).C } }, `
      @{ Expression = { (Convert-ToVersionKey $_.Name).D } }, `
      FullName |
    Select-Object -Last 1

  if (-not $latest) {
    return $null
  }

  $gpuDll = Join-Path $latest.FullName "WinPixGpuCapturer.dll"
  $timingDll = Join-Path $latest.FullName "WinPixTimingCapturer.dll"
  $pixTool = Join-Path $latest.FullName "pixtool.exe"

  return [PSCustomObject]@{
    Root = $latest.FullName
    GpuCapturerDll = $(if (Test-Path $gpuDll) { (Get-Item $gpuDll).FullName } else { $null })
    TimingCapturerDll = $(if (Test-Path $timingDll) { (Get-Item $timingDll).FullName } else { $null })
    PixTool = $(if (Test-Path $pixTool) { (Get-Item $pixTool).FullName } else { $null })
  }
}

Provision-WinPixEventRuntime

$markerReady = Test-WinPixEventLayoutComplete $SaveFolder
$pixInstall = Resolve-PixInstall $PixRoot
$gpuReady = $null -ne $pixInstall -and -not [string]::IsNullOrWhiteSpace($pixInstall.GpuCapturerDll)
$timingReady = $null -ne $pixInstall -and -not [string]::IsNullOrWhiteSpace($pixInstall.TimingCapturerDll)
$uiReady = $null -ne $pixInstall -and -not [string]::IsNullOrWhiteSpace($pixInstall.PixTool)

Write-Host "PIX setup summary"
Write-Host "  Marker runtime ready : $markerReady"
Write-Host "  WinPixEventRuntime   : $SaveFolder"
if ($pixInstall) {
  Write-Host "  PIX install root     : $($pixInstall.Root)"
} else {
  Write-Host "  PIX install root     : not found"
}
Write-Host "  GPU capturer ready   : $gpuReady"
if ($gpuReady) {
  Write-Host "  GPU capturer DLL     : $($pixInstall.GpuCapturerDll)"
}
Write-Host "  Timing capturer ready: $timingReady"
if ($timingReady) {
  Write-Host "  Timing capturer DLL  : $($pixInstall.TimingCapturerDll)"
}
Write-Host "  PIX tooling UI ready : $uiReady"
if ($uiReady) {
  Write-Host "  PIX tool             : $($pixInstall.PixTool)"
}

if (-not $pixInstall) {
  throw "PIX is not installed. Install Microsoft PIX under 'C:\Program Files\Microsoft PIX' or pass -PixRoot to a valid PIX installation."
}

if (-not $gpuReady) {
  throw "WinPixGpuCapturer.dll was not found under '$($pixInstall.Root)'. PIX GPU capture is not ready."
}

exit 0
