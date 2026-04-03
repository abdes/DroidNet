# Technology Stack

**Analysis Date:** 2026-04-03

## Languages

**Primary:**
- C++20 - Main engine, modules, tools, and examples built from `CMakeLists.txt`, `src/Oxygen/**/CMakeLists.txt`, and `Examples/**/CMakeLists.txt`; Windows and Linux Conan profiles pin `compiler.cppstd=20` in `profiles/windows-msvc.ini`, `profiles/windows-msvc-asan.ini`, `profiles/Linux-clang.ini`, and `profiles/Linux-gcc.ini`
- C - Enabled alongside C++ at the root project level in `CMakeLists.txt` for platform/SDK interoperability

**Secondary:**
- HLSL - Direct3D 12 shader sources under `src/Oxygen/Graphics/Direct3D12/**/*.hlsl` and `src/Oxygen/Graphics/Direct3D12/**/*.hlsli`, compiled by `src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CMakeLists.txt`
- Python - Build/package tooling in `conanfile.py`, `src/Oxygen/Core/Tools/BindlessCodeGen/pyproject.toml`, `src/Oxygen/Cooker/Tools/PakGen/pyproject.toml`, and `tools/codemod/pyproject.toml`
- PowerShell - Bootstrap, dependency provisioning, build, and run automation in `tools/generate-builds.ps1`, `tools/cli/oxybuild.ps1`, `tools/cli/oxyrun.ps1`, `GetDXC.ps1`, `GetPIX.ps1`, `GetRenderDoc.ps1`, and `GetAftermath.ps1`
- JSON / YAML - Presets, profiles, schemas, and content specs in `CMakePresets.json`, `tools/presets/*.json`, `src/Oxygen/Cooker/Import/Schemas/*.json`, and `src/Oxygen/Cooker/Tools/PakGen/specs/*.yaml`

## Runtime

**Environment:**
- Native desktop engine/toolchain driven by CMake 3.29+ as declared in `CMakeLists.txt`
- Windows is the primary graphics runtime: Direct3D 12 code lives in `src/Oxygen/Graphics/Direct3D12/` and Windows-focused build/run helpers live in `tools/generate-builds.ps1` and `tools/cli/oxyrun.ps1`
- Cross-platform profiles exist for Windows, Linux, and macOS in `profiles/` and matching preset files exist in `tools/presets/`

**Package Manager:**
- Conan 2 - Primary C/C++ dependency manager and toolchain generator in `conanfile.py`
- pip + setuptools - Python tooling install path for `BindlessCodeGen`, `PakGen`, and `codemod` via `src/Oxygen/Core/Tools/BindlessCodeGen/pyproject.toml`, `src/Oxygen/Cooker/Tools/PakGen/pyproject.toml`, and `tools/codemod/pyproject.toml`
- Lockfile: missing; dependency resolution is profile-driven through `conanfile.py` and pip requirements files such as `src/Oxygen/Core/Tools/BindlessCodeGen/requirements.txt`

## Frameworks

**Core:**
- CMake 3.29+ - Top-level build orchestration in `CMakeLists.txt`
- Conan 2 - Dependency resolution, CMake toolchain generation, and deploy layout in `conanfile.py`
- Direct3D 12 - Main renderer backend in `src/Oxygen/Graphics/Direct3D12/CMakeLists.txt`
- SDL3 - Windowing/input platform layer in `src/Oxygen/Platform/CMakeLists.txt`
- Dear ImGui - Engine UI module and platform/render backends in `src/Oxygen/ImGui/CMakeLists.txt`, `src/Oxygen/Platform/CMakeLists.txt`, and `src/Oxygen/Graphics/Direct3D12/CMakeLists.txt`
- Luau - Scripting runtime/compiler in `src/Oxygen/Scripting/CMakeLists.txt`
- Jolt Physics - Active physics backend selected in `src/Oxygen/Physics/CMakeLists.txt` and defaulted in `tools/presets/WindowsPresets.json`

**Testing:**
- CTest - Enabled from the root in `CMakeLists.txt`
- GoogleTest - C++ unit/integration test dependency declared in `conanfile.py` and consumed across `src/Oxygen/**/Test/CMakeLists.txt`
- Google Benchmark - Benchmark dependency declared in `conanfile.py` and used in `src/Oxygen/Composition/Benchmarks/CMakeLists.txt`, `src/Oxygen/OxCo/Benchmarks/CMakeLists.txt`, and `src/Oxygen/Scene/Benchmarks/CMakeLists.txt`
- pytest - Python tool test runner configured in `pytest.ini` and invoked from `src/Oxygen/Core/Tools/BindlessCodeGen/CMakeLists.txt`

**Build/Dev:**
- DXC (DirectX Shader Compiler) - Required build-time shader compiler staged under `packages/DXC` and wired in `src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CMakeLists.txt`
- clang-format - C/C++ formatting configured in `.clang-format`
- clang-tidy - Static analysis configured in `.clang-tidy`
- clangd - Editor integration using generated compilation database in `.clangd`
- gersemi - CMake formatter configured in `.gersemirc` and pre-commit in `.pre-commit-config.yaml`
- pre-commit - Commit-time hook runner in `.pre-commit-config.yaml`
- Pyright - Python type checking in `pyrightconfig.json`
- ccache - Optional compile cache enabled through `cmake/FasterBuild.cmake`
- Doxygen - Optional API docs generation through `cmake/DoxGeneration.cmake`

## Key Dependencies

**Critical:**
- `fmt/12.1.0` - Core formatting/logging dependency from `conanfile.py`, used broadly from modules such as `src/Oxygen/Base/CMakeLists.txt` and `src/Oxygen/Console/CMakeLists.txt`
- `sdl/3.2.28` - Native window/input backend used by `src/Oxygen/Platform/CMakeLists.txt`
- `imgui/1.92.5` - UI framework wrapped by `src/Oxygen/ImGui/CMakeLists.txt`
- `glm/1.0.1` - Math library used by core/content/graphics/renderer modules such as `src/Oxygen/Core/CMakeLists.txt`, `src/Oxygen/Content/CMakeLists.txt`, and `src/Oxygen/Renderer/CMakeLists.txt`
- `asio/1.36.0` - Async/coroutine support for `src/Oxygen/OxCo/CMakeLists.txt` and `Examples/Async/CMakeLists.txt`
- `luau/0.708` - Script compiler/runtime for `src/Oxygen/Scripting/CMakeLists.txt` and `src/Oxygen/Cooker/Tools/ImportTool/CMakeLists.txt`
- `joltphysics/5.5.0` - Physics backend for `src/Oxygen/Physics/CMakeLists.txt`

**Infrastructure:**
- `nlohmann_json/3.11.3` - JSON serialization/configuration across `src/Oxygen/Console/CMakeLists.txt`, `src/Oxygen/Cooker/CMakeLists.txt`, `src/Oxygen/Graphics/Direct3D12/CMakeLists.txt`, and tooling CMake files
- `json-schema-validator/2.4.0` - Schema validation for cooker/tooling flows in `src/Oxygen/Cooker/CMakeLists.txt` and `src/Oxygen/Cooker/Tools/ImportTool/CMakeLists.txt`
- `tinyexr/1.0.12` - EXR texture import support in `src/Oxygen/Cooker/CMakeLists.txt`
- `libspng/0.7.4` - PNG import support in `src/Oxygen/Cooker/CMakeLists.txt`
- `ftxui/6.1.9` - Terminal UI for tools in `src/Oxygen/Cooker/Tools/ImportTool/CMakeLists.txt`
- `pdcurses/3.9` - Console support dependency declared in `conanfile.py`
- `magic_enum/0.9.7` - Enum reflection in `src/Oxygen/Clap/CMakeLists.txt`
- `xxhash/0.8.3` - Content hashing in `src/Oxygen/Data/CMakeLists.txt`
- `gtest/master` - Test dependency declared in `conanfile.py`
- `benchmark/1.9.4` - Benchmark dependency declared in `conanfile.py`
- Vendored sources are also present where package managers are not used: `src/Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.*`, `src/Oxygen/Graphics/External/RenderDoc/renderdoc_app.h`, and `src/Oxygen/Base/Detail/loguru.cpp`

## Configuration

**Environment:**
- Use Conan profiles in `profiles/*.ini` to select compiler, standard library/runtime, sanitizer mode, and generator settings
- Use presets in `CMakePresets.json`, `ConanPresets-Ninja.json`, `ConanPresets-VS.json`, and `tools/presets/*.json` to standardize configure/build/test flows
- Use CMake cache variables in `src/Oxygen/Graphics/Direct3D12/CMakeLists.txt` to point at optional graphics tooling SDKs: `OXYGEN_WINPIXEVENTRUNTIME_DIR`, `OXYGEN_RENDERDOC_DIR`, `OXYGEN_AFTERMATH_DIR`, and `OXYGEN_PIX_DIR`
- Use the repo-local sanitizer setting file `conan-settings_user.yml` when installing Conan user settings
- No tracked `.env`-style runtime configuration was detected; the repository is configured through profiles, presets, CMake cache entries, and local package folders instead

**Build:**
- Root build entrypoint: `CMakeLists.txt`
- Dependency recipe: `conanfile.py`
- Windows dual-tree generation: `tools/generate-builds.ps1`
- DXC package layout requirement: `packages/DXC` as documented in `README.md` and enforced by `src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CMakeLists.txt`
- Python tool editable installs: `src/Oxygen/Core/Tools/BindlessCodeGen/CMakeLists.txt` and `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt`
- Quality/tooling config: `.clang-format`, `.clang-tidy`, `.clangd`, `.gersemirc`, `.pre-commit-config.yaml`, `pytest.ini`, and `pyrightconfig.json`

## Platform Requirements

**Development:**
- Git, CMake 3.29+, Conan 2, and Python are required to configure and build the repo from `CMakeLists.txt` and `conanfile.py`
- Windows development expects Visual Studio with the Desktop development with C++ workload as documented in `README.md`
- Windows presets/profiles currently target MSVC 19.5 (`compiler.version=195`) in `profiles/windows-msvc.ini` and `profiles/windows-msvc-asan.ini`
- Linux profiles are prepared for Clang 18 and GCC 14 in `profiles/Linux-clang.ini` and `profiles/Linux-gcc.ini`
- Python 3.11 is the safest common interpreter target because `src/Oxygen/Cooker/Tools/PakGen/pyproject.toml` requires `>=3.11`, while other Python tools accept `>=3.8`
- Install DXC with `GetDXC.ps1` before building `oxygen-graphics-direct3d12-shaderbake`; install optional PIX/RenderDoc/Aftermath packages with `GetPIX.ps1`, `GetRenderDoc.ps1`, and `GetAftermath.ps1` when graphics capture tooling is needed

**Production:**
- Deployment target is native desktop binaries plus config-scoped runtime DLL deployment under `out/install/<Config>/bin` as wired by `CMakeLists.txt`, `tools/generate-builds.ps1`, and `tools/cli/oxyrun.ps1`
- The shipping renderer/tooling path is Windows Direct3D 12; examples such as `Examples/RenderScene/CMakeLists.txt` and `Examples/Physics/CMakeLists.txt` link `oxygen::graphics-direct3d12`

---

*Stack analysis: 2026-04-03*
