# Visual Studio solutions

Solutions are generated, ignored views of the project graph. Project references
remain the dependency authority. Do not hand-edit generated solutions.

## Common workflows

```powershell
# Open a project and its dependencies.
./projects/Oxygen.Editor/open.cmd

# Generate without opening another Visual Studio window.
./projects/Storage/open.cmd -NoLaunch

# All product and test projects, including standalone native tests.
./tooling/GenerateSolution.ps1 -Scope projects -SolutionPath projects/Projects.sln

# Product projects and tooling samples.
./tooling/GenerateSolution.ps1

# Show every option and example.
./tooling/GenerateSolution.ps1 --help
```

All `open.cmd` entry points call the same generator and work independently of
the caller's current directory. Generated solutions use Debug/Release and x64.
The generator enumerates tracked and non-ignored new C# and C++/CLI projects;
it excludes the engine's CMake tree, ignores deleted paths awaiting commit, and
follows project references transitively.

## Test execution tiers

`-TestScope All` is the default: `open.cmd` includes every test project. Use `Unit`, `Integration`, or `Benchmarks` explicitly to select a reduced set. Production projects remain included.
Projects declare `DroidNetTestTier`; ordinary C# test projects default to `Unit`.
This selects whole projects through SlnGen's supported
[`IncludeInSolutionFile` property](https://microsoft.github.io/slngen/FAQ#how-do-i-leave-projects-out-of-the-solution),
without conditional test compilation or hidden discovery filters.

```powershell
./projects/Oxygen.Editor.WorldEditor/open.cmd -TestScope Integration
./projects/Oxygen.Editor.WorldEditor/open.cmd -TestScope Benchmarks
./tooling/GenerateSolution.ps1 -Scope projects -TestScope All -NoLaunch
```

The `open.cmd` wrappers supply a fixed solution path. Passing `-TestScope` to a
wrapper regenerates that same file with the selected projects. To keep a filtered
solution alongside the complete solution, call `GenerateSolution.ps1` directly
with a distinct `-SolutionPath`, or let it choose a scope-suffixed filename.

## Tool selection

Run `dotnet tool restore` once after cloning or updating the tool manifest.
`.config/dotnet-tools.json` pins SlnGen. The generator uses that package's .NET
Framework executable with 64-bit Visual Studio MSBuild discovered by `vswhere`.
It searches only `Bin\amd64` and supplies `PreferredToolArchitecture=x64`;
there is no 32-bit fallback. See the [Windows tool policy](build.md#windows-tool-policy).
This host supports C++/CLI evaluation; the `dotnet slngen` host cannot reliably
evaluate the native project imports in this solution.

The script preserves existing solutions so SlnGen can reuse Visual Studio's
cache. It never restores packages, builds projects, or upgrades tools implicitly.
Use `-UseDiagnostics` only when investigating generation; its log stays in
ignored `artifacts/build-streamlining/`.

See [build, analysis and packaging](build.md) for the resulting workflows.
