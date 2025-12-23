param(
  [string]$PackageId = 'sharpgltf.core'
)

$ErrorActionPreference = 'Stop'

$pkgRoot = Join-Path $env:USERPROFILE '.nuget\packages'
$asmPath = Get-ChildItem -Path $pkgRoot -Recurse -Filter 'SharpGLTF.Core.dll' |
  Where-Object { $_.FullName -match [regex]::Escape($PackageId) } |
  Select-Object -First 1 |
  ForEach-Object { $_.FullName }

if ([string]::IsNullOrWhiteSpace($asmPath)) {
  throw "SharpGLTF.Core.dll not found under $pkgRoot for package id filter '$PackageId'."
}

Write-Host "Using assembly: $asmPath"

$asm = [System.Reflection.Assembly]::LoadFrom($asmPath)
$t = $asm.GetType('SharpGLTF.Schema2.ModelRoot', $true)

Write-Host "ModelRoot found: $($t.FullName)"

$flags = [System.Reflection.BindingFlags]::Public -bor [System.Reflection.BindingFlags]::Static
$methods = $t.GetMethods($flags) |
  Where-Object { $_.Name -match 'GLTF|GLB|Read|Load|Parse|Open' } |
  Sort-Object Name

$methods | ForEach-Object { $_.ToString() }
