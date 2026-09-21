#requires -Version 7.3
<#
.SYNOPSIS
Run oxytidy with the active virtual environment or the default Python on PATH.
.DESCRIPTION
uv checks/installs the editable tool into the selected interpreter as needed.
No environment is created or activated. All CLI arguments go unchanged to Python.
Analysis requires LLVM 23.x clang-tidy and clang-scan-deps; formatting also
requires LLVM 23.x clang-format.
.EXAMPLE
.\tools\cli\oxytidy.ps1 src/Oxygen/Base --include-tests
.EXAMPLE
.\tools\cli\oxytidy.ps1 --help
#>

if ($env:VIRTUAL_ENV) {
    $relativePython = if ($IsWindows) { 'Scripts/python.exe' } else { 'bin/python' }
    $python = Join-Path $env:VIRTUAL_ENV $relativePython
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        Write-Error "Setup failed: the active virtual environment has no interpreter at '$python'. Fix or deactivate that environment. Analysis did not start."
        exit 2
    }
} else {
    $command = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $command) {
        Write-Error 'Setup failed: python was not found on PATH. Activate your virtual environment or make Python 3.10+ available. Analysis did not start.'
        exit 2
    }
    $python = $command.Source
}

$uv = Get-Command uv -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $uv) {
    Write-Error 'Setup failed: uv was not found on PATH. Install uv: https://docs.astral.sh/uv/getting-started/installation/ . Analysis did not start.'
    exit 2
}

$project = Join-Path $PSScriptRoot '../oxytools'
$PSNativeCommandArgumentPassing = 'Standard'
$PSNativeCommandUseErrorActionPreference = $false
# An explicit executable targets that interpreter, never a discovered .venv.
# uv audits satisfied installations instead of reinstalling them on every run.
& $uv.Source pip install --quiet --python $python --editable $project
if ($LASTEXITCODE -ne 0) {
    Write-Error "Setup failed: oxytidy could not be installed into '$python'. Analysis did not start."
    exit 2
}
& $python -m oxytidy @args
exit $LASTEXITCODE
