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
