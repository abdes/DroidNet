# Build System Design

Oxygen's build system has to serve three users: a contributor working in the
engine, an application consuming selected modules, and a developer receiving an
installed SDK. A change that helps one must preserve the contracts of the others.

This document explains the design rules behind those contracts. Read it before
adding a module, dependency, generator, build option or install rule. For setup
commands, start with the [engine README](../README.md). For helper signatures
and detailed behavior, use the [CMake helper reference](../cmake/README.md).

## 1. Give each decision one owner

| Concern                                      | Authority                                                                                         | Consequence for changes                                                         |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| C++ dependencies and package options         | [conanfile.py](../conanfile.py)                                                                   | Declare requirements in the recipe, including their build/host roles.           |
| Compiler, runtime and instrumentation inputs | [Conan profiles](../profiles)                                                                     | Direct Conan commands and the developer wrapper must resolve equivalent graphs. |
| Target relationships and usage requirements  | Module `CMakeLists.txt` files                                                                     | Native CMake targets remain the build model.                                    |
| Common compiler policy                       | [CompilerPolicy.cmake](../cmake/CompilerPolicy.cmake)                                             | Apply policy consistently without exporting private flags.                      |
| Shared preset defaults                       | [CMakePresets.json](../CMakePresets.json)                                                         | Do not copy policy into generated presets or editor settings.                   |
| Tree identity and native dependency metadata | Conan generation                                                                                  | Reject incompatible reuse before replacing metadata or deploying binaries.      |
| Product version and source provenance        | [VERSION](../VERSION), [SourceProvenance.cmake](../cmake/SourceProvenance.cmake) and Conan export | Keep runtime, CMake and package metadata consistent.                            |
| Python requirements                          | Local `pyproject.toml` files and the [root workspace lock](../../../uv.lock)                      | Declare dependencies near their owner; resolve them together.                   |
| SDK layout and exports                       | [Install.cmake](../cmake/Install.cmake) and module install rules                                  | An install rule determines what ships.                                          |
| Shell activation and machine setup           | Developer's user profile                                                                          | Repository tools must not install or maintain personal shell automation.        |

Prefer native CMake properties, target relationships, Conan options and profiles
over parallel Oxygen abstractions. A helper should remove repeated mechanics,
validate a contract, or supply diagnostics. It should not become another language
for describing targets.

The root CMake file orchestrates these owners. Moving working code into another
helper merely to shorten that file does not improve the design.

## 2. Keep provisioning, configuration and execution distinct

| Stage                    | Supported entry point                                        | Responsibility                                                                                                      |
| ------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- |
| Provision and initialize | `build-tree generate <profile>`                              | Run Conan installation, prepare locked Python tooling, configure selected trees and prepare Ninja clangd databases. |
| Reconfigure              | `build-tree configure <preset>`                              | Configure an existing graph and prepare Ninja clangd databases; no Conan installation or cleanup.                   |
| Build                    | `oxybuild` or `cmake --build`                                | Execute the generated graph, including necessary code generation.                                                   |
| Test                     | CTest or an executable through `oxyrun`                      | Run already-built tests with the selected runtime environment.                                                      |
| Install                  | `cmake --install`, VS `INSTALL`, or the install build target | Assemble the declared SDK payload for the selected configuration.                                                   |

Normal builds must not run package installers. Python imports are checked during
configuration; a stale environment is a setup error, not an invitation to install
into whichever interpreter happens to be on PATH. Missing dependencies should
identify the setup action needed to recover.

`conan install` itself generates dependency metadata; it does not configure the
engine. The developer wrapper composes those steps. Keep that distinction when
writing commands or troubleshooting a partially initialized tree.

Configuration overrides have limits. Developers can locally disable provisioned
tests, benchmarks, examples, documentation and optional tools. A normal preset
configure restores the recipe's defaults. Changing binary identity or enabling
an output whose dependencies were not provisioned requires a new Conan graph.
Conan cache builds must match their recipe options exactly.

Generation preserves existing output by default. `-Clean` is an explicit reset of
the selected trees and SDK family, not a prerequisite for ordinary iteration.
Validate cleanup paths and ownership before deletion; do not clean another
variant's outputs as a side effect of setup.

Operational details: [build-tree and presets](../tools/presets/README.md),
[build/run commands](../tools/cli/README.md).

## 3. Make the support boundary explicit

The full engine targets Windows x64, MSVC 19.50+ and C++23, with CMake 4.2+ and
Conan 2.32+. Supported generators are Ninja, Ninja Multi-Config and Visual Studio
18 2026. Existing platform profiles are not proof that the full engine supports
another compiler or OS. Reject unsupported combinations early and clearly.

Base, Composition, OxCo, Serio, TextWrap and Clap have a separate reusable-module
contract for Windows, Linux and macOS. Platform support and validation evidence
are different statements: do not claim a platform was tested because its
configuration was accepted. Existing Linux C++ portability gaps and unperformed
macOS validation are not resolved by CMake configuration alone.

Keep language requirements on targets using `target_compile_features`. Oxygen's
Conan C++23 setting does not require forcing every dependency to use that same
standard; dependency recipes retain their own compatible language settings.

Single-config generators select a configuration at configure time; default only
an absent or empty `CMAKE_BUILD_TYPE`. Multi-config generators select it at build
time. Never use a false-like explicit value as evidence that the user supplied
no value. Use native configuration handling rather than generator-specific path
guesses. Oxygen selects `CMAKE_INTERMEDIATE_DIR_STRATEGY=SHORT` for shorter
intermediate paths without renaming public targets or final artifacts.

See the [build contract](../cmake/README.md#full-engine-build-contract).

## 4. Treat consumption as an API

Oxygen has one Conan recipe, `oxygen`, with named components. The default builds
the full engine. An application requesting `OxCo,Clap` gets those modules and
their transitive closure, not all engine modules and development targets.
Full-engine Conan packages use shared libraries because the showcase loads its
graphics backend dynamically. The six reusable modules also support static
packages; a fully static showcase is not part of that package contract.

Consumers use the same native CMake names through Conan or an installed SDK:

```cmake
find_package(Oxygen CONFIG REQUIRED COMPONENTS OxCo Clap)
target_link_libraries(my_game PRIVATE oxygen::oxco oxygen::clap)
```

Dependency consumers do not build Oxygen tests, benchmarks or optional examples
by default, including when Conan builds their dependency from source. A contributor
checks out the repository and uses its development workflow instead.

Source embedding is deliberately bounded to the six reusable modules. Require an
explicit module selection and use the parent's dependency graph. Respect its
configuration, install prefix and explicit output settings; keep default outputs
inside Oxygen's binary subtree. Embedded tests, examples, documentation, optional
tools, benchmarks and installation default off. Oxygen must not rewrite the
parent's editor files or impose compiler-cache configuration on it.

`OXYGEN_PROJECT_SOURCE_DIR` identifies the engine project root even when embedded;
`OXYGEN_SOURCE_DIR` identifies its `src` directory. Do not substitute the parent's
`CMAKE_SOURCE_DIR` for an Oxygen-owned path in reusable code.

See [Conan consumption](../cmake/README.md#consume-oxygen-through-conan) and
[source embedding](../cmake/README.md#selecting-reusable-modules).

## 5. Separate private build policy from public requirements

Warnings are required across Oxygen-owned libraries, tools, tests and examples:
`/W4` for MSVC and `-Wall -Wextra` for GCC/Clang. They are private build policy.
External applications do not inherit Oxygen's warning switches simply by linking
a module. A ready module can enable `COMPILE_WARNING_AS_ERROR` on its own targets;
there is no engine-wide warnings-as-errors mandate.

Use `PUBLIC` and `INTERFACE` for requirements consumers actually need: public
headers, compile features, necessary definitions and dependency relationships.
Express them through targets, not global flag strings or incidental include paths.
Static libraries may need to transmit link requirements even when their compile
options stay private. Use native runtime, PIC and debug-information properties;
do not overwrite whole compiler/linker cache entries to add one option.

Oxygen preserves its no-native-RTTI boundary. Application translation units that
derive from Oxygen polymorphic classes must use compatible RTTI-disabled settings.
Unrelated application code may use native RTTI. This is an integration constraint,
not a reason to disable RTTI globally in every consumer.

ASan is a complete configuration contract: a matching dependency profile, package
identity, compile instrumentation, link options and runtime launch environment.
An Oxygen option alone cannot make ordinary dependencies sanitized. Keep ASan in
its separate Debug trees. Coverage instrumentation is intentionally absent.

See [compiler policy](../cmake/README.md#compiler-policy-and-instrumentation).

## 6. Keep module and IDE helpers honest

Module declarations establish names and metadata; module files still declare
ordinary CMake libraries, executables, sources and header file sets. Canonical
target names are unique lowercase dashed names, such as
`oxygen-graphics-direct3d12-shaderbake`. Link aliases use `oxygen::...`.

A dotted build shortcut may be declared explicitly in the owning module, as with
`Oxygen.Graphics.Direct3D12.ShaderBake`. A CMake `ALIAS` alone is not a build-tool
entry point. Do not introduce a global alias system to avoid one local declaration.

Source inventory warnings reveal files omitted from a target; they do not add
files automatically. Keep explicit source ownership and meaningful exclusions.
For header-only libraries, expose public headers through file sets and list them
on the native interface target so IDEs can display them. Visual Studio project
generation and header visibility need verification in the generated project, not
an assumption based on a target existing in CMake.

VS Code editing uses Ninja trees; Visual Studio trees belong to Visual Studio.
clangd must follow the selected build configuration. Ninja Multi-Config's combined
compilation database is split into per-configuration databases without rewriting
compiler commands. `build-tree` prepares these after configuration; VS Code uses
its CMake Tools post-configure task. Restart clangd after changing the selected
tree/configuration. Keep the source `.clangd` stable and use the native editor
integration rather than another extension or a copied source-root database.

See [module and IDE helpers](../cmake/README.md#module-declarations-diagnostics-and-ide-headers).
Editor operations are described in the [VS Code workflow](../.vscode/README.md).

## 7. Model identity before reusing a tree or package

The recipe checks tree identity before changing generated metadata or deployment.
Compiler/architecture, runtime linkage, static/shared mode and instrumentation
must agree. Debug, Release and RelWithDebInfo can share a compatible multi-config
tree. Module selection and other configure-time options must also agree across
its configurations; CMake does not have a different target graph for each one.

Profiles own dependency identity, including ASan's package-ID configuration.
The wrapper reads Conan's resolved profile, so inherited profiles behave like
direct installs. It must not recreate profile semantics with text matching or
inject a second identity scheme.

The recipe explicitly uses `CMakeConfigDeps`. It does not globally replace
generators inside dependency recipes. This API is experimental in the minimum
supported Conan version, so qualify upgrades against preset and consumer tests.
Native CMake exports preserve usage requirements; Conan component metadata is
derived from configured targets rather than another handwritten engine graph.

Conan build requirements run on the build machine; host requirements are linked
or loaded by the target program. DXC has both roles, owned directly by Oxygen.
It uses the prebuilt distribution even for Debug engine builds. Do not rely on
an unrelated dependency to supply the shader compiler or its runtime DLLs.

A version string alone does not identify a reproducible Conan build. For an
investigation or upgrade qualification, record recipe revisions, resolved graph,
package IDs, binary revisions and effective profiles/tool versions as appropriate.
This is evidence to capture, not a claim that all dependency inputs are globally
locked or that GitHub CI enforces reproducibility.

## 8. Preserve one Python environment per checkout

The root uv workspace and lock combine requirements declared near their owning
tools. All local C++ trees share the checkout's `.venv`; Ninja/VS and Debug/Release
do not need different Python dependencies. `.python-version` selects the developer
interpreter series independently of each package's compatibility declaration.

Provision the complete developer selection during setup. Do not repeatedly prune
that shared environment to different subsets for different C++ configurations.
Launchers and editor tasks select the owning checkout's interpreter instead of
trusting a foreign active environment. Gersemi is declared by Oxygen.Engine and
is available both as a command and a Python module in this environment.

Independent Conan source builds cannot use editable installations from a developer
checkout. Source export projects hashed build-tool requirements from the same
lock, including build backends. Those source builds provision private environments
from exported inputs. This isolation follows source ownership, not local build
configuration. Module-only cache builds do not need the full-engine generators.

Automatic directory-based activation belongs in personal shell profiles. Changing
directories must not become a hidden installation step.

See the [repository Python workflow](../../../tooling/PYTHON.md).

## 9. Make generation dependencies complete and no-op work cheap

Every generated artifact needs an owner, declared inputs, output location and
consumer ordering. An include directory does not establish a build dependency.
Track changes to generator code, added/removed source files, schemas and relevant
tooling metadata as well as the primary input. Write only when content changes;
a timestamp refresh should not force downstream C++ compilation.

Different generators have different contracts:

| Generator             | Established contract                                                                                                                                                                                                        |
| --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Embedded JSON schemas | Headers are available after configure for editors, updated during builds, and recoverable if deleted. All compiling consumers depend on generation.                                                                         |
| Bindless ABI          | Generation is input-driven; checked-in source outputs remain configuration-independent. Build-tree stamps protect them from native clean operations. The normal generation target includes the mandatory C++ compile check. |
| ShaderBake            | Run its update check when the shader target is built. ShaderBake owns include tracking and incremental compilation; CMake must not prevent it from seeing changed includes.                                                 |

Do not move checked-in generated files into a build tree without assessing editor,
consumer and source-export implications. Conversely, do not publish
configuration-dependent output into a shared source file.

ShaderBake's intermediate state is per configuration. Shared final archive
publication is serialized across competing trees. Validate no-change behavior,
input changes, direct consumer builds and supported generator differences. A
successful Ninja build is not evidence that Visual Studio tracks deleted outputs
or changed source inventories correctly.

Examples are executable specifications: validate every advertised input against
the current schema and semantic rules. Missing inputs or an unrecognized layout
must not become a successful skipped validation. See the
[Bindless examples](../src/Oxygen/Core/Tools/BindlessCodeGen/examples/README.md).

## 10. Ship a usable SDK

Conan dependency deployment prepares the developer tree; CMake install rules
assemble the SDK. They must agree on the destination and selected configuration:

| Family          | SDK destination              |
| --------------- | ---------------------------- |
| Ordinary        | `out/install/<Config>`       |
| ASan            | `out/install/Asan`           |
| Tracy           | `out/install-tracy/<Config>` |
| Tracy with ASan | `out/install-tracy/Asan`     |

Ninja and VS intentionally share the destination for the same family/configuration.
Install the standard tree, then copy or archive the complete SDK to relocate it.
The wrapper has no custom deployment-prefix workflow; this does not disable native
CMake or Conan command-line capabilities.

Ship what install rules declare. The full SDK includes engine libraries, all four
core cooker tools (ImportTool, Inspector, PakTool and PakDump), ShaderBake, schemas,
and RenderScene with DemoShell and representative content. Tests, benchmarks and
other examples are development outputs, not SDK payload. Disabling optional tools
does not remove mandatory SDK tools.

Consumers need Oxygen's public dependency headers, declared libraries, runtime
files and usable CMake metadata at the same SDK level. Include required resource
directories and redistribution licenses. Do not copy entire upstream packages or
rely on producer checkout/cache paths. Detect conflicting flattened destinations
before one package overwrites another. Never stage build-context executables as
host runtime dependencies just because native profiles happen to match.

The installed showcase and content cooking/packing commands must run from the
received tree without the recipient reconstructing the producer's PATH. Validate
relocation with a real external application and an actual showcase launch. The
showcase currently keeps settings and content outputs near itself, so it requires
a writable location; read-only SDK execution is not yet promised.

See the [SDK guide](../cmake/SDK_README.md) and
[content workflow](../Examples/Content/README.md).

## 11. Report provenance without inventing release policy

Oxygen.Engine is one versioned component, not independently versioned native
modules. `VERSION` supplies the product version for CMake, Conan and runtime APIs.
Each numeric part must fit the existing `uint8_t` API: 0 through 255.

The component revision is distinct from repository HEAD. Unrelated sibling
commits should not churn Oxygen binaries. Local modifications are reported as
dirty; missing or unverifiable provenance is `unknown`. A dirty baseline commit
does not reconstruct the edited snapshot. Conan export therefore captures source
metadata with the source snapshot, and cache builds consume that metadata without
borrowing the surrounding application's Git history.

Version generation is build-tree-local and content-stable. Verify the value in
the final executable as well as the generated header. MSVC static Core propagates
`/INCREMENTAL:NO` because qualification reproduced stale version data in final
executables despite a rebuilt object/archive. Keep that fix scoped to its owner.

**Current boundary:** Git component provenance covers the engine directory. The
shared root Python manifests, lock and interpreter request now also affect tooling,
but are not included in that Git path scope. Their locked/exported requirement
data and Conan revisions provide separate evidence. Align both CMake and Conan
provenance scopes if extending the component revision to cover those shared inputs;
do not describe the current commit field as a complete build-input identity.

CMake `Release` means an optimized configuration. It does not mean an official
publication, a clean-source gate or an established release process.

See [version and provenance](../cmake/README.md#product-version-and-source-provenance).

## 12. Keep optional tools optional, and prove their effect

Compiler caching is acceleration, not a build prerequisite. Respect explicit
caller launchers; otherwise select installed ccache when the generator supports
native launchers. Warn and compile normally when unavailable or unsupported.
CMake's native launcher mechanism does not integrate with Visual Studio; that
does not mean ccache cannot support MSVC through other integrations.

For MSVC with ccache, compatible debug information matters. Oxygen defaults to
`/Z7` only in the applicable target/launcher context, preserving explicit choices.
Prove cache effectiveness through actual recompilation and hits/misses. A no-op
build that never invoked the compiler is not a cache-hit measurement. Cache resets
are explicit qualification actions, never automatic configure behavior.

Doxygen and Graphviz remain installed tools. Documentation requires both relevant
options and an explicit documentation build target; it is not part of the default
build. Keep warnings visible, preserve unchanged configuration-file timestamps,
and report current modules' warnings without accumulating stale reports. The
documentation theme intentionally follows upstream `main`; a fresh theme download
is an explicit exception to reproducible documentation inputs.

## 13. Validate the contract that changed

Oxygen test creation requires both `BUILD_TESTING` and `OXYGEN_BUILD_TESTS`.
Disabling Oxygen tests must preserve a parent's tests and remove stale Oxygen
registrations. Directory-level CTest inventory generation therefore remains active
even when Oxygen has no tests.

Register each GoogleTest executable once in CTest. Select individual cases with
GoogleTest arguments rather than duplicate per-case discovery. Build first; CTest
does not provision tools or build test binaries. Runtime launchers provide the
configuration's DLL/sanitizer paths without changing the developer's shell.
Benchmarks stay outside correctness CTest runs. Real-GPU test executables share
the `oxygen_gpu` lock within one CTest invocation; it does not coordinate other
CTest processes or unrelated applications.

Choose the smallest meaningful proof for a change:

| Change                  | Evidence to collect                                                                   |
| ----------------------- | ------------------------------------------------------------------------------------- |
| Configure/preset policy | Fresh and repeated configure, explicit overrides, rejected incompatible inputs.       |
| Compiler or ABI policy  | Actual compile/link commands and a bounded consumer executable.                       |
| Generation              | Changed input, no-op repeat, supported deletion recovery, and real consumer ordering. |
| Package/export metadata | Consume exported sources or an installed tree without producer-specific paths.        |
| SDK/runtime layout      | Relocated application/tool execution and showcase behavior where relevant.            |
| IDE representation      | Generated project/database inspection and visual confirmation when needed.            |

Distinguish configured, compiled, test-passed and visually verified. Do not broaden
a focused check into a full engine rebuild or full CTest run without a reason.
GitHub CI is intentionally disabled during the current development stage; local
validation remains part of making a change. Do not add automation merely to claim
coverage that has no useful, maintained execution environment.

Before merging a build-system change, check that it preserves the relevant
consumer workflow, declares its inputs and outputs, uses the correct dependency
context, and has evidence at the level it claims. A new public option, dependency,
output location or supported workflow is a design decision, not incidental cleanup.
