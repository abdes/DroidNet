<#
.SYNOPSIS
Generate both Ninja and Visual Studio build trees with a single command.

.DESCRIPTION
- Ninja Multi-Config (for VSCode development)
- Visual Studio (for standard solution-based development)

The script performs dual Conan installations (Debug & Release) for both trees
to ensure all multi-config metadata is available.
#>

param(
    [string]$ProfileHost = "profiles/windows-msvc.ini",
    [string]$ProfileBuild = $null,
    [string]$Build = "missing",
    [string]$DeployerFolder = "out/install",
    [string]$DeployerPackage = "Oxygen/0.1.0"
)

if (-not $ProfileBuild) { $ProfileBuild = $ProfileHost }

$repoRoot = Resolve-Path "$PSScriptRoot/.."
$hostProfilePath = $null
if (Test-Path $ProfileHost) {
    $hostProfilePath = Resolve-Path $ProfileHost
} elseif (Test-Path (Join-Path $repoRoot $ProfileHost)) {
    $hostProfilePath = Resolve-Path (Join-Path $repoRoot $ProfileHost)
}

$isAsan = $false
if ($hostProfilePath) {
    $isAsan = [bool](Select-String -Path $hostProfilePath -Pattern "^\s*sanitizer\s*=\s*asan\s*$")
}

$suffix = if ($isAsan) { "asan-" } else { "" }
$vsBuildDir = "out/build-${suffix}vs"
$ninjaBuildDir = "out/build-${suffix}ninja"
$configurations = if ($isAsan) { @("Debug") } else { @("Debug", "Release") }

# Clean up specific directories
Write-Host "Cleaning build environment..." -ForegroundColor Gray
$targetsToClean = @(
    (Join-Path $repoRoot $vsBuildDir),
    (Join-Path $repoRoot $ninjaBuildDir)
)

foreach ($config in $configurations) {
    $sub = if ($isAsan) { "Asan" } else { $config }
    $targetsToClean += (Join-Path $repoRoot $DeployerFolder $sub)
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
    "--profile:host=$ProfileHost", "--profile:build=$ProfileBuild",
    "--build=$Build",
    "--deployer-folder=$DeployerFolder",
    "--deployer-package=$DeployerPackage",
    "-c", "tools.cmake.cmakedeps:new=will_break_next",
    # Force binary isolation in the Conan cache so that ASan and non-ASan
    # dependencies are treated as different packages and don't pollute each other.
    "-c", "tools.info.package_id:confs=['user.oxygen:sanitizer']",
    "-c", "user.oxygen:sanitizer=$($isAsan ? 'asan' : 'none')"
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
