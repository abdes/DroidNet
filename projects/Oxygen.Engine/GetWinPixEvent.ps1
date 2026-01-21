# Downloads the latest WinPixEventRuntime NuGet package and extracts it.
#
# -- Usage --
# `GetWinPixEvent.bat PathToFolder`
# or
# `powershell -ExecutionPolicy Bypass -File GetWinPixEvent.ps1 PathToFolder`
#
# Where PathToFolder is folder where extracted files will be placed.

param(
  [Parameter(Position = 0)]
  [string]$SaveFolder = ""
)

if ([string]::IsNullOrWhiteSpace($SaveFolder)) {
  $SaveFolder = Join-Path $PSScriptRoot "packages/WinPixEventRuntime"
}

$ErrorActionPreference = "Stop"

function Get-LatestNuGetVersion([string]$PackageId) {
  $idLower = $PackageId.ToLowerInvariant()
  $versionsUrl = "https://api.nuget.org/v3-flatcontainer/$idLower/index.json"

  $index = Invoke-RestMethod -Uri $versionsUrl -Method Get
  if (-not $index.versions -or $index.versions.Count -eq 0) {
    throw "No versions found for NuGet package '$PackageId'"
  }

  # Prefer stable versions (no pre-release suffix) when available.
  $stable = @($index.versions | Where-Object { $_ -notmatch "-" })
  $candidates = if ($stable.Length -gt 0) { $stable } else { @($index.versions) }

  $parsed = foreach ($v in $candidates) {
    $parts = ($v -split "[.-]")
    $nums = @()
    foreach ($p in $parts) {
      if ($p -match "^\d+$") {
        $nums += [int]$p
      } else {
        break
      }
    }
    while ($nums.Count -lt 4) { $nums += 0 }

    [PSCustomObject]@{
      A   = $nums[0]
      B   = $nums[1]
      C   = $nums[2]
      D   = $nums[3]
      Raw = $v
    }
  }

  $latest = ($parsed | Sort-Object -Property A, B, C, D | Select-Object -Last 1).Raw
  if ([string]::IsNullOrWhiteSpace($latest)) {
    throw "Failed to determine the latest version for NuGet package '$PackageId'"
  }
  return $latest
}

function New-EmptyDirectory([string]$Path) {
  if (Test-Path $Path) {
    Remove-Item -Recurse -Force -Path $Path
  }
  New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

New-Item -ItemType Directory -Force -Path $SaveFolder | Out-Null

$PackageId = "WinPixEventRuntime"
$Version = Get-LatestNuGetVersion $PackageId

Write-Host "Downloading $PackageId $Version"

$idLower = $PackageId.ToLowerInvariant()
$versionLower = $Version.ToLowerInvariant()
$nupkgUrl = "https://api.nuget.org/v3-flatcontainer/$idLower/$versionLower/$idLower.$versionLower.nupkg"

$tempDir = Join-Path $SaveFolder "_temp"
New-EmptyDirectory $tempDir

$nupkgPath = Join-Path $tempDir "$PackageId.$Version.nupkg"
Invoke-WebRequest -UseBasicParsing -Uri $nupkgUrl -OutFile $nupkgPath

Expand-Archive -Path $nupkgPath -DestinationPath $tempDir -Force

# Locate headers and libraries inside the package.
$pix3 = Get-ChildItem -Path $tempDir -Recurse -Filter "pix3.h" -File | Select-Object -First 1
if (-not $pix3) {
  throw "pix3.h not found inside extracted package"
}

$libCandidates = Get-ChildItem -Path $tempDir -Recurse -Filter "WinPixEventRuntime.lib" -File
if (-not $libCandidates -or $libCandidates.Count -eq 0) {
  throw "WinPixEventRuntime.lib not found inside extracted package"
}

# Prefer x64 library when present.
$lib = $libCandidates | Sort-Object -Property @{ Expression = { $_.FullName -notmatch "x64" } }, FullName | Select-Object -First 1

$dllCandidates = Get-ChildItem -Path $tempDir -Recurse -Filter "WinPixEventRuntime.dll" -File
$dll = $null
if ($dllCandidates -and $dllCandidates.Count -gt 0) {
  $dll = $dllCandidates | Sort-Object -Property @{ Expression = { $_.FullName -notmatch "x64" } }, FullName | Select-Object -First 1
}

# Create a layout matching our CMake expectations.
$includeOut = Join-Path $SaveFolder "Include"
$libOut = Join-Path $SaveFolder "Lib\x64"
$binOut = Join-Path $SaveFolder "Bin\x64"

New-Item -ItemType Directory -Force -Path $includeOut | Out-Null
New-Item -ItemType Directory -Force -Path $libOut | Out-Null
New-Item -ItemType Directory -Force -Path $binOut | Out-Null

Copy-Item -Force -Path (Join-Path $pix3.Directory.FullName "*") -Destination $includeOut
Copy-Item -Force -Path $lib.FullName -Destination $libOut

if ($dll) {
  Copy-Item -Force -Path $dll.FullName -Destination $binOut
}

Write-Host "Installed WinPixEventRuntime to: $SaveFolder"
Write-Host "  Include: $includeOut"
Write-Host "  Lib/x64 : $libOut"
if ($dll) {
  Write-Host "  Bin/x64 : $binOut"
}

# Cleanup temp folder.
Remove-Item -Recurse -Force -Path $tempDir

exit 0
