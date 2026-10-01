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
.PARAMETER NoPythonRestore
    Skip uv sync; expose the existing repository Python environment if present.
.PARAMETER NoToolRestore
    Skips the dotnet tool restore step.
.PARAMETER Interactive
    Runs NuGet restore in interactive mode. This can turn authentication
    failures into authentication challenges.
.PARAMETER NoPreCommitHooks
    Skips the installation of pre-commit (https://pre-commit.com/) and its
    hooks.
.PARAMETER Help
    Print all options and examples. Aliases: -h and --help.
.EXAMPLE
    ./init.ps1
    Restore repository tools and existing root/Projects solutions, and install hooks.
.EXAMPLE
    ./init.ps1 -NoRestore -NoPreCommitHooks
    Restore the pinned .NET tools and editable Python workspace, and expose its commands.
.EXAMPLE
    ./init.ps1 -DotNetInstall -Interactive
    Install the selected .NET SDK and allow authentication during restore.
.EXAMPLE
    ./init.ps1 -NoRestore -NoToolRestore -NoPythonRestore -NoPreCommitHooks
    Expose already-installed repository tools in this shell without restoring packages.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
Param (
    [Parameter()]
    [switch]$DotNetInstall,
    [Parameter()]
    [switch]$NoRestore,
    [Parameter()]
    [switch]$NoToolRestore,
    [Parameter()]
    [switch]$NoPythonRestore,
    [Parameter()]
    [switch]$Interactive,
    [Parameter()]
    [switch]$NoPreCommitHooks,
    [Parameter()]
    [Alias("h", "-help")][switch]$Help
)

if ($Help) {
    Get-Help $MyInvocation.MyCommand.Definition -Full
    return
}

$ErrorActionPreference = 'Stop'

# Environment variables and Path that can be propagated via a temp file to a
# caller script.
#
# For example: $EnvVars['KEY'] = "VALUE"
#
$EnvVars = @{}
$PrependPath = @()
$HeaderColor = 'Green'
$ToolsDirectory = "$PSScriptRoot\tooling"

Push-Location $PSScriptRoot
try {
    if (!$NoPythonRestore -and $PSCmdlet.ShouldProcess("Repository Python environment", "uv sync --locked")) {
        if (!(Get-Command uv -ErrorAction SilentlyContinue)) { throw 'Install uv, then rerun init. See tooling/PYTHON.md.' }
        & uv sync --locked --no-active
        if ($LASTEXITCODE -ne 0) { throw 'Repository Python environment setup failed.' }
    }
    $pythonScripts = Join-Path $PSScriptRoot '.venv/Scripts'
    foreach ($command in @('get-artifacts.exe', 'traverse.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $pythonScripts $command)) -and !$WhatIfPreference) {
            throw 'Repository Python commands are missing. Rerun init without -NoPythonRestore.'
        }
    }
    if ($env:PS1UnderCmd -ne '1' -and $PSCmdlet.ShouldProcess("Current PowerShell session", "Activate repository environment")) {
        & (Join-Path $pythonScripts 'Activate.ps1')
    }

    $lockFile = Join-Path $PSScriptRoot '.pre-commit.installed.lock'
    if (!$NoPreCommitHooks -and !(Test-Path -LiteralPath $lockFile) -and $PSCmdlet.ShouldProcess("Repository hooks", "Install")) {
        $preCommit = Join-Path $pythonScripts 'pre-commit.exe'
        if (!(Test-Path -LiteralPath $preCommit)) { throw 'Run init without -NoPythonRestore to provision pre-commit.' }
        & $preCommit install
        if ($LASTEXITCODE -ne 0) { throw 'Pre-commit hook installation failed.' }
        [void](New-Item -ItemType File -Path $lockFile -Force)
    }

    $pathBeforeSdk = $env:PATH
    if ($DotNetInstall -and $PSCmdlet.ShouldProcess(".NET SDK", "Install")) {
        & "$ToolsDirectory/dotnet-install.ps1" -JSonFile "$PSScriptRoot/global.json"
        if ($LASTEXITCODE -ne 0) { throw '.NET SDK installation failed.' }
    }
    if ($env:PATH -ne $pathBeforeSdk) { $EnvVars['PATH'] = $env:PATH }

    $RestoreArguments = @()
    if ($Interactive) {
        $RestoreArguments += '--interactive'
    }

    $restoreSolutions = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.sln' -File)
    if (!$restoreSolutions -and (Test-Path -LiteralPath "$PSScriptRoot/projects/Projects.sln")) {
        $restoreSolutions = @(Get-Item -LiteralPath "$PSScriptRoot/projects/Projects.sln")
    }
    if (!$NoRestore -and $restoreSolutions.Count -gt 0 -and $PSCmdlet.ShouldProcess("NuGet packages", "Restore")) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $msbuild = @(& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/amd64/MSBuild.exe') | Select-Object -First 1
        if (!$msbuild) { throw 'Install Visual Studio MSBuild before restoring projects.' }
        foreach ($solution in $restoreSolutions) {
            $arguments = @($solution.FullName, '/t:Restore', '/m', '/nologo')
            if ($Interactive) { $arguments += '/p:NuGetInteractive=true' }
            & $msbuild @arguments
            if ($LASTEXITCODE -ne 0) { throw "Restore failed: $($solution.FullName)" }
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

    if ($env:PS1UnderCmd -eq '1') {
        # A .cmd invoked from PowerShell cannot change that parent PowerShell's environment.
        $self = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
        $cmd = Get-CimInstance Win32_Process -Filter "ProcessId=$($self.ParentProcessId)"
        $caller = Get-CimInstance Win32_Process -Filter "ProcessId=$($cmd.ParentProcessId)"
        $EnvVars['DROIDNET_INIT_POWERSHELL_CALLER'] = if ($caller.Name -in @('pwsh.exe', 'powershell.exe') -and $cmd.CommandLine -match '(?i)\s/c\s') { '1' } else { '0' }
        & "$ToolsDirectory/Set-EnvVars.ps1" -Variables $EnvVars -PrependPath $PrependPath 6>$null | Out-Null
    } elseif (!$WhatIfPreference) {
        foreach ($command in @('get-artifacts', 'traverse')) {
            $resolved = Get-Command $command -ErrorAction Stop
            $expected = [IO.Path]::GetFullPath((Join-Path $pythonScripts "$command.exe"))
            if ($resolved.CommandType -ne 'Application' -or [IO.Path]::GetFullPath($resolved.Source) -ne $expected) { throw "$command resolves outside the repository environment: $($resolved.Source)" }
        }
        Write-Host 'Ready in this PowerShell session: get-artifacts --help; traverse --help' -ForegroundColor $HeaderColor
    }
}
catch {
    Write-Error $error[0]
    exit 1
}
finally {
    Pop-Location
}
