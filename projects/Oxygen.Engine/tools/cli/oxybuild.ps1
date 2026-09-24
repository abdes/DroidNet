<#
.SYNOPSIS
Build a target from an initialized CMake preset.
.DESCRIPTION
Without overrides, selects an existing preset by configuration (Release,
RelWithDebInfo, MinSizeRel, Debug), then ordinary before ASan/Tracy, then Ninja
before Visual Studio. Prints the selection. Never installs Conan dependencies.
.PARAMETER Target
CMake target name or fuzzy search pattern.
.PARAMETER BuildTree
Constrain selection to a tree name, out-relative path, or absolute path.
.PARAMETER Config
Constrain selection to a configuration. No implicit change to another config.
.PARAMETER Preset
Select an exact CMake build preset.
.PARAMETER Sanitized
Require an ASan Debug preset. A conflicting explicit choice is an error.
.PARAMETER ListBuilds
List available build/configuration choices in preference order.
.PARAMETER DryRun
Show selection and commands without configuring or building.
.PARAMETER Help
Show usage, options and examples. Alias: -h.
.EXAMPLE
oxybuild oxygen-base
.EXAMPLE
oxybuild oxygen-base -Preset oxygen-tracy-ninja-debug
.EXAMPLE
oxybuild -ListBuilds
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Target,
    [string]$Config,
    [string]$BuildTree,
    [string]$Preset,
    [switch]$DryRun,
    [switch]$Sanitized,
    [switch]$ListBuilds,
    [Alias('h')][switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'oxy-targets.ps1')
$global:VerboseMode = ($VerbosePreference -eq 'Continue')
try {
    if ($ListBuilds) {
        Get-OxygenBuildCandidates | Sort-Object ConfigRank, InstrumentationRank, GeneratorRank, BuildPreset |
            Format-Table BuildPreset, Config, Generator, BuildRoot -AutoSize
        return
    }
    if (-not $Target) { throw 'Specify a target, or use -ListBuilds to see available presets.' }
    $selection = Resolve-OxygenBuildSelection -BuildTree $BuildTree -Config $Config -Preset $Preset -Sanitized:$Sanitized
    Write-OxygenBuildSelection $selection
    $resolved = Resolve-TargetName $Target $selection.BuildRoot
    if (-not $resolved) { throw 'Target resolution failed or was cancelled.' }
    $null = Invoke-BuildForTarget $resolved $selection -DryRun:$DryRun
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
