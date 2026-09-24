param(
  [string]$BuildTree,

  [string]$Pattern = "*.Tests.exe",

  [Alias('h')][switch]$Help,

  [string]$Config,

  [string]$Preset
)

if ($Help) { Get-Help $PSCommandPath -Detailed; return }

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'cli/BuildSelection.ps1')
$selection = Resolve-OxygenBuildSelection -BuildTree $BuildTree -Config $Config -Preset $Preset -RequiredExecutables $Pattern
Write-OxygenBuildSelection $selection
$buildRoot = $selection.BuildRoot
$binaryRoot = Join-Path $buildRoot "bin/$($selection.Config)"
$tests = @(Get-ChildItem -LiteralPath $binaryRoot -File -Filter $Pattern | Sort-Object FullName)

if ($tests.Count -eq 0) {
  Write-Host "No test executables found under $buildRoot"
  exit 1
}

$overallExitCode = 0

foreach ($testExe in $tests) {
  $relative = [System.IO.Path]::GetRelativePath($buildRoot, $testExe.FullName)
  $output = Invoke-OxygenSelectedExecutable $selection $testExe.FullName @('--gtest_color=no', '--gtest_brief=1') 2>&1
  $exitCode = $LASTEXITCODE

  if ($exitCode -eq 0) {
    Write-Host ("PASS {0}" -f $relative)
    continue
  }

  $failedTests = [System.Collections.Generic.List[string]]::new()
  foreach ($line in $output) {
    $text = [string]$line
    $match = [regex]::Match($text, '^\[\s*FAILED\s*\]\s+(.+?)\s*$')
    if (-not $match.Success) {
      continue
    }

    $candidate = $match.Groups[1].Value.Trim()
    $candidate = [regex]::Replace($candidate, '\s+\(\d+\s+ms\)$', '')

    if ($candidate -match '^\d+\s+tests?,\s+listed\s+below:?$') {
      continue
    }
    if ($candidate -match '^\d+\s+FAILED\s+TESTS?$') {
      continue
    }
    if ($candidate -notmatch '\.') {
      continue
    }
    if (-not $failedTests.Contains($candidate)) {
      $failedTests.Add($candidate)
    }
  }

  if ($failedTests.Count -gt 0) {
    Write-Host ("FAIL {0} :: {1}" -f $relative, ($failedTests -join ", "))
  } else {
    Write-Host ("FAIL {0} :: exit {1}" -f $relative, $exitCode)
  }
  $overallExitCode = 1
}

exit $overallExitCode
