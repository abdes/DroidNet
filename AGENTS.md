# DroidNet agent instructions

## Task-specific guidance

Before editing, read only the matching guidance below; reuse it if already read.
Do not bulk-load `.github` or its `copilot-instructions.json` (duplicates this
file). Prompts are task guidance, not authorization for extra work or delegation.

- C# tests: [MSTest guidance](.github/prompts/csharp-mstest.prompt.md).
- New WinUI test projects: [UI scaffolding](.github/prompts/dn-create-test-project-ui.prompt.md).
- C# XMLDoc: [XMLDoc guidance](.github/prompts/update-xmldoc.prompt.md).
- Markdown documentation: [scope rules](.github/agents/docs-agent.md); only for the
  requested operation, read the matching
  `.github/prompts/{create-readme,readme-blueprint-generator,create-specification,update-specification,create-implementation-plan}.prompt.md`.

## Build and tooling

- Unless a module's instructions say otherwise, run commands from the repository
  root in PowerShell. Use the .NET SDK selected
  by `global.json`; individual projects still target different frameworks.
- Provision the shared Python workspace, pinned .NET tools and hooks with
  `./init.ps1 -NoRestore`; add `-DotNetInstall` only when the SDK is missing.
  Use `./init.ps1` in the current shell, not `pwsh ./init.ps1` or `init.cmd`,
  when you need its activated environment to remain available in PowerShell.
- Python uses one root `.venv` and `uv.lock` for every tool and engine build tree:
  `uv sync --locked` (uv 0.12.17+, Python selected by `.python-version`). Do not
  create per-tool environments. Change the owning `pyproject.toml`, then run
  `uv lock` and `uv sync --locked` when changing dependencies; see
  [Python tooling](tooling/PYTHON.md).
- Build scoped managed/interop projects with Visual Studio `MSBuild.exe`, from a VS developer
  shell; `dotnet build` cannot evaluate the editor's C++/CLI references:
  `MSBuild.exe projects/Storage/tests/Storage.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64`.
  Avoid concurrent builds sharing the same output configuration.
- Solutions are generated, not maintained by hand:
  `./tooling/GenerateSolution.ps1 -Scope projects/Storage -TestScope Unit`.
  Project `open.cmd` scripts also generate solutions; pass `-NoLaunch` to avoid
  opening Visual Studio. Restore the pinned tools before solution generation.
- Ordinary builds skip diagnostic analyzers but still run source generators.
  Opt in with a separate MSBuild invocation using `/p:RunAnalyzersDuringBuild=true`;
  do not disable `RunAnalyzers` or remove generator references to suppress warnings.
- NuGet versions belong in `Directory.packages.props`, not individual project
  references. Test SDK selection also lives in `global.json`; retain the preview
  MSTest feed in `NuGet.config` while that SDK requires it.
- Managed/interop outputs use root `artifacts/`, not project-local `bin/obj`. Query evaluated
  paths with `get-artifacts -p Storage -c Debug -j` rather than guessing them.
  [Build workflows](tooling/doc/build.md) is the current command reference;
  older module READMEs and Copilot setup/build commands may be stale.

## Managed/interop verification

- Build the owning test project first, then run its outputs:
  `traverse Invoke-Tests --start projects/Storage/tests --configuration Debug`.
  `--start` accepts a directory or project file; use the narrowest relevant scope.
- Forward test-runner options after `--`, for example:
  `traverse Invoke-Tests --start projects/Storage/tests --configuration Debug -- --filter FullyQualifiedName~MyTest`.
  All declared frameworks run unless `--framework` selects one; the default
  timeout is 300 seconds per process, overridable with `--timeout`.
- Managed tests use MSTest/Microsoft.Testing.Platform executables, including the
  shared unpackaged WinUI host; native interop tests use VSTest. UI tests can open
  windows. Test project names must end in `.Tests` (WinUI: `.UI.Tests`).
- Build/tooling regression checks:
  `uv run --locked --no-sync --no-active python -m unittest discover -s tooling/tests -v`.
- Run hooks only on changed files: `pre-commit run --files path/to/file`.
  C# `dotnet-format` is disabled in pre-commit; a passing hook run is not C# analysis.

## Commits

- Support a dirty working tree and concurrent user edits; do not require a clean
  tree or ask the user to stop editing. Preserve unrelated changes.
- Before committing, review the staged diff and let hooks run; do not bypass
  failures.
- Make thematic commits: group related implementation, tests and documentation;
  split unrelated changes. Do not split a coherent change merely by file or layer.
- Summary: `<type>(<scope>): <concise imperative description>` describing the
  actual outcome.
- Body: explain what changed and why, significant constraints/tradeoffs and
  relevant verification, including checks not run. Avoid file inventories,
  session narration and unsupported completion claims.

## Oxygen boundaries

For all Oxygen code and design work (`projects/Oxygen.*`, their examples/tools,
and Oxygen documents under `design/`), read and follow the shared
[Oxygen engineering rules](design/oxygen/RULES.md).

- The editor's actual startup/DI/routing wiring is
  `projects/Oxygen.Editor/src/Program.cs`, not the default WinUI XAML entry point.
- Editor/interop builds consume an already-installed native SDK at
  `projects/Oxygen.Engine/out/install/<Configuration>`; they do not build the
  CMake engine. Editor-only work does not authorize an engine build.
- `Oxygen.Engine` is a separate Conan/CMake/C++ project, excluded from the managed
  solution generator. Use its [local instructions](projects/Oxygen.Engine/AGENTS.md)
  for build-tree provisioning, native tests and C++ tooling; the MSBuild/MTP
  commands above do not apply to the engine.

## Editing constraints

- Write style-compliant C# from the first edit, including member order and `this.`
  qualification. Follow `.editorconfig` and surrounding conventions; do not use
  repeated analyzer runs and broad member-reordering sweeps as a substitute for
  correct initial placement. This is a workflow constraint, not permission to
  ignore or suppress diagnostics. Make necessary corrections deliberately.

- Follow `.gitattributes` and `.editorconfig`: LF for text on every OS;
  CRLF for `.bat` and `.cmd` scripts, including mixed-case extensions.
- Preserve encoding, BOM, file permissions, and binary/test-fixture bytes.
- Write the required line endings directly. Do not run a line-ending script
  after each edit; the existing pre-commit setup normalizes affected files.
- For a known mismatch, use `pre-commit run mixed-line-ending --files path/to/file`
  from the repository root. The standard hooks handle LF and CRLF exceptions.
- Do not normalize or stage unrelated files during an ordinary coding task.
