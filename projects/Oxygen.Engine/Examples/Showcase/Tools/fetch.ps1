#requires -Version 7.4
<#
.SYNOPSIS
Downloads and checks the pinned showcase media.

.DESCRIPTION
Reads Content/SourceMedia/showcase-assets.json and makes every file it lists
match its recorded size and SHA-256 at its path: a file that is missing or
differs is downloaded from its URL, or taken from the archive that holds it.
An archive is downloaded to a temporary folder and never kept. A file whose
archive has no URL cannot be fetched and is reported. Credits are in
Content/SourceMedia/ASSET_CREDITS.md.

.PARAMETER Check
Only checks the files; downloads nothing.

.EXAMPLE
Tools/fetch

.EXAMPLE
Tools/fetch -Check
#>

[CmdletBinding()]
param(
    [switch]$Check,
    [Alias('h')][switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# The inventory's paths are mount paths in the Showcase mount; Project.oxy
# names its folder.
$project = Get-Content -LiteralPath (Join-Path $root 'Project.oxy') -Raw | ConvertFrom-Json
$mount = @($project.Mounts | Where-Object Name -ceq 'Showcase')
if ($mount.Count -ne 1) { throw 'Project.oxy has no Showcase mount.' }
$mountRoot = [IO.Path]::GetFullPath((Join-Path $root $mount[0].Path))
$inventoryPath = Join-Path $mountRoot 'SourceMedia/showcase-assets.json'
$json = Get-Content -LiteralPath $inventoryPath -Raw
if (-not (Test-Json -Json $json -SchemaFile (Join-Path $PSScriptRoot 'showcase-assets.schema.json'))) {
    throw 'showcase-assets.json does not match Tools/showcase-assets.schema.json.'
}
$assets = @(($json | ConvertFrom-Json).assets)
$byName = @{}
foreach ($asset in $assets) { $byName[$asset.name] = $asset }

function Test-Pinned([string]$File, $Asset) {
    if (-not (Test-Path -LiteralPath $File -PathType Leaf)) { return $false }
    return (Get-Item -LiteralPath $File).Length -eq $Asset.bytes `
        -and (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Asset.sha256
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) "oxygen-fetch-$PID"
$archives = @{}
$failed = [Collections.Generic.List[string]]::new()
try {
    foreach ($asset in @($assets | Where-Object { $_.PSObject.Properties['path'] })) {
        $file = Join-Path $mountRoot ($asset.path.Substring('/Showcase/'.Length))
        if (Test-Pinned $file $asset) { continue }
        if ($Check) { $failed.Add("$($asset.path) is missing or differs"); continue }
        $parent = Split-Path -Parent $file
        $null = New-Item -ItemType Directory -Path $parent -Force
        if ($asset.PSObject.Properties['url']) {
            Write-Host "Downloading $($asset.name)..."
            Invoke-WebRequest -Uri $asset.url -OutFile $file
        } else {
            $archive = $byName[$asset.archive]
            if (-not $archive -or -not $archive.PSObject.Properties['url']) {
                $failed.Add("$($asset.path): its archive $($asset.archive) has no URL; obtain it by hand (see ASSET_CREDITS.md)")
                continue
            }
            if (-not $archives.ContainsKey($archive.name)) {
                $null = New-Item -ItemType Directory -Path $temporary -Force
                $zip = Join-Path $temporary $archive.name
                Write-Host "Downloading $($archive.name)..."
                Invoke-WebRequest -Uri $archive.url -OutFile $zip
                if (-not (Test-Pinned $zip $archive)) { throw "$($archive.name) does not match its recorded size and SHA-256." }
                $extracted = Join-Path $temporary ([IO.Path]::GetFileNameWithoutExtension($archive.name))
                Expand-Archive -LiteralPath $zip -DestinationPath $extracted -Force
                $archives[$archive.name] = $extracted
            }
            $member = Get-ChildItem -LiteralPath $archives[$archive.name] -Recurse -File -Filter $asset.name | Select-Object -First 1
            if (-not $member) { throw "$($archive.name) holds no $($asset.name)." }
            Copy-Item -LiteralPath $member.FullName -Destination $file -Force
        }
        if (-not (Test-Pinned $file $asset)) { $failed.Add("$($asset.path) does not match its recorded size and SHA-256") }
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
foreach ($problem in $failed) { Write-Host $problem -ForegroundColor Red }
if ($failed.Count -gt 0) { exit 1 }
Write-Host 'Every showcase file matches its pin.' -ForegroundColor Green
