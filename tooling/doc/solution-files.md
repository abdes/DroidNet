# Visual Studio solutions

Solutions are generated, ignored views of the project graph. Project references
remain the dependency authority. Do not hand-edit generated solutions.

## Common workflows

```powershell
# Open a project and its dependencies.
./projects/Oxygen.Editor/open.cmd

# Generate without opening another Visual Studio window.
./projects/Storage/open.cmd -NoLaunch

# All product projects, including standalone native tests.
./tooling/GenerateSolution.ps1 -Scope projects -SolutionPath projects/Projects.sln

# Product projects and tooling samples.
./tooling/GenerateSolution.ps1

# Show every option and example.
./tooling/GenerateSolution.ps1 --help
```

All `open.cmd` entry points call the same generator and work independently of
the caller's current directory. Generated solutions use Debug/Release and x64.
The generator enumerates tracked and non-ignored new C# and C++/CLI projects;
it excludes the engine's CMake tree and follows project references transitively.

## Tool selection

Run `dotnet tool restore` once after cloning or updating the tool manifest.
`.config/dotnet-tools.json` pins SlnGen. The generator uses that package's .NET
Framework executable with Visual Studio MSBuild discovered by `vswhere`.
This host supports C++/CLI evaluation; the `dotnet slngen` host cannot reliably
evaluate the native project imports in this solution.

The script preserves existing solutions so SlnGen can reuse Visual Studio's
cache. It never restores packages, builds projects, or upgrades tools implicitly.
Use `-UseDiagnostics` only when investigating generation; its log stays in
ignored `artifacts/build-streamlining/`.

See [build, analysis and packaging](build.md) for the resulting workflows.
