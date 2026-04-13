<#
.SYNOPSIS
Run scoped clang-tidy analysis using the repo compile database and clangd flag adjustments.

.DESCRIPTION
Wrapper for tools/cli/oxytidy.py. The Python tool:
- resolves compile_commands.json from .clangd by default
- applies .clangd Remove/Add compile flag rules to a sanitized temporary compile database
- runs clang-tidy per translation unit in parallel
- filters diagnostics to the selected source roots so reports stay focused on your code
- never uses autofix flags

Logs, per-file outputs, and the sanitized compile database are written under
out/clang-tidy/ unless -LogDir is provided.

.PARAMETER Paths
One or more files or directories to analyze. Defaults to src/Oxygen/Vortex.

.PARAMETER Configuration
Build configuration slice to select from a multi-config compile database.
Defaults to Debug.

.PARAMETER BuildDir
Optional override for the directory containing compile_commands.json.

.PARAMETER Jobs
Parallel worker count. Defaults to the Python tool default.

.PARAMETER IncludeTests
Include Test/ files under the selected roots.

.PARAMETER MaxFiles
Limit the number of selected translation units. Useful for smoke tests.

.PARAMETER Checks
Optional clang-tidy --checks override string.

.PARAMETER LogDir
Optional output directory for logs and the sanitized compile database.

.PARAMETER ListFiles
List selected translation units and exit.

.PARAMETER SummaryOnly
Print only the summary instead of replaying every diagnostic.

.PARAMETER NoQuiet
Do not pass --quiet to clang-tidy.

.EXAMPLE
.\tools\cli\oxytidy.ps1

Run clang-tidy for src/Oxygen/Vortex with repo defaults.

.EXAMPLE
.\tools\cli\oxytidy.ps1 src/Oxygen/Vortex -SummaryOnly

Run a focused Vortex pass and print only the summary.

.EXAMPLE
.\tools\cli\oxytidy.ps1 src/Oxygen/Vortex -Jobs 6 -Configuration Debug

Run Vortex in parallel against the Debug slice of the compile database.

.EXAMPLE
.\tools\cli\oxytidy.ps1 src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp -MaxFiles 1

Smoke-test a single translation unit.
#>
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Paths,
    [string]$Configuration = "Debug",
    [string]$BuildDir,
    [string]$ConfigFile,
    [int]$Jobs,
    [switch]$IncludeTests,
    [int]$MaxFiles,
    [string]$Checks,
    [string]$LogDir,
    [switch]$ListFiles,
    [switch]$SummaryOnly,
    [switch]$NoQuiet,
    [Alias("?")]
    [switch]$Help
)

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Write-Error "python was not found in PATH; oxytidy.ps1 requires Python to run tools/cli/oxytidy.py"
    exit 1
}

$scriptPath = Join-Path $PSScriptRoot "oxytidy.py"
$arguments = @($scriptPath)

if ($Help) {
    & $python.Source $scriptPath --help
    exit $LASTEXITCODE
}

if ($Paths -and $Paths.Count -gt 0) {
    $arguments += $Paths
}
if ($Configuration) {
    $arguments += @("--configuration", $Configuration)
}
if ($BuildDir) {
    $arguments += @("--build-dir", $BuildDir)
}
if ($ConfigFile) {
    $arguments += @("--config-file", $ConfigFile)
}
if ($PSBoundParameters.ContainsKey("Jobs")) {
    $arguments += @("--jobs", $Jobs.ToString())
}
if ($IncludeTests) {
    $arguments += "--include-tests"
}
if ($PSBoundParameters.ContainsKey("MaxFiles")) {
    $arguments += @("--max-files", $MaxFiles.ToString())
}
if ($Checks) {
    $arguments += @("--checks", $Checks)
}
if ($LogDir) {
    $arguments += @("--log-dir", $LogDir)
}
if ($ListFiles) {
    $arguments += "--list-files"
}
if ($SummaryOnly) {
    $arguments += "--summary-only"
}
if ($NoQuiet) {
    $arguments += "--no-quiet"
}

& $python.Source @arguments
exit $LASTEXITCODE
