<#
.SYNOPSIS
Run a built target using oxy-common helpers.

.DESCRIPTION
Builds and runs the specified target executable using CMake presets when available.
Command line arguments can be passed to the target executable by placing them after
a double dash (--).

The script integrates with CMake's build system and preset mechanism:
1. Automatically detects and uses CMake build presets (e.g., "windows-debug")
2. Runs configure presets if needed to generate CMake File API replies
3. Discovers target executables using CMake File API codemodel
4. Falls back to direct cmake commands if no presets are found
5. Forwards arguments to the target executable after building

.PARAMETER Target
The name of the target to build and run. This must match a CMake target name
defined in your CMakeLists.txt files.

Supports intelligent fuzzy matching:
- Exact match: "oxygen-base" matches exactly
- Substring match: "base" matches "oxygen-base"
- Component match: "gr-common" matches "oxygen-graphics-common"
- Abbreviation match: "asyncsim" matches "oxygen-asyncengine-simulator"
- Interactive selection: "graphics" shows menu of graphics-related targets

If multiple targets match, an interactive selection menu will be displayed.

.PARAMETER Config
The build configuration (Debug, Release, etc.). Defaults to "Debug". The script will
look for a CMake build preset named "<platform>-<config>" (e.g., "windows-debug") and
use it if available.

.PARAMETER NoBuild
Skip the build step and just run the target executable. Useful when the target is
already built.

.PARAMETER DryRun
Show what would be executed without actually running it. Displays the build command
and final execution command without performing any actions.

.EXAMPLE
oxyrun.ps1 oxygen-asyncengine-simulator
Run the oxygen-asyncengine-simulator target without arguments.

.EXAMPLE
oxyrun.ps1 asyncsim
Run the oxygen-asyncengine-simulator target using fuzzy matching abbreviation.

.EXAMPLE
oxyrun.ps1 gr-d3d -- --help
Use component matching to find oxygen-graphics-direct3d12 and pass --help argument.

.EXAMPLE
oxyrun.ps1 base
Display interactive menu to select from oxygen-base and other base-related targets.

.EXAMPLE
oxyrun.ps1 oxygen-asyncengine-simulator -DryRun
Show what would be executed for the target without actually running it.

.EXAMPLE
oxyrun.ps1 oxygen-asyncengine-simulator -- -f 1 --verbose
Run the target and pass "-f 1 --verbose" as arguments to the executable.

.EXAMPLE
oxyrun.ps1 oxygen-asyncengine-simulator -Config Release -NoBuild -- --help
Skip building, use Release configuration, and run the target with --help argument.

.NOTES
Build System Integration:
- Automatically detects CMake presets in CMakePresets.json
- Uses platform-specific presets: "windows-debug", "linux-release", etc.
- Runs configure presets automatically if CMake File API replies are missing
- Falls back to direct cmake --build commands if no presets are found

Target Discovery:
- Uses CMake File API codemodel to locate target executables
- Searches multiple patterns: target-<name>-<config>-*.json, target-<name>-*.json
- Falls back to searching build output directories for executables
- Prefers executables in runtime output directories specified by CMake

Argument Forwarding:
- All arguments after "--" are forwarded directly to the target executable
- Arguments are passed using PowerShell's @args splatting for proper handling
- No interpretation or modification of forwarded arguments is performed

Error Handling:
- Exits with target's exit code to preserve build/test result status
- Provides detailed logging with modern CLI styling and icons
- Fatal errors for build system failures (Conan, CMake configure, build failures)
- Validates build directory and target existence before proceeding
#>
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Target,
    [string]$Config = "Debug",
    [switch]$NoBuild,
    [switch]$DryRun,
    [Parameter(ValueFromRemainingArguments = $true)]$RemainingArgs
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

# Discover target entries from the CMake File API codemodel (first attempt)
$found = @()
try {
    $found = Get-TargetFromCodemodel $buildRoot $resolvedTarget
} catch {
    Write-LogVerbose "Initial codemodel lookup failed: $($_.Exception.Message)"
}

if (-not $found -or $found.Count -eq 0) {
    Write-LogVerbose "Target '$resolvedTarget' not found in codemodel on first attempt"
    $found = $null
}

# Build step - must come before artifact discovery
if (-not $NoBuild) {
    Invoke-BuildForTarget -Target $resolvedTarget -Config $Config -DryRun:$DryRun

    # Retry codemodel lookup after build (CMake File API should now be available)
    if (-not $found) {
        Write-LogVerbose "Retrying codemodel lookup after build..."
        try {
            $found = Get-TargetFromCodemodel $buildRoot $resolvedTarget
            if ($found -and $found.Count -gt 0) {
                Write-LogVerbose "Target '$resolvedTarget' found in codemodel after build"
            }
        } catch {
            Write-LogErrorAndExit "Codemodel lookup failed even after successful build: $($_.Exception.Message)." 3
        }
    }
}

# If multiple, prefer matching config
if ($found -and $found.Count -gt 1) {
    $sel = $found | Where-Object { $_.reply_file -and ($_.reply_file -match "-$Config-") }
    | Select-Object -First 1
    if ($sel) { $found = $sel } else { $found = $found | Select-Object -First 1 }
}

# Handle dry-run execution display (shared between build and no-build scenarios)
if ($DryRun) {
    Write-LogInfo "Running target: $resolvedTarget"
    Write-Host ""
    Write-Host "Dry Run Mode - Would Execute:" -ForegroundColor Magenta
    Write-Host "  Executable: " -NoNewline -ForegroundColor DarkGray
    Write-Host "$(Format-CompactPath (Join-Path $buildRoot "bin/$Config/$resolvedTarget.exe"))" -ForegroundColor White
    Write-Host "  Arguments:  " -NoNewline -ForegroundColor DarkGray
    if ($RemainingArgs -and $RemainingArgs.Count -gt 0) {
        Write-Host ($RemainingArgs -join ' ') -ForegroundColor White
    } else {
        Write-Host "(none)" -ForegroundColor DarkGray
    }
    Write-Host ""
    exit 0
}

# Discover artifact after building (only for actual execution)
$artifact = $null
$replyFile = Get-ReplyFileForTarget $buildRoot $resolvedTarget $Config
$artifact = Get-ArtifactFromReply $buildRoot $replyFile

# Fallbacks preserved from original
if (-not $artifact) {
    if ($found.runtime_output_directory -and $null -ne $found.runtime_output_directory) {
        $outdir = $found.runtime_output_directory
        $exeFilters = '*.exe', '*.bat', '*.cmd', '*.com'
        $candidates = @()
        foreach ($f in $exeFilters) {
            try {
                $candidates += Get-ChildItem -Path $outdir -Filter $f -Recurse -File -ErrorAction SilentlyContinue \
                | Where-Object { $_.Name -like "*${resolvedTarget}*" }
            } catch { }
        }
        if ($candidates.Count -gt 0) { $artifact = $candidates[0].FullName }
    }
}
if (-not $artifact) {
    try { $binPath = Join-Path $buildRoot "bin/$Config" } catch { $binPath = Join-Path $buildRoot "bin" }
    if (-not (Test-Path $binPath)) { $binPath = Join-Path $buildRoot "bin" }
    $exeCandidates = @()
    try { $exeCandidates = Get-ChildItem -Path $binPath -Include *.exe, *.bat, *.cmd, *.com -File -ErrorAction SilentlyContinue | Where-Object { $_.BaseName -eq $resolvedTarget -or $_.Name -like "$resolvedTarget*" } } catch {}
    if ($exeCandidates -and $exeCandidates.Count -gt 0) { $artifact = $exeCandidates[0].FullName }
}

if (-not $artifact) {
    # Check if this is a library target (not executable)
    if ($found -and $found.type -and ($found.type -eq "SHARED_LIBRARY" -or $found.type -eq "STATIC_LIBRARY" -or $found.type -eq "MODULE_LIBRARY")) {
        Write-LogErrorAndExit "Target '$resolvedTarget' is a library ($($found.type.ToLower())), not an executable. Use oxybuild to build libraries." 4
    } else {
        Write-LogErrorAndExit "Unable to locate executable for target '$resolvedTarget'. Target may not produce an executable." 4
    }
}

# Forward args after --
$forward = @()
if ($RemainingArgs) {
    # All remaining args are treated as forwarded arguments
    $forward = $RemainingArgs
}

Write-LogAction "Running: $(Format-CompactPath $artifact) $($forward -join ' ')"
try {
    & $artifact @forward
    $ec = $LASTEXITCODE
} catch {
    Write-LogErrorAndExit "Execution failed: $($_.Exception.Message)" 5
}

if (-not $DryRun) {
    Write-Host ""
}

exit $ec
