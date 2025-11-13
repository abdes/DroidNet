# Based on https://github.com/CommunityToolkit/Windows
<#
.SYNOPSIS
    Generates the solution file containing individual projects, samples, and
    tests.
.DESCRIPTION
    Used mainly for CI building of everything. Otherwise, it is recommended to
    focus on an individual scopes instead.
.PARAMETER UseDiagnostics
    Add extra diagnostic output to running slngen, such as a binlog, etc...
#>
Param (
    [Parameter(HelpMessage = "Add extra diagnostic output to slngen generator.")]
    [switch]$UseDiagnostics = $false
)

# Set up constant values
$generatedSolutionFilePath = 'AllProjects.sln'
$platforms = 'Any CPU;x64;x86;ARM64'
$slngenConfig = @(
    '--folders'
    'true'
    '--collapsefolders'
    'true'
    '--ignoreMainProject'
)

# Remove previous file if it exists
if (Test-Path -Path $generatedSolutionFilePath) {
    Remove-Item $generatedSolutionFilePath
    Write-Host "Removed previous solution file"
}

# Projects to include
$projects = [System.Collections.ArrayList]::new()

# Tools
# [void]$projects.Add(".\tooling\**\*.*csproj")

# Application projects
[void]$projects.Add(".\projects\**\src\*.csproj")
[void]$projects.Add(".\projects\**\tests\**\*.Tests.csproj")
[void]$projects.Add(".\projects\**\samples\**\*.csproj")

# Tooling Projects
[void]$projects.Add(".\tooling\samples\**\*.csproj")

if ($UseDiagnostics.IsPresent) {
    $sdkoptions = "-d"
}
else {
    $sdkoptions = ""
}

$cmd = 'dotnet'
$arguments = @(
    $sdkoptions
    'tool'
    'run'
    'slngen'
    '--launch'
    'false'
    '-o'
    $generatedSolutionFilePath
    $slngenConfig
    '--platform'
    "'$platforms'"
    $projects
)

if ($UseDiagnostics.IsPresent) {
    $arguments += @(
        '-bl:slngen.binlog'
        '--consolelogger:ShowEventId;Summary;Verbosity=Detailed'
    )
}

Write-Output "Running Command: $cmd $arguments"

&$cmd @arguments
