# CMake Helper Notes

## Full-engine build contract

Minimum tools: **CMake 4.2 and Conan 2.32**. The root CMake project and presets
enforce the same CMake minimum; the recipe declares its Conan minimum. CMake 4.2
is the first release with the Visual Studio 18 2026 generator. CI exercises only
the minimum versions, pinned to CMake 4.2.0 and Conan 2.32.0.

The supported full-engine target is **Windows x64 with MSVC 19.50 or newer**
(Visual Studio 2026, v145 or newer). Supported generators are `Ninja`,
`Ninja Multi-Config`, and `Visual Studio 18 2026`. CMake and Conan reject unsupported
target platforms and compiler versions before configuring engine dependencies or
generating build files, respectively. This contract does not remove portable
source modules; existing Linux/macOS profiles are not full-engine support claims.

Oxygen requires C++23. Keep language requirements on targets through
`target_compile_features()`; the recipe also validates `compiler.cppstd >= 23`.
The Windows profiles select `Oxygen/*:compiler.cppstd=23` while retaining C++20
as their dependency default. Dependencies that erase the standard setting from
their package identity continue to do so. This avoids changing dependency
language modes simply to correct Oxygen's own settings.

For a single-config generator, an absent or empty `CMAKE_BUILD_TYPE` defaults to
Debug. Explicit values are preserved on initial configuration and reconfiguration.
Multi-config generators retain their configured list of configurations and select
the active configuration at build time. The supplied development workflows install
Debug, Release and RelWithDebInfo dependencies; ASan remains Debug-only. Selecting
another configuration requires matching dependency artifacts and is not certified
by those workflows.

The policy is implemented in `ToolchainRequirements.cmake` and
`BuildConfiguration.cmake`, included by the engine root after compiler detection.
`tools/cmake/tests/test_build_contract.py` exercises the real helpers, builds and
runs a C++23 consumer with each generator, rejects a real Win32 target, and checks
the Conan recipe through dependency-free fixtures. Run it in an x64 VS 2026
developer shell:

```powershell
python -m unittest discover -s tools/cmake/tests -v
```

Set `OXYGEN_TEST_CMAKE` to an absolute CMake executable path to exercise another
tool version. These focused checks do not replace full-engine build/runtime tests.

Tool versions used to build dependencies remain dependency-specific. For example,
a recipe's build-context CMake requirement below version 4 does not lower the
minimum for configuring Oxygen itself.

## Selecting reusable modules

The reusable set is `Base`, `Composition`, `OxCo`, `Serio`, `TextWrap`, and `Clap`.
These modules support Windows, Linux and macOS; the full-engine Windows/MSVC
restriction is applied only when building the full engine. Public target compile
features determine module language requirements (currently C++23 through Base).
CMake 4.2 remains the common infrastructure minimum.

Validation of this infrastructure change covers Windows module builds and Linux
configuration. Linux C++ compilation has existing source portability blockers;
macOS execution has not been exercised. Those limits do not change the intended
three-platform module contract.

A standalone configure without `OXYGEN_MODULES` builds the full engine. An
embedded configure must select modules explicitly:

```cmake
# The parent's Conan graph supplies fmt (header-only), Asio and/or magic_enum
# as required by the selected modules. Oxygen does not run Conan itself.
set(OXYGEN_MODULES Clap OxCo)
add_subdirectory(external/Oxygen.Engine oxygen)
target_link_libraries(my_app PRIVATE oxygen::clap oxygen::oxco)
```

This selects Clap, OxCo, TextWrap and Base. It does not configure graphics,
physics, ShaderBake, or unrelated engine modules. The other five modules require Base;
Clap additionally requires TextWrap. Unknown names fail configuration.
Consumers can select an equivalent module-only standalone build with
`-DOXYGEN_MODULES="Clap;OxCo"`.

Embedded tests, examples, docs, optional tools, benchmarks and installation default
off. Parent normal variables override cache defaults using native `option()`
semantics. Oxygen preserves the parent's install prefix, global IDE folder policy,
build configuration and explicitly provided output directories. Otherwise its
outputs default inside its own binary subtree. Its private helpers cannot be
shadowed by a parent's same-named helper files. `OXYGEN_PROJECT_SOURCE_DIR` names
the engine project root; `OXYGEN_SOURCE_DIR` continues to name its `src` directory.
Compiler caching for embedded modules is configured by the parent through
`CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER`; `OXYGEN_USE_CCACHE`
is a standalone option and is rejected when embedded.

Composition's existing `oxygen::cs-init` target remains available in static builds
for applications that need its cross-module registry initialization contract.
Source-module support does not imply that the existing Conan package metadata or
native installed exports are complete; those are separate workstream items B06/B07.

## Compiler policy and instrumentation

`CompilerPolicy.cmake` applies private build policy to all compiled targets in
Oxygen's source and example directories, including auxiliary libraries, tools,
tests and benchmarks. MSVC uses `/W4`; GCC/Clang use `-Wall -Wextra`. These warning
levels are mandatory within Oxygen and do not propagate to external consumers.
Ready modules opt individual targets into warnings-as-errors using native CMake:

```cmake
set_property(TARGET my_module PROPERTY COMPILE_WARNING_AS_ERROR ON)
```

Oxygen compiles without native C++ RTTI and retains its own type system. Consumer
translation units deriving from Oxygen polymorphic classes must also disable
native RTTI (`/GR-` or `-fno-rtti`). Separate those from code using native RTTI for
unrelated classes. Linking Oxygen does not automatically disable RTTI throughout
the consuming application. Public language features and header requirements
(including Windows `NOMINMAX`) remain explicit usage requirements.

MSVC runtime selection comes from Conan's `CMAKE_MSVC_RUNTIME_LIBRARY`; Oxygen
does not substitute its own runtime switch. PIC uses `POSITION_INDEPENDENT_CODE`.
Portable static/object libraries default to PIC unless Conan or the parent has
set the property through native CMake policy.

The existing ASan profiles and separate Debug build trees remain the workflow.
ASan compile options are private; static/object libraries carry the sanitizer's
required link options to their consumers. Oxygen targets disable MSVC runtime
checks and incremental linking and use embedded debug information for ASan,
without overwriting unrelated compiler/linker cache entries. GoogleTest discovery
and CTest share a target launcher that resolves DLLs from the built targets, the
selected deployment (`Asan` for ASan builds), and the active MSVC compiler directory.
Tests run from their executable directory, without requiring an ordinary Debug
installation. An application embedding instrumented modules owns its own
compatible compiler settings and runtime launch environment.

Coverage instrumentation is no longer part of the build. The former Conan/CMake
options and Linux coverage profile have been removed.

Native compile/run tests live in `tools/cmake/tests/test_compiler_policy.py`.
They exercise private warning policy, module-local errors, RTTI isolation, PIC,
ASan static/shared linking, discovery and deliberate memory-error detection.
Enable `OXYGEN_RUN_CONAN_GRAPH_INTEGRATION=1` to additionally compare cached Conan
graphs using `test_conan_instrumentation.py`. Graph identity is checked separately
from actual compiler/runtime instrumentation.

## Conan defaults and local narrowing

`conan install` generates the dependency graph, toolchain and presets; it does not
configure CMake. A normal `cmake --preset oxygen-...` reapplies explicit recipe
defaults, including OFF values. A local configure may explicitly disable
provisioned tests/examples/docs/optional tools/benchmarks:

```powershell
cmake --preset oxygen-ninja-default -DOXYGEN_BUILD_TESTS=OFF
```

The next normal preset configure restores the recipe default. `cmake --build`
uses the last configured state. Enabling outputs the recipe disabled, changing
shared/static linkage, ASan or Tracy against the resolved graph, or
narrowing the contents of a Conan package build is an error. Change recipe options
and rerun Conan instead. A noncached native Conan toolchain block carries graph
expectations so changing recipe options cannot leave stale validation facts.

Mandatory code-generation tools remain required even with `OXYGEN_BUILD_TOOLS=OFF`.
The existing dependency graph is not yet pruned for disabled optional outputs;
that recipe work remains B07. Source-embedded consumers own their own dependency
graph and need only the selected modules' dependencies.

## Embedded JSON Schemas

Use `cmake/JsonSchemaHelpers.cmake` to embed JSON schema files into generated C++ headers at build time.

The public API is:

```cmake
include(JsonSchemaHelpers)

oxygen_embed_json_schemas(
  TARGET <target>
  OUTPUT_HEADER <path-to-generated-header>
  NAMESPACE <c++ namespace>
  INCLUDE_BASE_DIR <include-root-added-to-target>
  CHUNK_SIZE <max chars per raw-string chunk>
  SCHEMAS
    <symbol_name_1> <schema_file_1>
    <symbol_name_2> <schema_file_2>
)
```

Arguments:

1. `TARGET` is the module target that consumes the generated header.
2. `OUTPUT_HEADER` is where the generated `.h` is written.
3. `NAMESPACE` is the C++ namespace used in the generated header.
4. `INCLUDE_BASE_DIR` is added `BEFORE` to the target include dirs.
5. `CHUNK_SIZE` controls raw-string chunk size for compiler compatibility.
6. `SCHEMAS` is an alternating `<symbol_name> <schema_file>` list.

Implementation split:

1. `JsonSchemaHelpers.cmake` is configure-time API and build graph wiring.
2. `GenerateEmbeddedJsonSchemas.cmake` is build-time code generation backend.

Why split:

1. Correct incremental rebuild behavior when schema files change.
2. Cleaner `add_custom_command` invocation without large inline script logic.
3. More robust escaping/quoting across generators.

Current example:

1. Source schemas:
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
2. Generated header path: `out/<build>/generated/Oxygen/Cooker/Import/Internal/ImportManifest_schema.h`
3. Include in C++ remains: `<Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>`

Minimal module usage example:

```cmake
include(JsonSchemaHelpers)

oxygen_embed_json_schemas(
  TARGET ${META_MODULE_TARGET}
  OUTPUT_HEADER
    "${CMAKE_BINARY_DIR}/generated/Oxygen/Cooker/Import/Internal/ImportManifest_schema.h"
  NAMESPACE "oxygen::content::import"
  INCLUDE_BASE_DIR
    "${CMAKE_BINARY_DIR}/generated"
  CHUNK_SIZE 8192
  SCHEMAS
    kImportManifestSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.import-manifest.schema.json"
    kInputSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.input.schema.json"
    kInputActionSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.input-action.schema.json"
    kPhysicsSidecarSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.physics-sidecar.schema.json"
)
```

Conventions:

1. Keep schema source files under the owning module, for example `Import/Schemas/`.
2. Keep module control in module `CMakeLists.txt` by calling `oxygen_embed_json_schemas(...)` there.
3. Keep generation logic centralized under `cmake/`.
4. Prefer stable symbol names (`k...Schema`) and canonical schema filenames (`*.schema.json`).
