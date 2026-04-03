# Codebase Structure

**Analysis Date:** 2026-04-03

## Directory Layout

```text
Oxygen.Engine/
├── CMakeLists.txt                # Top-level build orchestration and options
├── src/                          # Production engine code
│   ├── CMakeLists.txt            # Adds the Oxygen source tree
│   └── Oxygen/                   # Engine modules and libraries
├── Examples/                     # Runnable demos and integration hosts
├── cmake/                        # Shared CMake helper modules
├── tools/                        # Build, run, diagnostics, and codegen scripts
├── packages/                     # Vendored/bundled runtime SDK payloads (DXC, PIX, etc.)
├── profiles/                     # Conan profiles and environment presets
├── out/                          # Generated build/install artifacts
├── design/                       # Design/reference material outside production code
├── doxygen/                      # Doxygen configuration/assets
├── .planning/                    # Planning artifacts, including generated codebase docs
└── .omx/                         # OMX state, logs, and workflow metadata
```

## Directory Purposes

**`src/Oxygen/`:**
- Purpose: Canonical engine source tree.
- Contains: One directory per engine module/library plus shared docs like `src/Oxygen/ARCHITECTURE.md`.
- Key files: `src/Oxygen/CMakeLists.txt`, `src/Oxygen/Engine/AsyncEngine.h`, `src/Oxygen/Core/EngineModule.h`, `src/Oxygen/Graphics/Common/Graphics.h`, `src/Oxygen/Renderer/Renderer.h`.

**`src/Oxygen/Base/`:**
- Purpose: Foundational utility layer.
- Contains: Logging, resource tables, handles, metaprogramming helpers, state machine examples, docs/tests.
- Key files: `src/Oxygen/Base/Logging.h`, `src/Oxygen/Base/Resource.h`, `src/Oxygen/Base/ResourceTable.h`.

**`src/Oxygen/Composition/`:**
- Purpose: Component/composition ownership model.
- Contains: `Composition`, `Component`, pool/registry support, tests, internals.
- Key files: `src/Oxygen/Composition/Composition.h`, `src/Oxygen/Composition/Component.h`, `src/Oxygen/Composition/ComponentPoolRegistry.h`.

**`src/Oxygen/Core/`:**
- Purpose: Shared engine contracts and typed core data.
- Contains: `FrameContext`, phase registry, clocks, transforms, metadata, engine module contract, tools.
- Key files: `src/Oxygen/Core/FrameContext.h`, `src/Oxygen/Core/EngineModule.h`, `src/Oxygen/Core/PhaseRegistry.h`.

**`src/Oxygen/Engine/`:**
- Purpose: Runtime coordinator and module management.
- Contains: `AsyncEngine`, module manager, engine docs/tests, scripting integration.
- Key files: `src/Oxygen/Engine/AsyncEngine.h`, `src/Oxygen/Engine/ModuleManager.h`, `src/Oxygen/Engine/README.md`.

**`src/Oxygen/Graphics/`:**
- Purpose: Graphics abstraction and concrete backends.
- Contains: `Common/` backend-agnostic APIs, `Direct3D12/` implementation, `Headless/` test/backend implementation, external tooling integration.
- Key files: `src/Oxygen/Graphics/Common/Graphics.h`, `src/Oxygen/Graphics/Common/BackendModule.h`, `src/Oxygen/Graphics/Direct3D12/`, `src/Oxygen/Graphics/Headless/`.

**`src/Oxygen/Renderer/`:**
- Purpose: High-level rendering module.
- Contains: Renderer engine module, passes, pipeline planning, upload helpers, virtual shadow map systems, tests and docs.
- Key files: `src/Oxygen/Renderer/Renderer.h`, `src/Oxygen/Renderer/RenderContext.h`, `src/Oxygen/Renderer/Passes/`, `src/Oxygen/Renderer/Pipeline/`.

**`src/Oxygen/Scene/`:**
- Purpose: Scene graph and scene-domain types.
- Contains: Scene/node APIs, environment, camera/light helpers, traversal/query support, tests/docs.
- Key files: `src/Oxygen/Scene/Scene.h`, `src/Oxygen/Scene/SceneNode.h`, `src/Oxygen/Scene/Environment/README.md`.

**`src/Oxygen/Content/`:**
- Purpose: Runtime content/asset loading.
- Contains: `AssetLoader`, loaders, internal services, docs, tests, fixtures/mocks.
- Key files: `src/Oxygen/Content/AssetLoader.h`, `src/Oxygen/Content/LoaderContext.h`, `src/Oxygen/Content/Internal/`.

**`src/Oxygen/Cooker/`:**
- Purpose: Offline cooking, packaging, inspection, and import tooling.
- Contains: Pak/loose-cooked format code, schemas, tools, tests, docs.
- Key files: `src/Oxygen/Cooker/CMakeLists.txt`, `src/Oxygen/Cooker/Tools/PakGen/`, `src/Oxygen/Cooker/Tools/Inspector/`.

**`src/Oxygen/Input/`:**
- Purpose: Runtime input mapping and snapshot system.
- Contains: Actions, triggers, mapping contexts, docs/tests.
- Key files: `src/Oxygen/Input/InputSystem.h`, `src/Oxygen/Input/Action.h`, `src/Oxygen/Input/README.md`.

**`src/Oxygen/Physics/` and `src/Oxygen/PhysicsModule/`:**
- Purpose: Separate pure physics domain code from engine-phase integration.
- Contains: Physics domain objects/adapters in `src/Oxygen/Physics/`; engine module bridge in `src/Oxygen/PhysicsModule/`.
- Key files: `src/Oxygen/Physics/Physics.h`, `src/Oxygen/PhysicsModule/PhysicsModule.h`, `src/Oxygen/PhysicsModule/ScenePhysics.h`.

**`src/Oxygen/Scripting/`:**
- Purpose: Runtime scripting compilation, execution, bindings, and engine module support.
- Contains: Bindings, execution, resolvers, compilers, tests.
- Key files: `src/Oxygen/Scripting/Module/ScriptingModule.h`, `src/Oxygen/Scripting/Bindings/README.md`.

**`src/Oxygen/Platform/`:**
- Purpose: OS/platform abstraction.
- Contains: Platform object, event pump, input events, SDL integration, ImGui SDL glue, tests.
- Key files: `src/Oxygen/Platform/Platform.h`, `src/Oxygen/Platform/SDL/Window.cpp`, `src/Oxygen/Platform/SDL/EventPump.cpp`.

**`src/Oxygen/Loader/`:**
- Purpose: Dynamic graphics backend loading boundary.
- Contains: Loader interface, detail helpers, tests, README.
- Key files: `src/Oxygen/Loader/GraphicsBackendLoader.h`, `src/Oxygen/Loader/GraphicsBackendLoader.cpp`, `src/Oxygen/Loader/README.md`.

**`src/Oxygen/EditorInterface/`:**
- Purpose: Native API surface for editor/tool hosting.
- Contains: Engine context, public C-style bridge, engine runner, tests.
- Key files: `src/Oxygen/EditorInterface/Api.h`, `src/Oxygen/EditorInterface/EngineContext.h`, `src/Oxygen/EditorInterface/EngineRunner.cpp`.

**`Examples/`:**
- Purpose: Runnable end-to-end demos and host shells.
- Contains: Shared bootstrap in `Examples/main.cpp`, demo-specific `main_impl.cpp`, demo modules/panels/assets.
- Key files: `Examples/CMakeLists.txt`, `Examples/main.cpp`, `Examples/RenderScene/main_impl.cpp`, `Examples/DemoShell/`.

**`tools/`:**
- Purpose: Repository scripts for build generation, test execution, CLI helpers, and focused experiments.
- Contains: PowerShell scripts plus topic-specific subdirectories such as `tools/cli/`, `tools/flicker/`, `tools/codemod/`.
- Key files: `tools/generate-builds.ps1`, `tools/run-test-exes.ps1`, `tools/cli/oxybuild.ps1`, `tools/cli/oxyrun.ps1`.

## Key File Locations

**Entry Points:**
- `CMakeLists.txt`: Root configure entry for the entire repository.
- `Examples/main.cpp`: Shared executable `main()` used by demo targets.
- `Examples/*/main_impl.cpp`: Demo-specific runtime bootstrap and module registration.
- `src/Oxygen/EditorInterface/Api.h`: Editor/interop public runtime entry surface.
- `src/Oxygen/EditorInterface/EngineRunner.cpp`: Tool-hosted engine startup/shutdown loop.

**Configuration:**
- `CMakePresets.json`: Top-level CMake preset definitions.
- `ConanPresets-Ninja.json`: Conan preset file for Ninja-oriented flows.
- `ConanPresets-VS.json`: Conan preset file for Visual Studio flows.
- `conanfile.py`: Dependency and toolchain recipe.
- `profiles/`: Conan profiles such as `profiles/windows-msvc.ini` and `profiles/windows-msvc-asan.ini`.
- `src/Oxygen/Config/`: Runtime config structs such as `EngineConfig.h`, `GraphicsConfig.h`, `RendererConfig.h`, `PlatformConfig.h`, and `PathFinderConfig.h`.

**Core Logic:**
- `src/Oxygen/Engine/`: Frame orchestration and module execution.
- `src/Oxygen/Graphics/Common/`: Device-scoped rendering backend abstraction.
- `src/Oxygen/Renderer/`: High-level renderer and passes.
- `src/Oxygen/Scene/`: Scene graph and scene-domain types.
- `src/Oxygen/Content/`: Runtime asset/content system.

**Testing:**
- `src/Oxygen/Testing/`: Shared testing support like `GTest.h` and `gtest_main.cpp`.
- `src/Oxygen/*/Test/`: Per-module test targets and fixtures, for example `src/Oxygen/Renderer/Test/`, `src/Oxygen/Scene/Test/`, `src/Oxygen/Content/Test/`, `src/Oxygen/Physics/Test/`.
- `pytest.ini`: Python-side test configuration used by tooling-related tests.

## Naming Conventions

**Files:**
- Public C++ types use PascalCase filenames matching the primary type, e.g. `src/Oxygen/Engine/AsyncEngine.h`, `src/Oxygen/Scene/SceneNode.h`, `src/Oxygen/Renderer/RenderContext.h`.
- Companion implementation files stay adjacent with the same basename, e.g. `src/Oxygen/Engine/AsyncEngine.cpp` for `src/Oxygen/Engine/AsyncEngine.h`.
- Internal/private support is grouped under subdirectories named `Internal/` or `Detail/`, e.g. `src/Oxygen/Renderer/Internal/` and `src/Oxygen/Platform/Detail/`.
- Design/reference docs live next to the code they describe in `README.md`, `Docs/`, or focused markdown files such as `src/Oxygen/Physics/AggregateMappingModel.md`.

**Directories:**
- Top-level engine modules use PascalCase nouns under `src/Oxygen/`, e.g. `Renderer`, `Scene`, `Content`, `Platform`.
- Backend specializations and subdomains are nested below their owner module, e.g. `src/Oxygen/Graphics/Direct3D12/Allocator/`, `src/Oxygen/Renderer/Passes/Vsm/`, `src/Oxygen/Scene/Environment/`.
- Tests are usually colocated in sibling `Test/` directories under the owning module, not centralized in one repository-wide tree.

## Where to Add New Code

**New Feature spanning existing engine subsystems:**
- Primary code: Add to the owning domain module under `src/Oxygen/<Module>/`.
- Tests: Add to `src/Oxygen/<Module>/Test/` and reuse helpers from `src/Oxygen/Testing/` when possible.
- Build wiring: Update the module-local `src/Oxygen/<Module>/CMakeLists.txt`; only update `src/Oxygen/CMakeLists.txt` if you add a brand-new top-level module.

**New engine module (phase-driven runtime system):**
- Implementation: Create a new directory under `src/Oxygen/<Module>/` with an `EngineModule` implementation similar to `src/Oxygen/PhysicsModule/PhysicsModule.h` or `src/Oxygen/SceneSync/SceneObserverSyncModule.h`.
- Registration point: Hosts register it in runtime bootstrap code such as `Examples/RenderScene/main_impl.cpp` or `src/Oxygen/EditorInterface/EngineRunner.cpp`.
- Build wiring: Add `add_subdirectory("<Module>")` to `src/Oxygen/CMakeLists.txt` and create `src/Oxygen/<Module>/CMakeLists.txt`.

**New render pass or renderer pipeline code:**
- Implementation: `src/Oxygen/Renderer/Passes/` for passes, `src/Oxygen/Renderer/Pipeline/` for frame/view planning, `src/Oxygen/Renderer/VirtualShadowMaps/` for VSM-specific work, `src/Oxygen/Renderer/Upload/` for upload flow.
- Tests: Mirror under `src/Oxygen/Renderer/Test/` using the closest existing folder such as `DepthPrePass/`, `ShaderPass/`, `Upload/`, or `VirtualShadow/`.

**New graphics backend functionality:**
- Backend-agnostic contract changes: `src/Oxygen/Graphics/Common/`.
- D3D12 implementation changes: `src/Oxygen/Graphics/Direct3D12/`.
- Headless/test backend changes: `src/Oxygen/Graphics/Headless/`.
- Loader coordination: `src/Oxygen/Loader/` if backend loading policy or backend DLL resolution changes.

**New scene-domain capability:**
- Scene graph logic: `src/Oxygen/Scene/`.
- Renderer synchronization with scene mutations: `src/Oxygen/SceneSync/`.
- Renderer consumption of scene data: `src/Oxygen/Renderer/ScenePrep/` or related renderer types.

**New content pipeline or asset type:**
- Runtime loading: `src/Oxygen/Content/` and `src/Oxygen/Data/`.
- Offline cooking/importing: `src/Oxygen/Cooker/`.
- Example/demo usage: `Examples/Content/` or a demo-specific directory under `Examples/`.

**New runnable demo or integration sample:**
- Implementation: Create `Examples/<DemoName>/` with `CMakeLists.txt`, `main_impl.cpp`, and any local modules/panels/assets.
- Shared bootstrap: Reuse `Examples/main.cpp` instead of creating a separate `main()`.
- Build wiring: Add the demo directory to `Examples/CMakeLists.txt`.

**Utilities:**
- Shared runtime helpers: Prefer the smallest owning module under `src/Oxygen/` instead of adding a generic catch-all directory.
- Developer scripts and automation: `tools/`.
- Build-system utilities: `cmake/`.

## Special Directories

**`src/Oxygen/*/Test/`:**
- Purpose: Module-local unit/integration tests and fixtures.
- Generated: No.
- Committed: Yes.

**`src/Oxygen/*/Docs/`:**
- Purpose: Design/reference documents kept next to the code.
- Generated: No.
- Committed: Yes.

**`src/Oxygen/*/Internal/` and `src/Oxygen/*/Detail/`:**
- Purpose: Non-public implementation details and helper types.
- Generated: No.
- Committed: Yes.

**`packages/`:**
- Purpose: Local SDK/tool payloads such as DXC, RenderDoc, PIX, and Aftermath support packages.
- Generated: Downloaded/populated by helper scripts, but treated as repository-managed support assets.
- Committed: Yes.

**`out/`:**
- Purpose: Build trees, generated intermediates, and install/deploy outputs.
- Generated: Yes.
- Committed: No.

**`.planning/codebase/`:**
- Purpose: Generated planning/mapping artifacts consumed by later GSD workflows.
- Generated: Yes.
- Committed: Typically yes when planning artifacts are tracked.

**`.omx/`:**
- Purpose: OMX workflow state, notes, plans, logs, and runtime metadata.
- Generated: Yes.
- Committed: Mixed; treat as workflow-owned rather than production runtime code.

---

*Structure analysis: 2026-04-03*
