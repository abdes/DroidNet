param([ValidateSet('InstancingTestScene', 'NewSponza_Main_glTF_003')][string]$Scene,
    [switch]$Tracy)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../../..").Path
$mode = if ($Tracy) { 'tracy' } else { 'native' }
$output = Join-Path $PSScriptRoot "scenes/$Scene-$mode"
if (Test-Path -LiteralPath $output) { throw "Preserve existing capture: $output" }
New-Item -ItemType Directory -Force $output | Out-Null
$tree = if ($Tracy) { 'build-tracy-ninja' } else { 'build-ninja' }
$cooked = if ($Scene -eq 'InstancingTestScene') { "$root/Examples/Content/.cooked" } else { "$root/Examples/RenderScene/.cooked" }
$saved = @{}
foreach ($relative in @('Examples/RenderScene/demo_settings.json', 'imgui.ini')) {
    $path = Join-Path $root $relative
    $saved[$path] = [IO.File]::ReadAllBytes($path)
    [IO.File]::WriteAllBytes((Join-Path $output ([IO.Path]::GetFileName($path))), $saved[$path])
}
try {
    . "$root/tools/cli/BuildSelection.ps1"
    $selection = Resolve-OxygenBuildSelection -BuildTree $tree -Config Release -RequiredExecutables @('Oxygen.Examples.RenderScene.exe')
    foreach ($entry in $selection.RuntimeEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    $exe = Join-Path $selection.BuildRoot 'bin/Release/Oxygen.Examples.RenderScene.exe'
    $inputs = @($exe, "$root/bin/Oxygen/Release/production/shaders.bin", "$cooked/container.index.bin", "$cooked/Scenes/$Scene.oscene", "$root/Examples/RenderScene/demo_settings.json", "$root/imgui.ini")
    $inputs += (Get-ChildItem (Split-Path $exe) -Filter 'Oxygen.*.dll').FullName
    $hashes = foreach ($path in $inputs) { $h=Get-FileHash -LiteralPath $path; @{ path=$path; sha256=$h.Hash.ToLowerInvariant() } }
    @{ git_head=(git -C $root rev-parse HEAD); build=$selection.BuildPreset; tracy=[bool]$Tracy; inputs=$hashes; runtime_environment=$selection.RuntimeEnvironment } | ConvertTo-Json -Depth 8 | Set-Content "$output/identity.json"
    $verbosity = if ($Tracy) { '-v=-1' } else { '-v=0' }
    $arguments = @($verbosity, '--frames', '0', '--fps', '0', '--vsync', 'false', '--resolution', '2560x1400', '--scene', $Scene, '--directional-shadows', 'conventional', '--debug-layer', 'false')
    $idle = $false
    for ($attempt=0; $attempt -lt 60; ++$attempt) {
        $preflight = "$output/load-preflight-$attempt.json"
        & pwsh -NoProfile -File "$root/tools/vortex/CheckBenchmarkLoad.ps1" -OutputPath $preflight
        if ($LASTEXITCODE -eq 0) {
            Copy-Item -LiteralPath $preflight -Destination "$output/load-preflight.json"
            $idle = $true
            break
        }
        if ($LASTEXITCODE -ne 2) { throw 'Load counters unavailable; refusing timing' }
        Write-Output "Machine busy; waiting to capture $Scene ($mode)"
        Start-Sleep -Seconds 10
    }
    if (!$idle) { throw 'Machine remained busy; no scene baseline captured' }
    if ($Tracy) {
        $capture = Start-Process -FilePath "$root/out/analysis/ex07d/tracy-013/tracy-capture.exe" -ArgumentList @('-a', '127.0.0.1', '-o', "$output/native.tracy", '-s', '55') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$output/tracy.stdout.log" -RedirectStandardError "$output/tracy.stderr.log"
    }
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput "$output/stdout.log" -RedirectStandardError "$output/stderr.log"
    @{ pid=$process.Id; executable=$exe; arguments=$arguments; started=(Get-Date).ToString('o'); measured_window_seconds=@(20,50) } | ConvertTo-Json | Set-Content "$output/run.json"
    Write-Output "Capturing $Scene ($mode), PID $($process.Id)"
    if ($Tracy) {
        $capture.WaitForExit()
        if ($capture.ExitCode -ne 0) { throw "Tracy failed: $($capture.ExitCode)" }
    } else {
        if ($process.WaitForExit(55000)) { throw 'Application exited before the measurement finished' }
    }
    $process.Refresh()
    @{ working_set_bytes=$process.WorkingSet64; private_bytes=$process.PrivateMemorySize64; cpu_time_seconds=$process.TotalProcessorTime.TotalSeconds; scope='Process CPU memory at capture end; not renderer GPU allocation' } | ConvertTo-Json | Set-Content "$output/process.json"
    & nvidia-smi --query-gpu=name,driver_version,pstate,temperature.gpu,power.draw,clocks.current.graphics,clocks.current.memory,utilization.gpu,memory.used --format=csv > "$output/hardware-end.csv"
    # Capture after the timed window; the screenshot tool foregrounds the app.
    & pwsh -NoProfile -File "$root/out/analysis/light-shadow-audit-20260924/CaptureWindow.ps1" -ProcessId $process.Id -OutputPath "$output/scene.png"
    if (!$process.HasExited) { $process.CloseMainWindow() | Out-Null }
    if (!$process.WaitForExit(30000)) { throw 'Application did not finish its window-close shutdown' }
    $process.ExitCode | Set-Content "$output/exit-code.txt"
    if ($process.ExitCode -ne 0) { throw "Application failed: $($process.ExitCode)" }
    Copy-Item -LiteralPath "$root/Examples/RenderScene/demo_settings.json" -Destination "$output/demo_settings.final.json"
    # --scene selects the runtime scene without replacing the persisted library
    # selection. Confirm the runtime workload from capture/log evidence instead.
    foreach ($h in $hashes) {
        if ([IO.Path]::GetFileName($h.path) -in @('demo_settings.json', 'imgui.ini')) { continue }
        if ((Get-FileHash -LiteralPath $h.path).Hash.ToLowerInvariant() -ne $h.sha256) { throw "Input changed during capture: $($h.path)" }
    }
    Write-Output "Completed $Scene ($mode)"
} finally {
    if ($process -and !$process.HasExited) { $process.CloseMainWindow() | Out-Null; $process.WaitForExit(30000) | Out-Null }
    foreach ($path in $saved.Keys) { [IO.File]::WriteAllBytes($path, $saved[$path]) }
}
