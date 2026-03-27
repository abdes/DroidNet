[CmdletBinding()]
param(
  [Parameter()]
  [string]$BuildTree = "out/build-ninja",

  [Parameter()]
  [string]$Config = "Debug",

  [Parameter()]
  [ValidateRange(-2, 9)]
  [int]$Verbosity = 0,

  [Parameter()]
  [switch]$StopOnFailure,

  [Parameter()]
  [switch]$ShowFullOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$testPrograms = @(
  "Oxygen.Renderer.VsmAvailablePages.Tests.exe",
  "Oxygen.Renderer.VsmBasic.Tests.exe",
  "Oxygen.Renderer.VsmBeginFrame.Tests.exe",
  "Oxygen.Renderer.VsmHierarchicalFlags.Tests.exe",
  "Oxygen.Renderer.VsmMappedMips.Tests.exe",
  "Oxygen.Renderer.VsmPageInitialization.Tests.exe",
  "Oxygen.Renderer.VsmPageMappings.Tests.exe",
  "Oxygen.Renderer.VsmPageRequests.Tests.exe",
  "Oxygen.Renderer.VsmPageReuse.Tests.exe",
  "Oxygen.Renderer.VsmProjectionRecords.Tests.exe",
  "Oxygen.Renderer.VsmRemap.Tests.exe",
  "Oxygen.Renderer.VsmVirtualAddressSpace.Tests.exe"
)

function Get-FailedTestNames {
  param(
    [Parameter()]
    [AllowNull()]
    [AllowEmptyCollection()]
    [string[]]$Lines
  )

  $failed = New-Object System.Collections.Generic.List[string]
  if ($null -eq $Lines -or $Lines.Count -eq 0) {
    return $failed
  }

  foreach ($line in $Lines) {
    # Match gtest failed test rows like:
    # [  FAILED  ] SuiteName.TestName (12 ms)
    if ($line -match '^\[\s*FAILED\s*\]\s+([^\s]+\.[^\s]+)\s*(\(|$)') {
      $name = $Matches[1]
      if (-not $failed.Contains($name)) {
        [void]$failed.Add($name)
      }
    }
  }

  return $failed
}

$exeRoot = Join-Path -Path $BuildTree -ChildPath (Join-Path -Path "bin" -ChildPath $Config)

Write-Host "Build tree : $BuildTree"
Write-Host "Config     : $Config"
Write-Host "Exe folder : $exeRoot"
Write-Host ""

$results = New-Object System.Collections.Generic.List[object]

foreach ($program in $testPrograms) {
  $exePath = Join-Path -Path $exeRoot -ChildPath $program

  if (-not (Test-Path -LiteralPath $exePath)) {
    Write-Warning "Missing test executable: $exePath"
    $results.Add([PSCustomObject]@{
      Program = $program
      Path = $exePath
      Missing = $true
      ExitCode = 1
      FailedTests = @("<missing executable>")
    })

    if ($StopOnFailure) {
      break
    }

    continue
  }

  Write-Host "==== Running $program ===="

  $arguments = @("-v", $Verbosity.ToString())
  $output = @(& $exePath @arguments 2>&1 | ForEach-Object { $_.ToString() })
  $exitCode = $LASTEXITCODE

  if ($ShowFullOutput) {
    foreach ($line in $output) {
      Write-Host $line
    }
  }

  $failedTests = Get-FailedTestNames -Lines $output

  $results.Add([PSCustomObject]@{
    Program = $program
    Path = $exePath
    Missing = $false
    ExitCode = $exitCode
    FailedTests = @($failedTests)
  })

  if ($exitCode -eq 0) {
    Write-Host "PASS: $program"
  } else {
    Write-Host "FAIL: $program (exit code $exitCode)"
  }

  Write-Host ""

  if ($StopOnFailure -and $exitCode -ne 0) {
    break
  }
}

$failedPrograms = @($results | Where-Object { $_.ExitCode -ne 0 -or $_.Missing })

Write-Host "========== Summary =========="
Write-Host "Programs run : $($results.Count)"
Write-Host "Programs pass: $(@($results | Where-Object { $_.ExitCode -eq 0 -and -not $_.Missing }).Count)"
Write-Host "Programs fail: $($failedPrograms.Count)"
Write-Host ""

if ($failedPrograms.Count -gt 0) {
  Write-Host "Failed Programs and Tests:"
  foreach ($programResult in $failedPrograms) {
    Write-Host "- $($programResult.Program)"

    if ($programResult.Missing) {
      Write-Host "  Missing executable: $($programResult.Path)"
      continue
    }

    if ($programResult.FailedTests.Count -gt 0) {
      foreach ($testName in $programResult.FailedTests) {
        Write-Host "  * $testName"
      }
    } else {
      Write-Host "  * <no per-test failure lines parsed; inspect test output>"
    }

    Write-Host "  ExitCode: $($programResult.ExitCode)"
  }

  exit 1
}

Write-Host "All VSM test programs passed."
exit 0
