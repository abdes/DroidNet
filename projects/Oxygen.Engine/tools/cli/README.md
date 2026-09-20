# Oxygen Engine CLI tools

Run these commands from the Oxygen.Engine directory. The build/run scripts use
that working directory to resolve build trees and runtime dependencies.

| Command     | Purpose                                  | Prerequisites                                                        |
| ----------- | ---------------------------------------- | -------------------------------------------------------------------- |
| `oxybuild`  | Build a CMake target                     | Initialized build tree, CMake 3.29+, configured compiler environment |
| `oxyrun`    | Build and run an executable target       | Same as `oxybuild`; `-NoBuild` uses an existing executable           |
| `oxytidy`   | Analyze selected C++ sources and headers | Python 3.10+, uv, compatible LLVM tools, compilation database        |
| `oxyformat` | Check or format owned C++ files          | Python 3.10+, shared tools installed, clang-format 22.x              |

The PowerShell launchers for oxytidy and oxyformat require PowerShell 7.3+.
`oxy-targets.ps1` contains the shared CMake target-discovery and build helpers;
it is not a separate command to run.

## Load the commands

Use the existing engine profile to define all four aliases:

```powershell
. ./.vscode/default-profile.ps1
Get-Alias oxybuild, oxyrun, oxytidy, oxyformat
```

The aliases invoke the scripts in `tools/cli` directly. Alternatively, use a
script path such as `./tools/cli/oxybuild.ps1 oxygen-base`.

The profile does not select a compiler toolchain or activate a Python environment.
Start from your configured developer shell and activate your chosen environment
when needed. See the [shared Python tools setup](../oxytools/README.md).

## Build and run

Initialize the build tree separately using `tools/generate-builds.ps1` or its
batch launcher; use `./tools/generate-builds.ps1 -Help` for its arguments. The
build/run commands do not install Conan dependencies automatically. They can
configure an initialized tree when CMake File API replies are missing.

```powershell
oxybuild oxygen-base
oxybuild oxygen-graphics-common -Config Release
oxyrun oxygen-examples-async
oxyrun oxygen-examples-async -NoBuild -- --help
oxybuild oxygen-base -BuildTree build-tracy-ninja
oxybuild oxygen-base -Sanitized
oxybuild oxygen-base -DryRun
```

Arguments after `--` are forwarded to the executable by `oxyrun`.
`-DryRun` displays commands without building or running the target.

| Parameter    | Behavior                                                |
| ------------ | ------------------------------------------------------- |
| `Target`     | Required target name or search pattern                  |
| `-Config`    | Build configuration; defaults to `Debug`                |
| `-BuildTree` | Build tree name or path; defaults to `out/build-ninja`  |
| `-Sanitized` | Select the Debug-only ASan tree, `out/build-asan-ninja` |
| `-NoBuild`   | Skip building; available only on `oxyrun`               |
| `-DryRun`    | Show the commands instead of executing the build/run    |

Do not combine `-Sanitized` with `-Config`, even `-Config Debug`.
`-BuildTree build-tracy-ninja` resolves beneath `out/`;
`-BuildTree out/build-tracy-ninja` and absolute paths are also accepted.
An explicit build tree takes precedence over the default tree selection.

Target discovery uses CMake File API replies. Exact names are preferable in
scripts. Interactive use also supports substring, component, and abbreviation
matching; ambiguous matches produce a selection menu. Available matches depend
on the configured targets, so short patterns are not stable aliases.

The helpers select a matching build preset when available and otherwise invoke
`cmake --build` directly. Executable discovery uses codemodel artifacts and
fallback searches in the configured build/runtime directories. Build failures
propagate a nonzero exit code.

## Analyze and format

```powershell
oxytidy src/Oxygen/Base --summary-only
oxytidy src/Oxygen/Base --list-files
oxytidy src/Oxygen/Base/Sha256.cpp --fail-on warning
oxyformat src/Oxygen/Base
oxyformat src/Oxygen/Base --fix
```

- [Oxytidy reference](../oxytools/docs/oxytidy.md): compilation contexts, header
  coverage, configuration selection, coordinated fixes, and incremental reuse.
- [Oxyformat reference](../oxytools/docs/oxyformat.md): required style, file
  selection, independent formatting, and the checking pre-commit hook.

Both use `.oxytools.json` for ownership and exclusions. Oxytidy excludes tests
unless `--include-tests` is supplied; oxyformat includes them by default.
Oxytidy's launcher checks/installs the shared package into the selected Python.
Oxyformat's launcher uses already installed dependencies and performs no package
installation during a run. Formatting does not require a build tree.

## VS Code terminals

When Oxygen.Engine is the opened workspace folder, add a terminal profile to
`.vscode/settings.json` using the existing script:

```json
{
  "terminal.integrated.profiles.windows": {
    "Oxygen PowerShell": {
      "path": "pwsh.exe",
      "args": [
        "-NoExit",
        "-File",
        "${workspaceFolder}/.vscode/default-profile.ps1"
      ]
    }
  },
  "terminal.integrated.defaultProfile.windows": "Oxygen PowerShell"
}
```

If VS Code opens the DroidNet monorepo root instead, use
`${workspaceFolder}/projects/Oxygen.Engine/.vscode/default-profile.ps1` and change
the terminal's working directory to `projects/Oxygen.Engine` before build/run
commands. The profile discovers the engine from its own location and also loads
VS Code shell integration when applicable.

To refresh aliases in an existing engine terminal, dot-source the same profile
again. Reloading `$PROFILE` is only equivalent if your personal profile sources
this engine profile. For access outside VS Code, add a dot-source command for
this checkout's profile to your personal PowerShell profile; avoid copying its
alias definitions into another script.

## Troubleshooting

- Missing command: reload the engine profile or invoke `./tools/cli/<name>.ps1`.
- Missing build tree: initialize it first; the wrappers do not run Conan.
- Unexpected target: use its complete CMake target name and the intended
  `-BuildTree`/`-Config`.
- Missing executable with `-NoBuild`: build that target and configuration first.
- Missing Python dependency: install the shared package in the interpreter
  selected by the active environment or PATH.

For the scripts' parameter help and shared target resolver:

```powershell
Get-Help ./tools/cli/oxybuild.ps1 -Full
Get-Help ./tools/cli/oxyrun.ps1 -Examples
. ./tools/cli/oxy-targets.ps1
Get-Help Resolve-TargetName -Detailed
```
