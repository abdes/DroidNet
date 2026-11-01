#requires -Version 7.4
<#
.SYNOPSIS
Plays a scene of an example in Oxygen.Player.

.DESCRIPTION
Writes a launch file that mounts the cooked examples (RenderScene's
.cooked/Showcase) and binds the examples project's source mounts, so editing a scene's script reloads it
while playing, then runs Oxygen.Player with it. The scene is
/Showcase/Scenes/<Example>/<Scene>.oscene; cook the example first (Tools/cook).

Player options follow the example and the scene, after `--` or directly (a
PowerShell prompt consumes `--` itself): the first argument that is not a
script option and everything after it go to the player unchanged, for example
`-- -w 1280x720 -r 60 -c vtx.occlusion.enable=true`, or `-- --control` to accept
JSON-RPC commands on loopback; run with `-- --help` to list them.

In an installed SDK, it uses the player in that SDK. In a source checkout, it
selects a configured build tree from CMake presets, preferring Release,
ordinary builds, then Ninja, and builds the player there first unless -NoBuild
is given. No reconfiguration is run.

Usage: Tools/play <Example> [<Scene>] [options] [-- <player options>]
       Tools/play -List

  <Example>            An example of Examples.json (BottleOnBox;
                       case-insensitive).
  <Scene>              One of its scenes; the first by default.
  -List                List the examples and their scenes, and exit.
  -NoBuild             Source checkouts only: run the selected player without
                       building it first.
  -Preset <name>       Source checkouts only: exact CMake build preset.
  -BuildTree <tree>    Source checkouts only: build tree name or path.
  -Config <config>     Source checkouts only: required configuration.
  -PlayerPath <exe>    Explicit player executable.
  -Help, -h            Show this help.

.EXAMPLE
Tools/play CameraSwitch

.EXAMPLE
Tools/play VsmBug1Flat VsmBug1LargeThinCube -Config Debug -- -w 1600x900

.EXAMPLE
Tools/play -List
#>

# Parsed by hand: `pwsh -File` would bind the player's options (`-c`, `-l`)
# and `--` to this script's parameters.
$Example = $null
$Scene = $null
$List = $false
$NoBuild = $false
$Preset = $null
$BuildTree = $null
$Config = $null
$PlayerPath = $null
$PlayerArgs = @()
$valueOptions = @('-Preset', '-BuildTree', '-Config', '-PlayerPath')
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
    } elseif ($arg.StartsWith('-') -and -not $Example) {
        Write-Error "Unknown option '$arg'; name the example before player options. Run with -Help."
        exit 2
    } elseif (-not $Example) { $Example = $arg }
    elseif (-not $Scene -and -not $arg.StartsWith('-')) { $Scene = $arg }
    else {
        # A PowerShell prompt consumes `--` before the script sees it, so the
        # first argument after the example and scene that is not a script
        # option starts the player options.
        $PlayerArgs = @($args[$i..($args.Count - 1)])
        break
    }
}

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'internal/ExamplesTools.ps1')

if ($List) {
    foreach ($entry in Get-ExamplesCatalog) {
        $scenes = if (@($entry.scenes).Count -gt 0) { $entry.scenes -join ', ' } else { '(models only)' }
        Write-Host ('{0,-28} {1}' -f $entry.name, $scenes)
    }
    exit 0
}
if (-not $Example) {
    Write-Error 'Name the example to play, or use -List. Run with -Help for usage.'
    exit 2
}

$entry = Select-Examples -Name $Example
if (@($entry.scenes).Count -eq 0) {
    Write-Error "$($entry.name) has no scene; it holds models that other examples instance."
    exit 1
}
if (-not $Scene) { $Scene = @($entry.scenes)[0] }
$sceneName = @($entry.scenes) | Where-Object { $_ -ieq $Scene } | Select-Object -First 1
if (-not $sceneName) {
    Write-Error "$($entry.name) has no scene '$Scene'; its scenes are $($entry.scenes -join ', ')."
    exit 1
}
$virtualPath = "/Showcase/Scenes/$($entry.name)/$sceneName.oscene"
if (-not (Test-Path -LiteralPath (Join-Path $ExamplesCookedRoot "Scenes/$($entry.name)/$sceneName.oscene") -PathType Leaf)) {
    Write-Error "$virtualPath is not cooked; run Tools/cook $($entry.name)."
    exit 1
}

$launch = [ordered]@{
    version       = 3
    mounts        = @($ExamplesCookedRoot)
    scene         = $virtualPath
    source_mounts = Get-ExamplesSourceMounts
    window        = [ordered]@{ title = "Oxygen Player - $($entry.name)/$sceneName" }
}
$launchDir = Join-Path ([IO.Path]::GetTempPath()) 'oxygen-player'
New-Item -ItemType Directory -Force -Path $launchDir | Out-Null
$launchFile = Join-Path $launchDir "$($entry.name).$sceneName.launch.json"
$launch | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $launchFile -Encoding utf8NoBOM
Write-Host "Launch file: $launchFile"
Write-Host "Scene: $virtualPath"

$tools = Select-ExamplesTools -Targets 'oxygen-player' -Overrides @{ 'oxygen-player' = $PlayerPath } -BuildTree $BuildTree -Config $Config -Preset $Preset -NoBuild:$NoBuild
$arguments = @('--launch', $launchFile) + @($PlayerArgs | Where-Object { $_ -ne '--' })
Invoke-OxygenTool -Context $tools -Target 'oxygen-player' -Arguments $arguments
exit $LASTEXITCODE
