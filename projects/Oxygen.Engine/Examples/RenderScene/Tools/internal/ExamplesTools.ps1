#requires -Version 7.4
# Shared by RenderScene's content tools: the examples project they read, the
# output they write, the engine tool selection and the catalog of examples.
#
# The examples project (Examples/Showcase, share/oxygen/Showcase in an SDK) is an
# editor project: these tools only read it. Everything they produce stays in
# RenderScene, so they never share an output folder with the editor.

# RenderScene's root: the parent of Tools/.
$RenderSceneRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# The examples project, beside RenderScene.
$ExamplesRoot = [IO.Path]::GetFullPath((Join-Path $RenderSceneRoot '../Showcase'))

# An installed SDK keeps the build selection beside RenderScene
# (share/oxygen/Tools); a source checkout is the engine two folders up.
$sdkSupport = Join-Path $RenderSceneRoot '../Tools/BuildSelection.ps1'
$ExamplesInstalledSdk = Test-Path -LiteralPath $sdkSupport
if ($ExamplesInstalledSdk) {
    . $sdkSupport
    $ExamplesEngineRoot = Get-OxygenSourceRoot
} else {
    $ExamplesEngineRoot = [IO.Path]::GetFullPath((Join-Path $RenderSceneRoot '../..'))
    . (Join-Path $ExamplesEngineRoot 'tools/cli/BuildSelection.ps1')
}

# What Tools/cook cooks: one cooked root, holding the Content mount.
$ExamplesCookedRoot = Join-Path $RenderSceneRoot '.cooked/Content'
# Where Tools/pak packages it.
$ExamplesPakDirectory = Join-Path $RenderSceneRoot '.build/pak'

# Project.oxy: the project's identity and its committed mounts.
$ExamplesProject = Get-Content -LiteralPath (Join-Path $ExamplesRoot 'Project.oxy') -Raw | ConvertFrom-Json

# The committed source mounts as absolute folders, by name, for the tools that
# take mounts on their command line (`--mount <Name>=<dir>`, a launch file's
# `source_mounts`). A developer's .mounts.json files add to them as usual.
function Get-ExamplesSourceMounts {
    $mounts = [ordered]@{}
    foreach ($mount in @($ExamplesProject.Mounts | Where-Object Kind -eq 'Source')) {
        $mounts[$mount.Name] = [IO.Path]::GetFullPath((Join-Path $ExamplesRoot $mount.Path))
    }
    return $mounts
}

function Get-ExamplesMountArguments {
    $arguments = @()
    foreach ($mount in (Get-ExamplesSourceMounts).GetEnumerator()) {
        $arguments += @('--mount', "$($mount.Key)=$($mount.Value)")
    }
    return $arguments
}

# Selects the engine tools: an explicit tool or an installed SDK is used as is;
# a source checkout builds them first, so a stale tool never runs.
function Select-ExamplesTools {
    param(
        [Parameter(Mandatory)][string[]]$Targets,
        [string[]]$Prerequisites = @(),
        [hashtable]$Overrides = @{},
        [string]$BuildTree,
        [string]$Config,
        [string]$Preset,
        [switch]$NoBuild
    )
    $explicit = @($Overrides.Values | Where-Object { $_ }).Count -gt 0
    if ($explicit -or $ExamplesInstalledSdk -or $NoBuild) {
        $tools = Resolve-OxygenExecutables -SourceRoot $ExamplesEngineRoot -Targets $Targets -BuildTree $BuildTree -Config $Config -Preset $Preset -Overrides $Overrides
    } else {
        $tools = Build-OxygenExecutables -SourceRoot $ExamplesEngineRoot -Targets $Targets -Prerequisites $Prerequisites -BuildTree $BuildTree -Config $Config -Preset $Preset
    }
    Write-OxygenExecutableSelection $tools
    return $tools
}

# The examples of Examples.json, checked against Tools/examples.schema.json.
function Get-ExamplesCatalog {
    $path = Join-Path $ExamplesRoot 'Examples.json'
    $json = Get-Content -LiteralPath $path -Raw
    if (-not (Test-Json -Json $json -SchemaFile (Join-Path $ExamplesRoot 'Tools/examples.schema.json'))) {
        throw "Examples.json does not match Tools/examples.schema.json."
    }
    return @(($json | ConvertFrom-Json).examples)
}

# The catalog entries named, tagged, or all; an unknown name throws.
function Select-Examples {
    param([string[]]$Name = @(), [string]$Tag, [switch]$All)
    $catalog = Get-ExamplesCatalog
    if ($All) { return $catalog }
    if ($Tag) {
        $tagged = @($catalog | Where-Object { $_.tags -contains $Tag })
        if ($tagged.Count -eq 0) { throw "No example has the tag '$Tag'." }
        return $tagged
    }
    foreach ($wanted in $Name) {
        $entry = $catalog | Where-Object { $_.name -ieq $wanted } | Select-Object -First 1
        if (-not $entry) { throw "No example named '$wanted' in Examples.json; run Tools/cook -List." }
        $entry
    }
}

# The manifest of an example.
function Get-ExampleManifestPath([Parameter(Mandatory)][string]$Name) {
    return Join-Path $ExamplesRoot "Manifests/$Name.import-manifest.json"
}

# Writes the warnings and errors an ImportTool report holds: the importer
# reports what it left out or repaired only there.
function Write-ImportReportDiagnostics([Parameter(Mandatory)][string]$ReportPath) {
    if ((Get-Item -LiteralPath $ReportPath).Length -eq 0) { return }
    $report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    foreach ($job in @($report.jobs)) {
        foreach ($diagnostic in @($job.diagnostics | Where-Object { $_ -and $_.severity -ne 'info' })) {
            # The report omits an empty object path.
            $objectPath = $diagnostic.PSObject.Properties['object_path']
            $where = if ($objectPath) { " ($($objectPath.Value))" } else { '' }
            Write-Warning "job $($job.index) [$($diagnostic.code)] $($diagnostic.message)$where"
        }
    }
}
