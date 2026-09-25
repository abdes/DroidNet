# Run the installed showcase without changing PATH or requiring a developer shell.
$ErrorActionPreference = 'Stop'
if ($args.Count -eq 1 -and $args[0] -in @('-Help', '-h')) {
    Write-Output @'
Usage: RenderScene.ps1 [showcase arguments]
       RenderScene.ps1 -Help | -h

Run this SDK's RenderScene without changing PATH or requiring a developer shell.
Arguments are forwarded to the application; use --help for application options.
Examples:
  .\RenderScene.ps1
  .\RenderScene.ps1 --scene SdkMaterials
'@
    return
}
$Executable = Join-Path $PSScriptRoot 'bin/Oxygen.Examples.RenderScene.exe'
$WorkingDirectory = Join-Path $PSScriptRoot 'share/oxygen/RenderScene'
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "RenderScene is missing from this SDK: $Executable"
}
Push-Location -LiteralPath $WorkingDirectory
try {
    & $Executable @args
    $Result = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $Result
