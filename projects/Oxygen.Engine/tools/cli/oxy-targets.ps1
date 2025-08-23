<#
.SYNOPSIS
    Shared helper functions for CMake target building and execution in the Oxygen Engine project.

.DESCRIPTION
    This module provides core functionality for oxybuild.ps1 and oxyrun.ps1, implementing
    CMake File API integration, preset management, and target artifact discovery. It serves
    as the foundation for the Oxygen Engine's build and execution workflow.

    Key capabilities:
    - Conan dependency management with platform-specific profiles
    - CMake preset discovery and execution (configure and build presets)
    - CMake File API reply parsing for target metadata
    - Cross-platform build root resolution and validation
    - Target artifact path discovery from CMake replies
    - Integrated logging with consistent formatting

.NOTES
    File Name   : oxy-targets.ps1
    Author      : Oxygen Engine Project
    Requires    : PowerShell 7.0+, CMake 3.15+ (for File API v1), Conan 2.0+
    Dependencies: CMakePresets.json, CMake File API replies, Conan profiles

.LINK
    https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html

.EXAMPLE
    # Discover and build a target using presets
    $buildRoot = Get-StandardBuildRoot
    Invoke-BuildForTarget -Target "MyTarget" -Config "Debug"

.EXAMPLE
    # Find target artifact for execution
    $replyFile = Get-ReplyFileForTarget $buildRoot "MyApp" "Release"
    $executable = Get-ArtifactFromReply $buildRoot $replyFile
    if ($executable) { & $executable }
#>

# Constants
$script:BUILD_DIR = "out/build"
$script:CONAN_DEPLOY_DIR = "out/full_deploy"
$script:CONAN_PROFILES_DIR = "profiles"

<#
.SYNOPSIS
    Writes an informational message with modern CLI styling.

.DESCRIPTION
    Outputs a standardized info message with visual icon and blue accent color.
    Follows modern CLI patterns from tools like npm, yarn, and VS Code.

.PARAMETER msg
    The message to display.

.EXAMPLE
    Write-LogInfo "Starting build process"
#>
function Write-LogInfo($msg) {
  Write-Host "ℹ " -ForegroundColor Blue -NoNewline
  Write-Host $msg -ForegroundColor White
}

<#
.SYNOPSIS
    Writes a success message with modern CLI styling.

.DESCRIPTION
    Outputs a success message with checkmark icon and green accent color.

.PARAMETER msg
    The message to display.

.EXAMPLE
    Write-LogSuccess "Build completed successfully"
#>
function Write-LogSuccess($msg) {
  Write-Host "✓ " -ForegroundColor Green -NoNewline
  Write-Host $msg -ForegroundColor White
}

<#
.SYNOPSIS
    Writes a warning message with modern CLI styling.

.DESCRIPTION
    Outputs a warning message with visual icon and amber/yellow accent color.

.PARAMETER msg
    The warning message to display.

.EXAMPLE
    Write-LogWarn "Build preset not found, using fallback"
#>
function Write-LogWarn($msg) {
  Write-Host "⚠ " -ForegroundColor Yellow -NoNewline
  Write-Host $msg -ForegroundColor White
}

<#
.SYNOPSIS
    Writes an error message and exits with modern CLI styling.

.DESCRIPTION
    Outputs an error message with X icon and red accent color, then exits.

.PARAMETER msg
    The error message to display.

.PARAMETER code
    The exit code to use. Defaults to 1.

.EXAMPLE
    Write-LogErrorAndExit "CMake not found in PATH" 127
#>
function Write-LogErrorAndExit($msg, $code = 1) {
  Write-Host "✗ " -ForegroundColor Red -NoNewline
  Write-Host $msg -ForegroundColor White
  exit $code
}

<#
.SYNOPSIS
    Writes a dimmed/secondary information message.

.DESCRIPTION
    Outputs a dimmed message for less important information like debug details.

.PARAMETER msg
    The message to display.

.EXAMPLE
    Write-LogDim "Using build directory: out/build/windows-debug"
#>
function Write-LogDim($msg) {
  Write-Host "  $msg" -ForegroundColor DarkGray
}

<#
.SYNOPSIS
    Writes a verbose/debug message that's only shown when -Verbose is enabled.

.DESCRIPTION
    Outputs a dimmed message for internal details like file discovery, only when verbose mode is active.

.PARAMETER msg
    The message to display.

.EXAMPLE
    Write-LogVerbose "Discovered reply file: target-example.json"
#>
function Write-LogVerbose($msg) {
  if ($global:VerboseMode) {
    Write-Host "  $msg" -ForegroundColor DarkGray
  }
}<#
.SYNOPSIS
    Writes a highlighted message for important operations.

.DESCRIPTION
    Outputs a highlighted message with arrow icon for important operations.

.PARAMETER msg
    The message to display.

.EXAMPLE
    Write-LogAction "Building target: oxygen-asyncengine-simulator"
#>
function Write-LogAction($msg) {
  Write-Host "→ " -ForegroundColor Cyan -NoNewline
  Write-Host $msg -ForegroundColor White
}

<#
.SYNOPSIS
    Selects random example targets from the available targets list.

.DESCRIPTION
    Randomly selects up to 5 targets from the available targets, preferring
    simpler targets (with fewer dashes) over complex ones for better usability.

.PARAMETER allTargets
    Array of all available target names.

.OUTPUTS
    String[]. Array of randomly selected target names, sorted alphabetically.

.EXAMPLE
    $examples = Get-RandomExampleTargets $allTargets
    foreach ($target in $examples) { Write-Host $target }
#>
function Get-RandomExampleTargets($allTargets) {
  # Prefer targets with no more than 2 dashes for simplicity
  $simpleTargets = $allTargets | Where-Object { ($_ -split '-').Count -le 3 } | Sort-Object
  $complexTargets = $allTargets | Where-Object { ($_ -split '-').Count -gt 3 } | Sort-Object

  # Combine with preference for simple targets
  $examplePool = @()
  $examplePool += $simpleTargets
  $examplePool += $complexTargets

  # Randomly select up to 5 targets
  $maxExamples = [Math]::Min(5, $examplePool.Count)
  return $examplePool | Get-Random -Count $maxExamples | Sort-Object
}

<#
.SYNOPSIS
    Generates fuzzy matching examples from selected targets.

.DESCRIPTION
    Creates fuzzy search examples by extracting meaningful components from
    target names to demonstrate how users can find targets with partial matches.
    Uses a circular sampling algorithm to create discriminative fuzzy patterns.

.PARAMETER randomTargets
    Array of target names to generate fuzzy examples from.

.EXAMPLE
    Show-FuzzyExamples @("oxygen-base", "oxygen-graphics")
    # Outputs: Try: 'oxbas' → oxygen-base
    #          Try: 'oxgra' → oxygen-graphics
#>
function Show-FuzzyExamples($randomTargets) {
  if ($randomTargets.Count -ge 2) {
    $target1 = $randomTargets[0]
    $target2 = $randomTargets[1]

    # Generate discriminative fuzzy examples using circular sampling
    $fuzzy1 = Get-FuzzyPattern $target1
    $fuzzy2 = Get-FuzzyPattern $target2

    if ($fuzzy1) {
      Write-Host "Try: " -ForegroundColor DarkGray -NoNewline
      Write-Host "'$fuzzy1'" -ForegroundColor White -NoNewline
      Write-Host " → " -ForegroundColor DarkGray -NoNewline
      Write-Host $target1 -ForegroundColor Cyan
    }
    if ($fuzzy2) {
      Write-Host "Try: " -ForegroundColor DarkGray -NoNewline
      Write-Host "'$fuzzy2'" -ForegroundColor White -NoNewline
      Write-Host " → " -ForegroundColor DarkGray -NoNewline
      Write-Host $target2 -ForegroundColor Cyan
    }
  }
}

<#
.SYNOPSIS
    Generates a discriminative fuzzy pattern from a target name.

.DESCRIPTION
    Creates a fuzzy pattern by taking the first character from each segment,
    then adding unique characters from subsequent positions. Simple and effective.

.PARAMETER targetName
    The target name to generate a fuzzy pattern from.

.OUTPUTS
    String. A fuzzy pattern (up to 5 characters), or $null if target is too short.

.EXAMPLE
    Get-FuzzyPattern "Oxygen.Base.Config.Tests"
    # Returns something like: "oxbct" (first char from each segment)

.EXAMPLE
    Get-FuzzyPattern "oxygen-engine"
    # Returns something like: "oxen" (unique chars from segments)
#>
function Get-FuzzyPattern($targetName) {
  if (-not $targetName -or $targetName.Length -lt 3) { return $null }

  # Split into meaningful segments (by hyphens, dots, underscores only - no CamelCase splitting)
  $segments = $targetName -split '[._-]' | Where-Object {
    $_.Length -ge 1 -and $_ -notin @('the', 'and', 'or', 'for', 'of', 'in', 'to', 'a', 'an', 'is', 'are', 'was', 'were')
  }

  # Convert segments to lowercase
  $segments = $segments | ForEach-Object { $_.ToLower() }

  if ($segments.Count -eq 0) { return $null }

  # Create abbreviation that works with sequential character matching
  # Calculate optimal character distribution across segments
  $fuzzyChars = @()

  # Use dynamic pattern length based on number of segments
  # For many segments, we can afford longer patterns for better specificity
  $maxPatternLength = [Math]::Min($segments.Count, 6)  # Cap at 6 for usability

  if ($segments.Count -eq 0) { return $null }

  # Calculate minimum characters per segment and remainder
  $minCharsPerSegment = [Math]::Floor($maxPatternLength / $segments.Count)
  $remainder = $maxPatternLength % $segments.Count

  # First pass: determine how many chars each segment will contribute
  $charsPerSegment = @()
  for ($i = 0; $i -lt $segments.Count; $i++) {
    $segment = $segments[$i]
    $charsToTake = $minCharsPerSegment

    # Add extra character to segments that can accommodate it (length > min + 1)
    if ($remainder -gt 0 -and $segment.Length -gt ($minCharsPerSegment + 1)) {
      $charsToTake++
      $remainder--
    }

    $charsPerSegment += [Math]::Min($charsToTake, $segment.Length)
  }

  # Second pass: extract characters from each segment
  for ($i = 0; $i -lt $segments.Count; $i++) {
    $segment = $segments[$i]
    $charsToTake = $charsPerSegment[$i]

    for ($j = 0; $j -lt $charsToTake; $j++) {
      if ($fuzzyChars.Count -lt $maxPatternLength -and $j -lt $segment.Length) {
        $fuzzyChars += $segment[$j]
      }
    }
  }

  return ($fuzzyChars -join '')
}

<#
.SYNOPSIS
    Gets the standard build directory path.

.DESCRIPTION
    Returns the standardized build directory path "out/build" from the project root.

.OUTPUTS
    String. The absolute path to the standard build directory.

.EXAMPLE
    $buildRoot = Get-StandardBuildRoot
#>
function Get-StandardBuildRoot() {
  return Join-Path (Get-Location) $script:BUILD_DIR
}

<#
.SYNOPSIS
    Gets the appropriate Conan profile name for the current platform and configuration.

.DESCRIPTION
    Returns the platform-specific Conan profile name based on the current platform
    and build configuration. Uses different profiles for Debug vs Release builds.

.PARAMETER Config
    The build configuration (Debug, Release, etc.).

.OUTPUTS
    String. The Conan profile name to use.

.EXAMPLE
    $profile = Get-ConanProfile "Debug"
    # Returns "windows-msvc-asan.ini" on Windows for Debug builds

.NOTES
    Platform mapping:
    - Windows: Debug -> windows-msvc-asan.ini, Release -> windows-msvc.ini
    - Linux: Debug -> linux-gcc-asan.ini, Release -> linux-clang.ini
    - macOS: Debug -> macos-clang-asan.ini, Release -> macos-clang.ini
#>
function Get-ConanProfile($Config) {
  $platform = Get-PlatformName
  $isDebug = ($Config -ieq "Debug")

  switch ($platform) {
    "windows" {
      if ($isDebug) { return "windows-msvc-asan.ini" } else { return "windows-msvc.ini" }
    }
    "linux" {
      if ($isDebug) { return "linux-gcc-asan.ini" } else { return "linux-clang.ini" }
    }
    "mac" {
      if ($isDebug) { return "macos-clang-asan.ini" } else { return "macos-clang.ini" }
    }
    default {
      Write-LogErrorAndExit "Unsupported platform: $platform" 3
    }
  }
}

<#
.SYNOPSIS
    Runs Conan install to set up dependencies and build environment.

.DESCRIPTION
    Executes conan install with platform-specific profiles to set up the build
    environment in the standard out/build directory.

.PARAMETER Config
    The build configuration (Debug, Release, etc.).

.PARAMETER DryRun
    If specified, shows what command would be executed without running it.

.EXAMPLE
    Invoke-Conan "Debug"

.NOTES
    Must be run from the project root directory. Creates the out/build directory
    and installs all dependencies specified in conanfile.py.
#>
function Invoke-Conan($Config, [switch]$DryRun) {
  $conanProfile = Get-ConanProfile $Config

  $conanCmd = "conan install . --profile:host=$script:CONAN_PROFILES_DIR/$conanProfile --profile:build=$script:CONAN_PROFILES_DIR/$conanProfile --deployer-folder=$script:CONAN_DEPLOY_DIR --build=missing --deployer=full_deploy -s build_type=$Config"

  if ($DryRun) {
    Write-Host ""
    Write-Host "Dry Run Mode - Would Execute:" -ForegroundColor Magenta
    Write-Host "  Command: " -NoNewline -ForegroundColor DarkGray
    Write-Host $conanCmd -ForegroundColor White
    Write-Host ""
    return
  }

  Write-LogAction "Installing dependencies with Conan"
  Write-LogDim $conanCmd

  Invoke-Expression $conanCmd
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    Write-LogErrorAndExit "Conan install failed with exit code $exitCode" $exitCode
  }

  Write-LogSuccess "Dependencies installed successfully"
  Write-Host ""
}

<#
.SYNOPSIS
    Runs CMake configuration using the appropriate preset.

.DESCRIPTION
    Executes cmake configuration using platform-specific presets to configure
    the build system when the build directory exists but isn't configured.

.PARAMETER Config
    The build configuration (Debug, Release, etc.).

.PARAMETER DryRun
    If specified, shows what command would be executed without running it.

.EXAMPLE
    Invoke-CMakeConfigure "Debug"

.NOTES
    Requires that Conan has already been run to set up the build environment.
    Uses platform-specific configure presets.
#>
function Invoke-CMakeConfigure($Config, [switch]$DryRun) {
  $platformName = Get-PlatformName
  $configurePreset = $platformName

  # Ensure CMake File API query exists for target discovery
  $buildRoot = Get-StandardBuildRoot
  $apiQueryDir = Join-Path $buildRoot ".cmake/api/v1/query"
  if (-not (Test-Path $apiQueryDir)) {
    if (-not $DryRun) {
      New-Item -Path $apiQueryDir -ItemType Directory -Force | Out-Null
      # Request codemodel-v2 for target discovery
      $codeModelQuery = Join-Path $apiQueryDir "codemodel-v2"
      "" | Out-File -FilePath $codeModelQuery -Encoding ASCII
      Write-LogVerbose "Created CMake File API query for codemodel-v2"
    }
  }

  $cmakeCmd = "cmake --preset $configurePreset"

  if ($DryRun) {
    Write-Host ""
    Write-Host "Dry Run Mode - Would Execute:" -ForegroundColor Magenta
    Write-Host "  Command: " -NoNewline -ForegroundColor DarkGray
    Write-Host $cmakeCmd -ForegroundColor White
    Write-Host ""
    return
  }

  Write-LogAction "Configuring CMake build system"
  Write-LogDim $cmakeCmd

  Invoke-Expression $cmakeCmd
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    Write-LogErrorAndExit "CMake configuration failed with exit code $exitCode" $exitCode
  }

  Write-LogSuccess "CMake configuration completed"
  Write-Host ""
}

<#
.SYNOPSIS
    Shortens an absolute path for display by showing drive + first folder + ... + relative path from project root.

.DESCRIPTION
    Formats absolute paths in a compact, readable way:
    - Shows the full relative path from project root
    - Includes drive letter and first folder of absolute path
    - Uses "..." to represent any intermediate folders
    - Omits "..." if project root is at drive root or one level deep

.PARAMETER fullPath
    The absolute path to shorten.

.OUTPUTS
    String. The shortened path for display.

.EXAMPLE
    # For F:\projects\DroidNet\projects\Oxygen.Engine\out\build\bin\Debug\app.exe
    # Returns: F:\projects\...\out\build\bin\Debug\app.exe

.EXAMPLE
    # For F:\MyProject\bin\app.exe (project root at F:\MyProject)
    # Returns: F:\MyProject\bin\app.exe (no ... needed)
#>
function Format-CompactPath($fullPath) {
  if (-not $fullPath) { return $fullPath }

  try {
    # Find project root by looking for key files
    $currentDir = Split-Path $fullPath -Parent
    $projectRoot = $null

    # Start from the file's directory and work upward
    while ($currentDir -and (Test-Path $currentDir)) {
      if ((Test-Path (Join-Path $currentDir "CMakePresets.json")) -or
        (Test-Path (Join-Path $currentDir "CMakeLists.txt")) -or
        (Test-Path (Join-Path $currentDir ".git"))) {
        $projectRoot = $currentDir
        break
      }
      $parent = Split-Path $currentDir -Parent
      if ($parent -eq $currentDir) { break } # Hit root
      $currentDir = $parent
    }

    # If we couldn't find project root, just return the filename
    if (-not $projectRoot) {
      return Split-Path $fullPath -Leaf
    }

    # Get relative path from project root
    $relativePath = $fullPath.Substring($projectRoot.Length)
    if ($relativePath.StartsWith('\') -or $relativePath.StartsWith('/')) {
      $relativePath = $relativePath.Substring(1)
    }

    # Parse absolute path components
    $drive = Split-Path $projectRoot -Qualifier  # e.g., "F:"
    $pathWithoutDrive = $projectRoot.Substring($drive.Length)
    if ($pathWithoutDrive.StartsWith('\') -or $pathWithoutDrive.StartsWith('/')) {
      $pathWithoutDrive = $pathWithoutDrive.Substring(1)
    }

    $pathComponents = $pathWithoutDrive -split '[/\\]' | Where-Object { $_ -ne '' }

    if ($pathComponents.Count -eq 0) {
      # Project root is at drive root (e.g., F:\)
      return "$drive\$relativePath"
    } elseif ($pathComponents.Count -eq 1) {
      # Project root is one level deep (e.g., F:\MyProject)
      return "$drive\$($pathComponents[0])\$relativePath"
    } else {
      # Project root is deeper, use ... (e.g., F:\projects\...\relativePath)
      return "$drive\$($pathComponents[0])\...\$relativePath"
    }
  } catch {
    # Fallback to just the filename if anything goes wrong
    return Split-Path $fullPath -Leaf
  }
}

<#
.SYNOPSIS
    Resolves a target name using fuzzy matching against available CMake targets.

.DESCRIPTION
    Performs intelligent target name resolution with multiple strategies:
    1. Exact match - returns immediately if target name matches exactly
    2. Unique fuzzy match - returns single best match if only one candidate
    3. Interactive selection - prompts user to choose from multiple matches

    Fuzzy matching supports:
    - Substring matching (e.g., "base" -> "oxygen-base")
    - Hyphen-separated component matching (e.g., "gr-common" -> "oxygen-graphics-common")
    - Abbreviation matching (e.g., "asyncsim" -> "oxygen-asyncengine-simulator")
    - Case-insensitive matching throughout

.PARAMETER targetPattern
    The target name or pattern to match. Can be exact name, substring, or abbreviation.

.PARAMETER buildRoot
    The build directory containing CMake File API replies with target information.

.OUTPUTS
    String. The resolved target name, or $null if no match found or user cancelled selection.

.EXAMPLE
    $target = Resolve-TargetName "base" $buildRoot
    # Returns "oxygen-base" if it exists

.EXAMPLE
    $target = Resolve-TargetName "gr-d3d" $buildRoot
    # Returns "oxygen-graphics-direct3d12" via component matching

.EXAMPLE
    $target = Resolve-TargetName "graphics" $buildRoot
    # Shows interactive menu if multiple graphics-related targets exist

.NOTES
    Requires CMake File API codemodel replies to be present for target discovery.
    Uses Write-Host for interactive prompts, compatible with all PowerShell hosts.
#>
function Resolve-TargetName($targetPattern, $buildRoot, [switch]$NoInteractive) {
  # Get all available targets from CMake codemodel
  $allTargets = @()
  try {
    $cmakeReplyDir = Join-Path $buildRoot ".cmake/api/v1/reply"
    if (-not (Test-Path $cmakeReplyDir)) {
      # If build directory doesn't exist, that's expected - don't warn yet
      if (Test-Path $buildRoot) {
        Write-LogVerbose "CMake replies not found, target resolution will be limited"
      }
      return $targetPattern # Return as-is, let build process handle validation
    }

    $codemodel = Get-ChildItem -Path $cmakeReplyDir -Filter "codemodel-v2*.json" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
    if (-not $codemodel) {
      Write-LogVerbose "CMake codemodel not found, target resolution will be limited"
      return $targetPattern
    }

    $cm = Get-Content $codemodel.FullName -Raw | ConvertFrom-Json
    $targetNames = @()
    foreach ($cfg in $cm.configurations) {
      foreach ($tref in $cfg.targets) {
        $tfile = Join-Path $cmakeReplyDir $tref.jsonFile
        if (Test-Path $tfile) {
          try {
            $tjson = Get-Content $tfile -Raw | ConvertFrom-Json
            if ($tjson.name -and $targetNames -notcontains $tjson.name) {
              $targetNames += $tjson.name
            }
          } catch { continue }
        }
      }
    }
    $allTargets = $targetNames | Sort-Object
  } catch {
    Write-LogVerbose "Target discovery failed: $($_.Exception.Message)"
    return $targetPattern
  }

  if ($allTargets.Count -eq 0) {
    Write-LogVerbose "No targets found in codemodel, proceeding with provided target name"
    return $targetPattern
  }

  # Strategy 1: Exact match (case-insensitive)
  $exactMatch = $allTargets | Where-Object { $_ -ieq $targetPattern }
  if ($exactMatch) {
    Write-Host "✓ " -ForegroundColor Green -NoNewline
    Write-Host "Exact target match: " -ForegroundColor White -NoNewline
    Write-Host $exactMatch -ForegroundColor Yellow
    return $exactMatch
  }

  # Strategy 2: Fuzzy matching with multiple scoring approaches
  $candidates = @()
  $pattern = $targetPattern.ToLower()

  foreach ($target in $allTargets) {
    $targetLower = $target.ToLower()
    $score = 0

    # Scoring: Exact substring match (highest priority)
    if ($targetLower.Contains($pattern)) {
      $score += 100
      # Bonus for start-of-string match
      if ($targetLower.StartsWith($pattern)) { $score += 50 }
      # Bonus for word boundary match (after hyphen)
      if ($targetLower.StartsWith("-$pattern") -or $targetLower.Contains("-$pattern")) { $score += 30 }
    }

    # Scoring: Hyphen-separated component matching (e.g., "gr-common" -> "graphics-common")
    $patternComponents = $pattern -split '-'
    $targetComponents = $targetLower -split '-'
    if ($patternComponents.Count -gt 1) {
      $componentMatches = 0
      foreach ($pc in $patternComponents) {
        foreach ($tc in $targetComponents) {
          if ($tc.StartsWith($pc) -and $pc.Length -ge 2) {
            $componentMatches++
            break
          }
        }
      }
      if ($componentMatches -eq $patternComponents.Count) {
        $score += 80 + ($componentMatches * 10)
      }
    }

    # Scoring: Abbreviation matching (e.g., "asyncsim" -> "async-engine-sim-ulator", "oxp" -> "oxygen...prettyprinter")
    if ($pattern.Length -ge 2) {
      $abbreviationScore = 0
      $patternIndex = 0
      $consecutiveMatches = 0
      $wordBoundaryMatches = 0

      for ($i = 0; $i -lt $targetLower.Length -and $patternIndex -lt $pattern.Length; $i++) {
        if ($targetLower[$i] -eq $pattern[$patternIndex]) {
          $abbreviationScore += 1
          $patternIndex++
          $consecutiveMatches++

          # Bonus for matches at word boundaries (start of string or after separator)
          if ($i -eq 0 -or $targetLower[$i-1] -eq '-' -or $targetLower[$i-1] -eq '.' -or $targetLower[$i-1] -eq '_') {
            $wordBoundaryMatches++
          }
        } elseif ($targetLower[$i] -eq '-' -or $targetLower[$i] -eq '.' -or $targetLower[$i] -eq '_') {
          # Reset consecutive matches at word boundaries
          if ($consecutiveMatches -gt 1) { $abbreviationScore += 2 }
          $consecutiveMatches = 0
        } else {
          $consecutiveMatches = 0
        }
      }

      # Bonus if we matched all characters
      if ($patternIndex -eq $pattern.Length) {
        $score += $abbreviationScore * 3
        # Extra bonus for matches at word boundaries
        $score += $wordBoundaryMatches * 5
        # Extra bonus for shorter patterns that match completely
        if ($pattern.Length -le 4) { $score += 10 }
      }
    }

    # Add candidate if it has any score
    if ($score -gt 0) {
      $candidates += [PSCustomObject]@{
        Target = $target
        Score  = $score
      }
    }
  }

  # Sort candidates by score (descending)
  $candidates = $candidates | Sort-Object Score -Descending

  if ($candidates.Count -eq 0) {
    Write-LogWarn "No fuzzy matches found for target pattern '$targetPattern'"
    Write-Host ""

    # Generate randomized example targets from available targets
    $randomTargets = Get-RandomExampleTargets $allTargets

    Write-Host "Example targets:" -ForegroundColor White
    foreach ($target in $randomTargets) {
      Write-Host "  " -NoNewline
      Write-Host $target -ForegroundColor Cyan
    }
    Write-Host ""

    # Show fuzzy matching examples using selected targets
    Show-FuzzyExamples $randomTargets

    Write-Host ""
    Write-Host "Tip: Use " -ForegroundColor DarkGray -NoNewline
    Write-Host "cmake --build . --target help" -ForegroundColor White -NoNewline
    Write-Host " to see all available targets" -ForegroundColor DarkGray
    Write-Host ""
    return $null
  }

  # Strategy 3: Return unique best match
  if ($candidates.Count -eq 1 -or $candidates[0].Score -gt ($candidates[1].Score + 20)) {
    Write-Host "✓ " -ForegroundColor Green -NoNewline
    Write-Host "Fuzzy match resolved: " -ForegroundColor White -NoNewline
    Write-Host "'$targetPattern'" -ForegroundColor White -NoNewline
    Write-Host " → " -ForegroundColor White -NoNewline
    Write-Host "'$($candidates[0].Target)'" -ForegroundColor Yellow
    return $candidates[0].Target
  }

  # Strategy 4: Interactive selection for multiple good matches (or return best match if non-interactive)
  if ($NoInteractive) {
    # Non-interactive mode: return the best match
    Write-LogVerbose "Non-interactive mode: selecting best match '$($candidates[0].Target)' (score: $($candidates[0].Score))"
    return $candidates[0].Target
  }

  # Interactive mode: show options and prompt for selection
  Write-Host ""
  Write-Host "Multiple targets found matching " -NoNewline -ForegroundColor White
  Write-Host "'$targetPattern'" -ForegroundColor Cyan -NoNewline
  Write-Host ":" -ForegroundColor White
  Write-Host ""

  for ($i = 0; $i -lt [Math]::Min($candidates.Count, 10); $i++) {
    $candidate = $candidates[$i]
    $num = ($i + 1).ToString().PadLeft(2)
    Write-Host "  " -NoNewline
    Write-Host "[$num]" -ForegroundColor Blue -NoNewline
    Write-Host " $($candidate.Target)" -ForegroundColor White -NoNewline
    Write-Host " (score: $($candidate.Score))" -ForegroundColor DarkGray
  }
  Write-Host ""
  Write-Host "  " -NoNewline
  Write-Host "[ 0]" -ForegroundColor DarkGray -NoNewline
  Write-Host " Cancel" -ForegroundColor DarkGray
  Write-Host ""

  do {
    Write-Host "→ " -ForegroundColor Cyan -NoNewline
    $choice = Read-Host "Select target (1-$([Math]::Min($candidates.Count, 10)), 0 to cancel)"
    if ($choice -eq "0") {
      Write-LogDim "Target selection cancelled by user"
      return $null
    }
    $choiceNum = $null
    if ([int]::TryParse($choice, [ref]$choiceNum) -and $choiceNum -ge 1 -and $choiceNum -le [Math]::Min($candidates.Count, 10)) {
      $selectedTarget = $candidates[$choiceNum - 1].Target
      Write-Host "✓ " -ForegroundColor Green -NoNewline
      Write-Host "Selected target: " -ForegroundColor White -NoNewline
      Write-Host $selectedTarget -ForegroundColor Yellow
      return $selectedTarget
    }
    Write-Host "✗ " -ForegroundColor Red -NoNewline
    Write-Host "Invalid selection. Please enter a number between 0 and $([Math]::Min($candidates.Count, 10))." -ForegroundColor White
  } while ($true)
}

<#
.SYNOPSIS
    Searches upward through the directory tree for a specific file.

.DESCRIPTION
    Starting from a given directory, recursively searches parent directories
    until the specified file is found or the filesystem root is reached.

.PARAMETER startDir
    The directory to start searching from.

.PARAMETER fileName
    The name of the file to search for (e.g., "CMakePresets.json").

.OUTPUTS
    String. The full path to the found file, or $null if not found.

.EXAMPLE
    $presetsFile = Get-FileUpwards $PWD "CMakePresets.json"
    if ($presetsFile) { Write-Host "Found presets at: $presetsFile" }

.NOTES
    This function is commonly used to locate project root files like CMakePresets.json
    from within build directories or subdirectories.
#>
function Get-FileUpwards($startDir, $fileName) {
  # Handle non-existent directories by starting from current location
  try {
    $cur = (Resolve-Path -LiteralPath $startDir -ErrorAction Stop).ProviderPath
  } catch {
    $cur = (Get-Location).Path
  }

  while ($true) {
    $candidate = Join-Path $cur $fileName
    if (Test-Path $candidate) { return $candidate }
    $parent = Split-Path $cur -Parent
    if ($parent -eq $cur) { break }
    $cur = $parent
  }
  return $null
}

<#
.SYNOPSIS
    Parses CMake presets from a CMakePresets.json file, including included files.

.DESCRIPTION
    Reads and parses a CMakePresets.json file, extracting both build and configure presets.
    Automatically processes any included preset files referenced in the "include" array.
    Returns a structured object containing all available presets.

.PARAMETER presetsFile
    The full path to the CMakePresets.json file to parse.

.OUTPUTS
    PSCustomObject. An object with 'buildPresets' and 'configurePresets' arrays
    containing all preset definitions from the file and its includes.

.EXAMPLE
    $presets = Read-Presets "C:\project\CMakePresets.json"
    foreach ($preset in $presets.buildPresets) {
        Write-Host "Available build preset: $($preset.name)"
    }

.NOTES
    Returns an empty object with empty arrays if the file cannot be parsed.
    Handles include resolution relative to the main presets file directory.
#>
function Read-Presets($presetsFile) {
  try {
    $raw = Get-Content $presetsFile -Raw -ErrorAction Stop
    $json = $raw | ConvertFrom-Json
  } catch {
    return @{}
  }

  $all = @{}
  if ($json.buildPresets) {
    $all.buildPresets = @($json.buildPresets)
  } else {
    $all.buildPresets = @()
  }

  if ($json.configurePresets) {
    $all.configurePresets = @($json.configurePresets)
  } else {
    $all.configurePresets = @()
  }

  if ($json.include) {
    foreach ($inc in $json.include) {
      $incPath = Join-Path (Split-Path $presetsFile -Parent) $inc
      if (Test-Path $incPath) {
        try {
          $incRaw = Get-Content $incPath -Raw -ErrorAction Stop
          $incJson = $incRaw | ConvertFrom-Json
        } catch {
          continue
        }
        if ($incJson.buildPresets) { $all.buildPresets += $incJson.buildPresets }
        if ($incJson.configurePresets) { $all.configurePresets += $incJson.configurePresets }
      }
    }
  }

  return $all
}

<#
.SYNOPSIS
    Returns a normalized platform name for CMake preset selection.

.DESCRIPTION
    Determines the current platform and returns a standardized name used by
    CMake presets in the Oxygen Engine project. Maps PowerShell platform
    variables to consistent preset naming conventions.

.OUTPUTS
    String. The platform name: 'windows', 'linux', 'mac', or the sanitized OS environment variable.

.EXAMPLE
    $platform = Get-PlatformName
    $presetName = "$platform-debug"

.NOTES
    Uses PowerShell's built-in platform detection variables ($IsWindows, $IsLinux, $IsMacOS).
    Falls back to the OS environment variable with whitespace removed for unknown platforms.
#>
function Get-PlatformName() {
  if ($IsWindows) { return 'windows' }
  if ($IsLinux) { return 'linux' }
  if ($IsMacOS) { return 'mac' }
  return ($env:OS -replace '\s', '')
}

<#
.SYNOPSIS
    Finds a matching CMake build preset for the given platform and configuration.

.DESCRIPTION
    Searches for a CMake build preset that matches the current platform and specified
    configuration using the naming convention '<platform>-<config>'. Uses Read-Presets
    to parse available presets from CMakePresets.json files.

.PARAMETER buildRoot
    The build directory path to search from for CMakePresets.json.

.PARAMETER Config
    The build configuration (e.g., "Debug", "Release").

.OUTPUTS
    String. The name of the matching build preset, or $null if not found.

.EXAMPLE
    $preset = Find-BuildPreset (Join-Path $script:BUILD_DIR "windows-debug") "Debug"
    if ($preset) {
        Write-Host "Using build preset: $preset"
    }

.NOTES
    Preset names are expected to follow the pattern '<platform>-<config>' where
    platform comes from Get-PlatformName and config is lowercased.
#>
function Find-BuildPreset($buildRoot, $Config) {
  $projectPresetsFile = Get-FileUpwards $buildRoot 'CMakePresets.json'
  if (-not $projectPresetsFile) { return $null }
  $presets = Read-Presets $projectPresetsFile
  $platformName = Get-PlatformName
  $desired = "$platformName-$(($Config).ToLower())"
  $available = @()
  foreach ($bp in $presets.buildPresets) { if ($bp.name) { $available += $bp.name } }
  if ($available -contains $desired) { return $desired }
  return $null
}

<#
.SYNOPSIS
    Builds a CMake target using the standardized Oxygen Engine build workflow.

.DESCRIPTION
    Orchestrates the complete build process for a CMake target using the standardized
    workflow: Conan -> CMake Configure -> Build. Automatically detects the build state
    and runs the appropriate steps to ensure a complete build environment.

.PARAMETER Target
    The name of the CMake target to build. Can be exact or fuzzy name - will be resolved internally if CMake configure runs.

.PARAMETER Config
    The build configuration (Debug, Release, etc.).

.PARAMETER DryRun
    If specified, shows what commands would be executed without running them.

.EXAMPLE
    Invoke-BuildForTarget -Target "oxygen-asyncengine-simulator" -Config "Release"

.EXAMPLE
    Invoke-BuildForTarget -Target "asyncsim" -Config "Debug" -DryRun

.NOTES
    Workflow:
    1. Check if out/build exists -> if not, run Conan install and configure
    2. Check if CMake is configured -> if not, run CMake configure
    3. If configure ran, resolve target name using CMake File API
    4. Build the target using appropriate preset or direct cmake command

    Always uses the standard out/build directory. Custom build directories are not supported.
    Target resolution happens automatically when CMake configure runs during this function.
#>
function Invoke-BuildForTarget($Target, $Config, [switch]$DryRun) {
  $buildRoot = Get-StandardBuildRoot
  $conanRan = $false

  # Step 1: Check if we need to run Conan
  if (-not (Test-Path $buildRoot)) {
    Write-LogInfo "Build directory not found. Running Conan install..."
    Invoke-Conan $Config -DryRun:$DryRun
    $conanRan = $true
  }

  # Step 2: Check if we need to run CMake configure
  $cmakeReplyDir = Join-Path $buildRoot ".cmake/api/v1/reply"
  $hasCodemodel = $false
  if (Test-Path $cmakeReplyDir) {
    $cmFiles = Get-ChildItem -Path $cmakeReplyDir -Filter "codemodel-v2*.json" -File -ErrorAction SilentlyContinue
    if ($cmFiles -and $cmFiles.Count -gt 0) { $hasCodemodel = $true }
  }

  # Step 3: Run configure if needed
  $configureRan = $false
  if ($conanRan -or -not $hasCodemodel) {
    Write-LogInfo "CMake not configured. Running CMake configuration..."
    Invoke-CMakeConfigure $Config -DryRun:$DryRun
    $configureRan = $true
  }

  # If configure ran, we definitely have codemodel now
  $resolvedTarget = $Target
  if ($configureRan) {
    $resolvedTarget = Resolve-TargetName $Target $buildRoot
  }

  # Step 4: Build the target
  Write-LogInfo "Building target: $resolvedTarget"
  $preset = Find-BuildPreset $buildRoot $Config
  if ($preset) {
    $buildCmd = "cmake --build --preset $preset --target $resolvedTarget"
    Write-LogVerbose "Using build preset: $preset"
  } else {
    $buildCmd = "cmake --build `"$buildRoot`" --config $Config --target $resolvedTarget"
    Write-LogVerbose "No build preset found, using direct cmake build"
  }

  if ($DryRun) {
    Write-Host ""
    Write-Host "Dry Run Mode - Would Execute:" -ForegroundColor Magenta
    Write-Host "  Command: " -NoNewline -ForegroundColor DarkGray
    Write-Host $buildCmd -ForegroundColor White
    Write-Host ""
    return
  }

  Write-LogDim $buildCmd

  Write-Host ""
  Invoke-Expression $buildCmd
  if ($LASTEXITCODE -ne 0) {
    Write-LogErrorAndExit "Build failed with exit code $LASTEXITCODE" $LASTEXITCODE
  }
  Write-Host ""
  Write-LogSuccess "Build completed: $Target"
  Write-Host ""
}

<#
.SYNOPSIS
    Extracts target information from CMake File API codemodel replies.

.DESCRIPTION
    Parses CMake File API codemodel JSON files to extract detailed information
    about a specific target, including its artifacts, type, and runtime output directory.
    Used for target discovery and artifact path resolution.

.PARAMETER buildRoot
    The build directory containing CMake File API replies.

.PARAMETER Target
    The name of the target to search for.

.OUTPUTS
    PSCustomObject[]. Array of target entries with properties: name, type, artifacts,
    reply_file, and runtime_output_directory.

.EXAMPLE
    $targets = Get-TargetFromCodemodel "C:\build" "MyApp"
    foreach ($target in $targets) {
        Write-Host "Target: $($target.name), Type: $($target.type)"
    }

.NOTES
    Requires CMake File API replies to be present. Exits with code 3 if codemodel
    files are missing or cannot be parsed.
#>
function Get-TargetFromCodemodel($buildRoot, $Target) {
  $cmakeReplyDir = Join-Path $buildRoot ".cmake/api/v1/reply"
  if (-not (Test-Path $cmakeReplyDir)) {
    throw "CMake replies not configured"
  }

  $codemodel = Get-ChildItem -Path $cmakeReplyDir -Filter "codemodel-v2*.json" -File -ErrorAction SilentlyContinue |
  Select-Object -First 1
  if (-not $codemodel) {
    throw "CMake codemodel not found"
  }

  try {
    $cm = Get-Content $codemodel.FullName -Raw | ConvertFrom-Json
  } catch {
    throw "Failed to parse codemodel JSON '$($codemodel.FullName)': $($_.Exception.Message)"
  }

  $targetsList = @()
  foreach ($cfg in $cm.configurations) {
    foreach ($tref in $cfg.targets) {
      $tfile = Join-Path $cmakeReplyDir $tref.jsonFile
      if (Test-Path $tfile) {
        try { $tjson = Get-Content $tfile -Raw | ConvertFrom-Json } catch { continue }
        $entry = [PSCustomObject]@{
          name                     = $tjson.name
          type                     = $tjson.type
          artifacts                = $tjson.artifacts
          reply_file               = $tfile
          runtime_output_directory = $tjson.runtimeOutputDirectory
        }
        $targetsList += $entry
      }
    }
  }

  return $targetsList | Where-Object { $_.name -eq $Target }
}

<#
.SYNOPSIS
    Discovers the CMake File API reply file for a specific target and configuration.

.DESCRIPTION
    Locates the appropriate target reply JSON file from CMake File API using multiple
    discovery strategies: direct file pattern matching and codemodel parsing fallback.
    Essential for extracting target artifact information for execution.

.PARAMETER buildRoot
    The build directory containing CMake File API replies.

.PARAMETER Target
    The name of the target to find a reply file for.

.PARAMETER Config
    The build configuration (used for preferential matching).

.OUTPUTS
    String. The full path to the target reply JSON file, or $null if not found.

.EXAMPLE
    $replyFile = Get-ReplyFileForTarget "C:\build" "MyApp" "Release"
    if ($replyFile) {
        $artifact = Get-ArtifactFromReply "C:\build" $replyFile
    }

.NOTES
    Uses multiple fallback strategies:
    1. Direct file pattern matching with various naming conventions
    2. Codemodel parsing to find target reply files
    Logs the discovery method used for transparency.
#>
function Get-ReplyFileForTarget($buildRoot, $Target, $Config) {
  # Discover a suitable CMake File API target reply JSON using only PowerShell.
  $cmakeReplyDir = Join-Path $buildRoot ".cmake/api/v1/reply"
  if (-not (Test-Path $cmakeReplyDir)) {
    Write-LogWarn "CMake reply directory not present: $cmakeReplyDir"
    return $null
  }

  $patterns = @()
  $patterns += "target-$($Target)-$Config-*.json"
  $patterns += "target-$($Target)-*.json"
  $patterns += "target-$($($Target.ToLower()))-$Config-*.json"
  $patterns += "target-$($($Target.ToLower()))-*.json"

  foreach ($pat in $patterns) {
    try {
      $c = Get-ChildItem -Path $cmakeReplyDir -Filter $pat -File -ErrorAction SilentlyContinue
      if ($c -and $c.Count -gt 0) {
        $f = $c | Select-Object -First 1
        Write-LogVerbose "Discovered reply file: $($f.Name)"
        return $f.FullName
      }
    } catch {}
  }

  # Fallback: parse codemodel and find the target entry and its reply_file
  try {
    $entries = Get-TargetFromCodemodel $buildRoot $Target
    if ($entries -and $entries.Count -gt 0) {
      # Prefer reply file matching config if present in filename
      $byConfig = $entries | Where-Object { $_.reply_file -and ($_.reply_file -match "-$Config-") } |
      Select-Object -First 1
      $sel = $byConfig ? $byConfig : ($entries | Select-Object -First 1)
      if ($sel.reply_file -and (Test-Path $sel.reply_file)) {
        Write-LogInfo "Using reply file from codemodel: $($sel.reply_file)"
        return $sel.reply_file
      }
    }
  } catch {
    Write-LogWarn "Codemodel parsing fallback failed: $($_.Exception.Message)"
  }

  $msg = "Unable to discover a reply file for target '$Target' (config: $Config) in $cmakeReplyDir"
  Write-LogWarn $msg
  return $null
}

<#
.SYNOPSIS
    Extracts the executable artifact path from a CMake File API target reply file.

.DESCRIPTION
    Parses a target reply JSON file to find executable artifacts (files with extensions
    like .exe, .bat, .cmd, .com). Returns the first executable found, with preference
    for .exe files. Handles both absolute and relative artifact paths.

.PARAMETER buildRoot
    The build directory (used to resolve relative artifact paths).

.PARAMETER replyFile
    The full path to the target reply JSON file to parse.

.OUTPUTS
    String. The full path to the executable artifact, or $null if none found.

.EXAMPLE
    $replyFile = Get-ReplyFileForTarget $buildRoot "MyApp" "Release"
    $executable = Get-ArtifactFromReply $buildRoot $replyFile
    if ($executable) {
        Write-Host "Found executable: $executable"
        & $executable --version
    }

.NOTES
    - Prioritizes executable file extensions over other artifact types
    - Resolves relative paths against the build root directory
    - Validates that discovered artifacts actually exist on disk
    - Returns $null if no executable artifacts are found or if the reply file is invalid
#>
function Get-ArtifactFromReply($buildRoot, $replyFile) {
  if (-not $replyFile -or -not (Test-Path $replyFile)) { return $null }

  try {
    $tjson = Get-Content $replyFile -Raw | ConvertFrom-Json
  } catch {
    Write-LogWarn "Failed to parse reply JSON '$replyFile': $($_.Exception.Message)"
    return $null
  }

  if ($tjson -and $tjson.artifacts -and $tjson.artifacts.Count -gt 0) {
    $exeExts = '\.exe$', '\.bat$', '\.cmd$', '\.com$'
    foreach ($ext in $exeExts) {
      $exeCandidate = $tjson.artifacts | Where-Object { $_.path -match $ext } |
      Select-Object -First 1
      if ($exeCandidate) {
        $p = $exeCandidate.path
        if (-not ([System.IO.Path]::IsPathRooted($p))) { $p = Join-Path $buildRoot $p }
        try {
          $artifact = (Resolve-Path -LiteralPath $p -ErrorAction Stop).Path
        } catch {
          try { $artifact = (Get-Item -LiteralPath $p).FullName } catch {
            Write-LogWarn "Artifact path '$p' referenced in reply file not found on disk."
            $artifact = $null
          }
        }
        if ($artifact) {
          Write-LogVerbose "Artifact discovered from reply file: $(Format-CompactPath $artifact)"
          return $artifact
        }
      }
    }
    # No executable artifacts found - silently return null
    # The caller will handle this appropriately (e.g., library vs executable detection)
  }
  return $null
}

<#
function Test-FuzzyPatternResolution($buildRoot = $null) {
  if (-not $buildRoot) {
    $buildRoot = Get-StandardBuildRoot
  }

  Write-Host "🧪 Testing Fuzzy Pattern Resolution" -ForegroundColor Cyan
  Write-Host "   Build root: $(Format-CompactPath $buildRoot)" -ForegroundColor DarkGray
  Write-Host ""

  # Step 1: Get all available targets
  $allTargets = @()
  try {
    $cmakeReplyDir = Join-Path $buildRoot ".cmake/api/v1/reply"
    if (-not (Test-Path $cmakeReplyDir)) {
      Write-LogErrorAndExit "CMake replies not found. Run 'oxybuild' first to configure CMake." 2
    }

    $codemodel = Get-ChildItem -Path $cmakeReplyDir -Filter "codemodel-v2*.json" -File -ErrorAction SilentlyContinue |
                 Select-Object -First 1
    if (-not $codemodel) {
      Write-LogErrorAndExit "CMake codemodel not found. Run 'oxybuild' first to configure CMake." 2
    }

    $cm = Get-Content $codemodel.FullName -Raw | ConvertFrom-Json
    $targetNames = @()
    foreach ($cfg in $cm.configurations) {
      foreach ($tref in $cfg.targets) {
        $tfile = Join-Path $cmakeReplyDir $tref.jsonFile
        if (Test-Path $tfile) {
          try {
            $tjson = Get-Content $tfile -Raw | ConvertFrom-Json
            if ($tjson.name -and $targetNames -notcontains $tjson.name) {
              $targetNames += $tjson.name
            }
          } catch { continue }
        }
      }
    }
    $allTargets = $targetNames | Sort-Object
  } catch {
    Write-LogErrorAndExit "Failed to discover targets: $($_.Exception.Message)" 2
  }

  if ($allTargets.Count -eq 0) {
    Write-LogErrorAndExit "No targets found in codemodel." 2
  }

  Write-LogInfo "Discovered $($allTargets.Count) targets"
  Write-Host ""

  # Step 2: Test fuzzy pattern generation and resolution
  $passCount = 0
  $failCount = 0

  for ($i = 1; $i -le 50; $i++) {
    Write-Host "Test $i/50: " -ForegroundColor Yellow -NoNewline

    # Get random target
    $randomTarget = $allTargets | Get-Random
    Write-Host "$randomTarget" -ForegroundColor White

    # Generate fuzzy pattern
    $fuzzyPattern = Get-FuzzyPattern $randomTarget
    if (-not $fuzzyPattern) {
      Write-Host "  ✗ " -ForegroundColor Red -NoNewline
      Write-Host "Failed to generate fuzzy pattern" -ForegroundColor White
      $failCount++
      continue
    }

    Write-Host "  Pattern: " -ForegroundColor DarkGray -NoNewline
    Write-Host "'$fuzzyPattern'" -ForegroundColor Cyan

    # Test resolution
    $originalVerbose = $global:VerboseMode
    $global:VerboseMode = $false  # Suppress verbose output during test

    try {
      # Test fuzzy pattern resolution
      $originalVerbose = $global:VerboseMode
      $global:VerboseMode = $false

      try {
        $resolvedTarget = Resolve-TargetName $fuzzyPattern $buildRoot -NoInteractive

        # Check if resolution was successful
        if ($resolvedTarget) {
          Write-Host "  ✓ " -ForegroundColor Green -NoNewline
          Write-Host "Resolved to: " -ForegroundColor DarkGray -NoNewline
          Write-Host "$resolvedTarget" -ForegroundColor Yellow
          $passCount++
        } else {
          Write-Host "  ✗ " -ForegroundColor Red -NoNewline
          Write-Host "No match found for pattern" -ForegroundColor White
          $failCount++
        }
      } finally {
        # Restore original verbose setting
        $global:VerboseMode = $originalVerbose
      }
    } catch {
      Write-Host "  ✗ " -ForegroundColor Red -NoNewline
      Write-Host "Resolution failed: $($_.Exception.Message)" -ForegroundColor White
      $failCount++
    }

    Write-Host ""
  }

  # Step 3: Report results
  Write-Host "📊 Test Results:" -ForegroundColor Green
  Write-Host "  Passed: " -ForegroundColor DarkGray -NoNewline
  Write-Host "$passCount/50" -ForegroundColor Green
  Write-Host "  Failed: " -ForegroundColor DarkGray -NoNewline
  Write-Host "$failCount/50" -ForegroundColor Red
  Write-Host "  Success Rate: " -ForegroundColor DarkGray -NoNewline
  Write-Host "$([math]::Round($passCount * 100 / 50, 1))%" -ForegroundColor $(if ($passCount -ge 40) { "Green" } elseif ($passCount -ge 30) { "Yellow" } else { "Red" })

  Write-Host ""
  if ($passCount -ge 40) {
    Write-LogSuccess "Fuzzy pattern resolution is working well!"
  } elseif ($passCount -ge 30) {
    Write-LogWarn "Fuzzy pattern resolution has some issues but is mostly functional."
  } else {
    Write-LogErrorAndExit "Fuzzy pattern resolution is not working properly. Algorithm needs improvement." 1
  }
}
#>
