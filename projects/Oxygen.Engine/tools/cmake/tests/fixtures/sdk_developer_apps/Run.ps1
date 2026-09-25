param([Parameter(Mandatory=$true)][ValidateSet('scene-playground', 'async-loading')][string]$App)
$Host.UI.RawUI.WindowTitle = "Oxygen SDK developer app - $App"
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
Set-Location $PSScriptRoot
& (Join-Path $PSScriptRoot "build\$App.exe")
Write-Host "Application exited with code $LASTEXITCODE"
