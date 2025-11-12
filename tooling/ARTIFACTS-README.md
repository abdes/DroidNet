# Artifacts Output (UseArtifactsOutput) — Repo guidance

This repository uses the .NET SDK artifacts output layout to centralize build outputs for predictable CI and tooling.

What changed (already enabled)

- `Directory.build.props` now sets `UseArtifactsOutput=true` and `ArtifactsPath=$(MSBuildThisFileDirectory)artifacts`.

- `Common.props` and `Directory.build.targets` were updated to respect `UseArtifactsOutput`:

  - `BaseOutputPath` and `BaseIntermediateOutputPath` are not set when `UseArtifactsOutput==true`.

  - `PackageOutputPath` only applies when `UseArtifactsOutput!=true`.

Why this helps

- Build outputs and intermediate files are centralized under a single `artifacts` folder in the repo root (or custom ArtifactsPath) with consistent subfolders: `bin`, `obj`, `package`, `publish`.
- This is handy for CI, packaging, and downstream steps that need to locate build results programmatically.

Default layout (examples)

- `artifacts/bin/<PROJECT_NAME>/<pivot>/`              - final outputs (dll, exe)

- `artifacts/obj/<PROJECT_NAME>/<pivot>/`              - intermediate output

- `artifacts/package/<configuration>/`                 - nuget packages

Pivot example: `debug_net9.0-windows10.0.26100.0` or `debug` depending on TFM

How to get the path from scripts/CI

- Query MSBuild properties directly (recommended for scripted CI):

  - Example (dotnet CLI):
    `dotnet build projects\Routing\Routing.Debugger.UI\src\Routing.Debugger.UI.csproj -c Debug --getProperty:OutputPath --getProperty:ArtifactsPath --getProperty:ArtifactsPivots`

- OR: Use the Python helper script included in `tooling/scripts` to find artifact locations and optionally list files. Examples:

  ```pwsh
  # print artifact paths (JSON)
  # Short flags are supported: -p -c -t -r -l -j -n
  python -m tooling.scripts.get_artifacts -p projects/Routing/Routing.Debugger.UI/src/Routing.Debugger.UI.csproj -c Debug -j

  # print artifact paths and list files
  python -m tooling.scripts.get_artifacts -p projects/Mvvm.Generators/src/Mvvm.Generators.csproj -c Debug -l

  # query all TFMs for multi-targeted projects and print per-framework artifacts
  python -m tooling.scripts.get_artifacts -p projects/TimeMachine/src/TimeMachine.csproj -c Debug --framework-all -j

  # Colored output (requires rich; use --no-color to disable)
  python -m tooling.scripts.get_artifacts -p projects/Routing/Routing.Debugger.UI/src/Routing.Debugger.UI.csproj -c Debug

  # Machine-readable (JSON only)
  python -m tooling.scripts.get_artifacts -p projects/Routing/Routing.Debugger.UI/src/Routing.Debugger.UI.csproj -c Debug -j
  ```

Notes on output formatting:

- The human-readable table (`--list`) displays columns: Time (YYYY-MM-DD HH:MM:SS), Size (bytes, right aligned), and Path.
- Table headers include file counts (e.g., "Files under OutputPath (7)").
- When `rich` is installed, only labels are colored while values are unstyled to avoid coloring parts of values like `netstandard2.0`.
- The script uses colored output whenever the `rich` package is present. To force plain output, pass `--no-color`.

- Use the Repo `ArtifactsPath` property in MSBuild tasks, or hardcode if needed in scripts:
  - `$(ArtifactsPath)\bin\<ProjectName>\<pivot>`

How to opt-out for a specific project

- To keep a single project's previous layout, set the property to false in the project or its own Directory.Build.props:

  ```xml
  <PropertyGroup>
    <UseArtifactsOutput>false</UseArtifactsOutput>
  </PropertyGroup>
  ```

Notes and compatibility

- Requires .NET SDK 8+ to use `UseArtifactsOutput` and make use of the simplified output layout.
- If you maintain tooling that expects `bin\projects\...` layout, update it to use `ArtifactsPath` (or `OutputPath` returned by `--getProperty`) for compatibility.
- The repo still uses `SlnGen` to generate solutions; solution generation and build are unaffected by this change.

- Troubleshooting

- If you find outputs still going to `bin\\projects` (legacy path), ensure that `UseArtifactsOutput` is set BEFORE `Common.props` is imported (we do this in `Directory.build.props`).
- If you need to discover where the CI should look for `nupkg` files, query the `PackageOutputPath` property or inspect `ArtifactsPath\package`.

The repository includes a Python helper `tooling/scripts/get_artifacts.py` for CI use. It prefers querying MSBuild for `ArtifactsPivots` and `ArtifactsPath` and falls back to a computed path when these properties are not present. Use `--framework-all` to collect per-targetframework artifacts for multi-targeted projects.

Short path heuristics:

- When you pass a relative project path it is interpreted relative to `repo-root/projects/` automatically so you can use `-p Mvvm.Generators/src/...` instead of passing the full relative path starting with `projects/`.
- If you pass an absolute path (e.g. `F:\...\foo.csproj`) it is used as-is.
- `-p .` or `-p ./` still behaves like the legacy behavior (current working directory).
