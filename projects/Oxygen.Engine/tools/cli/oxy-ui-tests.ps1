<#
.SYNOPSIS
Run the real demo widget suite in an explicitly instrumented existing build.
#>
param(
    [ValidateSet('LightBench', 'TexturedCube')][string]$Demo = 'LightBench',
    [string]$BuildTree = 'build-tracy-ninja',
    [ValidateSet('Debug', 'Release')][string]$Config = 'Release',
    [string]$Filter = '',
    [string]$Output,
    [ValidateRange(10, 600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/BuildSelection.ps1"
$selection = Resolve-OxygenBuildSelection -BuildTree $BuildTree -Config $Config
$cache = Get-Content -LiteralPath (Join-Path $selection.BuildRoot 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^OXYGEN_BUILD_UI_TESTS:[^=]+=(ON|TRUE|1)\r?$') {
    throw 'This tree is not configured with OXYGEN_BUILD_UI_TESTS. Install Conan with -o ui_tests=True and configure the same tree.'
}
$exe = Join-Path $selection.BuildRoot "bin/$Config/Oxygen.Examples.$Demo.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build $exe first." }
if (-not $Output) {
    $Output = Join-Path (Get-OxygenSourceRoot) "out/ui-tests/$(Get-Date -Format yyyyMMdd-HHmmss)-$Demo"
}
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw "Use a new output directory to preserve evidence: $Output" }
[IO.Directory]::CreateDirectory($Output) | Out-Null
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = 'Hidden'
$start.WorkingDirectory = $Output
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($entry in $selection.RuntimeEnvironment.GetEnumerator()) {
    $start.Environment[$entry.Key] = [string]$entry.Value
}
$start.Environment['OXYGEN_UI_TEST_OUTPUT'] = $Output
$start.Environment['OXYGEN_UI_TEST_FILTER'] = $Filter
foreach ($argument in @('--resolution', '1920x1080', '--fps', '60', '-v=INFO')) {
    $start.ArgumentList.Add($argument)
}
$startedUtc = [DateTime]::UtcNow.ToString('O')
$process = [Diagnostics.Process]::Start($start)
try {
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
while (-not $process.WaitForExit(1000) -and [DateTime]::UtcNow -lt $deadline) { }
$timedOut = -not $process.HasExited
if ($timedOut) {
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(10000)) { $process.Kill($true); $process.WaitForExit() }
}
[IO.File]::WriteAllText((Join-Path $Output 'stdout.log'), $stdout.GetAwaiter().GetResult())
[IO.File]::WriteAllText((Join-Path $Output 'stderr.log'), $stderr.GetAwaiter().GetResult())
$record = @{
    executable = $exe; sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    buildTree = $selection.BuildRoot; configuration = $Config; filter = $Filter
    processId = $process.Id; exitCode = $process.ExitCode; timedOut = $timedOut
    startedUtc = $startedUtc; finishedUtc = [DateTime]::UtcNow.ToString('O')
}
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'run.json') -Encoding utf8
if ($timedOut -or $process.ExitCode -ne 0) { throw "UI test process failed; see $Output" }
[xml]$report = Get-Content -LiteralPath (Join-Path $Output 'results.xml') -Raw
$cases = $report.SelectNodes('//testcase[not(skipped)]')
if ($cases.Count -eq 0 -or $report.SelectNodes('//failure|//error').Count -ne 0 -or
    (-not $Filter -and $report.SelectNodes('//skipped').Count -ne 0)) {
    throw "UI test report is empty or unsuccessful; see $Output"
}
Write-Output "$($cases.Count) UI tests passed. Results: $Output"
} finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(10000)) { $process.Kill($true); $process.WaitForExit() }
    }
    $process.Dispose()
}
