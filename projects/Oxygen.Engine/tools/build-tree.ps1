#requires -Version 7.0


<#
.SYNOPSIS
Generate or configure Oxygen build trees without building the engine.
.DESCRIPTION
generate installs Conan dependencies, configures the selected trees and prepares
clangd for Ninja. Existing output is preserved unless -Clean is explicit.
configure reconfigures one existing preset and prepares clangd for Ninja. It never
runs Conan, deploys dependencies or cleans output. VS Code keeps its own hook.
Run from an initialized compiler environment. PowerShell 7 is required.
.PARAMETER Command
generate or configure.
.PARAMETER Selection
For generate: a Conan profile name or path. Relative paths use the engine root.
The profile is used for both host and build (native development).
For configure: an existing configure preset, such as oxygen-ninja-default.
.PARAMETER Generator
generate only: All (default), Ninja, or VisualStudio.
.PARAMETER DependencyBuild
generate only: Conan --build policy (default: missing). Does not build Oxygen.
.PARAMETER WithTracy
generate only: select the Tracy family instead of the ordinary family.
.PARAMETER Clean
generate only: remove selected build trees and their family's SDK configuration
directories before generation. Other families and the Conan cache are preserved.
.PARAMETER Define
configure only: CMake cache definitions, as NAME[:TYPE]=VALUE strings.
Example: -Define 'OXYGEN_BUILD_TESTS=OFF','OXYGEN_BUILD_DOCS=OFF'.
.PARAMETER Help
Show help without discovering tools or touching output. Alias: -h.
.EXAMPLE
./tools/build-tree.ps1 generate profiles/windows-msvc.ini
.EXAMPLE
./tools/build-tree.ps1 generate profiles/windows-msvc-asan.ini -Generator Ninja
.EXAMPLE
./tools/build-tree.ps1 configure oxygen-ninja-default
.EXAMPLE
./tools/build-tree.ps1 configure oxygen-vs-default -Define 'OXYGEN_BUILD_TESTS=OFF'
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('generate', 'configure')][string]$Command,
    [Parameter(Position = 1)][string]$Selection,
    [ValidateSet('All', 'Ninja', 'VisualStudio')][string]$Generator = 'All',
    [ValidateNotNullOrEmpty()][string]$DependencyBuild = 'missing',
    [switch]$WithTracy,
    [switch]$Clean,
    [string[]]$Define = @(),
    [Alias('h', '?')][switch]$Help
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

if ($Help -or -not $Command) {
    Get-Help $PSCommandPath -Detailed
    exit 0
}

# PowerShell parameter sets cannot dispatch on a positional parameter's value.
# Validate the few command-specific options before looking up tools or paths.
try {
    if (-not $Selection) { throw "$Command requires a $(if ($Command -eq 'generate') { 'Conan profile' } else { 'configure preset' }). See -Help." }
    $invalidOptions = if ($Command -eq 'configure') { @('Generator', 'DependencyBuild', 'WithTracy', 'Clean') } else { @('Define') }
    foreach ($option in $invalidOptions) {
        if ($PSBoundParameters.ContainsKey($option)) { throw "-$option is not valid for $Command. See -Help." }
    }
    foreach ($definition in $Define) {
        if ($definition -notmatch '^[A-Za-z_][A-Za-z0-9_]*(?::(?:BOOL|FILEPATH|PATH|STRING|INTERNAL))?=') {
            throw "Invalid CMake definition '$definition'. Use NAME[:TYPE]=VALUE."
        }
    }

    $engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    . (Join-Path $PSScriptRoot 'cli/BuildSelection.ps1')
    $prepareClangd = Join-Path $engineRoot '.vscode/prepare_clangd.py'
    $null = Get-Command cmake -ErrorAction Stop

    function Invoke-TreeConfigure([string]$Preset, [string]$BuildRoot, [string[]]$Definitions = @()) {
        $python = Get-OxygenPython $engineRoot
        Write-Host "Configuring $Preset..."
        $cmakeArgs = @('--preset', $Preset, "-DPython3_EXECUTABLE=$python") + @($Definitions | ForEach-Object { "-D$_" })
        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $cache = Get-OxygenCacheValues $BuildRoot
        if ($cache['CMAKE_GENERATOR'] -in @('Ninja', 'Ninja Multi-Config')) {
            if ($cache['CMAKE_EXPORT_COMPILE_COMMANDS'] -match '^(ON|TRUE|YES|1)$') {
                & $python $prepareClangd --build-dir $BuildRoot
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            } else {
                Write-Host 'Compile-command export is disabled; clangd databases were not refreshed.'
            }
        }
    }

    Push-Location -LiteralPath $engineRoot
    try {
        if ($Command -eq 'configure') {
            $graph = Read-OxygenPresetGraph $engineRoot
            if (-not $graph.configurePresets.Contains($Selection)) {
                throw "Unknown configure preset '$Selection'. Run build-tree.ps1 generate <profile> first, or cmake --list-presets."
            }
            $preset = Resolve-OxygenPreset $graph.configurePresets $Selection
            $environment = if ($preset['environment']) { $preset['environment'] } else { @{} }
            $buildRoot = Expand-OxygenPresetText $preset['binaryDir'] $engineRoot $engineRoot $Selection $environment
            if (-not [IO.Path]::IsPathRooted($buildRoot)) { $buildRoot = Join-Path $engineRoot $buildRoot }
            Invoke-TreeConfigure $Selection $buildRoot $Define
        } else {
            $null = Get-Command conan -ErrorAction Stop
            $null = Get-Command uv -ErrorAction Stop
            if ($Generator -ne 'VisualStudio') {
                if (-not (Test-Path -LiteralPath $prepareClangd -PathType Leaf)) { throw "Missing clangd helper: $prepareClangd" }
            }
            # Preserve Conan profile names; resolve file paths against the engine.
            $conanProfile = $Selection
            $candidate = if ([IO.Path]::IsPathRooted($conanProfile)) { $conanProfile } else { Join-Path $engineRoot $conanProfile }
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $conanProfile = (Resolve-Path -LiteralPath $candidate).Path
            } elseif ([IO.Path]::IsPathRooted($conanProfile) -or $conanProfile -match '[/\\]') {
                throw "Profile file does not exist: $candidate"
            }
            $conanProfileJson = & conan profile show "--profile:host=$conanProfile" "--profile:build=$conanProfile" --format=json
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            $resolvedProfile = ($conanProfileJson -join "`n") | ConvertFrom-Json
            $isAsan = $resolvedProfile.host.conf.'user.oxygen:sanitizer' -eq 'asan'
            $family = $(if ($WithTracy) { 'tracy-' } else { '' }) + $(if ($isAsan) { 'asan-' } else { '' })
            $sdkRoot = Join-Path $engineRoot $(if ($WithTracy) { 'out/install-tracy' } else { 'out/install' })
            $configurations = if ($isAsan) { @('Debug') } else { @('Debug', 'Release', 'RelWithDebInfo') }
            $trees = @(
                @{ Selection = 'Ninja'; Name = 'ninja'; Generator = 'Ninja Multi-Config' }
                @{ Selection = 'VisualStudio'; Name = 'vs'; Generator = 'Visual Studio 18 2026' }
            ) | Where-Object { $Generator -eq 'All' -or $_.Selection -eq $Generator }

            if ($Clean) {
                $outputRoot = [IO.Path]::GetFullPath((Join-Path $engineRoot 'out'))
                $targets = @($trees | ForEach-Object { Join-Path $outputRoot "build-$family$($_.Name)" })
                $targets += @($configurations | ForEach-Object { Join-Path $sdkRoot $(if ($isAsan) { 'Asan' } else { $_ }) })
                # Check all destinations before removing any. Refuse redirected
                # roots so an out/SDK junction cannot extend the cleanup scope.
                foreach ($target in $targets) {
                    $resolved = [IO.Path]::GetFullPath($target)
                    if (-not $resolved.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                        throw "Refusing to clean outside Oxygen's out directory: $resolved"
                    }
                    for ($ancestor = $resolved; $ancestor -ne $engineRoot; $ancestor = [IO.Path]::GetDirectoryName($ancestor)) {
                        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                            throw "Refusing to clean through a linked directory: $ancestor"
                        }
                    }
                }
                foreach ($target in $targets) {
                    if (Test-Path -LiteralPath $target) {
                        Write-Host "Cleaning $target"
                        Remove-Item -LiteralPath $target -Recurse -Force
                    }
                }
            }

            $conanArgs = @('install', '.', "--profile:host=$conanProfile", "--profile:build=$conanProfile",
                "--build=$DependencyBuild", "--deployer-folder=$sdkRoot", '--deployer-package=oxygen/*',
                '-o', "&:with_tracy=$([bool]$WithTracy)",
                '-o', '&:tools=True', '-o', '&:tests=True', '-o', '&:benchmarks=True',
                '-o', '&:examples=True', '-o', '&:docs=True')
            foreach ($tree in $trees) {
                Write-Host "Generating $family$($tree.Name): $($configurations -join ', ')"
                foreach ($config in $configurations) {
                    & conan @conanArgs -s "build_type=$config" -c "tools.cmake.cmaketoolchain:generator=$($tree.Generator)"
                    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
                }
                Invoke-TreeConfigure "oxygen-$family$($tree.Name)-default" (Join-Path $engineRoot "out/build-$family$($tree.Name)")
            }
        }
    } finally { Pop-Location }
    Write-Host '=== Success ==='
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
