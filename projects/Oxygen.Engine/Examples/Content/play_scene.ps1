#requires -Version 7.4
<#
.SYNOPSIS
Plays a cooked example scene in Oxygen.Player.

.DESCRIPTION
Finds the scene among the cooked content under .cooked (the main root and
every imported model root), writes a launch file that mounts it, and runs
Oxygen.Player with that launch file. Arguments after `--` go to the player
unchanged, for example `-- -w 1280x720 -r 60 -c vtx.occlusion.enable=true`;
run with `-- --help` to list them.

The launch file mounts .cooked/main and, for an imported scene, its import
root last so it wins; it uses this folder as the script source root, so
editing a scene's script reloads it while playing.

In an installed SDK, it uses the player in that SDK's bin directory. In a
source checkout, it selects a configured build tree from CMake presets,
preferring Release, ordinary builds, then Ninja, and builds the player there
first unless -NoBuild is given. No reconfiguration is run.

Usage: play_scene.ps1 <scene> [options] [-- <player options>]
       play_scene.ps1 -List

  <scene>              Cooked scene name (camera_switch,
                       NewSponza_Main_glTF_003; case-insensitive) or its
                       source folder under scenes/ (camera-switch).
  -List                List the cooked scenes and exit.
  -NoBuild             Source checkouts only: run the selected player without
                       building it first.
  -Preset <name>       Source checkouts only: exact CMake build preset.
  -BuildTree <tree>    Source checkouts only: build tree name or path.
  -Config <config>     Source checkouts only: required configuration.
  -PlayerPath <exe>    Explicit player executable.
  -ContentRoot <dir>   Folder holding .cooked and scenes/; defaults to this
                       script's folder.
  -Help, -h            Show this help.

.EXAMPLE
.\play_scene.ps1 camera_switch

.EXAMPLE
.\play_scene.ps1 camera-switch -Config Debug -- -w 1600x900 -s false -r 0

.EXAMPLE
.\play_scene.ps1 NewSponza_Main_glTF_003 -NoBuild -- -c vtx.occlusion.enable=true

.EXAMPLE
.\play_scene.ps1 -List
#>

# Parsed by hand: `pwsh -File` would bind the player's options (`-c`, `-l`)
# and `--` to this script's parameters.
$Scene = $null
$List = $false
$NoBuild = $false
$Preset = $null
$BuildTree = $null
$Config = $null
$PlayerPath = $null
$ContentRoot = $null
$PlayerArgs = @()
$valueOptions = @('-Preset', '-BuildTree', '-Config', '-PlayerPath', '-ContentRoot')
for ($i = 0; $i -lt $args.Count; ++$i) {
    $arg = [string]$args[$i]
    if ($arg -eq '--') {
        $PlayerArgs = @($args[($i + 1)..($args.Count)] | Where-Object { $null -ne $_ })
        break
    }
    $option = $valueOptions | Where-Object { $_ -ieq $arg } | Select-Object -First 1
    if ($option) {
        if ($i + 1 -ge $args.Count) { Write-Error "$option needs a value."; exit 2 }
        Set-Variable -Name $option.Substring(1) -Value ([string]$args[++$i])
    } elseif ($arg -ieq '-List') { $List = $true }
    elseif ($arg -ieq '-NoBuild') { $NoBuild = $true }
    elseif ($arg -ieq '-Help' -or $arg -ieq '-h' -or $arg -eq '-?') {
        # The help block above; Get-Help cannot read it without a param block.
        $text = Get-Content -LiteralPath $PSCommandPath -Raw
        $text.Substring($text.IndexOf('<#') + 2, $text.IndexOf('#>') - $text.IndexOf('<#') - 2).Trim() |
            Write-Host
        return
    } elseif ($arg.StartsWith('-')) {
        Write-Error "Unknown option '$arg'; player options go after '--'. Run with -Help."
        exit 2
    } elseif (-not $Scene) { $Scene = $arg }
    else { Write-Error "Unexpected argument '$arg'; player options go after '--'."; exit 2 }
}
if (-not $List -and -not $Scene) {
    Write-Error 'Name the scene to play, or use -List. Run with -Help for usage.'
    exit 2
}

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ContentRoot)) { $ContentRoot = $PSScriptRoot }
$ContentDir = [IO.Path]::GetFullPath($ContentRoot)
$CookedDir = Join-Path $ContentDir '.cooked'
if (-not (Test-Path -LiteralPath $CookedDir -PathType Container)) {
    Write-Error "No cooked content in $CookedDir; cook the scenes first (cook_scenes.ps1 -All)."
    exit 1
}

# Every cooked root: .cooked/main and .cooked/imports/<source>/<import>.
function Get-CookedScenes([string]$CookedDir) {
    $roots = @(Get-ChildItem -LiteralPath $CookedDir -Recurse -Filter 'container.index.bin' -File |
        ForEach-Object { $_.Directory })
    foreach ($root in $roots) {
        $scenesDir = Join-Path $root.FullName 'Scenes'
        if (-not (Test-Path -LiteralPath $scenesDir -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $scenesDir -Filter '*.oscene' -File) {
            [pscustomobject]@{
                Name        = $file.BaseName
                Root        = $root.FullName
                VirtualPath = "/.cooked/Scenes/$($file.Name)"
            }
        }
    }
}

$scenes = @(Get-CookedScenes $CookedDir | Sort-Object Name)

if ($List) {
    $mainRoot = Join-Path $CookedDir 'main'
    foreach ($entry in $scenes) {
        $origin = if ($entry.Root -eq $mainRoot) { 'main' }
        else { [IO.Path]::GetRelativePath($CookedDir, $entry.Root) }
        Write-Host ('{0,-40} {1}' -f $entry.Name, $origin)
    }
    exit 0
}

# A source folder name maps to the scene its manifest cooks.
$sceneName = $Scene
$manifest = Join-Path $ContentDir "scenes/$Scene/import-manifest.json"
if (Test-Path -LiteralPath $manifest -PathType Leaf) {
    $job = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).jobs |
        Where-Object { $_.type -eq 'scene-descriptor' } | Select-Object -First 1
    if ($job) { $sceneName = $job.name }
}

$found = @($scenes | Where-Object { $_.Name -ieq $sceneName })
if ($found.Count -eq 0) {
    Write-Error "No cooked scene named '$Scene'. Run with -List to see the cooked scenes."
    exit 1
}
if ($found.Count -gt 1) {
    Write-Error "Several cooked roots have a scene named '$Scene':`n  $($found.Root -join "`n  ")"
    exit 1
}
$target = $found[0]

$mounts = @()
$mainRoot = Join-Path $CookedDir 'main'
if ((Test-Path -LiteralPath $mainRoot -PathType Container) -and $target.Root -ne $mainRoot) {
    $mounts += $mainRoot
}
$mounts += $target.Root

$launch = [ordered]@{
    version             = 1
    mounts              = $mounts
    scene               = $target.VirtualPath
    script_source_roots = @($ContentDir)
    window              = [ordered]@{ title = "Oxygen Player - $($target.Name)" }
}
$launchDir = Join-Path ([IO.Path]::GetTempPath()) 'oxygen-player'
New-Item -ItemType Directory -Force -Path $launchDir | Out-Null
$launchFile = Join-Path $launchDir "$($target.Name).launch.json"
$launch | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $launchFile -Encoding utf8NoBOM
Write-Host "Launch file: $launchFile"
Write-Host "Scene: $($target.VirtualPath)"
Write-Host "Mounts: $($mounts -join ', ')"

$SdkSupport = Join-Path $PSScriptRoot '../Tools/BuildSelection.ps1'
if (Test-Path -LiteralPath $SdkSupport) {
    . $SdkSupport
    $RepoRoot = Get-OxygenSourceRoot
} else {
    $RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    . (Join-Path $RepoRoot 'tools/cli/BuildSelection.ps1')
}
# An explicit player, an installed SDK or -NoBuild uses the player as is; a
# source checkout otherwise builds it so a stale player never runs.
$installedSdk = Test-Path -LiteralPath $SdkSupport
if ($PlayerPath -or $installedSdk -or $NoBuild) {
    $tools = Resolve-OxygenExecutables -SourceRoot $RepoRoot -Targets 'oxygen-player' -BuildTree $BuildTree -Config $Config -Preset $Preset -Overrides @{ 'oxygen-player' = $PlayerPath }
} else {
    $tools = Build-OxygenExecutables -SourceRoot $RepoRoot -Targets 'oxygen-player' -BuildTree $BuildTree -Config $Config -Preset $Preset
}
Write-OxygenExecutableSelection $tools

$arguments = @('--launch', $launchFile) + @($PlayerArgs | Where-Object { $_ -ne '--' })
Invoke-OxygenTool -Context $tools -Target 'oxygen-player' -Arguments $arguments
exit $LASTEXITCODE
