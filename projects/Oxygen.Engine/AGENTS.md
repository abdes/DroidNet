# Oxygen.Engine

Follow the repository [AGENTS.md](../../AGENTS.md) and the shared
[Oxygen engineering rules](../../design/oxygen/RULES.md).

## Task-specific guidance

Read only what applies before editing; reuse guidance already read:

- C++ changes: [coding conventions](../../.github/instructions/cpp_coding_style.instructions.md).
- Native tests: [test conventions](../../.github/instructions/unit_tests.instructions.md).
- Doxygen comments: [comment conventions](../../.github/instructions/doc_comments.instructions.md).

## Native workflow

- Run the commands below from `projects/Oxygen.Engine`, in PowerShell 7 with the
  compiler environment initialized. The full engine requires CMake 4.2+, Conan
  2.32+, Windows x64, MSVC 19.50+ (VS 2026) and C++23; reusable modules have a
  separate cross-platform contract in [cmake/README.md](cmake/README.md).
- On Windows, use only 64-bit build hosts and x64-hosted MSVC tools.
  Initialize `vcvars64.bat` or a developer shell with
  `-arch=x64 -host_arch=x64`; use `Bin\amd64\MSBuild.exe` and
  `/p:PreferredToolArchitecture=x64` for MSBuild. Never use 32-bit MSBuild or
  `Hostx86` compilers. Keep the same VS installation for shared build trees.
- Before first provisioning, check the README's [Conan setup](README.md#conan):
  contributor profiles require Oxygen's custom sanitizer settings and recipe fork.
  Conan is installed separately, not inside the shared repository `.venv`.
- Only provision missing trees/dependencies when needed:
  `./tools/build-tree.ps1 generate profiles/windows-msvc.ini -Generator Ninja`.
  Generation runs Conan and CMake, not an engine build. It preserves output;
  `-Clean` deletes the selected trees and their family's installed SDK outputs.
- For an existing tree, use
  `./tools/build-tree.ps1 configure oxygen-ninja-default`.
  This does not run Conan, deploy dependencies or clean; Ninja configuration also
  prepares clangd's configuration-specific compile databases.
- Inspect available presets with `cmake --list-presets=all`. Use `oxygen-*`, not
  raw `conan-*`, to retain project policy. Do not hand-edit generated
  `CMakeUserPresets.json` or Conan toolchains; shared defaults live in
  `CMakePresets.json`. See the [preset guide](tools/presets/README.md).
- Select builds explicitly: helpers otherwise prefer Release, ordinary builds,
  then Ninja. ASan uses `profiles/windows-msvc-asan.ini` in a separate tree and
  is Debug-only; do not toggle instrumentation in an existing ordinary tree.
- Build only the owning target, for example:
  `cmake --build --preset oxygen-ninja-debug --target Oxygen.Base.Config.Tests`.
  Then run `ctest --preset oxygen-ninja-debug -R '^Oxygen\.Base\.Config\.Tests$'`.
  Engine tests use GoogleTest, not the managed `traverse` runner; both
  `BUILD_TESTING` and `OXYGEN_BUILD_TESTS` must be enabled. CTest selects whole
  executables, not individual GoogleTest cases.
- A single case, with the selected tree's runtime dependencies:
  `./tools/cli/oxyrun.ps1 Oxygen.Base.Config.Tests -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=Suite.Case`.
- Format changed C++ before building with
  `./tools/cli/oxyformat.ps1 src/Oxygen/Base/Sha256.cpp --fix`; reserve scoped
  `./tools/cli/oxytidy.ps1 src/Oxygen/Base/Sha256.cpp --configuration Debug`
  for the pre-commit check. These consume the root `.venv` without installing.
- Preset-tooling checks without compiling the engine:
  `../../.venv/Scripts/python.exe -m unittest discover -s tools/presets/tests -v`.
  GitHub CI is intentionally disabled; select and report local validation.
