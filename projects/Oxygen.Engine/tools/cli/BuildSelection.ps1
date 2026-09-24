<#
.SYNOPSIS
Shared preset selection and executable invocation for Oxygen tools.
.DESCRIPTION
Dot-source this library from PowerShell, or use -AsJson to retrieve the selected
build and runtime environment. CMake determines which presets are enabled.
.PARAMETER RequestedHelp
Show help without discovering build trees. Aliases: -Help, -h.
#>
param(
    [Alias('Help', 'h')][switch]$RequestedHelp,
    [switch]$AsJson,
    [Alias('BuildTree')][string]$RequestedBuildTree,
    [Alias('Config')][string]$RequestedConfig,
    [Alias('Preset')][string]$RequestedPreset,
    [Alias('RequiredExecutables')][string[]]$RequestedExecutables = @(),
    [Alias('Tracy')][switch]$RequestedTracy
)

if ($RequestedHelp) { Get-Help $PSCommandPath -Detailed; return }

function Get-OxygenSourceRoot {
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
}

function Merge-OxygenPresetMap([System.Collections.IDictionary]$Base, [System.Collections.IDictionary]$Overlay) {
    $result = [hashtable]::new([StringComparer]::Ordinal)
    foreach ($key in $Base.Keys) { $result[$key] = $Base[$key] }
    foreach ($key in $Overlay.Keys) {
        if ($result[$key] -is [System.Collections.IDictionary] -and $Overlay[$key] -is [System.Collections.IDictionary]) {
            $result[$key] = Merge-OxygenPresetMap $result[$key] $Overlay[$key]
        } else { $result[$key] = $Overlay[$key] }
    }
    return $result
}

function Expand-OxygenPresetText {
    param([string]$Value, [string]$SourceRoot, [string]$FileDir, [string]$PresetName,
        [System.Collections.IDictionary]$Environment = @{}, [string[]]$Stack = @())
    $macros = @{
        sourceDir = $SourceRoot; sourceParentDir = [IO.Path]::GetDirectoryName($SourceRoot)
        sourceDirName = [IO.Path]::GetFileName($SourceRoot); fileDir = $FileDir
        presetName = $PresetName; dollar = '$'; pathListSep = [string][IO.Path]::PathSeparator
        hostSystemName = $(if ($IsWindows) { 'Windows' } elseif ($IsMacOS) { 'Darwin' } else { 'Linux' })
    }
    return [regex]::Replace($Value, '\$\{([^}]+)\}|\$(p?env)\{([^}]+)\}', {
        param($match)
        if ($match.Groups[1].Success) {
            $name = $match.Groups[1].Value
            if (-not $macros.ContainsKey($name)) { throw "Unsupported preset macro '$name' in '$Value'." }
            return [string]$macros[$name]
        }
        $name = $match.Groups[3].Value
        if ($match.Groups[2].Value -eq 'env' -and $Environment.Contains($name)) {
            if ($Stack -contains $name) { throw "Cyclic preset environment variable: $name" }
            return Expand-OxygenPresetText $Environment[$name] $SourceRoot $FileDir $PresetName $Environment ($Stack + $name)
        }
        return [string][Environment]::GetEnvironmentVariable($name)
    })
}

function Read-OxygenPresetGraph([string]$SourceRoot) {
    $graph = @{}
    foreach ($kind in @('configurePresets', 'buildPresets', 'testPresets')) {
        $graph[$kind] = [hashtable]::new([StringComparer]::Ordinal)
    }
    $comparer = if ($IsWindows) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal }
    $seen = [Collections.Generic.HashSet[string]]::new($comparer)
    function Read-PresetFile([string]$Path) {
        $Path = [IO.Path]::GetFullPath($Path)
        if (-not $seen.Add($Path)) { return }
        # fileDir is evaluated where the field is defined, before inheritance.
        $text = [IO.File]::ReadAllText($Path)
        $fileDir = [IO.Path]::GetDirectoryName($Path)
        $escapedDirectory = (ConvertTo-Json $fileDir -Compress).Trim('"')
        $data = $text.Replace('${fileDir}', $escapedDirectory) | ConvertFrom-Json -AsHashtable
        foreach ($include in $data['include']) {
            $include = Expand-OxygenPresetText $include $SourceRoot $fileDir ''
            if (-not [IO.Path]::IsPathRooted($include)) { $include = Join-Path $fileDir $include }
            Read-PresetFile $include
        }
        foreach ($kind in @('configurePresets', 'buildPresets', 'testPresets')) {
            foreach ($preset in $data[$kind]) {
                if ($graph[$kind].ContainsKey($preset.name)) { throw "Duplicate $kind name '$($preset.name)'." }
                $graph[$kind][$preset.name] = $preset
            }
        }
    }
    Read-PresetFile (Join-Path $SourceRoot 'CMakePresets.json')
    $user = Join-Path $SourceRoot 'CMakeUserPresets.json'
    if (Test-Path -LiteralPath $user) { Read-PresetFile $user }
    return $graph
}

function Resolve-OxygenPreset([System.Collections.IDictionary]$Presets, [string]$Name, [string[]]$Stack = @()) {
    if (-not $Presets.Contains($Name)) { throw "Unknown preset '$Name'." }
    if ($Stack -contains $Name) { throw "Cyclic preset inheritance at '$Name'." }
    $raw = $Presets[$Name]
    $result = @{}
    $parents = @($raw['inherits'])
    for ($i = $parents.Count - 1; $i -ge 0; --$i) {
        if ($parents[$i]) {
            $parent = Resolve-OxygenPreset $Presets $parents[$i] ($Stack + $Name)
            foreach ($field in @('name', 'hidden', 'inherits', 'description', 'displayName')) { $parent.Remove($field) }
            $result = Merge-OxygenPresetMap $result $parent
        }
    }
    return Merge-OxygenPresetMap $result $raw
}

function Get-OxygenCacheValues([string]$BuildRoot) {
    $values = @{}
    $path = Join-Path $BuildRoot 'CMakeCache.txt'
    if (Test-Path -LiteralPath $path) {
        foreach ($line in [IO.File]::ReadLines($path)) {
            if ($line -match '^([^#/:][^:]*):[^=]+=(.*)$') { $values[$Matches[1]] = $Matches[2] }
        }
    }
    return $values
}

function Get-OxygenBuildCandidates([string]$SourceRoot = (Get-OxygenSourceRoot)) {
    $SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
    $listing = & cmake --list-presets=build -S $SourceRoot 2>&1
    if ($LASTEXITCODE -ne 0) { throw "CMake could not load the presets:`n$($listing -join "`n")" }
    $enabled = @($listing | ForEach-Object { if ([string]$_ -match '^\s*"([^"]+)"') { $Matches[1] } })
    $graph = Read-OxygenPresetGraph $SourceRoot
    foreach ($name in $enabled) {
        # Project presets inherit the matching Conan preset and add our shared
        # policy. Offer one choice per configuration, never the raw base as well.
        if ($name.StartsWith('conan-', [StringComparison]::Ordinal) -and
            $enabled -ccontains ('oxygen-' + $name.Substring(6))) { continue }
        $build = Resolve-OxygenPreset $graph.buildPresets $name
        $configure = Resolve-OxygenPreset $graph.configurePresets $build.configurePreset
        $environment = if ($configure['environment']) { $configure['environment'] } else { @{} }
        $variables = if ($configure['cacheVariables']) { $configure['cacheVariables'] } else { @{} }
        $root = Expand-OxygenPresetText $configure['binaryDir'] $SourceRoot $SourceRoot $configure.name $environment
        if (-not [IO.Path]::IsPathRooted($root)) { $root = Join-Path $SourceRoot $root }
        $root = [IO.Path]::GetFullPath($root)
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $cache = Get-OxygenCacheValues $root
        if ($cache['CMAKE_HOME_DIRECTORY'] -and
            [IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY']) -ne $SourceRoot) { continue }
        $config = $build['configuration']
        if (-not $config) { $config = $variables['CMAKE_BUILD_TYPE'] }
        if ($config -is [System.Collections.IDictionary]) { $config = $config['value'] }
        if (-not $config) { $config = $cache['CMAKE_BUILD_TYPE'] }
        if (-not $config) { continue }
        $asan = $configure.name -match '(^|-)asan(-|$)'
        $tracy = $configure.name -match '(^|-)tracy(-|$)'
        foreach ($flag in @('OXYGEN_WITH_ASAN', 'OXYGEN_WITH_TRACY')) {
            $value = $variables[$flag]
            if ($value -is [System.Collections.IDictionary]) { $value = $value.value }
            if ($null -eq $value -and $configure.name -notmatch '^conan-(tracy-)?(asan-)?(ninja|vs)-') { $value = $cache[$flag] }
            if ($null -ne $value) {
                $on = [string]$value -match '^(ON|TRUE|YES|1)$'
                if ($flag -eq 'OXYGEN_WITH_ASAN') { $asan = $on } else { $tracy = $on }
            }
        }
        $runtimeEnvironment = $environment
        $testNames = @($graph.testPresets.Keys | Sort-Object { $_ -cne $name }, { $_ })
        foreach ($testName in $testNames) {
            $test = Resolve-OxygenPreset $graph.testPresets $testName
            if ($test['configurePreset'] -eq $configure.name -and $test['configuration'] -eq $config) {
                $base = if ($test['inheritConfigureEnvironment'] -eq $false) { @{} } else { $environment }
                $runtimeEnvironment = Merge-OxygenPresetMap $base $(if ($test['environment']) { $test['environment'] } else { @{} })
                break
            }
        }
        $expandedEnvironment = @{}
        foreach ($key in $runtimeEnvironment.Keys) {
            $expandedEnvironment[$key] = if ($null -eq $runtimeEnvironment[$key]) { $null } else {
                Expand-OxygenPresetText $runtimeEnvironment[$key] $SourceRoot $SourceRoot $configure.name $runtimeEnvironment @($key)
            }
        }
        [pscustomobject]@{
            SourceRoot = $SourceRoot; BuildRoot = $root; Config = [string]$config
            BuildPreset = $name; ConfigurePreset = $configure.name; Generator = $configure.generator
            Sanitized = $asan; Tracy = $tracy; RuntimeEnvironment = $expandedEnvironment
            ConfigRank = $(switch ($config) { 'Release' { 0 } 'RelWithDebInfo' { 1 } 'MinSizeRel' { 2 } 'Debug' { 3 } default { 4 } })
            InstrumentationRank = [int]($asan -or $tracy)
            GeneratorRank = $(if ($configure.generator -like 'Ninja*') { 0 } elseif ($configure.generator -like 'Visual Studio*') { 1 } else { 2 })
        }
    }
}

function Resolve-OxygenBuildSelection {
    param([string]$SourceRoot = (Get-OxygenSourceRoot), [string]$BuildTree, [string]$Config,
        [string]$Preset, [switch]$Sanitized, [switch]$Tracy, [string[]]$RequiredExecutables = @(),
        [scriptblock]$Accept)
    $candidates = @(Get-OxygenBuildCandidates $SourceRoot)
    if ($BuildTree) {
        $root = if ([IO.Path]::IsPathRooted($BuildTree)) { $BuildTree }
        elseif ($BuildTree -match '[/\\]') { Join-Path $SourceRoot $BuildTree }
        else { Join-Path (Join-Path $SourceRoot 'out') $BuildTree }
        $root = [IO.Path]::GetFullPath($root)
        $candidates = @($candidates | Where-Object { $_.BuildRoot -eq $root })
        if (-not $candidates.Count -and -not $Preset) {
            # Explicit cached custom trees remain usable without registering an
            # automatic fallback. Never discover them by scanning arbitrary dirs.
            $cache = Get-OxygenCacheValues $root
            if ($cache['CMAKE_HOME_DIRECTORY'] -and
                [IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY']) -ne [IO.Path]::GetFullPath($SourceRoot)) {
                throw "Build tree '$root' belongs to another source checkout: $($cache['CMAKE_HOME_DIRECTORY'])"
            }
            $configs = if ($cache['CMAKE_CONFIGURATION_TYPES']) { $cache['CMAKE_CONFIGURATION_TYPES'] -split ';' }
            elseif ($cache['CMAKE_BUILD_TYPE']) { @($cache['CMAKE_BUILD_TYPE']) } else { @() }
            $candidates = @($configs | ForEach-Object {
                [pscustomobject]@{
                    SourceRoot = $SourceRoot; BuildRoot = $root; Config = $_
                    BuildPreset = $null; ConfigurePreset = $null; Generator = $cache['CMAKE_GENERATOR']
                    Sanitized = ($cache['OXYGEN_WITH_ASAN'] -match '^(ON|TRUE|1)$')
                    Tracy = ($cache['OXYGEN_WITH_TRACY'] -match '^(ON|TRUE|1)$')
                    RuntimeEnvironment = @{}
                    ConfigRank = $(switch ($_) { 'Release' { 0 } 'RelWithDebInfo' { 1 } 'MinSizeRel' { 2 } 'Debug' { 3 } default { 4 } })
                    InstrumentationRank = 0; GeneratorRank = 0
                }
            })
        }
    }
    if ($Config) { $candidates = @($candidates | Where-Object { $_.Config -eq $Config }) }
    if ($Preset) { $candidates = @($candidates | Where-Object { $_.BuildPreset -ceq $Preset }) }
    if ($Sanitized) { $candidates = @($candidates | Where-Object { $_.Sanitized -and $_.Config -eq 'Debug' }) }
    if ($Tracy) { $candidates = @($candidates | Where-Object { $_.Tracy }) }
    $candidates = @($candidates | Sort-Object ConfigRank, InstrumentationRank, GeneratorRank, BuildPreset)
    foreach ($candidate in $candidates) {
        $bin = Join-Path $candidate.BuildRoot "bin/$($candidate.Config)"
        $missing = @($RequiredExecutables | Where-Object {
            -not @(Get-ChildItem -LiteralPath $bin -Filter $_ -File -ErrorAction SilentlyContinue).Count
        })
        if ($missing.Count -or ($Accept -and -not (& $Accept $candidate))) { continue }
        return $candidate
    }
    $request = @("tree='$BuildTree'", "config='$Config'", "preset='$Preset'") -join ', '
    $needed = if ($RequiredExecutables.Count) { " Required executable(s): $($RequiredExecutables -join ', ')." } else { '' }
    throw "No available preset matches $request.$needed Run cmake --list-presets=build or initialize a tree with tools/generate-builds.ps1."
}

function Write-OxygenBuildSelection($Selection) {
    $name = if ($Selection.BuildPreset) { $Selection.BuildPreset } else { 'explicit build tree' }
    Write-Host "Using ${name}: $($Selection.BuildRoot) [$($Selection.Config)]"
}

function Invoke-OxygenSelectedExecutable {
    param($Selection, [string]$Executable, [string[]]$Arguments = @())
    $PSNativeCommandUseErrorActionPreference = $false
    $saved = @{}
    $pushed = $false
    try {
        foreach ($key in $Selection.RuntimeEnvironment.Keys) {
            $saved[$key] = [Environment]::GetEnvironmentVariable($key)
            [Environment]::SetEnvironmentVariable($key, $Selection.RuntimeEnvironment[$key])
        }
        Push-Location $Selection.SourceRoot
        $pushed = $true
        & $Executable @Arguments
        $global:LASTEXITCODE = $LASTEXITCODE
    } finally {
        if ($pushed) { Pop-Location }
        foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key]) }
    }
}

function Get-OxygenFileApiIndex([string]$BuildRoot) {
    $reply = Join-Path $BuildRoot '.cmake/api/v1/reply'
    $index = Get-ChildItem -LiteralPath $reply -Filter 'index-*.json' -File -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $index) { return $null }
    return Get-Content -LiteralPath $index.FullName -Raw | ConvertFrom-Json -AsHashtable
}

function Test-OxygenFileApiReply([string]$BuildRoot) {
    $index = Get-OxygenFileApiIndex $BuildRoot
    if (-not $index) { return $false }
    $reply = Join-Path $BuildRoot '.cmake/api/v1/reply'
    foreach ($kind in @('cache', 'codemodel', 'cmakeFiles', 'toolchains')) {
        $object = $index['objects'] | Where-Object { $_['kind'] -eq $kind } | Select-Object -First 1
        if (-not $object -or -not (Test-Path -LiteralPath (Join-Path $reply $object['jsonFile']) -PathType Leaf)) { return $false }
    }
    return $true
}

function Get-OxygenCodemodel([string]$BuildRoot) {
    $data = Get-OxygenFileApiIndex $BuildRoot
    if (-not $data) { return $null }
    $reply = Join-Path $BuildRoot '.cmake/api/v1/reply'
    $object = $data['objects'] | Where-Object { $_['kind'] -eq 'codemodel' -and $_['version']['major'] -eq 2 } | Select-Object -First 1
    if (-not $object) { return $null }
    $path = Join-Path $reply $object['jsonFile']
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
}

function Get-OxygenTargetReply([string]$BuildRoot, [string]$Target, [string]$Config) {
    $model = Get-OxygenCodemodel $BuildRoot
    if (-not $model) { return $null }
    $configuration = $model['configurations'] | Where-Object { $_['name'] -eq $Config } | Select-Object -First 1
    if (-not $configuration) { return $null }
    $entry = $configuration['targets'] | Where-Object { $_['name'] -eq $Target } | Select-Object -First 1
    if (-not $entry) { return $null }
    return Join-Path (Join-Path $BuildRoot '.cmake/api/v1/reply') $entry['jsonFile']
}

function Get-OxygenExecutableArtifact([string]$BuildRoot, [string]$Target, [string]$Config, [switch]$AllowMissing) {
    $reply = Get-OxygenTargetReply $BuildRoot $Target $Config
    if (-not $reply) {
        if (Get-OxygenCodemodel $BuildRoot) { return $null }
        # A VS solution can be built without a File API query. Oxygen module
        # targets use lowercase hyphens; OUTPUT_NAME retains dotted module names.
        # Accept only a unique equivalent filename in the requested config.
        return Find-OxygenExecutableInDirectory (Join-Path $BuildRoot "bin/$Config") $Target
    }
    if (-not (Test-Path -LiteralPath $reply -PathType Leaf)) { return $null }
    $data = Get-Content -LiteralPath $reply -Raw | ConvertFrom-Json -AsHashtable
    if ($data['type'] -ne 'EXECUTABLE') { return $null }
    foreach ($artifact in $data['artifacts']) {
        $path = $artifact['path']
        if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $BuildRoot $path }
        if ($AllowMissing -or (Test-Path -LiteralPath $path -PathType Leaf)) { return $path }
    }
    return $null
}

function Find-OxygenExecutableInDirectory([string]$Directory, [string]$Target) {
    $identity = $Target -replace '[._-]', ''
    $files = @(Get-ChildItem -LiteralPath $Directory -File -ErrorAction SilentlyContinue |
        Where-Object {
            (!$IsWindows -or $_.Extension -in @('.exe', '.cmd', '.bat', '.com')) -and
            ($(if ($IsWindows) { $_.BaseName } else { $_.Name }) -replace '[._-]', '') -ieq $identity
        })
    if ($files.Count -eq 1) { return $files[0].FullName }
    return $null
}

function Resolve-OxygenExecutables {
    param([Parameter(Mandatory)][ValidateNotNullOrEmpty()][string[]]$Targets,
        [string]$SourceRoot = (Get-OxygenSourceRoot), [string]$BuildTree,
        [string]$Config, [string]$Preset, [switch]$Sanitized,
        [System.Collections.IDictionary]$Overrides = @{}, $Selection,
        [scriptblock]$TargetResolver, [switch]$AllowMissing)

    $paths = @{}
    $resolved = @{}
    foreach ($target in $Targets) { $resolved[$target] = $target }
    foreach ($target in $Overrides.Keys) {
        if ($Targets -notcontains $target) { throw "Unknown executable override '$target'." }
        if (-not $Overrides[$target]) { continue }
        if ($Selection -or $BuildTree -or $Config -or $Preset -or $Sanitized) {
            throw 'Explicit executable paths cannot be combined with build selection options.'
        }
        $path = [IO.Path]::GetFullPath($Overrides[$target], $PWD.Path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Executable not found: $path" }
        $paths[$target] = $path
    }
    if ($paths.Count) {
        # Explicit paths bypass preset discovery, but still use the same launch
        # path. Missing companion tools must be siblings, not a fallback build.
        $directories = @($paths.Values | ForEach-Object { [IO.Path]::GetDirectoryName($_) } | Sort-Object -Unique)
        foreach ($target in $Targets) {
            if ($paths.ContainsKey($target)) { continue }
            if ($directories.Count -ne 1) { throw "Specify an explicit path for companion tool '$target'." }
            $path = Find-OxygenExecutableInDirectory $directories[0] $target
            if (-not $path) { throw "Companion executable '$target' not found in '$($directories[0])'." }
            $paths[$target] = $path
        }
        $Selection = [pscustomobject]@{
            SourceRoot = $SourceRoot; BuildRoot = $null; Config = $null
            BuildPreset = $null; RuntimeEnvironment = @{}
        }
    } else {
        if (-not $Selection) {
            $accept = { param($candidate)
                foreach ($target in $Targets) {
                    $name = if ($TargetResolver) { & $TargetResolver $target $candidate $true } else { $target }
                    if (-not $name -or -not (Get-OxygenExecutableArtifact $candidate.BuildRoot $name $candidate.Config)) { return $false }
                }
                return $true
            }
            $Selection = Resolve-OxygenBuildSelection -SourceRoot $SourceRoot -BuildTree $BuildTree -Config $Config -Preset $Preset -Sanitized:$Sanitized -Accept $accept
        }
        foreach ($target in $Targets) {
            $name = if ($TargetResolver) { & $TargetResolver $target $Selection $false } else { $target }
            $path = if ($name) { Get-OxygenExecutableArtifact $Selection.BuildRoot $name $Selection.Config -AllowMissing:$AllowMissing } else { $null }
            if (-not $path -and -not $AllowMissing) {
                throw "No executable artifact for '$target' [$($Selection.Config)] in '$($Selection.BuildRoot)'."
            }
            $resolved[$target] = $name
            $paths[$target] = $path
        }
    }
    return [pscustomobject]@{ Selection = $Selection; Targets = $resolved; Paths = $paths }
}

function Write-OxygenExecutableSelection($Context) {
    if ($Context.Selection.BuildRoot) { Write-OxygenBuildSelection $Context.Selection }
    else {
        foreach ($target in $Context.Paths.Keys | Sort-Object) { Write-Host "Using explicit ${target}: $($Context.Paths[$target])" }
    }
}

function Invoke-OxygenTool {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)][string]$Target,
        [string[]]$Arguments = @(), [string]$LogPath, [switch]$CheckExitCode)
    if (-not $Context.Paths.ContainsKey($Target) -or -not $Context.Paths[$Target]) {
        throw "No resolved executable for '$Target'."
    }
    $executable = $Context.Paths[$Target]
    if ($LogPath) {
        $LogPath = [IO.Path]::GetFullPath($LogPath, $PWD.Path)
        $stage = [IO.Path]::GetFileNameWithoutExtension($LogPath)
        Write-Host "Running ${stage}: $executable; log: $LogPath"
        Invoke-OxygenSelectedExecutable $Context.Selection $executable $Arguments *> $LogPath
    } else {
        Invoke-OxygenSelectedExecutable $Context.Selection $executable $Arguments
    }
    if ($CheckExitCode -and $LASTEXITCODE -ne 0) {
        $details = if ($LogPath) { " See $LogPath" } else { '' }
        throw "$([IO.Path]::GetFileName($executable)) exited $LASTEXITCODE.$details"
    }
    if ($LogPath -and $LASTEXITCODE -eq 0) { Write-Host "Completed $stage" }
}

if ($AsJson) {
    try {
        Resolve-OxygenBuildSelection -BuildTree $RequestedBuildTree -Config $RequestedConfig -Preset $RequestedPreset -Tracy:$RequestedTracy -RequiredExecutables $RequestedExecutables |
            ConvertTo-Json -Depth 12 -Compress
    } catch {
        Write-Error $_ -ErrorAction Continue
        exit 1
    }
}
