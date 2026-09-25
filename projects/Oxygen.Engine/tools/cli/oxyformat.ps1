#requires -Version 7.3


<#
.SYNOPSIS
Run oxyformat with this checkout's locked repository Python environment.
.DESCRIPTION
Provision with uv sync --locked at the repository root or build-tree generate.
No packages are installed during tool invocation. Caller arguments are preserved.
#>

$toolArgs = @($args)
. (Join-Path $PSScriptRoot 'BuildSelection.ps1')
try {
    $python = Get-OxygenPython
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 2
}
$PSNativeCommandArgumentPassing = 'Standard'
$PSNativeCommandUseErrorActionPreference = $false
& $python (Join-Path $PSScriptRoot '../oxytools/run_oxyformat.py') @toolArgs
exit $LASTEXITCODE
