# Oxygen Engine CLI tools

Use the helpers from the intended Oxygen.Engine checkout. They resolve its
presets from their own location, so invoking them from a subdirectory is safe.

| Command     | Purpose                                  | Prerequisites                                                        |
| ----------- | ---------------------------------------- | -------------------------------------------------------------------- |
| `oxybuild`  | Build a CMake target                     | Initialized build tree, CMake 4.2+, configured compiler environment  |
| `oxyrun`    | Build and run an executable target       | Same as `oxybuild`; `-NoBuild` uses an existing executable           |
| `oxytidy`   | Analyze selected C++ sources and headers | Repository Python environment, LLVM 23.x tools, compilation database |
| `oxyformat` | Check or format owned C++ files          | Repository Python environment, clang-format 23.x                     |

The PowerShell launchers for oxytidy and oxyformat require PowerShell 7.3+.
`oxy-targets.ps1` contains the shared CMake target-discovery and build helpers;
it is not a separate command to run.

## Load the commands

Use the existing engine profile to define the setup and build/tool aliases:

```powershell
. ./.vscode/default-profile.ps1
Get-Alias build-tree, oxybuild, oxyrun, oxytidy, oxyformat
```

The aliases use absolute paths into this checkout and work from any directory.
`build-tree` invokes `tools/build-tree.ps1`; the other aliases invoke scripts in
`tools/cli`. Alternatively, use a script path such as
`./tools/cli/oxybuild.ps1 oxygen-base`.

```powershell
build-tree generate profiles/windows-msvc.ini -Generator Ninja
build-tree configure oxygen-ninja-default
```

The engine profile defines aliases; your user profile owns compiler setup and
automatic venv activation. Python tool launchers select this checkout's root
`.venv` regardless of the caller's active environment. Provision it with root
`uv sync --locked` or `build-tree generate`; tool invocation never installs packages.
See the [repository Python workflow](../../../../tooling/PYTHON.md).

## Build and run

Initialize dependencies with `tools/build-tree.ps1 generate <profile>`. Use
`tools/build-tree.ps1 configure <configure-preset>` for repeatable terminal
configuration, including automatic clangd preparation for Ninja. It never calls
Conan or cleans outputs. VS Code continues using its post-configure task. The
build/run helpers never install dependencies. They select only initialized trees
declared by the project/user CMake presets, then print the selected preset,
directory and config.

Without explicit constraints, choices are ranked in this order:

1. Release, RelWithDebInfo, MinSizeRel, Debug, then other configurations.
2. Ordinary builds before ASan or Tracy builds.
3. Ninja before Visual Studio, then other generators.

The order is lexicographic: a Tracy Release build precedes an ordinary Debug
build. Equal choices use preset-name order for deterministic selection. Missing
directories and disabled presets are excluded. To see all current choices:

```powershell
oxybuild -ListBuilds
oxybuild -h
oxyrun -Help
oxybuild oxygen-base
oxyrun oxygen-examples-renderscene
oxyrun oxygen-examples-renderscene -NoBuild -- --help
oxybuild oxygen-base -Preset oxygen-tracy-ninja-debug
oxybuild oxygen-base -BuildTree build-vs -Config Debug
oxybuild oxygen-base -Sanitized
oxybuild oxygen-base -DryRun
```

| Parameter     | Behavior                                                                  |
| ------------- | ------------------------------------------------------------------------- |
| `Target`      | Target name or fuzzy search pattern                                       |
| `-Preset`     | Exact build preset name                                                   |
| `-BuildTree`  | Tree name under `out`, an engine-relative path, or absolute path          |
| `-Config`     | Required configuration; never silently changes to another one             |
| `-Sanitized`  | Require an ASan Debug preset; compatible explicit constraints are allowed |
| `-NoBuild`    | Run only an already-built artifact; `oxyrun` only                         |
| `-DryRun`     | Print selection and commands without configuring, building or running     |
| `-ListBuilds` | List available presets in preference order                                |
| `-Help`, `-h` | Show usage without a target or initialized build tree                     |

Explicit options constrain the candidate set. A missing explicit tree, preset,
configuration or executable is an error; it does not redirect the command to a
different choice. Explicit cached custom trees remain supported even without a
registered preset, provided they belong to this checkout.

`oxyrun -NoBuild` skips candidates without the requested executable. The active
CMake File API index and exact configuration identify the artifact; the launcher
never substitutes a Debug executable for a Release request. For VS builds made
without File API metadata, it accepts only one matching Oxygen module filename
inside the requested configuration directory. Arguments after
`--` remain separate arguments. The selected tree's Conan runtime environment is
applied for execution and restored afterward. Build/child exit codes propagate.

The selection is resolved once and retained through configure, target discovery,
build and run. A missing or incomplete File API reply, or newer preset/toolchain
metadata, causes a configure using that selected preset. The CMake project owns
the complete IDE metadata request; the scripts do not create query files.
Native commands receive argument arrays, not shell-evaluated command text.

Interactive fuzzy matching is retained; use exact target names in automation.

## Other build-dependent tools

`BuildSelection.ps1` is the shared selector for build/run, Content cooking and
packaging, RenderScene reimport, test executable runners and runtime validation
launchers. These tools accept `-BuildTree`; generic launchers also accept
`-Config` and/or `-Preset`. Explicit `-ToolPath`/`--executable` still wins when
provided. Native content tools must exist before selection; the reimporter picks
an ImportTool/Inspector pair from the same tree and configuration.

Native workflow callers use `Resolve-OxygenExecutables` with CMake target names,
then `Invoke-OxygenTool` with argument arrays. This is also `oxyrun`'s launch
path. Workflow scripts must not concatenate executable paths, implement their
own ranking/environment rules, or launch explicit overrides directly. Optional
log capture and checked exit status belong to the shared invoker. Explicit paths
use the caller's inherited environment through that same invocation path;
unspecified companion tools are resolved beside the explicit executable.

`run-test-exes.ps1` runs one selected configuration, rather than recursively
mixing Debug and Release executables. Rendering qualification scripts retain
mandatory Debug configurations, and performance baselines retain Release. Their
tree choice still follows the common instrumentation/generator preference.
`RunManyLightBaseline.py` requires a Tracy tree when `--tracy-capture` is used
without an explicit executable, and uses the same selector through its JSON mode.

Analysis tools that explicitly select `--build-dir` or a compilation database
in `.clangd` retain that explicit input. Formatting has no build-tree dependency.

## Analyze and format

Pylance reads the engine's `pyrightconfig.json`, which inherits generated-output
and dependency exclusions from the DroidNet root `pyrightconfig.json`. Maintain
that exclusion list in the root file; it also applies when opening the full
monorepo. Python source and tests remain discoverable. Excluded directories can
still supply imports needed by source files, as Pylance normally allows.

```powershell
oxytidy src/Oxygen/Base --summary-only
oxytidy src/Oxygen/Base --list-files
oxytidy src/Oxygen/Base/Sha256.cpp --fail-on warning
oxytidy src/Oxygen/Base/Sha256.cpp --checks="-*,misc-include-cleaner"
oxyformat src/Oxygen/Base
oxyformat src/Oxygen/Base --fix
```

Use `--checks="-*,CHECK-NAME"` to run only one tidy check. See
[single-check analysis and fixes](../oxytools/docs/oxytidy.md#run-only-one-check)
for examples; omitting `-*,` keeps the configured checks enabled.

- [Oxytidy reference](../oxytools/docs/oxytidy.md): compilation contexts, header
  coverage, configuration selection, coordinated fixes, and incremental reuse.
- [Oxyformat reference](../oxytools/docs/oxyformat.md): required style, file
  selection, independent formatting, and the checking pre-commit hook.

Both use `.oxytools.json` for ownership and exclusions. Oxytidy excludes tests
unless `--include-tests` is supplied; oxyformat includes them by default.
Both launchers use the checkout's provisioned Python environment and perform no
package installation during a run. Formatting does not require a build tree.

`codemod` is an optional installed Python command for rename patches, with no
PowerShell launcher. See [codemod usage](../oxytools/docs/codemod.md).

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
`${workspaceFolder}/projects/Oxygen.Engine/.vscode/default-profile.ps1` for commands bound to that checkout. The profile discovers the engine from its own location and also loads
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
- Missing Python dependency: run `uv sync --locked` at the repository root or
  repeat `build-tree generate <profile>`.

For the scripts' parameter help and shared target resolver:

```powershell
Get-Help ./tools/cli/oxybuild.ps1 -Full
Get-Help ./tools/cli/oxyrun.ps1 -Examples
. ./tools/cli/oxy-targets.ps1
Get-Help Resolve-TargetName -Detailed
```
