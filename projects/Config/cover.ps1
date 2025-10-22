<#
.SYNOPSIS
Runs Config tests with coverage and optionally generates an HTML report using ReportGenerator.

.DESCRIPTION
This helper builds and runs the `Config.Tests` project with coverage collection (Cobertura XML).
It attempts to determine the test output path from MSBuild's OutputPath. Note: `dotnet test`
writes test run artifacts (TRX, logs) under the project's build OutputPath in a `TestResults`
folder, while coverage collectors write the coverage XML to the results directory passed to
`dotnet test` (the `--results-directory`). This script checks the results directory for the
Cobertura XML first, then falls back to the OutputPath's `TestResults` folder, and finally
searches the `bin` tree as a last resort.

.PARAMETER Configuration
The build configuration to use (Debug/Release). Default: Debug.

.PARAMETER Project
Relative path to the test project file. Default: `tests\Config.Tests.csproj`.

.PARAMETER ResultsDir
Explicit results directory passed to `dotnet test`. Default: `\TestResults` under the script folder.

.PARAMETER InstallReportGenerator
If set, the script will attempt to install the `dotnet-reportgenerator-globaltool` when missing.

.INPUTS
None. This script does not accept pipeline input; use the named parameters instead.

.OUTPUTS
None. The script writes coverage files and an HTML report to disk and opens the report but does not emit objects to the pipeline.

.EXAMPLE
PS> .\cover.ps1
Runs tests and writes Cobertura XML to the script's TestResults folder. If ReportGenerator is installed,
an HTML report will be generated and opened.

.EXAMPLE
PS> .\cover.ps1 -InstallReportGenerator
Installs ReportGenerator (if needed) and generates an HTML report.

.NOTES
- The script uses `dotnet msbuild -getProperty:OutputPath` to find the build output directory for the
    test project and resolves it to an absolute path.
- ReportGenerator is invoked quietly; its output is shown only on failure.
#>
param(
    [string]$Configuration = "Debug",
    [string]$Project = "tests\Config.Tests.csproj",
    [string]$ResultsDir = "$PSScriptRoot\TestResults",
    [switch]$InstallReportGenerator
)

Set-StrictMode -Version Latest

function Write-Log {
    param([string]$Message, [string]$Level = 'INFO')
    Write-Host "[$Level] $Message"
}

Write-Log "Starting coverage run for project '$Project' (Configuration=$Configuration)"

$projPath = Join-Path -Path $PSScriptRoot -ChildPath $Project
if (-not (Test-Path $projPath)) {
    Write-Log "Project file not found: $projPath" "ERROR"
    throw "Project file not found: $projPath"
}

Write-Log "Resolving OutputPath via msbuild..."

# Query MSBuild for OutputPath (quiet) and take the last non-empty line as the path
$msOut = & dotnet msbuild $projPath -nologo -p:"Configuration=$Configuration" -v:q -t:Build -getProperty:OutputPath 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Log "msbuild failed. Output:" "ERROR"
    $msOut | ForEach-Object { Write-Host $_ }
    throw "msbuild failed with exit code $LASTEXITCODE"
}

$lastLine = ($msOut | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' } | Select-Object -Last 1)
if ($lastLine -and $lastLine -match '^(?:.*OutputPath\s*(?:=|:)\s*)?(.*)$') {
    $outputRelative = $Matches[1].Trim()
} else {
    $outputRelative = $null
}

if ($outputRelative) {
    if ([System.IO.Path]::IsPathRooted($outputRelative)) {
        $outputDir = [System.IO.Path]::GetFullPath($outputRelative)
    } else {
        $outputDir = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Path $projPath -Parent) $outputRelative))
    }
    Write-Log "Resolved absolute output directory: $outputDir"
} else {
    Write-Log "msbuild did not return an OutputPath; proceeding without it" "WARN"
    $outputDir = $null
}

# Prepare results directory
if (-not (Test-Path $ResultsDir)) {
    New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
}

Write-Log "Running tests with coverage..."

$testArgs = @(
    $projPath,
    '--configuration', $Configuration,
    '--',
    '--coverage',
    '--coverage-output-format', 'cobertura',
    '--coverage-output', 'coverage.cobertura.xml'
)

Write-Log "dotnet test $($testArgs -join ' ')"
& dotnet test @testArgs
if ($LASTEXITCODE -ne 0) {
    Write-Log "dotnet test failed with exit code $LASTEXITCODE" "ERROR"
    throw "dotnet test failed with exit code $LASTEXITCODE"
}

# Locate coverage file: prefer MSBuild OutputPath\TestResults, then ResultsDir, then bin search
if ($outputDir) {
    $coverageFile = Join-Path $outputDir 'TestResults\coverage.cobertura.xml'
    $coverageFile = [System.IO.Path]::GetFullPath($coverageFile)
    Write-Log "Looking for cobertura file at msbuild TestResults location: $coverageFile"
    if (Test-Path $coverageFile) {
        Write-Log "Found coverage file at: $coverageFile"
    } else {
        # Fallback to ResultsDir
        $coverageFile = Join-Path $ResultsDir 'coverage.cobertura.xml'
        Write-Log "Looking for cobertura file in results directory: $coverageFile"
        if (Test-Path $coverageFile) {
            Write-Log "Found coverage file in results directory: $coverageFile"
        } else {
            # Broad fallback: search the bin tree
            $binRoot = Join-Path $PSScriptRoot '..\..\bin'
            Write-Log "Searching bin tree for coverage.cobertura.xml under: $binRoot"
            $found = Get-ChildItem -Path $binRoot -Recurse -Filter 'coverage.cobertura.xml' -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($found) {
                $coverageFile = $found.FullName
                Write-Log "Found coverage file at: $coverageFile"
            } else {
                Write-Log "Coverage file not found in msbuild output, results directory, or bin tree." "ERROR"
                throw "Coverage file not found. Expected: $coverageFile"
            }
        }
    }
} else {
    # No outputDir available: check ResultsDir then bin tree
    $coverageFile = Join-Path $ResultsDir 'coverage.cobertura.xml'
    Write-Log "Looking for cobertura file in results directory: $coverageFile"
    if (-not (Test-Path $coverageFile)) {
        $binRoot = Join-Path $PSScriptRoot '..\..\bin'
        Write-Log "Searching bin tree for coverage.cobertura.xml under: $binRoot"
        $found = Get-ChildItem -Path $binRoot -Recurse -Filter 'coverage.cobertura.xml' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            $coverageFile = $found.FullName
            Write-Log "Found coverage file at: $coverageFile"
        } else {
            Write-Log "Coverage file not found in results directory or bin tree." "ERROR"
            throw "Coverage file not found. Expected: $coverageFile"
        }
    } else {
        Write-Log "Found coverage file in results directory: $coverageFile"
    }
}

# Ensure reportgenerator is available
$rgCmd = Get-Command reportgenerator -ErrorAction SilentlyContinue
if (-not $rgCmd) {
    Write-Log "reportgenerator not found on PATH." "WARN"
    if ($InstallReportGenerator) {
        Write-Log "Installing reportgenerator global tool (dotnet-reportgenerator-globaltool)..."
        & dotnet tool install -g dotnet-reportgenerator-globaltool
        if ($LASTEXITCODE -ne 0) {
            Write-Log "Failed to install reportgenerator." "ERROR"
            throw "Could not install reportgenerator global tool"
        }
        $rgCmd = Get-Command reportgenerator -ErrorAction SilentlyContinue
        if (-not $rgCmd) {
            Write-Log "reportgenerator still not found after install. You may need to restart your shell to refresh PATH." "ERROR"
            throw "reportgenerator not available"
        }
    } else {
        Write-Log "To generate HTML coverage reports install the tool: dotnet tool install -g dotnet-reportgenerator-globaltool" "INFO"
        Write-Log "Coverage XML is available at: $coverageFile" "INFO"
        Write-Log "Skipping HTML generation." "WARN"
        exit 0
    }
}

# Generate HTML report
$reportDir = Join-Path $ResultsDir 'CoverageReport'
if (Test-Path $reportDir) { Remove-Item -Recurse -Force $reportDir }

Write-Log "Generating HTML report with reportgenerator..."
# Run reportgenerator quietly: capture output and only display if it fails
$rgOutput = & reportgenerator -reports:$coverageFile -targetdir:$reportDir -reporttypes:Html 2>&1
$rgExit = $LASTEXITCODE
if ($rgExit -ne 0) {
    Write-Log "reportgenerator failed with exit code $rgExit" "ERROR"
    if ($rgOutput) { Write-Log "reportgenerator output:`n$rgOutput" "ERROR" }
    throw "reportgenerator failed with exit code $rgExit"
}

$index = Join-Path $reportDir 'index.html'
if (Test-Path $index) {
    Write-Log "HTML coverage report generated: $index"
    Write-Log "Opening report..."
    Start-Process $index
} else {
    Write-Log "Report generation appears to have succeeded but 'index.html' was not found." "WARN"
}

Write-Log "Coverage run finished successfully."
