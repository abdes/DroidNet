param(
  [string]$BuildTree = "out/build-vs",
  [string]$Pattern = "*.Tests.exe"
)

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildTree)) {
  $BuildTree
} else {
  Join-Path $repoRoot $BuildTree
}

if (-not (Test-Path $buildRoot)) {
  Write-Error "Build tree not found: $buildRoot"
  exit 1
}

$tests = Get-ChildItem -Path $buildRoot -Recurse -File -Filter $Pattern `
  -ErrorAction SilentlyContinue `
  | Sort-Object FullName

if ($tests.Count -eq 0) {
  Write-Host "No test executables found under $buildRoot"
  exit 1
}

$overallExitCode = 0

foreach ($testExe in $tests) {
  $relative = [System.IO.Path]::GetRelativePath($buildRoot, $testExe.FullName)
  $output = & $testExe.FullName --gtest_color=no --gtest_brief=1 2>&1
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
