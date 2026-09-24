<#
.SYNOPSIS
Build and run an executable from one selected CMake preset.
.DESCRIPTION
Uses the same preset preference and explicit overrides as oxybuild. -NoBuild
requires an existing artifact in the selected configuration. Runtime environment
comes from that tree's Conan test preset and is restored after execution.
Arguments after -- are passed unchanged to the executable.
.PARAMETER Target
CMake executable target name or fuzzy search pattern.
.PARAMETER BuildTree
Constrain selection to a tree name or path.
.PARAMETER Config
Constrain selection to a configuration.
.PARAMETER Preset
Select an exact CMake build preset.
.PARAMETER Sanitized
Require an ASan Debug preset.
.PARAMETER NoBuild
Run an existing executable. Automatic selection skips unavailable artifacts.
.PARAMETER DryRun
Show selection, build commands and executable without modifying the build tree.
.PARAMETER ListBuilds
List available build/configuration choices in preference order.
.PARAMETER Help
Show usage, options and examples. Alias: -h.
.EXAMPLE
oxyrun oxygen-examples-renderscene
.EXAMPLE
oxyrun oxygen-examples-renderscene -Preset oxygen-ninja-release -NoBuild -- --help
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Target,
    [string]$Config,
    [string]$BuildTree,
    [string]$Preset,
    [switch]$NoBuild,
    [switch]$DryRun,
    [switch]$Sanitized,
    [switch]$ListBuilds,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$RemainingArgs = @()
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
    if ($NoBuild) {
        $targetResolver = { param($name, $candidate, $selectionProbe)
            if ($selectionProbe) { Resolve-TargetName $name $candidate.BuildRoot -NoInteractive 6>$null }
            else { Resolve-TargetName $name $candidate.BuildRoot }
        }
        $run = Resolve-OxygenExecutables -Targets $Target -BuildTree $BuildTree -Config $Config -Preset $Preset -Sanitized:$Sanitized -TargetResolver $targetResolver
        Write-OxygenExecutableSelection $run
        $resolved = $run.Targets[$Target]
        $runKey = $Target
    } else {
        $selection = Resolve-OxygenBuildSelection -BuildTree $BuildTree -Config $Config -Preset $Preset -Sanitized:$Sanitized
        Write-OxygenBuildSelection $selection
        $resolved = Resolve-TargetName $Target $selection.BuildRoot
        if (-not $resolved) { throw 'Target resolution failed or was cancelled.' }
        $resolved = Invoke-BuildForTarget $resolved $selection -DryRun:$DryRun
        $run = Resolve-OxygenExecutables -Targets $resolved -Selection $selection -AllowMissing:$DryRun
        $runKey = $resolved
    }
    $artifact = $run.Paths[$runKey]
    if ($RemainingArgs.Count -gt 0 -and $RemainingArgs[0] -eq '--') {
        $RemainingArgs = @($RemainingArgs | Select-Object -Skip 1)
    }
    if ($DryRun) {
        if ($artifact) { Write-Host "Would run: $artifact" }
        else { Write-Host "Would run the executable reported by CMake after building '$resolved' [$($run.Selection.Config)]." }
        Write-Host "Arguments: $($RemainingArgs -join ' ')"
        return
    }
    Write-LogAction "Running: $artifact"
    Invoke-OxygenTool -Context $run -Target $runKey -Arguments $RemainingArgs
    exit $LASTEXITCODE
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
