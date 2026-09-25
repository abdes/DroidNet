# Oxygen Game Engine

## Build requirements

The full engine requires CMake 4.2+, Conan 2.32+, and Windows x64 with MSVC 19.50+
(Visual Studio 2026) and C++23. See the [build contract](cmake/README.md#full-engine-build-contract) for
supported generators, configuration behavior, and focused validation commands.

Reusable module selection and embedding are described in the
[CMake helper notes](cmake/README.md#selecting-reusable-modules).
For configuration-aware clangd setup, see the [VS Code workflow](.vscode/README.md).

## Install latest VC Redistributable Package

**Optimized version crashes on Mutex machinery in the STL.**

<https://developercommunity.visualstudio.com/t/Visual-Studio-17100-Update-leads-to-Pr/10669759?sort=newest>
I’m resolving it as By Design, as explained in our release notes:

Fixed mutex’s constructor to be constexpr.
Note: Programs that aren’t following the documented restrictions on binary compatibility may encounter null dereferences in mutex machinery. You must follow this rule:
When you mix binaries built by different supported versions of the toolset, the Redistributable version must be at least as new as the latest toolset used by any app component.

You can define \_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR as an escape hatch.
That is, if you’re seeing crashes due to null dereferences in mutex locking machinery, you’re deploying a program built with new STL headers, but without a sufficiently new msvcp140.dll, which is unsupported. You need to be (re)distributing a new STL DLL too. (If a VS 2022 17.10 VCRedist has been independently installed on the machine - then everything will happen to work.)

Solve the problem based on Karel Van de Rostyne’s comment:

**The solution for this problem is:**
Download the latest Microsoft Visual C++ Redistributables and install them on
the machine that gives the problem.

On this Microsoft site you find the downloads.
<https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170>

## Shader Compilation Setup

Oxygen declares `dxc/1.9.2607` directly in Conan for both its host API/runtime
and its build-machine compiler executable. Run the normal Conan dependency
installation or `tools/generate-builds.ps1`; there is no separate DXC download.
CMake resolves only the directories supplied by that Conan graph.

ShaderBake links `dxc::dxcompiler`. Its build target stages `dxcompiler.dll` and
`dxil.dll` beside the executable; the native SDK installs both DLLs and their
license notices through dependency deployment. Shader-probe commands use the
build-context `dxc` executable. The shared runtime launcher supplies other build
and sanitizer runtime paths for the selected configuration without changing the
user's shell environment.

If DXC is missing, regenerate dependencies for the selected tree/configuration,
then reconfigure and build ShaderBake. Do not copy a compiler from another build
or add a machine-wide DXC PATH entry. For an installed SDK, retain its complete
`bin` directory. See the [ShaderBake guide](src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/README.md)
for cache and compilation commands.

$env:VIRTUAL_ENV_DISABLE_PROMPT = 1
oh-my-posh init pwsh --config E:\dev\ohmyposh-config.json | Out-String | Invoke-Expression
function AutoActivateVenv {
$venvActivate = ".venv\Scripts\Activate"
if (Test-Path $venvActivate) {
& $venvActivate
}
}

function Set-Location {
param ([string]$Path)
Microsoft.PowerShell.Management\Set-Location -Path $Path
AutoActivateVenv
}

## Python venv

cd dev/projects
python -m venv .venv

.venv/Scripts/activate

## Pre-commit

./Init.cmd

## Visual Studio

Make sure the "Desktop development with C++" workload is checked.
After installation, check for vcvarsall.bat in:

```pwsh
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\
```

## Conan

### Install prerequisites

```shell
cd dev/projects
pip install conan
git clone https://github.com/abdes/conan-center-index.git
```

### Sanitizer-aware package IDs (Conan 2)

Goal: avoid mixing ASan and non-ASan binaries in the Conan cache by making the
sanitizer a first-class setting that participates in package IDs.

#### 1) Project-owned user settings file

We keep a repo-local Conan user settings file at:

- [conan-settings_user.yml](conan-settings_user.yml)

It contains:

```text
sanitizer: [None, asan]
```

#### 2) Install the user settings into Conan home

Use Conan to install the user settings file into Conan home (this is the
recommended flow in the Conan docs):

PowerShell (one line):

```powershell
$tmp=Join-Path $env:TEMP "settings_user.yml"; Copy-Item -Path "conan-settings_user.yml" -Destination $tmp -Force; conan config install $tmp
```

Conan only reads settings_user.yml from Conan home and merges it with the
built-in settings at runtime.

When switching between ASan and non-ASan, regenerate the Conan toolchain and
reconfigure CMake in a clean build folder (CMake caches OXYGEN_WITH_ASAN).

#### 3) Add sanitizer to profiles

- In [profiles/windows-msvc-asan.ini](profiles/windows-msvc-asan.ini)
  - [settings] → sanitizer=asan
- In [profiles/windows-msvc.ini](profiles/windows-msvc.ini)
  - [settings] → sanitizer=None

#### 4) Wire the setting in the recipe

Update [conanfile.py](conanfile.py) to use the setting inside `generate()`:

- If sanitizer is asan, set `OXYGEN_WITH_ASAN=ON` and add -fsanitize=address

This makes the sanitizer a package ID dimension across all dependencies,
without requiring per-dependency options.

### Example install commands

**Preferred (recommended):** use the helper script `tools/generate-builds.ps1` to perform Conan installs and generate both Ninja (multi-config) and Visual Studio build trees with sane defaults. The script accepts a single required positional `profile` argument and resolves relative profile/output paths relative to the repository root.

```powershell
# ASan (recommended)
.\tools\generate-builds.ps1 profiles/windows-msvc-asan.ini

# Non-ASan, preserving existing build products
.\tools\generate-builds.ps1 profiles/windows-msvc.ini -NoClean

# Generate Tracy-enabled builds alongside standard builds
.\tools\generate-builds.ps1 profiles/windows-msvc.ini -WithTracy -NoClean

# Show usage
.\tools\generate-builds.ps1 -Help
```

**CLI tools (oxybuild / oxyrun / oxytidy):**

Conan generates native presets per tree. The recipe adds Oxygen presets for
installed configurations to `CMakeUserPresets.json`, inheriting both the native
presets and the shared root defaults. CMake and VS Code use the same names:
configure `oxygen-ninja-default` or `oxygen-tracy-ninja-default`, then build with
`oxygen-ninja-debug` or `oxygen-tracy-ninja-release`, for example. Run
`cmake --list-presets=all` to see initialized trees. See the
[preset guide](tools/presets/README.md) for migration, schema compatibility, and
VS Code selection. The old platform wrapper names are no longer used.

- Use `tools\oxybuild.ps1` and `tools\oxyrun.ps1` to build and run targets with convenient, preset-based workflows.
- Use `tools\cli\oxytidy.ps1` to run scoped parallel `clang-tidy` with the repo's `.clang-tidy`, `.clangd`, and CMake compile database.
- Important: these CLI helpers **do not** run Conan automatically. Initialize build roots with `tools\generate-builds.ps1` (or `tools\generate-builds.bat`).
- Build-root conventions:
  - Automatic selection uses initialized CMake presets: Release first, ordinary
    builds before ASan/Tracy, then Ninja before VS. Use `oxybuild -ListBuilds`.
  - `-BuildTree`, `-Config`, and `-Preset` constrain the selection.
- Sanitized builds details:
  - Use `-Sanitized` to require an ASan Debug build/run.
  - **Sanitized builds are always Debug.** An incompatible explicit configuration is rejected.
  - With `-Sanitized`, the available ASan Debug tree is selected using the same generator preference.

Examples:

```powershell
# Initialize build roots (ASan)
.\tools\generate-builds.ps1 profiles/windows-msvc-asan.ini

# Build and run using sanitized presets (defaults to Debug and uses asan presets)
.\tools\cli\oxybuild.ps1 MyApp -Sanitized
.\tools\cli\oxyrun.ps1 MyApp -Sanitized -- --help
```

**Advanced / manual (direct Conan):**

```shell
conan remote remove conancenter
conan remote add mycenter ./conan-center-index

cd DroidNet/projects/Oxygen.Engine

# ASan
conan install . --profile:host=profiles/windows-msvc-asan.ini --profile:build=profiles/windows-msvc-asan.ini --build=missing -s build_type=Debug --deployer-folder=out/install --deployer-package=Oxygen/0.1.0

# Non-ASan
conan install . --profile:host=profiles/windows-msvc.ini --profile:build=profiles/windows-msvc.ini --build=all -s build_type=Debug --deployer-folder=out/install --deployer-package=Oxygen/0.1.0
```

## Useful commands

```powershell
$repoRoot=$(git rev-parse --show-toplevel); git diff --name-only --cached | Where-Object { $_ -match '\.(h|cpp)$' } | ForEach-Object { $abs=Join-Path $repoRoot $_; clang-format -i $abs; Write-Output "Formatted: $abs" }
```

```powershell
$repoRoot=$(git rev-parse --show-toplevel); git diff --name-only --cached | Where-Object { $_ -match '(CMakeLists\.txt|\.cmake)$' } | ForEach-Object { $abs=Join-Path $repoRoot $_; gersemi -i $abs; Write-Output "Formatted: $abs" }
```

## CMake helper docs

1. See [cmake/README.md](cmake/README.md) for reusable CMake helper usage, including build-time JSON schema embedding.

## Developer notes: running the BindlessCodeGen CLI

The bindless codegen tool is provided as a small library and a CLI entrypoint. To avoid a Python runtime warning when running the CLI directly, prefer invoking it as a module from a clean interpreter process:

```powershell
& F:/projects/.venv/Scripts/python.exe -m bindless_codegen.cli --input <path-to-BindingSlots.yaml> --out-cpp out.h --out-hlsl out.hlsl
```

Notes:

- The package uses lazy imports for submodules (no import-time side-effects), so `python -m bindless_codegen.cli` is the recommended invocation for development. Installing a console_scripts entrypoint (via setup/pyproject) is also a convenient option for CI and developer workflows.
- If you see a RuntimeWarning from runpy about modules found in sys.modules, it means the interpreter already had the package imported; running the CLI in a fresh process will avoid that.

## Console CVar precedence

Archived CVars are provenance-aware. Live values follow this precedence ladder:

1. `AppForced`
2. `RuntimeExplicit`
3. `StartupExplicit`
4. `PersistedPreference`
5. `AppDefault`

Important rules:

- Archive load is passive. It records persisted preferences and only applies them when no higher-precedence source already owns the live value.
- App and module config objects carry operational values, not parallel provenance metadata.
- Explicit startup intent should be passed through `oxygen::console::ConsoleStartupPlan`, not by calling `SetCVarFromText()` during startup.
- Automatic archive save does not promote startup-only overrides; explicit save does, except for `AppForced` values.
