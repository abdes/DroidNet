<#
.SYNOPSIS
Generate both Ninja and Visual Studio build trees with a single command.

.DESCRIPTION
- Ninja Multi-Config (for VSCode development)
- Visual Studio (for standard solution-based development)

The script performs dual Conan installations (Debug & Release) for both trees
to ensure all multi-config metadata is available.

.PARAMETER Profile
Required positional path to the Conan profile used for both host and build.

.PARAMETER Build
Conan build mode passed to 'conan install' (default: "missing").

.PARAMETER DeployerFolder
Output folder for deployment artifacts (default: "out/install").

.PARAMETER DeployerPackage
Deployer package name (default: "Oxygen/0.1.0").

.PARAMETER Help
Show this help message and exit.

.EXAMPLE
.\tools\generate-builds.ps1 -ProfileHost profiles/windows-msvc-asan.ini

.EXAMPLE
.\tools\generate-builds.ps1 -Help

#>

param(
    [Alias('h','?')][switch]$Help,
    [Alias('u')][switch]$Usage,
    [Parameter(Position=0)][string]$BuildProfile,
    [string]$Build = "missing",
    [string]$DeployerFolder = "out/install",
    [string]$DeployerPackage = "Oxygen/0.1.0"
)

function Show-Usage {
    Write-Host ""
    Write-Host "Usage: .\tools\generate-builds.ps1 <profile> [-Build <mode>] [-DeployerFolder <path>] [-DeployerPackage <pkg>] [-Help]" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Parameters:" -ForegroundColor Gray
    Write-Host "  profile             Path to Conan profile used for both host and build (required, positional)"
    Write-Host "  -Build              Conan build mode (default: missing)"
    Write-Host "  -DeployerFolder     Deployer output folder (default: out/install)"
    Write-Host "  -DeployerPackage    Deployer package (default: Oxygen/0.1.0)"
    Write-Host "  -Help, -h, -?       Show this help message and exit"
    Write-Host ""
    Write-Host "Note: Relative paths for profiles and output (e.g. 'out') are resolved relative to the repository root." -ForegroundColor Gray
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Gray
    Write-Host "  .\tools\generate-builds.ps1 profiles/windows-msvc-asan.ini"
    Write-Host "  .\tools\generate-builds.ps1 -Help"
    Write-Host ""
}

if ($Help -or $Usage) {
    Show-Usage
    exit 0
}

if (-not $Help -and -not $BuildProfile) {
    Write-Host "Error: profile argument is required." -ForegroundColor Red
    Show-Usage
    exit 1
}

if ($Help -or $Usage) {
    function Show-Usage {
        Write-Host ""
        Write-Host "Usage: .\tools\generate-builds.ps1 [-ProfileHost <path>] [-ProfileBuild <path>] [-Build <mode>] [-DeployerFolder <path>] [-DeployerPackage <pkg>] [-Help]" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Parameters:" -ForegroundColor Gray
        Write-Host "  profile             Path to Conan profile used for both host and build (required, positional)"        Write-Host "  -Build              Conan build mode (default: missing)"
        Write-Host "  -DeployerFolder     Deployer output folder (default: out/install)"
        Write-Host "  -DeployerPackage    Deployer package (default: Oxygen/0.1.0)"
        Write-Host "  -Help, -h, -?       Show this help message and exit"
        Write-Host ""
        Write-Host "Note: Relative paths for profiles and output (e.g. 'out') are resolved relative to the repository root." -ForegroundColor Gray
        Write-Host ""
        Write-Host "Examples:" -ForegroundColor Gray
        Write-Host "  .\tools\generate-builds.ps1 -Profile profiles/windows-msvc-asan.ini"
        Write-Host "  .\tools\generate-builds.ps1 -Help"
        Write-Host ""
    }
    Show-Usage
    exit 0
}

# Use single profile for both host and build (keeps compatibility with older flags via aliases)
$BuildProfileHost = $BuildProfile
$BuildProfileBuild = $BuildProfile

$repoRoot = Resolve-Path "$PSScriptRoot/.."

# Resolve profiles and output paths relative to the repository root when they are not absolute.
if (-not [System.IO.Path]::IsPathRooted($BuildProfileHost)) {
    $candidate = Join-Path $repoRoot $BuildProfileHost
    if (Test-Path $BuildProfileHost) {
        $BuildProfileHost = (Resolve-Path $BuildProfileHost).Path
    } elseif (Test-Path $candidate) {
        $BuildProfileHost = (Resolve-Path $candidate).Path
    } else {
        # Convert to repo-root relative even if it doesn't exist yet.
        $BuildProfileHost = $candidate
    }
}

if (-not [System.IO.Path]::IsPathRooted($BuildProfileBuild)) {
    $candidate = Join-Path $repoRoot $BuildProfileBuild
    if (Test-Path $BuildProfileBuild) {
        $BuildProfileBuild = (Resolve-Path $BuildProfileBuild).Path
    } elseif (Test-Path $candidate) {
        $BuildProfileBuild = (Resolve-Path $candidate).Path
    } else {
        $BuildProfileBuild = $candidate
    }
}

if (-not [System.IO.Path]::IsPathRooted($DeployerFolder)) {
    $DeployerFolder = Join-Path $repoRoot $DeployerFolder
}

$hostProfilePath = $null
if (Test-Path $BuildProfileHost) {
    $hostProfilePath = Resolve-Path $BuildProfileHost
}

$isAsan = $false
if ($hostProfilePath) {
    $isAsan = [bool](Select-String -Path $hostProfilePath -Pattern "^\s*sanitizer\s*=\s*asan\s*$")
}

$suffix = if ($isAsan) { "asan-" } else { "" }
$vsBuildDir = "out/build-${suffix}vs"
$ninjaBuildDir = "out/build-${suffix}ninja"
$configurations = if ($isAsan) { @("Debug") } else { @("Debug", "Release") }

$sanitizer = if ($isAsan) { 'asan' } else { 'none' }

# Clean up specific directories
Write-Host "Cleaning build environment..." -ForegroundColor Gray
$targetsToClean = @(
    (Join-Path $repoRoot $vsBuildDir),
    (Join-Path $repoRoot $ninjaBuildDir)
)

foreach ($config in $configurations) {
    $sub = if ($isAsan) { "Asan" } else { $config }
    $targetsToClean += (Join-Path $DeployerFolder $sub)
}

foreach ($target in $targetsToClean) {
    if (Test-Path $target) {
        Remove-Item -Path $target -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$ninjaPreset = "windows-ninja"

# Common base args
$conanBaseArgs = @(
    "install", ".",
    "--profile:host=$BuildProfileHost", "--profile:build=$BuildProfileBuild",
    "--build=$Build",
    "--deployer-folder=$DeployerFolder",
    "--deployer-package=$DeployerPackage",
    "-c", "tools.cmake.cmakedeps:new=will_break_next",
    # Force binary isolation in the Conan cache so that ASan and non-ASan
    # dependencies are treated as different packages and don't pollute each other.
    "-c", "tools.info.package_id:confs=['user.oxygen:sanitizer']",
    "-c", "user.oxygen:sanitizer=$sanitizer"
)

Write-Host "`n=== Phase 1: Ninja Multi-Config ===" -ForegroundColor Cyan
$ninjaConanArgs = $conanBaseArgs + @(
    "-c", "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config"
)

Write-Host "Installing dependencies (Debug + Release)..." -ForegroundColor Gray
foreach ($config in $configurations) {
    conan @ninjaConanArgs -s build_type=$config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Configuring Ninja build tree..." -ForegroundColor Gray
cmake --preset $ninjaPreset
if ($LASTEXITCODE -ne 0) { Write-Host "Ninja configuration failed" -ForegroundColor Yellow }

Write-Host "`n=== Phase 2: Visual Studio 18 ===" -ForegroundColor Cyan
$vsConanArgs = $conanBaseArgs + @(
    "-c", "tools.cmake.cmaketoolchain:generator=Visual Studio 18 2026"
)

Write-Host "Installing dependencies (Debug + Release)..." -ForegroundColor Gray
foreach ($config in $configurations) {
    conan @vsConanArgs -s build_type=$config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Generating Visual Studio solution (Plain old CMake)..." -ForegroundColor Gray
$vsToolchain = "$vsBuildDir/generators/conan_toolchain.cmake"
cmake -G "Visual Studio 18 2026" -A x64 -S . -B $vsBuildDir -D "CMAKE_TOOLCHAIN_FILE=$vsToolchain"
if ($LASTEXITCODE -ne 0) { Write-Host "VS configuration failed" -ForegroundColor Yellow }

Write-Host "`n=== Success ===" -ForegroundColor Green
Write-Host "Ninja Folder:  $ninjaBuildDir/"
Write-Host "VS Folder:     $vsBuildDir/"
