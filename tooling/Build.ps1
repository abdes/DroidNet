# SPDX-License-Identifier: MIT
<#
.SYNOPSIS
Build a DroidNet project or generated solution with x64 Visual Studio tools.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][string]$Solution,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('quiet', 'minimal', 'normal', 'detailed', 'diagnostic')][string]$Verbosity = 'minimal'
)

$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) {
    throw 'DroidNet requires a 64-bit PowerShell build host. 32-bit build hosts are not permitted.'
}
if ($env:OS -ne 'Windows_NT') {
    throw 'This wrapper requires Visual Studio on Windows.'
}
$installation = $env:VSINSTALLDIR
if (-not $installation) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio discovery is unavailable. Initialize an x64 VS developer shell.'
    }
    $installation = & $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw 'Visual Studio with 64-bit MSBuild is required.'
    }
}
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "64-bit MSBuild was not found at '$msbuild'. There is no 32-bit fallback."
}
if ($PSCmdlet.ShouldProcess($Solution, "Build with $msbuild (Platform=x64, PreferredToolArchitecture=x64)")) {
    Push-Location (Split-Path $PSScriptRoot -Parent)
    try {
        & $msbuild $Solution /restore /m /nologo "/v:$Verbosity" "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64
        $buildExitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    exit $buildExitCode
}
