<#
.SYNOPSIS
Cooks one or all scenes in the Examples/Content/scenes folder using the Oxygen.Cooker.ImportTool.

.DESCRIPTION
This script finds and runs the Oxygen.Cooker.ImportTool against scene 'import-manifest.json' files.
It can target a specific scene by folder name, or all scenes found in the 'scenes' subdirectory.
By default, it parses the CMake presets to find the built tool without running `cmake` directly, avoiding reconfigurations.

.PARAMETER Scene
The name of the scene folder to cook (e.g. "bottle-on-box"). Required if -All is not specified.

.PARAMETER All
Switch to cook all scene folders that contain an import-manifest.json.

.PARAMETER Preset
The name of the CMake preset to resolve the tool from (default: "windows-debug").

.PARAMETER NoTUI
Switch to disable the Text User Interface of the ImportTool (useful for CI or plain logs).

.PARAMETER ToolPath
Optional explicit path to the Oxygen.Cooker.ImportTool.exe. If omitted, it will try resolving via the specified CMake Preset.

.EXAMPLE
.\cook_scenes.ps1 -Scene bottle-on-box

.EXAMPLE
.\cook_scenes.ps1 -All -NoTUI -Preset windows-release
#>

[CmdletBinding(DefaultParameterSetName = 'Single')]
param (
    [Parameter(Mandatory = $true, ParameterSetName = 'Single', Position = 0)]
    [string]$Scene,

    [Parameter(Mandatory = $true, ParameterSetName = 'All')]
    [switch]$All,

    [Parameter(Mandatory = $false)]
    [string]$Preset = "windows-debug",

    [switch]$NoTUI,

    [string]$ToolPath
)

$ErrorActionPreference = "Stop"

# Paths
$ContentDir = $PSScriptRoot
$ScenesDir = Join-Path $ContentDir "scenes"

# Ensure scenes directory exists
if (-not (Test-Path $ScenesDir)) {
    Write-Error "Scenes directory not found: $ScenesDir"
    exit 1
}

function Get-ExpandedCMakePresets {
    param([string]$Path, [System.Collections.Hashtable]$Seen = @{})

    $Path = [System.IO.Path]::GetFullPath($Path)
    if ($Seen.ContainsKey($Path) -or -not (Test-Path $Path)) { return @() }

    $Seen[$Path] = $true

    # Using Try/Catch for parse errors just in case
    try {
        $Content = Get-Content -Raw -Path $Path -ErrorAction Stop | ConvertFrom-Json
    } catch {
        return @()
    }

    $Result = New-Object System.Collections.ArrayList
    $null = $Result.Add($Content)

    if ($Content.include) {
        foreach ($Inc in $Content.include) {
            if ([System.IO.Path]::IsPathRooted($Inc)) {
                $IncPath = $Inc
            } else {
                $IncPath = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $Path) $Inc))
            }
            $Sub = Get-ExpandedCMakePresets -Path $IncPath -Seen $Seen
            foreach ($s in $Sub) { $null = $Result.Add($s) }
        }
    }
    return $Result
}

# Resolve Tool Path
if ([string]::IsNullOrWhiteSpace($ToolPath) -and -not [string]::IsNullOrWhiteSpace($Preset)) {
    $RepoRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName
    $ToolName = "Oxygen.Cooker.ImportTool.exe"

    $RootBase = Join-Path $RepoRoot "CMakePresets.json"
    $RootUser = Join-Path $RepoRoot "CMakeUserPresets.json"

    $AllPresets = New-Object System.Collections.ArrayList
    if (Test-Path $RootUser) {
        $Sub = Get-ExpandedCMakePresets -Path $RootUser
        foreach ($s in $Sub) { $null = $AllPresets.Add($s) }
    }
    if (Test-Path $RootBase) {
        $Sub = Get-ExpandedCMakePresets -Path $RootBase
        foreach ($s in $Sub) { $null = $AllPresets.Add($s) }
    }

    $BuildPresets = @{}
    $ConfigurePresets = @{}

    foreach ($File in $AllPresets) {
        if ($File.buildPresets) {
            foreach ($BP in $File.buildPresets) {
                if (-not $BuildPresets.ContainsKey($BP.name)) { $BuildPresets[$BP.name] = $BP }
            }
        }
        if ($File.configurePresets) {
            foreach ($CP in $File.configurePresets) {
                if (-not $ConfigurePresets.ContainsKey($CP.name)) { $ConfigurePresets[$CP.name] = $CP }
            }
        }
    }

    $Configuration = "Debug" # default fallback

    $BP = $BuildPresets[$Preset]
    if ($BP) {
        $ConfigName = $BP.configurePreset
        if ($BP.configuration) { $Configuration = $BP.configuration }
    } else {
        $ConfigName = $Preset
    }

    # Resolve configure preset inheritance for binaryDir
    $CP = $ConfigurePresets[$ConfigName]
    while ($null -ne $CP -and [string]::IsNullOrWhiteSpace($CP.binaryDir) -and $null -ne $CP.inherits) {
        $Inherits = if ($CP.inherits -is [array]) { $CP.inherits[0] } else { $CP.inherits }
        $CP = $ConfigurePresets[$Inherits]
    }

    if ($null -ne $CP -and (-not [string]::IsNullOrWhiteSpace($CP.binaryDir))) {
        # Expand common macros
        $ResolvedBinDir = $CP.binaryDir.Replace('`$sourceDir', $RepoRoot).Replace('`${sourceDir}', $RepoRoot)
        $ResolvedBinDir = [System.IO.Path]::GetFullPath($ResolvedBinDir)

        # Test standard locations inside this binary dir
        $Candidates = @(
            (Join-Path $ResolvedBinDir "bin\$Configuration\$ToolName"),
            (Join-Path $ResolvedBinDir "bin\$ToolName"),
            (Join-Path $ResolvedBinDir "$ToolName")
        )

        # Multi-config ninja specific:
        $Candidates += (Join-Path $ResolvedBinDir "src\Oxygen\Cooker\Tools\ImportTool\oxygen-cooker-importtool.dir\$Configuration\$ToolName")

        foreach ($cand in $Candidates) {
            if (Test-Path $cand -PathType Leaf) {
                $ToolPath = $cand
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($ToolPath)) {
        Write-Error "Could not resolve tool path for Preset '$Preset'. Ensure it's built or provide -ToolPath manually."
        exit 1
    }
} elseif ([string]::IsNullOrWhiteSpace($ToolPath)) {
    Write-Error "Please provide either -Preset or -ToolPath parameter."
    exit 1
}

Write-Host "Using ImportTool: $ToolPath" -ForegroundColor DarkGray

# Collect scenes to process
$ScenesToCook = @()

if ($All) {
    $AllDirs = Get-ChildItem -Path $ScenesDir -Directory
    foreach ($dir in $AllDirs) {
        $ManifestPath = Join-Path $dir.FullName "import-manifest.json"
        if (Test-Path $ManifestPath) {
            $ScenesToCook += $dir
        }
    }

    if ($ScenesToCook.Count -eq 0) {
        Write-Host "No scenes found with an import-manifest.json file in $ScenesDir" -ForegroundColor Yellow
        exit 0
    }
} else {
    $TargetSceneDir = Join-Path $ScenesDir $Scene
    if (-not (Test-Path $TargetSceneDir -PathType Container)) {
        Write-Error "Scene directory not found: $TargetSceneDir"
        exit 1
    }

    $ManifestPath = Join-Path $TargetSceneDir "import-manifest.json"
    if (-not (Test-Path $ManifestPath)) {
        Write-Error "No import-manifest.json found in scene directory: $TargetSceneDir"
        exit 1
    }

    $ScenesToCook = @(Get-Item $TargetSceneDir)
}

# Process each scene
$TotalCount = $ScenesToCook.Count
$SuccessCount = 0
$FailedCount = 0

foreach ($SceneDir in $ScenesToCook) {
    $ManifestPath = Join-Path $SceneDir.FullName "import-manifest.json"

    Write-Host ""
    Write-Host "=======================================================" -ForegroundColor Cyan
    Write-Host "Cooking Scene: $($SceneDir.Name)" -ForegroundColor Cyan
    Write-Host "=======================================================" -ForegroundColor Cyan

    # Base arguments for the tool
    $ArgsList = @(
        "batch",
        "--manifest",
        "`"$ManifestPath`""
    )

    if ($NoTUI) {
        $ArgsList += "--no-tui"
    }

    $ArgString = $ArgsList -join " "
    Write-Host "Executing:`n& `"$ToolPath`" $ArgString`n" -ForegroundColor DarkGray

    # Use Start-Process with Wait and NoNewWindow to keep output in current console and capture exit code
    $process = Start-Process -FilePath $ToolPath -ArgumentList $ArgString -Wait -NoNewWindow -PassThru

    if ($process.ExitCode -ne 0) {
        Write-Host "FAILED: Cooking scene $($SceneDir.Name) exited with code $($process.ExitCode)" -ForegroundColor Red
        $FailedCount++
    } else {
        Write-Host "SUCCESS: $($SceneDir.Name) cooked successfully." -ForegroundColor Green
        $SuccessCount++
    }
}

Write-Host "`nSummary: Cooked $TotalCount scene(s) ($SuccessCount succeeded, $FailedCount failed)." -ForegroundColor $(if ($FailedCount -gt 0) { "Red" } else { "Green" })

if ($FailedCount -gt 0) {
    exit 1
} else {
    exit 0
}
