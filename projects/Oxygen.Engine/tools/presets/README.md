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
.\tools\generate-builds.ps1 profiles/windows-msvc.ini -Generator Ninja -NoClean
# Generate both generators for a family (the original default):
.\tools\generate-builds.ps1 profiles/windows-msvc.ini -NoClean
.\tools\generate-builds.ps1 profiles/windows-msvc.ini -WithTracy -NoClean
.\tools\generate-builds.ps1 profiles/windows-msvc-asan.ini -NoClean

cmake --list-presets=all
cmake --preset oxygen-tracy-ninja-default
cmake --build --preset oxygen-tracy-ninja-release --target oxygen-examples-renderscene
ctest --preset oxygen-tracy-ninja-release

.\tools\cli\oxybuild.ps1 -ListBuilds
.\tools\cli\oxyrun.ps1 oxygen-examples-renderscene -BuildTree build-tracy-ninja -Config Release
```

`generate-builds` installs the supported configurations for the selected family.
`-Generator Ninja` or `-Generator VisualStudio` selects one tree; the default
`All` selects both. It stops on install/configure failure. `-NoClean`
preserves build products; omitting it retains the script's clean behavior.
For just one generator/configuration, use `conan install` directly with the
corresponding profile, build type, options and generator; it publishes the same
Oxygen presets automatically. The CLI helpers prefer Release, ordinary builds,
then Ninja, and retain that selection through build and launch.

## Shared settings

| Setting                                              | Root preset                  |
| ---------------------------------------------------- | ---------------------------- |
| Compile commands and ccache enabled                  | `oxygen-configure-defaults`  |
| Eight build jobs; non-verbose build commands         | `oxygen-build-defaults`      |
| Debug/ASan test failure output and verbose reporting | `oxygen-test-debug-defaults` |
| Release test failure output and default reporting    | `oxygen-test-defaults`       |
| Windows Jolt backend                                 | `oxygen-windows-defaults`    |
| Linux/macOS install directory and `caexcludepath`    | `oxygen-posix-defaults`      |

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

Regenerate existing trees once with `-NoClean` when migrating. Old renamed or
postprocessed Conan preset metadata is removed before native regeneration; build
products and caches are preserved. Reinstall all desired configurations in those
trees. Old `ConanPresets-Ninja.json` and `ConanPresets-VS.json` are unused.

Tooling validation uses real Conan generation without downloading dependencies
or compiling the engine:

```powershell
python -m unittest discover -s tools/presets/tests -v
```
