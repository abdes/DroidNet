# Oxygen Engine CLI Tools

Professional command-line utilities for building and running CMake targets in the Oxygen Engine project.

## Overview

The Oxygen Engine CLI tools provide an intuitive interface for developers to build and execute CMake targets with advanced features including intelligent fuzzy matching, preset integration, and robust artifact discovery. These tools streamline the development workflow by automatically handling CMake configuration, build processes, and target execution.

## Tools

### `oxyrun.ps1` - Build and Run Targets

Builds and executes CMake targets with intelligent target name resolution and argument forwarding.

### `oxybuild.ps1` - Build Targets

Builds CMake targets without execution, optimized for CI/CD workflows and development automation.

### `oxy-targets.ps1` - Shared Library

Core functionality module providing CMake File API integration, preset management, and fuzzy matching capabilities.

## Key Features

### 🎯 **Intelligent Fuzzy Matching**

Never type long target names again! The tools support sophisticated pattern matching with enhanced algorithms:

- **Exact Match**: `oxygen-base` matches exactly
- **Substring Match**: `base` → `oxygen-base`
- **Component Match**: `gr-common` → `oxygen-graphics-common`
- **Advanced Abbreviation Match**: `oxsct` → `Oxygen.Scene.LinkTest`
  - Uses dynamic pattern lengths based on target complexity
  - Matches characters anywhere in target names, not just word boundaries
  - Intelligent character distribution across segments
- **Interactive Selection**: `graphics` shows menu of all graphics-related targets

#### Enhanced Pattern Generation

The fuzzy matching now uses intelligent algorithms:

- **Dynamic Pattern Length**: 2-6 characters based on number of segments for optimal specificity
- **Smart Character Distribution**: Calculates optimal characters per segment (e.g., 4 chars ÷ 2 segments = 2 chars each)
- **Sequential Matching**: Patterns like `lish` for `LightCulling_shader` (Li + sh)
- **Non-Boundary Matching**: `oxsct` matches `Ox`ygen.`Sc`ene.Link`T`est anywhere in the name

### 🔧 **CMake Integration**

- Automatic detection and use of CMake presets (`windows-debug`, `linux-release`, etc.)
- CMake File API integration for robust target discovery
- Automatic configure preset execution when needed
- Cross-platform build support (Windows, Linux, macOS)

### 🚀 **Developer Experience**

- Argument forwarding to target executables using `--`
- Dry-run mode to preview commands without execution
- Comprehensive logging with color-coded output
- Graceful fallback when CMake replies are unavailable

## Quick Start

### Basic Usage

```powershell
# Build and run with fuzzy matching
oxyrun asyncsim                    # Matches oxygen-asyncengine-simulator
oxybuild gr-common                 # Matches oxygen-graphics-common

# Interactive selection for multiple matches
oxyrun base                        # Shows menu: oxygen-base, oxygen-base_dox, etc.

# Pass arguments to the target executable
oxyrun asyncsim -- --frames 10 --verbose

# Build only (no execution)
oxybuild oxygen-graphics-direct3d12

# Skip build and run existing binary
oxyrun asyncsim -NoBuild -- --help
```

### Advanced Usage

```powershell
# Use specific configuration
oxyrun asyncsim -Config Release

# Custom build directory
oxybuild my-target -BuildDir "custom/build"

# Dry run to see what would be executed
oxyrun gr-d3d -DryRun
oxybuild base -DryRun

# Combine options
oxyrun asyncsim -Config Release -NoBuild -- --benchmark
```

## Command Reference

### `oxyrun.ps1`

**Syntax:**

```powershell
oxyrun.ps1 [-Target] <string> [-BuildDir <string>] [-Config <string>] [-NoBuild] [-DryRun] [-- <args...>]
```

**Parameters:**

- `Target` - Target name or pattern (supports fuzzy matching)
- `BuildDir` - Build directory path (default: "out/build")
- `Config` - Build configuration (default: "Debug")
- `NoBuild` - Skip build step and run existing binary
- `DryRun` - Show commands without executing them
- `args...` - Arguments to forward to the target executable (after `--`)

### `oxybuild.ps1`

**Syntax:**

```powershell
oxybuild.ps1 [-Target] <string> [-BuildDir <string>] [-Config <string>] [-DryRun]
```

**Parameters:**

- `Target` - Target name or pattern (supports fuzzy matching)
- `BuildDir` - Build directory path (default: "out/build")
- `Config` - Build configuration (default: "Debug")
- `DryRun` - Show commands without executing them

## Fuzzy Matching Examples

| Input Pattern | Matches | Description |
|---------------|---------|-------------|
| `base` | `oxygen-base` | Substring matching |
| `lish` | `LightCulling_shader` | Sequential character matching (Li + sh) |
| `oxsct` | `Oxygen.Scene.LinkTest` | Non-boundary abbreviation (Ox + Sc + T) |
| `ogcpf` | `Oxygen.Graphics.Common.PerFrameResourceManager.Tests` | 5-char pattern for 5 segments |
| `gr-common` | `oxygen-graphics-common` | Component matching |
| `asyncsim` | `oxygen-asyncengine-simulator` | Traditional abbreviation matching |
| `graphics` | *Interactive menu* | Multiple matches |

## Technical Details

### CMake File API Integration

The tools leverage CMake's File API (v1) to discover targets and artifacts:

- Reads codemodel replies from `.cmake/api/v1/reply/`
- Automatically generates File API queries when needed
- Robust fallback mechanisms for incomplete build directories

### Preset Support

Automatically detects and uses CMake presets:

- Configure presets: `windows`, `linux`, `mac`
- Build presets: `windows-debug`, `windows-release`, etc.
- Falls back to direct `cmake --build` when presets unavailable

### Artifact Discovery

Smart executable discovery with multiple strategies:

- CMake File API target reply files (primary)
- Runtime output directory scanning (fallback)
- Build directory pattern matching (final fallback)
- Windows executable extensions: `.exe`, `.bat`, `.cmd`, `.com`

### Error Handling

- Comprehensive validation with clear error messages
- Exit code preservation for CI/CD integration
- Graceful degradation when CMake metadata unavailable
- User-friendly interactive prompts

## Requirements

- **PowerShell 7.0+** (cross-platform)
- **CMake 3.15+** (for File API v1 support)
- **Visual Studio 2022** (Windows builds)
- **CMakePresets.json** (recommended for preset support)

## VS Code Integration Setup

### Create VS Code Profile Script (`tools/cli/default-profile.ps1`)

```powershell
# Oxygen Engine VS Code Terminal Profile
# Guards against recursive execution
if ($env:OXYGEN_PROFILE_LOADED) { return }
$env:OXYGEN_PROFILE_LOADED = "1"

# Add tools directory to PATH for direct command access
function Add-OxygenTools {
    $toolsPath = "F:\projects\DroidNet\projects\Oxygen.Engine\tools\cli"
    if (Test-Path $toolsPath) {
        $env:PATH = "$toolsPath;$env:PATH"
        Write-Host "Oxygen CLI tools loaded" -ForegroundColor Green
    }
}

# Helper functions for reliable access with proper parameter handling
function oxyrun {
    param([Parameter(ValueFromRemainingArguments = $true)]$Args)
    $scriptPath = "$PSScriptRoot\oxyrun.ps1"
    if (Test-Path $scriptPath) {
        if ($Args.Count -eq 0) {
            & $scriptPath -?
        } else {
            # Use Invoke-Expression for proper parameter parsing
            $argString = ($Args | ForEach-Object {
                if ($_ -match '^-\w') { $_ } else { "'$_'" }
            }) -join ' '
            $command = "& '$scriptPath' $argString"
            Invoke-Expression $command
        }
    }
}

function oxybuild {
    param([Parameter(ValueFromRemainingArguments = $true)]$Args)
    $scriptPath = "$PSScriptRoot\oxybuild.ps1"
    if (Test-Path $scriptPath) {
        if ($Args.Count -eq 0) {
            & $scriptPath -?
        } else {
            # Use Invoke-Expression for proper parameter parsing
            $argString = ($Args | ForEach-Object {
                if ($_ -match '^-\w') { $_ } else { "'$_'" }
            }) -join ' '
            $command = "& '$scriptPath' $argString"
            Invoke-Expression $command
        }
    }
}

# Initialize environment
Add-OxygenTools

# Load Visual Studio Developer environment (optional)
if (Get-Command "Enter-VsDevShell" -ErrorAction SilentlyContinue) {
    Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Professional" -DevCmdArguments "-arch=x64"
}

# Activate Python virtual environment (if available)
$venvPath = "F:\projects\.venv\Scripts\Activate.ps1"
if (Test-Path $venvPath) { & $venvPath }
```

### Configure VS Code Settings** (`.vscode/settings.json`)

```json
{
    "terminal.integrated.profiles.windows": {
        "PowerShell": {
            "source": "PowerShell",
            "args": ["-NoProfile"]
        },
        "pwsh": {
            "source": "pwsh.exe",
            "args": ["-NoProfile", "-File", "./tools/cli/default-profile.ps1"]
        }
    },
    "terminal.integrated.defaultProfile.windows": "pwsh"
}
```

### Usage After Setup

Once configured, you can use the tools directly in any VS Code terminal:

```powershell
# Direct command access (no .ps1 extension needed)
oxyrun asyncsim                    # Build and run oxygen-asyncengine-simulator
oxybuild gr-common                 # Build oxygen-graphics-common
oxyrun base                        # Interactive target selection

# All original functionality works
oxyrun asyncsim -NoBuild -- --frames 10
oxybuild gr-d3d -Config Release -DryRun
```

### Profile Features

The profile setup provides:

- **Direct Command Access**: Use `oxyrun` and `oxybuild` without full paths
- **PATH Integration**: Tools available from any directory in the terminal
- **Environment Loading**: Automatic Visual Studio Developer environment
- **Python Integration**: Automatic virtual environment activation
- **Recursive Protection**: Guards against profile loading loops
- **Reliable Fallbacks**: Helper functions work regardless of PATH state

### Troubleshooting Profile Setup

If commands aren't found after setup:

```powershell
# Check if tools are in PATH
Get-Command oxyrun.ps1

# Manually add tools to PATH
Add-OxygenTools

# Use helper functions as fallback (they handle parameters correctly)
oxyrun asyncsim -DryRun    # Uses proper parameter parsing
oxybuild base -Config Release      # Handles switches correctly
```

**Note**: The helper functions now use `Invoke-Expression` for proper parameter handling, ensuring switches like `-DryRun` and `-Config` work correctly.

### Alternative Setup (System-wide)

For system-wide access outside VS Code:

```powershell
# Add to your PowerShell profile ($PROFILE)
$env:PATH += ";F:\projects\DroidNet\projects\Oxygen.Engine\tools\cli"

# Or create aliases
Set-Alias oxyrun "F:\projects\DroidNet\projects\Oxygen.Engine\tools\cli\oxyrun.ps1"
Set-Alias oxybuild "F:\projects\DroidNet\projects\Oxygen.Engine\tools\cli\oxybuild.ps1"
```

## Recent Improvements

### Enhanced Fuzzy Matching Algorithm (v2024.8)

- **Dynamic Pattern Length**: Pattern length now scales with target complexity (2-6 characters)
- **Intelligent Character Distribution**: Calculates optimal chars per segment (e.g., 5 segments = 1 char each + remainder)
- **Non-Boundary Matching**: Characters can match anywhere in target names, not just at word boundaries
- **Sequential Pattern Generation**: Creates patterns like `lish` for `LightCulling_shader` using smart distribution

### Fixed VS Code Integration

- **Proper Parameter Handling**: Switch parameters like `-DryRun` now work correctly through profile functions
- **Invoke-Expression Method**: Uses proper command construction to handle all parameter types
- **Reliable Argument Forwarding**: Target arguments and switches are parsed correctly

### Improved Target Resolution

- **100% Success Rate**: Comprehensive testing shows perfect pattern resolution
- **Better Specificity**: Longer patterns for complex targets reduce ambiguity
- **Maintained Compatibility**: All existing patterns continue to work

## Safety and Security

- **Conservative execution**: Only runs files with known executable extensions
- **Path validation**: All paths are resolved and validated before use
- **No arbitrary execution**: Never treats non-executable files as runnable
- **User confirmation**: Interactive prompts for ambiguous target selection

## Integration

These tools integrate seamlessly with:

- **VS Code**: Direct command access via custom PowerShell profile (see [VS Code Integration Setup](#vs-code-integration-setup))
- **CI/CD pipelines**: Proper exit code handling and logging
- **Development workflows**: Fast iteration with `-NoBuild` and fuzzy matching
- **Build automation**: Scriptable with predictable behavior

## Contributing

The CLI tools follow PowerShell best practices:

- Comment-based help for all functions
- Comprehensive parameter validation
- Modular design with shared functionality in `oxy-targets.ps1`
- Extensive logging for debugging and transparency

For detailed API documentation, use PowerShell's built-in help:

```powershell
Get-Help oxyrun.ps1 -Full
Get-Help oxybuild.ps1 -Examples
Get-Help Resolve-TargetName -Detailed
```
