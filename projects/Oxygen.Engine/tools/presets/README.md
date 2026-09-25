# CMake presets

The root [CMakePresets.json](../../CMakePresets.json) owns shared project settings.
Conan owns each tree's native `conan-*` presets, toolchain and dependency
environments. `CMakeToolchain.presets_prefix` prevents collisions between trees;
the recipe does not rewrite generated Conan presets.

After native generation, the recipe writes the ignored `CMakeUserPresets.json`.
It includes only existing trees and adds `oxygen-*` presets inheriting the root
defaults first, then the matching native Conan preset. CMake performs inheritance;
the recipe does not copy settings, reconstruct environments, or resolve macros.
Changes to root defaults apply immediately without rerunning Conan.

This small generation step is needed because trees and configurations are optional.
Static inheritance would reference presets that might not exist, and schema 9
has no optional includes. Installing only Ninja Release works; VS, Debug, Tracy
and ASan do not need to exist. Deleted trees are removed from the include list
on the next install. `CMakeUserPresets.json` is generated: edit shared policy in
the root file. The recipe refuses to overwrite a user-owned preset file.

## Select a tree

ASan multi-config trees generate only Debug, matching their provisioned dependency
graph. This also avoids generating IDE and File API metadata for unused Release
configurations. Ordinary trees keep their existing configuration selection.

| Tree                         | Configure preset                  | Debug build/test preset         |
| ---------------------------- | --------------------------------- | ------------------------------- |
| `out/build-ninja`            | `oxygen-ninja-default`            | `oxygen-ninja-debug`            |
| `out/build-tracy-ninja`      | `oxygen-tracy-ninja-default`      | `oxygen-tracy-ninja-debug`      |
| `out/build-asan-ninja`       | `oxygen-asan-ninja-default`       | `oxygen-asan-ninja-debug`       |
| `out/build-tracy-asan-ninja` | `oxygen-tracy-asan-ninja-default` | `oxygen-tracy-asan-ninja-debug` |

Visual Studio trees use `vs` instead of `ninja`. Non-ASan trees can also provide
`release` and `relwithdebinfo` build/test presets. Only installed configurations
are exposed. A configure preset selects the tree; a build/test preset also selects
the configuration. ASan remains Debug only.

Use **CMake: Select Configure Preset**, then **CMake: Select Build Preset** in
VS Code. Select `oxygen-*` presets to get project defaults. CMake and IDEs also
list the raw `conan-*` base presets: Conan has no public option to hide them.
Those bases remain useful for inspecting dependency configuration, but bypass
project preset policy. The CLI helpers automatically select the Oxygen variants
and list each tree/configuration once.

From a development shell with the compiler environment initialized:

```powershell
# Each command is independent; run only the families you need.
# Generate only Ninja (or use -Generator VisualStudio):
.\tools\build-tree.ps1 generate profiles/windows-msvc.ini -Generator Ninja
# Generate both generators for a family (the original default):
.\tools\build-tree.ps1 generate profiles/windows-msvc.ini
.\tools\build-tree.ps1 generate profiles/windows-msvc.ini -WithTracy
.\tools\build-tree.ps1 generate profiles/windows-msvc-asan.ini

cmake --list-presets=all
.\tools\build-tree.ps1 configure oxygen-tracy-ninja-default
cmake --build --preset oxygen-tracy-ninja-release --target oxygen-examples-renderscene
ctest --preset oxygen-tracy-ninja-release

.\tools\cli\oxybuild.ps1 -ListBuilds
.\tools\cli\oxyrun.ps1 oxygen-examples-renderscene -BuildTree build-tracy-ninja -Config Release
```

`build-tree generate <profile>` installs the supported configurations for the selected family.
`-Generator Ninja` or `-Generator VisualStudio` selects one tree; the default
`All` selects both. Existing output is preserved by default. Explicit `-Clean`
removes the selected build trees and their family's SDK configuration directories
before generation. Conan's cache and other families are preserved.
`-UiTests` defaults to enabled for developer generation, including Release.
Use `-UiTests:$false` when provisioning an uninstrumented build. This selects
the matching ImGui dependency variant and must agree across every configuration
in the tree; it cannot be changed with a configure-only CMake override.
Conan consumers still default to `ui_tests=False`.
After each successful Ninja configuration, the script prepares clangd's
per-configuration databases automatically and stops if preparation fails.

`build-tree configure <configure-preset>` runs CMake once, then prepares clangd
for Ninja. It does not call Conan, deploy dependencies or remove output, so it
can be repeated after editing CMake files. It does not accept generation options.
Use `-Define 'NAME=VALUE','OTHER:BOOL=OFF'` for explicit local cache overrides;
the normal preset defaults apply again on the next configure without overrides.
Disabling `CMAKE_EXPORT_COMPILE_COMMANDS` explicitly skips database preparation.

| Generate option    | Default   | Purpose                                                       |
| ------------------ | --------- | ------------------------------------------------------------- |
| `-Generator`       | `All`     | Select Ninja, VisualStudio, or both.                          |
| `-DependencyBuild` | `missing` | Conan `--build` policy; `never` requires cached dependencies. |
| `-WithTracy`       | Off       | Select the separate Tracy family.                             |
| `-Clean`           | Off       | Explicitly reset the selected family/generator outputs.       |

ASan selection comes from the resolved profile. The same profile is used for
host and build for this native contributor workflow. SDK roots and the Oxygen
deployer are owned by the tool; neither has a CLI override. `-Help` works without
Conan, CMake or Python installed. Required tools and profile resolution are checked
before cleanup. Compiler environment setup remains the caller's responsibility.
For just one generator/configuration, use `conan install` directly with the
corresponding profile, build type, options and generator; it publishes the same
Oxygen presets automatically. The CLI helpers prefer Release, ordinary builds,
then Ninja, and retain that selection through build and launch.

The Windows profiles own the ordinary/ASan Conan package-identity configuration.
Direct installs and `build-tree generate` therefore resolve the same dependency IDs
for equivalent inputs. The wrapper reads Conan's resolved profile, including
profile inheritance; it no longer injects a separate sanitizer identity or
overrides the profile's ASan option. Use the ASan profile rather than setting
`with_asan=True` against an ordinary dependency graph. The recipe rejects that
inconsistent combination. Header-only packages can still share their cache entry.

Oxygen explicitly uses Conan's `CMakeConfigDeps` in its recipe for both entry
points. The wrapper does not globally replace generators in upstream recipes.
`CMakeConfigDeps` is experimental in the minimum supported Conan 2.32; qualify
Conan upgrades against the preset and SDK consumer checks. Both it and `CMakeDeps`
support multi-config builds.

Each generated tree records its generator, target-platform/compiler settings,
runtime linkage, Oxygen shared/static mode and instrumentation. An incompatible
install fails before replacing its toolchain or deploying dependencies. Debug,
Release and RelWithDebInfo can share a multi-config tree; their build type and
MSVC Debug/Release runtime variants are deliberately excluded from this check.
Module selection, awaiter checking and optional-output recipe options must also
agree across configurations sharing that tree; CMake has one configure-time
choice for each. A single-configuration tree can still update those options.
Local disabling of optional outputs through CMake remains supported.

To change that identity, use `build-tree generate <profile> -Clean` with the
intended `-Generator` and `-WithTracy` selection. With direct Conan commands, remove the selected build tree
and repeat its installs. Older trees without an identity record also require
this one-time regeneration because their compatibility cannot be verified.
Other trees do not need to be removed. A partially provisioned tree is valid;
installing another configuration with the same identity extends it normally.

Generator selection follows Conan's resolved configuration, not the presence of
VS Code. The existing Ninja Multi-Config and Visual Studio names above remain
unchanged. Plain Ninja uses `build-ninja-single/{Config}` and its own preset
namespace, so it can coexist with Ninja Multi-Config.

## SDK destinations

Ordinary builds install to `out/install/{Config}`; Tracy builds install to
`out/install-tracy/{Config}`. ASan uses `Asan` as the configuration directory
in the corresponding root. Dependency deployment, CMake installation and runtime
lookup use that same selected root. Cleaning a Tracy family leaves the ordinary
SDK intact, and vice versa. Ninja and Visual Studio of the same family continue
to share its SDK destination.

The wrapper has no deployment-root override. Copy the completed SDK when it is
needed elsewhere. For equivalent direct Conan dependency deployment, pass
`--deployer-package="oxygen/*" --deployer-folder=out/install` for ordinary
builds, or `--deployer-folder=out/install-tracy` when `with_tracy=True`.

ASan keeps the existing Debug configuration and `out/install/Asan` deployment.
TinyEXR retains threading but disables OpenMP specifically in ASan builds because
MSVC does not support that combination. Ordinary TinyEXR options are unchanged.
The first use of the changed TinyEXR option may require a new dependency binary.

## Shared settings

| Setting                                              | Root preset                  |
| ---------------------------------------------------- | ---------------------------- |
| Compile commands and ccache enabled                  | `oxygen-configure-defaults`  |
| Eight build jobs; non-verbose build commands         | `oxygen-build-defaults`      |
| Debug/ASan test failure output and verbose reporting | `oxygen-test-debug-defaults` |
| Release test failure output and default reporting    | `oxygen-test-defaults`       |
| Windows Jolt backend                                 | `oxygen-windows-defaults`    |
| Linux/macOS `caexcludepath`                          | `oxygen-posix-defaults`      |

RelWithDebInfo tests retain Conan's original output defaults. Native test
parallelism and runtime environments are inherited. These policies apply to the
Oxygen presets used by CMake, CTest, VS Code and the CLI helpers. Conan's separate
`conan build`/`conan create` package workflow uses the recipe's CMake helper and
does not interpret project preset inheritance.

The root and generated user file use schema **9**, supported by
VS Code CMake Tools **1.20.52+**. Oxygen requires **CMake 4.2+** for its full
toolchain contract, including VS 2026; schema 9 alone only requires CMake 3.30.
Conan's native files retain their own schema.
CMakeUserPresets implicitly includes the root; no platform include hierarchy or
include cycle is needed. This uses normal [CMake preset inheritance](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
and the [Conan extension pattern](https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/extend_own_cmake_presets.html),
with the small availability-driven generation step described above.

The top-level CMake project requests complete File API replies (codemodel,
cache, CMake files and toolchains). A CLI configure therefore supplies the same
metadata VS Code needs; old partial replies need only a configure to refresh.

The old `generate-builds.ps1` entry point has been replaced by `build-tree.ps1
generate`. Replace `-Build` with `-DependencyBuild`; omit the former `-NoClean`
flag because preservation is now the default. Existing B12 identity records and
native presets remain valid; this CLI rename does not require cleaning them.
Trees older than B12 still require the explicit regeneration described above.

Tooling validation uses real Conan generation without downloading dependencies
or compiling the engine:

```powershell
python -m unittest discover -s tools/presets/tests -v
```
