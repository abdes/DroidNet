<#
.SYNOPSIS
Build a target using the standardized Oxygen Engine build workflow.

.DESCRIPTION
Builds the specified CMake target using the standardized build workflow that ensures
all dependencies and configuration are properly set up. This script is designed for
use in CI/CD pipelines and developer workflows where only building (not running) is required.

The script follows the standardized build workflow:
1. Check if out/build exists -> if not, run Conan install with platform-specific profiles
2. Check if CMake is configured -> if not, run CMake configure preset
3. Build the target using appropriate CMake preset or direct cmake command

Uses the fixed out/build directory for all builds to ensure consistency and proper
integration with Conan dependency management and CMake presets.

.PARAMETER Target
The name of the target to build. This must match a CMake target name defined
in your CMakeLists.txt files.

Supports intelligent fuzzy matching:
- Exact match: "oxygen-base" matches exactly
- Substring match: "base" matches "oxygen-base"
- Component match: "gr-common" matches "oxygen-graphics-common"
- Abbreviation match: "asyncsim" matches "oxygen-asyncengine-simulator"
- Interactive selection: "graphics" shows menu of graphics-related targets

If multiple targets match, an interactive selection menu will be displayed.

.PARAMETER Config
The build configuration (Debug, Release, etc.). Defaults to "Debug". The script will
use platform-specific Conan profiles and CMake presets based on this configuration.

Debug builds use ASAN-enabled profiles for better debugging, Release builds use
optimized profiles without debugging overhead.

.PARAMETER DryRun
Show what would be executed without actually running it. Displays all commands
(Conan install, CMake configure, build) without performing any actions.

.EXAMPLE
oxybuild.ps1 oxygen-asyncengine-simulator
Build the oxygen-asyncengine-simulator target using default Debug configuration.

.EXAMPLE
oxybuild.ps1 asyncsim
Build the oxygen-asyncengine-simulator target using fuzzy matching abbreviation.

.EXAMPLE
oxybuild.ps1 gr-common -Config Release
Build the oxygen-graphics-common target using component matching and Release configuration.

.EXAMPLE
oxybuild.ps1 base
Display interactive menu to select from oxygen-base and other base-related targets.

.EXAMPLE
oxybuild.ps1 my-target -DryRun
Show what build commands would be executed without actually building.

.EXAMPLE
oxybuild.ps1 my-target -Config Release
Build using Release configuration with optimized Conan profile.

.NOTES
Build System Integration:
- Uses standardized out/build directory for all builds
- Automatically runs Conan install with platform-specific profiles when needed
- Uses platform-specific CMake presets: "windows", "linux", "mac"
- Runs CMake configure automatically if build system is not configured
- Falls back to direct cmake --build commands if no presets are found

Platform-Specific Profiles:
- Windows: Debug -> windows-msvc-asan.ini, Release -> windows-msvc.ini
- Linux: Debug -> linux-gcc-asan.ini, Release -> linux-clang.ini
- macOS: Debug -> macos-clang-asan.ini, Release -> macos-clang.ini

Target Validation:
- Uses CMake File API codemodel to validate target existence when available
- Continues with build even if codemodel validation fails (useful for CI)
- Prefers target configurations matching the specified Config parameter

Error Handling:
- Exits with build tool's exit code for proper CI/CD integration
- Provides detailed logging with modern CLI styling and icons
- Validates all prerequisites before proceeding with build

Designed for CI/CD:
- Focused on building only (no execution)
- Preserves exit codes for pipeline integration
- Consistent environment setup through Conan and CMake presets
#>
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Target,
    [string]$Config = "Debug",
    [switch]$DryRun
)

. (Join-Path $PSScriptRoot 'oxy-targets.ps1')

# Set global verbose mode based on PowerShell's built-in VerbosePreference
$global:VerboseMode = ($VerbosePreference -eq 'Continue')

$buildRoot = Get-StandardBuildRoot

# Resolve target name using fuzzy matching
$resolvedTarget = Resolve-TargetName $Target $buildRoot
if (-not $resolvedTarget) {
    Write-LogErrorAndExit "Target resolution failed or cancelled" 1
}

# We don't require target-metadata.json; prefer codemodel for hinting but allow
# builds even if codemodel is absent.
try {
    $found = Get-TargetFromCodemodel $buildRoot $resolvedTarget
} catch {
    Write-LogVerbose "Codemodel lookup failed: $($_.Exception.Message)"
    $found = $null
}

# If multiple, prefer matching config
if ($found -and $found.Count -gt 1) {
    $sel = $found | Where-Object { $_.reply_file -and ($_.reply_file -match "-$Config-") } | Select-Object -First 1
    if ($sel) {
        $found = $sel
    } else {
        $found = $found | Select-Object -First 1
    }
}

Invoke-BuildForTarget -Target $resolvedTarget -Config $Config -DryRun:$DryRun

if ($DryRun) {
    Write-Host ""
}
