#Requires -Version 7.0
<#
.SYNOPSIS
Tests a built editor/SDK set and explicitly promotes its fixed qualification manifest.
.DESCRIPTION
Build the editor and qualification test projects first. This command does not build
the engine or modify source. Artifact read leases remain held while tests run.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration,

    [string] $RepositoryRoot = (Join-Path $PSScriptRoot '..\..\..'),
    [string] $EditorRoot,
    [string] $EngineRoot,
    [string] $VSTestPath,
    [string] $TargetFramework = 'net9.0-windows10.0.26100.0'
)

$ErrorActionPreference = 'Stop'
if ([Environment]::Version.Major -lt 9) {
    throw 'Use PowerShell running on .NET 9 or later.'
}
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
if (!$EditorRoot) { $EditorRoot = Join-Path $RepositoryRoot "artifacts\bin\Oxygen.Editor.App\${Configuration}_${TargetFramework}" }
if (!$EngineRoot) { $EngineRoot = Join-Path $RepositoryRoot "projects\Oxygen.Engine\out\install\$Configuration" }
$EditorRoot = [IO.Path]::GetFullPath($EditorRoot)
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)
if (!$VSTestPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = & $vswhere -latest -prerelease -products '*' -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'Visual Studio test runner was not found. Supply -VSTestPath.' }
    $VSTestPath = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
}
& git -C $RepositoryRoot diff --quiet HEAD -- projects
if ($LASTEXITCODE -ne 0) { throw 'Commit the source before qualifying its artifacts.' }
$untrackedSource = & git -C $RepositoryRoot ls-files --others --exclude-standard -- projects
if ($LASTEXITCODE -ne 0 -or $untrackedSource) { throw 'Commit or remove untracked source before qualifying its artifacts.' }
$revision = (& git -C $RepositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'The source revision could not be read.' }

$suites = @(
    @{ Name = 'Core'; Project = 'Oxygen.Managed.Core.Tests'; Extension = '.dll' },
    @{ Name = 'ContentPipeline'; Project = 'Oxygen.Editor.ContentPipeline.Tests'; Extension = '.dll' },
    @{ Name = 'Runtime'; Project = 'Oxygen.Editor.Runtime.Tests'; Extension = '.dll' },
    @{ Name = 'Inspector'; Project = 'Oxygen.Editor.WorldEditor.UI.Tests'; Extension = '.build.appxrecipe' }
)
foreach ($suite in $suites) {
    $suite.Root = Join-Path $RepositoryRoot "artifacts\bin\$($suite.Project)\${Configuration}_${TargetFramework}"
    $suite.Path = Join-Path $suite.Root ($suite.Project + $suite.Extension)
    if (!(Test-Path -LiteralPath $suite.Path -PathType Leaf)) { throw "Build the qualification suite first: $($suite.Path)" }
}

[void][Reflection.Assembly]::LoadFrom((Join-Path $EditorRoot 'Oxygen.Managed.Core.dll'))
$installation = [Oxygen.Managed.Core.Compatibility.EditorArtifactInstallation]::new($EditorRoot, $EngineRoot, $Configuration)
$inventory = [Oxygen.Managed.Core.Compatibility.EditorArtifactInventory]::Create($installation)
$streams = [Collections.Generic.List[IO.FileStream]]::new()
$artifacts = [Collections.Generic.List[object]]::new()
$evidence = [Collections.Generic.List[object]]::new()
$runId = [Guid]::NewGuid().ToString('N')
$resultsRoot = Join-Path $RepositoryRoot "artifacts\TestResults\qualification-$Configuration-$runId"
[void][IO.Directory]::CreateDirectory($resultsRoot)
$originalPath = $env:PATH
try {
    foreach ($artifact in $inventory) {
        $stream = [IO.File]::Open($artifact.FullPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $streams.Add($stream)
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
        $artifacts.Add([ordered]@{ id = $artifact.Id; size = $stream.Length; sha256 = $hash; schemaId = $artifact.SchemaId })
    }

    foreach ($schema in $artifacts | Where-Object { $_.id.StartsWith('engine/schemas/') }) {
        $editorId = 'editor/Schemas/' + [IO.Path]::GetFileName($schema.id)
        $editorSchema = $artifacts | Where-Object { $_.id -ceq $editorId }
        if ($editorSchema -and $editorSchema.sha256 -ne $schema.sha256) {
            throw "Editor and installed engine schemas differ: $editorId"
        }
    }

    # Test deployments must use the same owned editor assemblies as the candidate.
    foreach ($suite in $suites) {
        $testAssembly = Join-Path $suite.Root ($suite.Project + '.dll')
        $stream = [IO.File]::Open($testAssembly, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $streams.Add($stream)
        $suite.AssemblyHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
        foreach ($artifact in $artifacts) {
            if ($artifact.id -notmatch '^editor/(Oxygen\.[^/]+\.dll|DroidNet\.Oxygen\.Editor\.Interop\.dll)$') { continue }
            $copy = Join-Path $suite.Root ([IO.Path]::GetFileName($artifact.id))
            if (!(Test-Path -LiteralPath $copy -PathType Leaf)) { continue }
            $stream = [IO.File]::Open($copy, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            $streams.Add($stream)
            $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
            if ($hash -ne $artifact.sha256) { throw "Test deployment differs from the editor. Rebuild $($suite.Project): $copy" }
        }
    }

    $env:PATH = (Join-Path $EngineRoot 'bin') + [IO.Path]::PathSeparator + $originalPath
    foreach ($suite in $suites) {
        $suiteResults = Join-Path $resultsRoot $suite.Name
        [void][IO.Directory]::CreateDirectory($suiteResults)
        & $VSTestPath $suite.Path "/Logger:trx;LogFileName=$($suite.Name).trx" "/ResultsDirectory:$suiteResults"
        if ($LASTEXITCODE -ne 0) { throw "Qualification failed: $($suite.Name). The accepted manifest was not changed." }
        $trxPath = Join-Path $suiteResults ($suite.Name + '.trx')
        [xml]$trx = Get-Content -LiteralPath $trxPath -Raw
        $counters = $trx.TestRun.ResultSummary.Counters
        if ([int]$counters.total -eq 0 -or [int]$counters.passed -ne [int]$counters.total) {
            throw "Qualification requires every case to pass: $($suite.Name)."
        }
        $evidence.Add([ordered]@{
            suite = $suite.Name; passed = [int]$counters.passed
            assemblySha256 = $suite.AssemblyHash
            results = $trxPath; resultsSha256 = (Get-FileHash -LiteralPath $trxPath -Algorithm SHA256).Hash
        })
    }

    & git -C $RepositoryRoot diff --quiet HEAD -- projects
    if ($LASTEXITCODE -ne 0 -or ((& git -C $RepositoryRoot rev-parse HEAD).Trim() -ne $revision)) {
        throw 'Source changed during qualification. The accepted manifest was not changed.'
    }
    $currentInventory = [Oxygen.Managed.Core.Compatibility.EditorArtifactInventory]::Create($installation)
    $changedInventory = Compare-Object @($inventory | ForEach-Object { $_.Id + '|' + $_.FullPath + '|' + $_.SchemaId }) @($currentInventory | ForEach-Object { $_.Id + '|' + $_.FullPath + '|' + $_.SchemaId })
    if ($changedInventory) { throw 'Artifact inventory changed during qualification. Retry with a stable installation.' }

    $manifest = [ordered]@{ version = 1; configuration = $Configuration; sourceRevision = $revision; artifacts = $artifacts.ToArray() }
    $report = [ordered]@{
        configuration = $Configuration; sourceRevision = $revision; completedUtc = [DateTimeOffset]::UtcNow
        editorRoot = $EditorRoot; engineRoot = $EngineRoot; tests = $evidence.ToArray()
        machine = [ordered]@{
            cpu = @((Get-CimInstance Win32_Processor).Name)
            ramBytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
            gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name, AdapterRAM, DriverVersion)
            os = [Runtime.InteropServices.RuntimeInformation]::OSDescription
            runtime = [Runtime.InteropServices.RuntimeInformation]::FrameworkDescription
        }
    }
    $qualificationDirectory = [IO.Path]::GetDirectoryName($installation.ManifestPath)
    [void][IO.Directory]::CreateDirectory($qualificationDirectory)
    $temporaryManifest = Join-Path $qualificationDirectory "$Configuration.$runId.tmp"
    $temporaryReport = Join-Path $qualificationDirectory "$Configuration.$runId.report.tmp"
    $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporaryManifest -Encoding utf8NoBOM
    $manifestHash = (Get-FileHash -LiteralPath $temporaryManifest -Algorithm SHA256).Hash
    $report.manifestSha256 = $manifestHash
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporaryReport -Encoding utf8NoBOM
    $reportPath = Join-Path $qualificationDirectory "$Configuration.$manifestHash.report.json"
    [IO.File]::Move($temporaryReport, $reportPath, $true)
    [IO.File]::Move($temporaryManifest, $installation.ManifestPath, $true)
    Write-Output "Qualified $Configuration artifact set: $($installation.ManifestPath)"
    Write-Output "Qualification report: $reportPath"
}
finally {
    $env:PATH = $originalPath
    foreach ($stream in $streams) { $stream.Dispose() }
}
