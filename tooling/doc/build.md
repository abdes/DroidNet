# Build, analyze and package

Use [Visual Studio](#visual-studio), [command-line verification](#command-line-verification),
[tests](#tests), or [packaging](#packaging). Engine CMake builds are separate;
the editor consumes an installed Debug or Release engine SDK.

Runtime projects and tests target .NET 10, with Windows-specific projects retaining
their Windows SDK target. Source generators and their compiler-facing attribute
assemblies retain `netstandard2.0`. Use the SDK selected by `global.json`.
The editor's EF Core packages and SQLite driver are aligned at 10.0.12.

## Windows tool policy

Windows DroidNet builds use **64-bit build hosts and x64-hosted compiler/linker
tools only**. Never select 32-bit MSBuild or `Hostx86` tools. The target platform
(`x64`) is separate from the host architecture.

Initialize an x64 Visual Studio developer shell with `vcvars64.bat` or
`-arch=x64 -host_arch=x64`. Use the same Visual Studio installation for all
builders sharing an output configuration; changing installations or compiler
hosts invalidates C++ incremental tracking. Check command resolution:

```powershell
Get-Command MSBuild -All | Select-Object CommandType, Source, Definition
(Get-Command cl.exe -CommandType Application).Source
$msbuild = Join-Path $env:VSINSTALLDIR 'MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild -nologo -version
```

MSBuild must resolve to `MSBuild\Current\Bin\amd64\MSBuild.exe`, and MSVC to
`VC\Tools\MSVC\<version>\bin\Hostx64\x64\cl.exe`. A bare `MSBuild` command or an
initialized shell alone does not prove correct resolution. Repository builds
default `PreferredToolArchitecture=x64` and reject 32-bit hosts/compiler-host
overrides. SDK-internal utilities are not a reason to select a 32-bit build host;
their architecture is controlled by the SDK.

The [build wrapper](../Build.ps1), also used by VS Code build tasks, selects only
64-bit Visual Studio MSBuild, preferring the initialized shell's installation.
It has no 32-bit fallback. Do not start it while someone else is building the
same output configuration.

```powershell
.\tooling\Build.ps1 -Solution projects\Oxygen.Editor\src\Oxygen.Editor.App.csproj -Configuration Debug
# Inspect tool selection without building or restoring.
.\tooling\Build.ps1 -Solution projects\Oxygen.Editor\src\Oxygen.Editor.App.csproj -WhatIf
```

## Visual Studio

Generate a scoped solution with the project's `open.cmd`, or use
[GenerateSolution.ps1](../GenerateSolution.ps1). See [solution generation](solution-files.md).
Select **Debug | x64** or **Release | x64**.

| Action                                                 | Behavior                                                                                                                      |
| ------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| Build / Rebuild / F5                                   | Compile, run required source generators/XAML tools, link and copy dependencies. No diagnostic-analyzer pass or NuGet packing. |
| Analyze → Run Code Analysis → On Project / On Solution | Run configured analyzers and report diagnostics in Error List.                                                                |
| Project Properties → Code Analysis                     | Control build-time and live analysis independently.                                                                           |
| Pack on a reusable library                             | Create its NuGet package explicitly. Editor modules are internal and non-packable.                                            |

`RunAnalyzersDuringBuild=false` is the repository default. Compiler diagnostics,
nullable warnings and source-generator diagnostics remain active. Analyzer
packages stay referenced so Visual Studio's on-demand analysis works. Live
analysis retains its default; choose the active-document scope in Visual Studio
settings instead of continuously analyzing the entire solution. Error List's
**Build + IntelliSense** view can still show live warnings after a successful
build; select **Build** to inspect compiler/build diagnostics alone.

`.editorconfig` owns diagnostic severities and style. IDE rules own language
style; StyleCop retains documentation, ordering and layout checks. Confirmed
cross-package duplicates are disabled individually. Distinct correctness rules
remain enabled. Do not globally disable `RunAnalyzers`, remove source-generator
references, or use bulk code fixes to silence an infrastructure check.

## Command-line verification

Run from an x64 Visual Studio developer shell, using **64-bit MSBuild**, including for
C# entry projects that reference the C++/CLI bridge:

```powershell
& $msbuild projects\Projects.sln /restore /m /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64
& $msbuild projects\Oxygen.Editor\src\Oxygen.Editor.App.csproj /restore /m /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64
```

For explicit automated analysis, opt in on a separate invocation:

```powershell
& $msbuild projects\Oxygen.Editor\src\Oxygen.Editor.App.csproj /m /p:RunAnalyzersDuringBuild=true /p:Platform=x64 /p:PreferredToolArchitecture=x64
```

This invocation may compile affected C# inputs to run analysis. Visual Studio's
Analyze command operates on its loaded workspace. Analysis does not substitute
for build or runtime tests. Avoid simultaneous builds against the same output
configuration. Do not regenerate or edit CMake-generated engine projects.

## Tests

Build first, then use **Test Explorer** or the repository runner:

```powershell
traverse Invoke-Tests --start projects/Storage/tests --configuration Debug
traverse Invoke-Tests --start projects/Oxygen.Editor.ContentPipeline/tests --timeout 900
traverse Invoke-Tests --start projects/Oxygen.Editor.Interop/test/native --configuration Release
```

The runner uses each project's evaluated target paths. C# tests, including WinUI,
run their MTP executable; native C++ tests use VSTest. It tests every declared
framework unless `--framework` selects one. UI tests may open a test window.
Failures and timeouts fail the command; the default timeout is 300 seconds per
process. Forward runner-specific options after `--`.

### WinUI hosting

UI test projects use the shared unpackaged MTP host, consistently locally
and in CI. DroidNet's dispatcher, realized-content, render-wait and fixture
helpers remain available. Use a packaged host only for tests whose behavior
requires package identity.

The host starts its UI dispatcher without displaying a window. The first
non-null `ContentRoot` assignment or `MainWindow` access creates the test window
on the UI thread. Tests reuse it; fixture cleanup unloads content without creating
a window. Discovery and dispatcher-only tests remain window-free. The host closes
an existing window and exits when the runner finishes.

Build with Visual Studio MSBuild, then run selected tests without rebuilding:

```powershell
traverse Invoke-Tests --start projects/Oxygen.Editor.ContentBrowser/tests/UI -- --filter FullyQualifiedName~AssetStatusUpdatesPreserve
dotnet test --project projects/Controls/DynamicTree/tests/UI/Controls.DynamicTree.UI.Tests.csproj --no-build -c Release --filter FullyQualifiedName~ReusesWindowAcrossContentLoads
```

The SDK is pinned to `4.5.0-preview.26480.13` (MTP `2.5.0-preview.26480.13`),
source commit `19db2c848caec237de9ba5e3388e5e694b68ad37`, from Microsoft's
`test-tools` feed. [DroidNet #19](https://github.com/abdes/DroidNet/issues/19)
tracks qualification of the final release and removal of the preview feed.
See the [migration plan](test-migration.md) for validation status and commit scope.

C# test projects use embedded English diagnostics for the MSTest adapter and
platform-service assemblies via `EnableMSTestV2CopyResources=false`. Their localized
satellites otherwise enter the Windows PRI index without a neutral file candidate,
causing `PRI263`. Application localization and the test thread's culture remain
unchanged; warnings are not suppressed.

Build-infrastructure regression checks:

```powershell
python -m unittest discover -s tooling/tests -v
```

These verify evaluated paths, analysis separation, duplicate diagnostics, process
failure propagation and SDK receipt invalidation. Generated probes and logs live
under ignored `artifacts/`; no engine build is performed.

## Packaging

Only reusable DroidNet libraries opt into NuGet packaging. Both Debug and Release
ordinary builds have `GeneratePackageOnBuild=false`. To produce a Release package:

```powershell
& $msbuild projects\Storage\src\Storage.csproj /restore /m /t:Pack /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64
```

NuGet packages go to `artifacts/package/Release`. Generator packages retain their
analyzer assemblies and required dependencies. SDK packing is used directly.

For MSIX applications, use Visual Studio's **Package and Publish** UI, or:

```powershell
traverse New-Package --start projects/Oxygen.Editor --configuration Release --PackageCertificateKeyFile C:/certs/test.pfx
```

MSIX packaging builds its project references. Signing credentials remain local;
do not put certificate passwords in source-controlled files.

## Shared configuration

- Root `Directory.build.props` and `Common.props`: defaults and project categories.
- Root `Directory.build.targets`: final metadata, scoped analyzers and shared targets.
- `tooling/msbuild/Artifacts.props`: artifact root and native intermediate root.
- `tooling/msbuild/ArtifactPivots.props`: configuration/framework/RID identity,
  evaluated after the managed project's framework properties.
- `Directory.packages.props`: NuGet versions; `.config/dotnet-tools.json`: CLI tools.
- `NativeSdkReceipt.targets`: DLL identity for assembly metadata, and a separate
  header/library fingerprint for native compilation. Unchanged headers are not
  rewritten. DLL-only identity changes do not force every native source to compile.

Only ordinary non-WinUI SDK projects default to VS build acceleration. Projects
with custom helper-process staging explicitly opt out. Actual source generation,
SDK compatibility checks and required runtime copies remain build dependencies.

## Incremental build baseline

Measured on 2026-10-01, Ryzen 9 9950X, .NET SDK 10.0.401, VS MSBuild 18.10.1,
Debug/x64. Entry point: `Oxygen.Editor.ContentPipeline.csproj`, `MSBuild.exe /m`.
Both runs use warm outputs; recompilation is triggered by touching
`ContentPipelineService.cs`, without changing its contents.

| Scenario                | Before (`2c05649e2`) | Streamlined configuration  |
| ----------------------- | -------------------- | -------------------------- |
| Unchanged build         | 3.92 s               | 2.59 s median; 2.56–2.79 s |
| C# source recompilation | 9.94 s               | 5.15 s median; 5.03–5.27 s |

Before has one measured sample per scenario; after has three. These are local
iteration measurements, not whole-solution or clean-build estimates. Retain the
entry point and command when establishing a new comparison.

The ordinary C# consumer check preserves `Controls.DynamicTree.dll` when
`Collections` recompiles without an API change. The WinUI editor still regenerates in that scenario; build acceleration
stays limited to ordinary SDK projects.
