<#
.SYNOPSIS
    Generates a scoped x64 Visual Studio solution from source projects.
.DESCRIPTION
    Includes C# and C++/CLI projects in the selected directory, plus their
    references. Generated solutions remain untracked. Uses the pinned SlnGen
    package with Visual Studio MSBuild; never builds the engine or the editor.
.PARAMETER Scope
    Project file or directory to include. Defaults to the repository root.
.PARAMETER SolutionPath
    Output solution path. Defaults to Scope/ScopeName.sln, or AllProjects.sln
    at the repository root. For a project file, defaults beside that project.
.PARAMETER Launch
    Open the generated solution in Visual Studio. Off by default.
.PARAMETER NoLaunch
    Suppress opening Visual Studio, including when open.cmd supplies -Launch.
.PARAMETER UseDiagnostics
    Save the generator's binary log in artifacts/build-streamlining/slngen.binlog.
.PARAMETER TestScope
    Include All (default), Unit, Integration, or Benchmarks test projects.
    Production projects and their references remain included. Non-default scopes
    use a distinct solution filename unless SolutionPath is supplied.
.PARAMETER Help
    Show all options and examples. Aliases: -h and --help.
.EXAMPLE
    ./tooling/GenerateSolution.ps1
    Generate AllProjects.sln without opening Visual Studio.
.EXAMPLE
    ./tooling/GenerateSolution.ps1 -Scope projects/Storage -Launch
    Open a solution for Storage, its tests and references.
.EXAMPLE
    ./projects/Oxygen.Editor/open.cmd -NoLaunch
    Generate the editor solution without opening another Visual Studio window.
.EXAMPLE
    ./tooling/GenerateSolution.ps1 -Scope projects -SolutionPath artifacts/Check.sln -UseDiagnostics
    Generate all product projects, including native tests, with a diagnostic log.
.EXAMPLE
    ./projects/Oxygen.Editor.WorldEditor/open.cmd -TestScope Integration
    Open native integration tests separately from routine unit and UI tests.
.EXAMPLE
    ./projects/Oxygen.Editor.WorldEditor/open.cmd -TestScope Benchmarks -NoLaunch
    Generate the explicit benchmark solution without opening Visual Studio.
#>
[CmdletBinding()]
param(
    [string] $Scope,
    [string] $SolutionPath,
    [switch] $Launch,
    [switch] $NoLaunch,
    [switch] $UseDiagnostics,
    [ValidateSet('Unit', 'Integration', 'Benchmarks', 'All')][string] $TestScope = 'All',
    [Alias('h', '-help')][switch] $Help
)
if ($Help) { Get-Help $PSCommandPath -Full; return }
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if (!$Scope) { $Scope = $repoRoot }
$scopePath = [IO.Path]::GetFullPath($Scope)
if (!(Test-Path -LiteralPath $scopePath)) { throw "Scope does not exist: $scopePath" }
if (!$SolutionPath) {
    $name = if ($scopePath.TrimEnd('\', '/') -eq $repoRoot) { 'AllProjects' } else { Split-Path $scopePath -Leaf }
    $directory = if (Test-Path -LiteralPath $scopePath -PathType Leaf) { Split-Path $scopePath -Parent } else { $scopePath }
    if (Test-Path -LiteralPath $scopePath -PathType Leaf) { $name = [IO.Path]::GetFileNameWithoutExtension($name) }
    $suffix = if ($TestScope -eq 'All') { '' } else { ".$TestScope" }
    $SolutionPath = Join-Path $directory "$name$suffix.sln"
}
$SolutionPath = [IO.Path]::GetFullPath($SolutionPath)
$originalPath = $env:PATH
Push-Location $repoRoot
try {
    if (Test-Path -LiteralPath $scopePath -PathType Leaf) {
        $projects = @($scopePath)
    } else {
        $files = @(& git ls-files --cached --others --exclude-standard -- '*.csproj' '*.vcxproj' ':(exclude)projects/Oxygen.Engine/**')
        if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate source projects.' }
        $prefix = $scopePath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $projects = @($files | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $repoRoot $_)) } |
            Where-Object { $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -and [IO.File]::Exists($_) } | Sort-Object -Unique)
    }
    if ($projects.Count -eq 0) { throw "No source projects in $scopePath" }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($SolutionPath))
    # Use the VS/MSBuild host supplied by the same pinned tool package. The
    # dotnet host cannot evaluate C++/CLI imports and native project references.
    $manifest = Get-Content -LiteralPath (Join-Path $repoRoot '.config/dotnet-tools.json') -Raw | ConvertFrom-Json
    $version = $manifest.tools.'microsoft.visualstudio.slngen.tool'.version
    $cacheInfo = @(& dotnet nuget locals global-packages --list --force-english-output)
    if ($LASTEXITCODE -ne 0) { throw 'Could not resolve the NuGet global package directory.' }
    $cache = ($cacheInfo | Where-Object { $_ -like 'global-packages:*' } | Select-Object -First 1) -replace '^global-packages:\s*', ''
    if (!$cache) { throw 'NuGet did not report its global package directory.' }
    $slngen = Join-Path $cache "microsoft.visualstudio.slngen.tool/$version/tools/slngen/net472/slngen.exe"
    if (!(Test-Path -LiteralPath $slngen)) { throw 'Run dotnet tool restore before generating solutions.' }
    $arguments = @('--launch', ($Launch.IsPresent -and !$NoLaunch.IsPresent).ToString().ToLowerInvariant(),
        '--solutionfile', $SolutionPath, '--folders', 'false', '--platform', 'x64', '--configuration', 'Debug;Release')
    $arguments += @('--property', "DroidNetTestScope=$TestScope", '--property', 'PreferredToolArchitecture=x64')
    if ($UseDiagnostics) {
        $logs = Join-Path $repoRoot 'artifacts/build-streamlining'
        [void][IO.Directory]::CreateDirectory($logs)
        $arguments += "-bl:$logs/slngen.binlog"
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $msbuild = @(& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/amd64/MSBuild.exe') | Select-Object -First 1
    if (!$msbuild) { throw 'Install Visual Studio MSBuild before generating solutions.' }
    $installation = @(& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath) | Select-Object -First 1
    $devenv = Join-Path $installation 'Common7/IDE/devenv.exe'
    if ($Launch -and !$NoLaunch) {
        if (!(Test-Path -LiteralPath $devenv)) { throw 'This installation provides Build Tools only; use -NoLaunch or install the Visual Studio IDE.' }
        $arguments += @('--devenvfullpath', $devenv)
    }
    $env:PATH = (Split-Path $msbuild -Parent) + [IO.Path]::PathSeparator + $env:PATH
    & $slngen @arguments @projects
    if ($LASTEXITCODE -ne 0) { throw "SlnGen failed with exit code $LASTEXITCODE" }
} finally {
    $env:PATH = $originalPath
    Pop-Location
}
