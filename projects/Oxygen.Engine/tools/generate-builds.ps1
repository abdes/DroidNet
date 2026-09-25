<#
.SYNOPSIS
Generate selected Ninja and Visual Studio build trees with a single command.

.DESCRIPTION
- Ninja Multi-Config (for VSCode development)
- Visual Studio (for standard solution-based development)

The script installs Debug, Release and RelWithDebInfo dependencies for both
generators (Debug only for ASan), or just the selected -Generator.
Each tree has its own Conan preset namespace.
Standard, Tracy and ASan trees can coexist in the root preset list.

.PARAMETER BuildProfile
Required positional path to the Conan profile used for both host and build.

.PARAMETER Build
Conan build mode passed to 'conan install' (default: "missing").

.PARAMETER DeployerFolder
Output folder for deployment artifacts (default: "out/install").

.PARAMETER DeployerPackage
Deployer package name (default: "oxygen/0.1.0").

.PARAMETER NoClean
Do not clean existing build directories.

.PARAMETER WithTracy
Generate Tracy (profiler enabled) build trees instead of standard builds.

.PARAMETER Generator
Generate Ninja, VisualStudio, or All (the default).

.PARAMETER Help
Show this help message and exit.

.EXAMPLE
.\tools\generate-builds.ps1 -BuildProfile profiles/windows-msvc-asan.ini

.EXAMPLE
.\tools\generate-builds.ps1 -Help

#>

param(
    [Alias('h', '?')][switch]$Help,
    [Alias('u')][switch]$Usage,
    [Parameter(Position = 0)][string]$BuildProfile,
    [string]$Build = "missing",
    [string]$DeployerFolder = "out/install",
    [string]$DeployerPackage = "oxygen/0.1.0",
    [switch]$NoClean,
    [switch]$WithTracy,
    [ValidateSet('All', 'Ninja', 'VisualStudio')][string]$Generator = 'All'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Show-Usage {
    Write-Host ""
    Write-Host "Usage: .\tools\generate-builds.ps1 <profile> [-Generator All|Ninja|VisualStudio] [-Build <mode>] [-DeployerFolder <path>] [-DeployerPackage <pkg>] [-NoClean] [-WithTracy] [-Help]" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Parameters:" -ForegroundColor Gray
    Write-Host "  profile             Path to Conan profile used for both host and build (required, positional)"
    Write-Host "  -Build              Conan build mode (default: missing)"
    Write-Host "  -DeployerFolder     Deployer output folder (default: out/install)"
    Write-Host "  -DeployerPackage    Deployer package (default: oxygen/0.1.0)"
    Write-Host "  -NoClean            Do not clean existing build directories"
    Write-Host "  -WithTracy          Generate Tracy build trees; keep standard trees"
    Write-Host "  -Generator          All (default), Ninja, or VisualStudio"
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



# Use single profile for both host and build (keeps compatibility with older flags via aliases)
$BuildProfileHost = $BuildProfile
$BuildProfileBuild = $BuildProfile

$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path

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

# Resolve includes and profile composition exactly as Conan does. Resolve from the
# same directory as install, before touching any build/deployment output.
Push-Location $repoRoot
try {
    $profileJson = conan profile show "--profile:host=$BuildProfileHost" "--profile:build=$BuildProfileBuild" --format=json
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $resolvedProfile = ($profileJson -join "`n") | ConvertFrom-Json
} finally {
    Pop-Location
}
$isAsan = $resolvedProfile.host.conf.'user.oxygen:sanitizer' -eq 'asan'

$suffix = if ($isAsan) { "asan-" } else { "" }
$prefix = if ($WithTracy) { 'tracy-' } else { '' }
$trees = @(
    @{ Selection = 'Ninja'; Name = 'ninja'; Generator = 'Ninja Multi-Config' }
    @{ Selection = 'VisualStudio'; Name = 'vs'; Generator = 'Visual Studio 18 2026' }
) | Where-Object { $Generator -eq 'All' -or $_.Selection -eq $Generator }
$configurations = if ($isAsan) { @("Debug") } else { @("Debug", "Release", "RelWithDebInfo") }


# Clean up specific directories
if (-not $NoClean) {
    Write-Host "Cleaning build environment..." -ForegroundColor Gray
    $targetsToClean = @()
    foreach ($tree in $trees) {
        $targetsToClean += (Join-Path $repoRoot "out/build-${prefix}${suffix}$($tree.Name)")
    }

    foreach ($config in $configurations) {
        $sub = if ($isAsan) { "Asan" } else { $config }
        $targetsToClean += (Join-Path $DeployerFolder $sub)
    }

    foreach ($target in $targetsToClean) {
        if (Test-Path $target) {
            Remove-Item -Path $target -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
} else {
    Write-Host "Skipping clean step (--no-clean)..." -ForegroundColor Gray
}

# Common base args
$conanBaseArgs = @(
    "install", ".",
    "--profile:host=$BuildProfileHost", "--profile:build=$BuildProfileBuild",
    "--build=$Build",
    "--deployer-folder=$DeployerFolder",
    "--deployer-package=$DeployerPackage",
    "-o", "with_tracy=$([bool]$WithTracy)",
    # This is the contributor entry point. Dependency consumers default these off.
    "-o", "&:tools=True", "-o", "&:tests=True", "-o", "&:benchmarks=True",
    "-o", "&:examples=True", "-o", "&:docs=True",
    # NOTE: CMakeConfigDeps is required for multi-config generators (Ninja Multi-Config, Visual Studio).
    # Conan only generates Debug/Release packages, but multi-config generators (especially Ninja)
    # may request other configurations (RelWithDebInfo, MinSizeRel, etc.). CMakeDeps cannot map
    # these gracefully; only CMakeConfigDeps handles this scenario without breaking builds.
    # This flag is marked "will_break_next" (experimental), so monitor Conan releases for changes.
    "-c", "tools.cmake.cmakedeps:new=will_break_next"

)

Push-Location $repoRoot
try {
    foreach ($tree in $trees) {
        Write-Host "`n=== $($tree.Generator): ${prefix}${suffix}$($tree.Name) ===" -ForegroundColor Cyan
        $conanArgs = $conanBaseArgs + @(
            "-c", "tools.cmake.cmaketoolchain:generator=$($tree.Generator)"
        )

        Write-Host "Installing dependencies ($($configurations -join ', '))..." -ForegroundColor Gray
        foreach ($config in $configurations) {
            conan @conanArgs -s build_type=$config
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        Write-Host "Configuring $($tree.Name) build tree..." -ForegroundColor Gray
        $preset = "oxygen-${prefix}${suffix}$($tree.Name)-default"
        cmake --preset $preset
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally {
    Pop-Location
}

Write-Host "`n=== Success ===" -ForegroundColor Green
foreach ($tree in $trees) {
    Write-Host "$($tree.Name) Folder: out/build-${prefix}${suffix}$($tree.Name)/"
}

Write-Host "`nRuntime dependency PATH is resolved per build configuration by CMake custom commands and tools/cli/oxyrun.ps1." -ForegroundColor Gray
