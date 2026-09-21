#requires -Version 7.3
<#
.SYNOPSIS
Check or format owned C++ files with clang-format 23.x.
.DESCRIPTION
Uses the active virtual environment or Python on PATH. Install tools/oxytools
once in that interpreter. This launcher does not install packages during runs.
#>

if ($env:VIRTUAL_ENV) {
    $relativePython = if ($IsWindows) { 'Scripts/python.exe' } else { 'bin/python' }
    $python = Join-Path $env:VIRTUAL_ENV $relativePython
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        Write-Error "The active virtual environment has no interpreter at '$python'."
        exit 2
    }
} else {
    $command = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $command) {
        Write-Error 'Python 3.10+ is required. Install tools/oxytools in your chosen interpreter.'
        exit 2
    }
    $python = $command.Source
}

$PSNativeCommandArgumentPassing = 'Standard'
$PSNativeCommandUseErrorActionPreference = $false
& $python (Join-Path $PSScriptRoot '../oxytools/run_oxyformat.py') @args
exit $LASTEXITCODE
