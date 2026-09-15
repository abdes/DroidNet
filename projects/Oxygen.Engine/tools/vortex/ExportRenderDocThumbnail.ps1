#requires -Version 7.4
<#
.SYNOPSIS
Extracts a RenderDoc capture's embedded image as PNG, without replay or a UI.
.DESCRIPTION
Runs the supported renderdoccmd thumb command in a background console process.
Checks exit status, fresh PNG structure/dimensions, and SHA256, then writes a
JSON report identifying the result as an embedded capture thumbnail, not replay.
MaxSize 0 preserves the largest embedded image available; it cannot restore
resolution or detail discarded when the capture was created.
.PARAMETER CapturePath
Existing .rdc capture.
.PARAMETER OutputPath
PNG destination. Replaced only after a fresh export passes validation.
.PARAMETER ReportPath
JSON evidence path; defaults to <OutputPath>.report.json. Wrapper diagnostics
are logged as <ReportPath>.log; the CLI's short console status is inherited.
.PARAMETER RenderDocCmdPath
Native CLI executable; defaults to C:/Program Files/RenderDoc/renderdoccmd.exe.
.PARAMETER MaxSize
Optional maximum image dimension; default 0 applies no additional size limit.
.PARAMETER TimeoutSeconds
Maximum CLI duration; default 30 seconds. A timeout is a failure.
Termination grace is at most five seconds. No output pipes/readers are created.
.EXAMPLE
./tools/vortex/ExportRenderDocThumbnail.ps1 -CapturePath ./out/rgb.rdc -OutputPath ./out/rgb.png
.NOTES
Exits nonzero on missing inputs, native failure, absent/malformed PNG, or timeout.
No Qt, PySide, Python, replay controller, desktop capture, or input automation.
Upstream thumb may return zero even when it cannot export; output validation is
therefore required in addition to exit-code validation.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CapturePath,
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$ReportPath,
    [string]$RenderDocCmdPath = 'C:/Program Files/RenderDoc/renderdoccmd.exe',
    [ValidateRange(0, 4294967295)][uint32]$MaxSize = 0,
    [ValidateRange(1, 300)][int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$process = $null
$started = $false
$terminationAttempted = $false
$temporary = $null
$writeReport = $false
$result = [ordered]@{
    status = 'failed'
    image_kind = 'embedded_capture_thumbnail'
    replay_performed = $false
    additional_max_size = $MaxSize
    resolution_note = 'Dimensions describe the embedded capture image, which may be smaller than the native viewport.'
}

try {
    $CapturePath = [IO.Path]::GetFullPath($CapturePath)
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)
    $RenderDocCmdPath = [IO.Path]::GetFullPath($RenderDocCmdPath)
    if (-not $ReportPath) { $ReportPath = "$OutputPath.report.json" }
    $ReportPath = [IO.Path]::GetFullPath($ReportPath)
    $logPath = "$ReportPath.log"
    if ([IO.Path]::GetExtension($OutputPath) -ne '.png') { throw 'OutputPath must end in .png.' }
    $paths = @($CapturePath, $OutputPath, $ReportPath, $logPath, $RenderDocCmdPath)
    if (@($paths | Sort-Object -Unique).Count -ne $paths.Count) { throw 'Input, output, report, log, and executable paths must be distinct.' }
    foreach ($file in @($CapturePath, $RenderDocCmdPath)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file missing: $file" }
    }
    foreach ($directory in @([IO.Path]::GetDirectoryName($OutputPath), [IO.Path]::GetDirectoryName($ReportPath))) {
        $null = New-Item -ItemType Directory -Path $directory -Force
    }
    $writeReport = $true
    $result.capture = $CapturePath
    $result.capture_sha256 = (Get-FileHash -LiteralPath $CapturePath -Algorithm SHA256).Hash
    $result.png_path = $OutputPath
    $result.tool = $RenderDocCmdPath
    $result.tool_version = (Get-Item -LiteralPath $RenderDocCmdPath).VersionInfo.FileVersion
    $temporary = Join-Path ([IO.Path]::GetDirectoryName($OutputPath)) ([IO.Path]::GetFileNameWithoutExtension($OutputPath) + '.' + [Guid]::NewGuid().ToString('N') + '.tmp.png')
    $arguments = @('thumb', "--out=$temporary", '--format=png', "--max-size=$MaxSize", $CapturePath)
    $result.arguments = $arguments
    $startInfo = [Diagnostics.ProcessStartInfo]::new($RenderDocCmdPath)
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($argument in $arguments) { $startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $started = $process.Start()
    if (-not $started) { throw 'Could not start renderdoccmd.' }
    $completed = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $completed) {
        $terminationAttempted = $true
        try { $process.Kill() } catch { $result.kill_error = $_.Exception.Message }
        $result.exited_after_kill = $process.WaitForExit(5000)
    }
    if ($process.HasExited) { $result.exit_code = $process.ExitCode }
    if (-not $completed) { throw "renderdoccmd timed out after $TimeoutSeconds seconds. See $logPath" }
    if ($process.ExitCode -ne 0) { throw "renderdoccmd failed with exit code $($process.ExitCode). See $logPath" }
    if (-not (Test-Path -LiteralPath $temporary -PathType Leaf)) { throw "Native export produced no PNG. See $logPath" }
    $bytes = [IO.File]::ReadAllBytes($temporary)
    if ($bytes.Length -lt 45 -or [Convert]::ToHexString($bytes[0..7]) -ne '89504E470D0A1A0A' -or
        [Text.Encoding]::ASCII.GetString($bytes, 12, 4) -ne 'IHDR' -or
        [Convert]::ToHexString($bytes[($bytes.Length - 12)..($bytes.Length - 1)]) -ne '0000000049454E44AE426082') {
        throw 'Native export is not a complete PNG.'
    }
    $width = [uint32]($bytes[16] * 16777216L + $bytes[17] * 65536L + $bytes[18] * 256L + $bytes[19])
    $height = [uint32]($bytes[20] * 16777216L + $bytes[21] * 65536L + $bytes[22] * 256L + $bytes[23])
    if ($width -eq 0 -or $height -eq 0 -or ($MaxSize -gt 0 -and [Math]::Max($width, $height) -gt $MaxSize)) {
        throw 'Exported dimensions are invalid or exceed MaxSize.'
    }
    $result.width = $width
    $result.height = $height
    $result.png_bytes = $bytes.Length
    $result.png_sha256 = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash
    [IO.File]::Move($temporary, $OutputPath, $true)
    $result.status = 'exported'
    Write-Host "Exported embedded capture image ${width}x${height}: $OutputPath"
    Write-Host "Report: $ReportPath"
} catch {
    $result.error = $_.Exception.Message
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
} finally {
    if ($process) {
        if ($started -and -not $process.HasExited -and -not $terminationAttempted) {
            $terminationAttempted = $true
            try { $process.Kill() } catch { $result.cleanup_kill_error = $_.Exception.Message }
            $result.cleanup_process_exited = $process.WaitForExit(5000)
        }
        $process.Dispose()
    }
    if ($temporary -and (Test-Path -LiteralPath $temporary)) { Remove-Item -LiteralPath $temporary }
    if ($writeReport) {
        $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReportPath -Encoding utf8NoBOM
        @("capture=$CapturePath", "output=$OutputPath", "status=$($result.status)",
            "timeout_seconds=$TimeoutSeconds", "details=$ReportPath") |
            Set-Content -LiteralPath $logPath -Encoding utf8NoBOM
    }
}
