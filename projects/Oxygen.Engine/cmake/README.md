# CMake Helper Notes

## Module declarations, diagnostics and IDE headers

`asap_module_declare(MODULE_NAME Oxygen.Base DESCRIPTION "...")` declares
metadata, not targets. Callers retain native `add_library`, `add_executable`,
`target_sources` and file-set declarations. Dotted module names derive stable
lowercase target names (`oxygen-base`) and link aliases (`oxygen::base`). Unknown
arguments, missing values, malformed names and collisions with existing targets
fail configuration. Quoted descriptions may contain spaces and semicolons.
The unused `MODULE_TARGET_NAME` override and `WITHOUT_VERSION_H` switch have been
removed, together with the unused version/export-header generators. Keep the
existing checked-in export headers and Core's product version/provenance API.

Use the existing `declare`, `asap_push_module`, child configuration,
`asap_pop_module` sequence. Hierarchy state is inherited by child directories;
helper inclusion does not reset it. Empty or mismatched pops fail explicitly.
Helper temporaries do not overwrite caller variables. Hierarchy and target
messages remain visible at the normal CMake log level.

`arrange_target_files_for_ide(target [EXCLUDE_PATTERNS regex...])` must be called
in the directory defining a local, non-alias target, after its source/header
declarations. It groups declared source files, header sets and generated headers
under `src`, `generated` or `external`, without adding files to compilation.
Its separate automatic advisory inventory scans `.h`, `.hpp`, `.c` and `.cpp`
files at configure time. It excludes `Test`, `Benchmarks`, `Examples` and `Tools`
directory segments and any explicit exclusion patterns (case-insensitive).
Scripting's existing whole-`Bindings/` exclusion is intentionally retained;
[issue #17](https://github.com/abdes/DroidNet/issues/17) tracks its review.
The inventory does not watch new files or replace explicit source lists: rerun
configuration to audit a newly added file. Warnings never add or delete files.

Inventory recognizes literal paths and single literal paths wrapped by
`$<0:path>`, `$<1:path>`, `$<BUILD_INTERFACE:path>` or a single `BOOL`,
`PLATFORM_ID` or `CONFIG` guard, such as `$<$<BOOL:${WIN32}>:Platform_win.cpp>`.
An inactive guarded path still counts as declared. Other expressions produce an
explicit incomplete-inventory diagnostic; the helper does not pretend to evaluate
arbitrary nested expressions. Multi-path expression payloads are not supported.

For header-only libraries, list headers directly on the native interface target
as well as exposing the appropriate header file set. OxCo and Config already do
this. For Visual Studio, the IDE helper sets `VS_TOOL_OVERRIDE=ClInclude` on their
declared `.h`/`.hpp` files unless an explicit override already exists. CMake
otherwise classifies interface-library files as generic `None` items. This keeps
the headers explicitly represented as C++ header items in both the native project
and its filters, with the dotted module name as the project display label.
An `INTERFACE`-only file-set declaration does not put the headers in the native
project's own source list. The old artificial targets used an undefined
`header_files` variable, yielding empty projects; the native targets replace them.
This changes IDE representation only, without adding compilation or link steps.
See [CMake's item-type override](https://cmake.org/cmake/help/latest/prop_sf/VS_TOOL_OVERRIDE.html)
and [Microsoft's project/filter contract](https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-filters-files).

Dotted build shortcuts remain explicit and local, as in ShaderBake:

```cmake
add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
```

This command-free dependency target lets either build name select the same
artifact/configuration. A CMake `ALIAS` alone is not a build-tool entry point.
Keep the executable output in a binary subdirectory, as Oxygen does, so a POSIX
executable basename cannot collide with a same-named Ninja custom target at the
build root.
No global shortcut registry or auxiliary-target renaming is introduced.

`test_build_helpers.py` checks metadata, all production declaration preambles,
hierarchy isolation, argument errors, inventory exclusions and generated Visual
Studio header projects. Set `OXYGEN_RUN_HELPER_BUILD_TESTS=1` to additionally build
and run a C++23 shortcut fixture with Ninja, Ninja Multi-Config and Visual Studio
on Windows, checking unchanged object/executable timestamps across both names.
The contract CI job enables these tests on the minimum CMake version.

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
The Windows profiles select `oxygen/*:compiler.cppstd=23` while retaining C++20
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

## Consume Oxygen through Conan

The Conan reference is `oxygen/0.1.0` (lowercase, as required by Conan).
The native CMake package remains `Oxygen` and its targets remain `oxygen::...`.
Update external profiles that still use the former `Oxygen/*` package pattern.

No module selection builds the full engine, including its four core cooker
tools, ShaderBake, RenderScene, DemoShell, schemas and showcase content. To build
only reusable modules, set `oxygen/*:modules=OxCo,Clap`, for example. Supported
names are `Base`, `Composition`, `OxCo`, `Serio`, `TextWrap` and `Clap`.
Dependencies are added automatically: this example builds Base, OxCo, TextWrap
and Clap, and requires only fmt, Asio and magic_enum. Order and redundant names
do not create different package IDs. The former inert `base` and `oxco` Boolean
options have been replaced by this explicit selection.

`shared=True` is the Conan default. Full-engine packages require shared libraries
because the showcase dynamically loads its graphics backend DLL. The six reusable
modules also support `shared=False`. This package restriction does not remove
static-library development through CMake; a fully static showcase needs a separate
loader design and is not advertised as a supported SDK configuration.

An application recipe can declare:

```python
requires = "oxygen/0.1.0"
default_options = {"oxygen/*:modules": "OxCo,Clap"}
generators = "CMakeToolchain", "CMakeDeps"
```

Its CMake project uses the same interface as a native SDK consumer:

```cmake
find_package(Oxygen CONFIG REQUIRED COMPONENTS OxCo Clap)
target_link_libraries(my_game PRIVATE oxygen::oxco oxygen::clap)
```

Use a C++23-capable toolchain. The six reusable modules support Windows, Linux
and macOS; the full engine retains its Windows x64/MSVC baseline. Ordinary Conan
consumers need no Oxygen-specific `settings_user.yml`. Dependencies stay in the
Conan graph; use Conan's run environment for packaged tools. The separately
assembled `out/install/<Config>` SDK remains the self-contained distribution.

Consumer defaults disable tests, benchmarks, optional examples, documentation
and optional development tools, including when Conan builds Oxygen from source.
`tools/generate-builds.ps1` explicitly enables those contributor outputs, retaining
the repository development workflow. They can also be enabled individually with
recipe options. CPU benchmarks can be built without unit tests. Vortex's GPU
benchmark workloads still require both tests and benchmarks because they share
the GoogleTest exposure fixtures. These development executables are not installed.

`awaitable_state_checker=auto` enables OxCo checking in Debug, including ASan,
and disables it in Release and RelWithDebInfo. Explicit `True` and `False`
override the default. The equivalent CMake setting is
`OXYGEN_AWAITER_STATE_CHECKER=AUTO|ON|OFF`. Conan prevents a CMake override from
disagreeing with the selected recipe option. A generated `Oxygen/OxCo/Config.h`
records the built mode, so installed headers remain consistent with the binaries
even when the consuming application uses a different configuration name. Change
the build option instead of defining checker macros in individual source files.

The effective checker mode and canonical module closure participate in package
identity. ASan still requires matching compiler flags and dependency identity
configuration from the ASan profile; enabling `with_asan` alone is rejected.

Native CMake exports are authoritative. Conan component metadata is derived from
the configured targets and CMake File API, including actual library names,
definitions, system libraries and dependency edges. `test_package` compiles and
runs an external application against the created package, independently of the
engine's development test suite.

## Product version and source provenance

`VERSION` is the single product-version authority for CMake, Conan and the runtime
API. It contains canonical decimal `major.minor.patch`, with each component in
0..255 to match the existing `std::uint8_t` numeric accessors. Invalid versions
fail explicitly. `asap_version_read(VERSION_FILE ...)` accepts an absolute path
or a path relative to the calling source directory; omitting the argument uses
`VERSION`. Editing the selected file triggers CMake reconfiguration.

Oxygen.Engine is one component within the repository. Its revision is the last
commit affecting its directory, including its build files, tooling and docs.
Unrelated sibling-project commits do not change it. Local modifications and
relevant untracked files mark it dirty; Git-ignored outputs and the uncommitted
`plans/CMAKE_CONAN_MODERNIZATION_PLAN.md` are excluded. There are currently no
shared repository build files outside the engine directory in this scope. If
that changes, update both the CMake and Conan scope and their parity tests.

The existing string API reports:

| Function        | Clean source                            | Modified source                               |
| --------------- | --------------------------------------- | --------------------------------------------- |
| `Version()`     | `0.1.0`                                 | `0.1.0`                                       |
| `VersionFull()` | `0.1.0 (<full component commit>)`       | `0.1.0 (<full component commit>-dirty)`       |
| `NameVersion()` | `Oxygen v0.1.0 (<12-character commit>)` | `Oxygen v0.1.0 (<12-character commit>-dirty)` |

Missing or unverifiable provenance is `unknown`, never revision zero or a clean
claim. Shallow checkouts conservatively report unknown; fetch full history
explicitly when component provenance is needed. CMake never fetches automatically.
Rebasing or committing inside the component scope may change the revision even
when compiled behavior is unchanged; it is a source-history identifier.

Core generates `version/include/Oxygen/Core/version-info.h` and the corresponding
`version/oxygen-source.json` within its binary directory. A small dependency of
Core checks provenance at each Core build, writes only changed content, and lets
normal header dependencies recompile the version implementation when needed.
Separate build trees cannot overwrite each other's header. The legacy ignored
source-tree header is neither preferred by Core nor exported by Conan.

MSVC static Core carries `/INCREMENTAL:NO` as a link requirement for its final
consumers. Validation reproduced the incremental linker retaining an old revision
even though the generated header, object and archive already contained the new
one. Full linking avoids that stale binary. This affects link time when those
consumers need relinking; unchanged builds do not relink. Shared Core and unrelated
targets retain their existing link policy.

Local `conan install` does not freeze the checkout's provenance: subsequent builds
observe current source state. Conan export captures deterministic metadata under
the schema in `cmake/oxygen-source.schema.json`, alongside both the recipe and
exported sources. Cache builds use that capsule, without Git lookup. Captured
metadata takes precedence over a surrounding application's repository and must
match the product version. The package/native SDK retains the capsule under
`share/oxygen/oxygen-source.json`. Conan keeps its normal content-derived recipe
and package revisions; no commit-hash package option is added.

Local dirty packages are allowed and identified. B04 adds no official-release
mode or publication gate. Release automation must eventually apply its own policy
to this factual metadata; CMake `Release` is only a build configuration.

`test_source_provenance.py` runs script/Git/export checks without invoking a
compiler. Its native incremental, runtime and install tests are opt-in:

```powershell
$env:OXYGEN_RUN_PROVENANCE_BUILD_TESTS = '1'
python -m unittest discover -s tools/cmake/tests -p test_source_provenance.py -v
```

Obtain build permission first on shared hosts. The native fixtures build the
production version implementation sequentially, without engine dependencies.

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
without overwriting unrelated compiler/linker cache entries. CTest uses a target
launcher that resolves DLLs from the built targets, the
selected deployment (`Asan` for ASan builds), and the active MSVC compiler directory.
Tests do not require an ordinary Debug installation. An application embedding instrumented modules owns its own
compatible compiler settings and runtime launch environment.

Coverage instrumentation is no longer part of the build. The former Conan/CMake
options and Linux coverage profile have been removed.

Native compile/run tests live in `tools/cmake/tests/test_compiler_policy.py`.
They exercise private warning policy, module-local errors, RTTI isolation, PIC,
ASan static/shared linking, test execution and deliberate memory-error detection.
Enable `OXYGEN_RUN_CONAN_GRAPH_INTEGRATION=1` to additionally compare cached Conan
graphs using `test_conan_instrumentation.py`. Graph identity is checked separately
from actual compiler/runtime instrumentation.

## Test execution

Oxygen tests require both `BUILD_TESTING=ON` and `OXYGEN_BUILD_TESTS=ON`.
Global OFF disables Oxygen tests without overwriting the cached Oxygen choice.
Oxygen OFF leaves an embedding parent's tests alone. Local disabling is allowed;
Conan cache-package builds must still match the recipe's requested options.
Oxygen always enables directory-level CTest inventory generation, so disabling
tests in an existing tree removes stale Oxygen registrations. The generic `test`
target can therefore remain available with no Oxygen tests. An embedding parent
owns its root-level `enable_testing()` and the registration of its own tests.

Build the desired test targets before invoking CTest. Each GoogleTest executable
has one CTest entry, using its existing executable name. There is no build-time
GoogleTest case discovery or second per-case CTest run. For example:

```powershell
cmake --build --preset oxygen-ninja-debug --target Oxygen.Base.Config.Tests
ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Base\.Config\.Tests$' --output-on-failure
```

CTest reports each executable's result; GoogleTest's output identifies its cases.
For a single case, run the executable directly with `--gtest_filter=Suite.Case`
in an environment that provides its runtime dependencies. Whole-executable tests
retain CTest's default working directory (the module's binary directory).

Executables using a real GPU declare `GPU` in `gtest_program` or
`m_gtest_program`. They share the `oxygen_gpu` CTest resource lock: within one
CTest run, only one such executable runs at a time, while CPU tests can run in
parallel. This does not coordinate separate CTest runs or other applications.
Mocked graphics tests do not need the lock.

Benchmark targets remain available through their explicit executables/scripts
but are excluded from CTest. GoogleTest-based benchmarks use `NO_TEST` in
`gtest_program`. Loader retains four distinct initialization entries because
each argument combination must exercise singleton first access in a fresh process.
Bindless generated-header compilation remains a mandatory Core build prerequisite
and an explicit build target; CTest runs its Python tests without invoking a build.

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

## API documentation

Documentation uses locally installed Doxygen 1.14 or newer and Graphviz `dot`.
Both `OXYGEN_BUILD_DOCS` and `OXYGEN_WITH_DOXYGEN` must be ON; missing required
tools are configuration errors only when documentation setup is requested.
Conan's `docs=True` option allows documentation targets but does not install
Doxygen/Graphviz or automatically generate/package HTML. The contributor
`tools/generate-builds.ps1` workflow already sets that recipe option. If your
current Conan graph has `docs=False`, regenerate it with `docs=True` before
enabling documentation in CMake.

```powershell
cmake --preset oxygen-ninja-default -DOXYGEN_BUILD_DOCS=ON -DOXYGEN_WITH_DOXYGEN=ON
cmake --build --preset oxygen-ninja-debug --target dox
# Or request one opted-in module:
cmake --build --preset oxygen-ninja-debug --target oxygen-base_dox
```

Visual Studio exposes the same `dox` and `<module>_dox` targets. Documentation
remains an explicit action and is not part of the default build. Existing
standalone module opt-ins are preserved. CMake's `DOXYGEN_EXECUTABLE` and
`DOXYGEN_DOT_EXECUTABLE` cache entries can select specific installed executables.

Each generated `<module>.Doxyfile` lives in that module's binary directory.
Inputs, examples, and displayed source paths are explicit; running from a build
directory therefore does not change the documented sources. HTML stays under
`<build-dir>/dox/<module>/html`. Unchanged configure runs preserve Doxyfile timestamps.

Warnings remain visible and nonfatal. Each module target prints its current
warnings and writes `module_warnings.txt` alongside its HTML output directory.
After a successful aggregate `dox` build, `dox/doxygen_warnings.txt` contains the
current registered modules' reports, without accumulating previous runs or
including leftover reports from modules removed from the configuration.

The theme continues to follow `doxygen-awesome-css/main` using FetchContent's
normal download/cache behavior. It is intentionally unpinned; a fresh cache needs
network access, and separate downloads may contain different upstream revisions.

## Embedded JSON Schemas

Use `cmake/JsonSchemaHelpers.cmake` to embed JSON schema files into generated C++
headers. Configure makes them available immediately for editor use; the build
updates them when inputs change and recovers deleted headers.

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
2. `GenerateEmbeddedJsonSchemas.cmake` is the shared configure/build generator.

Unchanged manifests and headers keep their timestamps. A stamp records processed
inputs even when their contents produce an identical header, avoiding unnecessary
C++ recompilation. Both the stamp and header are declared outputs so Visual Studio
and Ninja can recover a missing header.

Generation is ordered before the owning target. If another target compiles source
files that include that same private header, it must also depend on the generation
targets recorded in the owner's `OXYGEN_JSON_SCHEMA_TARGETS` property. An include
directory alone does not establish that build dependency. For example:

```cmake
get_target_property(schema_targets owner OXYGEN_JSON_SCHEMA_TARGETS)
add_dependencies(consumer ${schema_targets})
```

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
