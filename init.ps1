#!/usr/bin/env pwsh

# Based on: https://github.com/dotnet/Nerdbank.GitVersioning/blob/main/init.ps1
<#
.SYNOPSIS
    Installs dependencies required to build and test the projects in this
    repository.
.DESCRIPTION
    This does not require elevation, as the SDK and runtimes are installed to a
    per-user location.
.PARAMETER DotNetInstall
    Installs the .Net SDK version specified in global.json. Use this in CI
    environments to install the SDK and Runtime, without requiring elvation, and
    add install location to the current process PATH.
.PARAMETER NoRestore
    Skips the package restore step.
.PARAMETER NoToolRestore
    Skips the dotnet tool restore step.
.PARAMETER Interactive
    Runs NuGet restore in interactive mode. This can turn authentication
    failures into authentication challenges.
.PARAMETER NoPreCommitHooks
    Skips the installation of pre-commit (https://pre-commit.com/) and its
    hooks.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
Param (
    [Parameter()]
    [switch]$DotNetInstall,
    [Parameter()]
    [switch]$UpgradePrerequisites,
    [Parameter()]
    [switch]$NoRestore,
    [Parameter()]
    [switch]$NoToolRestore,
    [Parameter()]
    [switch]$Interactive,
    [Parameter()]
    [switch]$NoPreCommitHooks,
    [Parameter()]
    [switch]$Help
)

if ($Help) {
    Get-Help $MyInvocation.MyCommand.Definition
    exit
}

# Environment variables and Path that can be propagated via a temp file to a
# caller script.
#
# For example: $EnvVars['KEY'] = "VALUE"
#
$EnvVars = @{}
$PrependPath = @()
$HeaderColor = 'Green'
$ToolsDirectory = "$PSScriptRoot\tooling"

if ($DotNetInstall) {
    & "$ToolsDirectory\dotnet-install.ps1" -JSonFile "$PSScriptRoot\global.json"
    if ($LASTEXITCODE -ne 0) {
        Exit $LASTEXITCODE
    }
}

# Check if the pre-commit hooks were already installed in the repo.
$lockFile = ".pre-commit.installed.lock";
$preCommitInstalled = Test-Path -Path $lockFile
if (!$NoPreCommitHooks -and !$preCommitInstalled -and $PSCmdlet.ShouldProcess("pip install", "pre-commit")) {
    Write-Host "Installing pre-commit and its hooks" -ForegroundColor $HeaderColor
    New-Item $lockFile

    pip install pre-commit
    if ($LASTEXITCODE -ne 0) {
        Exit $LASTEXITCODE
    }

    pre-commit install
    if ($LASTEXITCODE -ne 0) {
        Exit $LASTEXITCODE
    }

    Write-Host ""
}

Push-Location $PSScriptRoot
try {
    $RestoreArguments = @()
    if ($Interactive) {
        $RestoreArguments += '--interactive'
    }

    # Check if the current directory contains a .sln or .csproj file and if yes,
    # restore nuget packages
    $haveSolutionOrProject = Test-Path -Path "*.sln,*.csproj"
    if (!$NoRestore -and $haveSolutionOrProject -and $PSCmdlet.ShouldProcess("NuGet packages", "Restore")) {
        Write-Host "Restoring NuGet packages" -ForegroundColor $HeaderColor
        dotnet restore @RestoreArguments
        if ($lastexitcode -ne 0) {
            throw "Failure while restoring packages."
        }
    }

    # Check if we have a dotnet tools manifest config file and if yes, restore
    # dotnet tools
    $toolsManifestExists = Test-Path -Path ".config/dotnet-tools.json" -PathType Leaf
    if (!$NoToolRestore -and $toolsManifestExists -and $PSCmdlet.ShouldProcess("dotnet tool", "restore")) {
        Write-Host "Restoring .Net tools" -ForegroundColor $HeaderColor
        dotnet tool restore @RestoreArguments
        if ($lastexitcode -ne 0) {
            throw "Failure while restoring dotnet CLI tools."
        }
    }

    & "$ToolsDirectory/Set-EnvVars.ps1" -Variables $EnvVars -PrependPath $PrependPath | Out-Null
}
catch {
    Write-Error $error[0]
    exit $lastexitcode
}
finally {
    Pop-Location
}
