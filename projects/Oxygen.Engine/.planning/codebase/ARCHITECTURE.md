# Architecture

**Analysis Date:** 2026-04-03

## Pattern Overview

**Overall:** Modular C++ engine organized as CMake-built libraries, with a coroutine-driven runtime coordinator, module-based frame phases, backend-agnostic rendering APIs, and example/editor hosts that compose the subsystems.

**Key Characteristics:**
- The top-level build wires the repository as a CMake superproject from `CMakeLists.txt`, then enters `src/CMakeLists.txt`, `src/Oxygen/CMakeLists.txt`, and `Examples/CMakeLists.txt` to assemble libraries and runnable demos.
- Runtime orchestration is centralized in `src/Oxygen/Engine/AsyncEngine.h`, which owns frame sequencing, service access, and module registration instead of letting subsystems drive themselves independently.
- Engine subsystems are split into domain libraries under `src/Oxygen/*`, with additional separation between high-level renderer logic in `src/Oxygen/Renderer/` and low-level graphics backend code in `src/Oxygen/Graphics/`.

## Layers

**Build & Composition Layer:**
- Purpose: Define targets, dependencies, build options, and repository-wide inclusion order.
- Location: `CMakeLists.txt`, `src/CMakeLists.txt`, `src/Oxygen/CMakeLists.txt`, `Examples/CMakeLists.txt`
- Contains: CMake options, install/output setup, target registration, example toggles, test toggles.
- Depends on: `cmake/`, Conan configuration from `conanfile.py`, presets in `CMakePresets.json` and `ConanPresets-*.json`.
- Used by: Every engine library, test target, and example executable.

**Host / Bootstrap Layer:**
- Purpose: Create the platform, graphics backend, async engine, and initial module set.
- Location: `Examples/main.cpp`, `Examples/RenderScene/main_impl.cpp`, `src/Oxygen/EditorInterface/Api.h`, `src/Oxygen/EditorInterface/EngineRunner.cpp`
- Contains: Process entrypoints, CLI parsing, backend selection, event-loop startup, module registration.
- Depends on: `src/Oxygen/Platform/`, `src/Oxygen/Loader/`, `src/Oxygen/Graphics/Common/`, `src/Oxygen/Engine/`.
- Used by: Example apps in `Examples/*` and editor/interop callers through `src/Oxygen/EditorInterface/`.

**Runtime Orchestration Layer:**
- Purpose: Execute deterministic frame phases and dispatch work to registered modules.
- Location: `src/Oxygen/Engine/AsyncEngine.h`, `src/Oxygen/Engine/ModuleManager.h`, `src/Oxygen/Core/EngineModule.h`, `src/Oxygen/Core/FrameContext.h`
- Contains: Phase ordering, frame clocks, module lifecycle, per-phase dispatch, shared frame state.
- Depends on: Platform/graphics handles, config types in `src/Oxygen/Config/`, coroutines in `src/Oxygen/OxCo/`.
- Used by: Renderer, input, physics, scripting, scene sync, and any future engine module.

**Foundational Service Layer:**
- Purpose: Provide reusable engine-wide primitives and system services.
- Location: `src/Oxygen/Base/`, `src/Oxygen/Composition/`, `src/Oxygen/Core/`, `src/Oxygen/OxCo/`, `src/Oxygen/Serio/`, `src/Oxygen/Nexus/`, `src/Oxygen/Config/`
- Contains: Type system, composition/component model, resource handles/tables, clocks, phase registry, coroutine/runtime primitives, serialization streams, bindless/index reuse helpers, configs.
- Depends on: Mostly standard library plus a few shared third-party libraries surfaced through build files.
- Used by: Nearly every module under `src/Oxygen/`.

**Platform & Device Layer:**
- Purpose: Own OS integration, input/event pumping, windows, graphics backend loading, and device-scoped graphics services.
- Location: `src/Oxygen/Platform/Platform.h`, `src/Oxygen/Loader/GraphicsBackendLoader.h`, `src/Oxygen/Graphics/Common/Graphics.h`, `src/Oxygen/Graphics/Direct3D12/`, `src/Oxygen/Graphics/Headless/`
- Contains: SDL-backed platform code, backend loader, queue/command abstractions, surfaces, descriptor/resource registries, concrete D3D12 and headless backends.
- Depends on: `SDL3`, graphics backend packages under `packages/`, runtime config from `src/Oxygen/Config/GraphicsConfig.h` and `src/Oxygen/Config/PathFinderConfig.h`.
- Used by: Engine bootstrap, renderer, ImGui integration, tests, and editor interop.

**Gameplay / World Domain Layer:**
- Purpose: Represent scene data, assets, input, scripting, and physics state manipulated by runtime modules.
- Location: `src/Oxygen/Scene/`, `src/Oxygen/Content/`, `src/Oxygen/Data/`, `src/Oxygen/Input/`, `src/Oxygen/Scripting/`, `src/Oxygen/Physics/`, `src/Oxygen/PhysicsModule/`, `src/Oxygen/SceneSync/`
- Contains: Scene graph, asset loading/runtime data, input actions and snapshots, scripting execution/bindings, physics primitives and engine module bridge, scene-to-renderer synchronization.
- Depends on: Foundational services, engine phase contracts, and in some cases graphics-agnostic render metadata.
- Used by: Example applications, renderer scene preparation, and future gameplay modules.

**Rendering Layer:**
- Purpose: Convert scene/view state into per-frame render plans and GPU work.
- Location: `src/Oxygen/Renderer/Renderer.h`, `src/Oxygen/Renderer/RenderContext.h`, `src/Oxygen/Renderer/Passes/`, `src/Oxygen/Renderer/Pipeline/`, `src/Oxygen/Renderer/ScenePrep/`, `src/Oxygen/Renderer/VirtualShadowMaps/`, `src/Oxygen/ImGui/`
- Contains: Engine-side renderer module, render passes, typed frame bindings, per-view planning, upload coordination, shadow systems, ImGui modules/passes.
- Depends on: `src/Oxygen/Graphics/Common/`, scene/content/data modules, engine phase callbacks.
- Used by: Example/demo hosts and any runtime that registers `oxygen::engine::Renderer`.

## Data Flow

**Application Bootstrap Flow:**

1. Top-level build declares the repository and options in `CMakeLists.txt`, then adds `src/` and optionally `Examples/`.
2. A host entrypoint such as `Examples/main.cpp` or `src/Oxygen/EditorInterface/EngineRunner.cpp` constructs `Platform`, resolves a graphics backend through `src/Oxygen/Loader/GraphicsBackendLoader.h`, and creates `AsyncEngine` from `src/Oxygen/Engine/AsyncEngine.h`.
3. The host activates `Platform`, `Graphics`, and `AsyncEngine`, then registers concrete `EngineModule` implementations such as `src/Oxygen/Input/InputSystem.h`, `src/Oxygen/Renderer/Renderer.h`, `src/Oxygen/PhysicsModule/PhysicsModule.h`, `src/Oxygen/SceneSync/SceneObserverSyncModule.h`, and `src/Oxygen/Scripting/Module/ScriptingModule.h`.

**Per-Frame Runtime Flow:**

1. `AsyncEngine` advances ordered phases defined by methods like `PhaseFrameStart`, `PhaseInput`, `PhaseFixedSim`, `PhasePreRender`, `PhaseRender`, and `PhaseCompositing` in `src/Oxygen/Engine/AsyncEngine.h`.
2. `ModuleManager` in `src/Oxygen/Engine/ModuleManager.h` dispatches phase callbacks declared by `src/Oxygen/Core/EngineModule.h` to modules based on supported phase masks and priorities.
3. Input, scripting, physics, and scene mutation modules update authoritative state through `FrameContext` and module-owned systems before renderer-facing publication points.
4. Renderer phases consume prepared scene/view state, acquire command recorders from `src/Oxygen/Graphics/Common/Graphics.h`, and submit/present through the backend.

**Scene to Render Flow:**

1. Authoritative scene hierarchy lives in `src/Oxygen/Scene/Scene.h` and node wrappers in `src/Oxygen/Scene/SceneNode.h`.
2. Change propagation modules such as `src/Oxygen/SceneSync/SceneObserverSyncModule.h` align scene mutations with renderer-visible state during gameplay and scene-mutation phases.
3. `src/Oxygen/Renderer/ScenePrep/` and `src/Oxygen/Renderer/PreparedSceneFrame.h` build per-frame renderable snapshots.
4. Render passes under `src/Oxygen/Renderer/Passes/` read `RenderContext` and typed bindings from `src/Oxygen/Renderer/Types/` to record GPU work.

**Asset / Content Flow:**

1. Runtime asset loading enters through `src/Oxygen/Content/AssetLoader.h` and loader contracts in `src/Oxygen/Content/LoaderContext.h`.
2. Content sources, dependency tracking, and in-flight work are delegated to internals under `src/Oxygen/Content/Internal/`.
3. Immutable runtime asset representations live in `src/Oxygen/Data/` and are consumed by scene and renderer code.
4. Offline cooking and package tooling live separately in `src/Oxygen/Cooker/` and feed runtime content consumers instead of mixing tooling code into runtime modules.

**State Management:**
- Frame-scoped runtime state is carried in `src/Oxygen/Core/FrameContext.h` and renderer-specific `src/Oxygen/Renderer/RenderContext.h`.
- Persistent subsystem ownership usually sits in `Composition` roots such as `src/Oxygen/Engine/AsyncEngine.h`, `src/Oxygen/Graphics/Common/Graphics.h`, and `src/Oxygen/Scene/Scene.h`.
- Snapshot semantics are explicit in modules like `src/Oxygen/Input/README.md` and in `EngineModule::OnParallelTasks` from `src/Oxygen/Core/EngineModule.h`, which documents read-only snapshot use during parallel work.

## Key Abstractions

**EngineModule:**
- Purpose: Phase-driven plugin contract for engine systems.
- Examples: `src/Oxygen/Core/EngineModule.h`, `src/Oxygen/Renderer/Renderer.h`, `src/Oxygen/PhysicsModule/PhysicsModule.h`, `src/Oxygen/SceneSync/SceneObserverSyncModule.h`
- Pattern: Modules declare supported phases, priority, lifecycle hooks, and optional coroutine handlers.

**AsyncEngine:**
- Purpose: Runtime coordinator that owns the frame loop and engine services.
- Examples: `src/Oxygen/Engine/AsyncEngine.h`, `src/Oxygen/Engine/AsyncEngine.cpp`
- Pattern: Central orchestrator with explicit ordered phase methods and service accessors.

**Composition / Component model:**
- Purpose: Reusable ownership and dependency mechanism for engine objects.
- Examples: `src/Oxygen/Composition/Composition.h`, `src/Oxygen/Composition/Component.h`, `src/Oxygen/Graphics/Common/Graphics.h`, `src/Oxygen/Scene/Scene.h`
- Pattern: Composition root plus components, often using `UpdateDependencies` and typed component lookup.

**Graphics backend boundary:**
- Purpose: Separate backend-agnostic device APIs from backend-specific implementations.
- Examples: `src/Oxygen/Graphics/Common/Graphics.h`, `src/Oxygen/Loader/GraphicsBackendLoader.h`, `src/Oxygen/Graphics/Direct3D12/`, `src/Oxygen/Graphics/Headless/`
- Pattern: Abstract base plus dynamically loaded backend module.

**Render pass lifecycle:**
- Purpose: Encapsulate discrete rendering work units.
- Examples: `src/Oxygen/Renderer/Passes/RenderPass.h`, `src/Oxygen/Renderer/Passes/DepthPrePass.h`, `src/Oxygen/Renderer/Passes/ShaderPass.h`
- Pattern: Validate -> prepare resources -> execute, coordinated through `RenderContext`.

**Scene handle / resource-table model:**
- Purpose: Keep scene nodes dense, handle-based, and centrally owned.
- Examples: `src/Oxygen/Scene/Scene.h`, `src/Oxygen/Scene/SceneNode.h`, `src/Oxygen/Base/Resource.h`
- Pattern: Resource-table storage with lightweight handles/wrappers and mutation routed through the owning manager.

## Entry Points

**Top-level build entry:**
- Location: `CMakeLists.txt`
- Triggers: CMake configure/generate.
- Responsibilities: Declare options, output directories, toolchain behavior, tests/docs/examples toggles, then add `src/` and `Examples/`.

**Example process entry:**
- Location: `Examples/main.cpp`
- Triggers: Every executable declared by example `CMakeLists.txt` files that includes `../main.cpp`.
- Responsibilities: Common `main`, logging initialization, crash-safe error reporting, and dispatch to `MainImpl`.

**Example application bootstrap:**
- Location: `Examples/RenderScene/main_impl.cpp` (representative pattern also repeated in `Examples/InputSystem/main_impl.cpp`, `Examples/Physics/main_impl.cpp`, `Examples/Platform/main_impl.cpp`, and others)
- Triggers: Example executable startup.
- Responsibilities: Build CLI, create platform/backend/engine objects, register modules, run the event loop.

**Editor / interop entry surface:**
- Location: `src/Oxygen/EditorInterface/Api.h`, `src/Oxygen/EditorInterface/EngineRunner.cpp`
- Triggers: External host or managed/editor caller.
- Responsibilities: Create and run an `EngineContext`, expose logging/runtime helpers, and bridge native engine startup for tooling.

**Graphics backend loading:**
- Location: `src/Oxygen/Loader/GraphicsBackendLoader.h`
- Triggers: Host startup when a graphics backend is needed.
- Responsibilities: Resolve backend DLL, enforce strict/relaxed initialization policy, and return a concrete `Graphics` instance.

## Error Handling

**Strategy:** Mixed defensive style: assertions and contracts on internal boundaries, `bool`/`std::optional` for recoverable runtime operations, and exceptions for bootstrap/configuration failures.

**Patterns:**
- Engine and scene APIs frequently surface recoverable failure through return values instead of exceptions, as documented in `src/Oxygen/Scene/Scene.h` and lookup methods in `src/Oxygen/Engine/ModuleManager.h`.
- Bootstrap code uses exceptions for fatal startup problems, such as invalid CLI options in `Examples/Common/DemoCli.h` and backend loading failures described in `src/Oxygen/Loader/README.md`.
- Internal contracts rely on checks and logging, visible in `src/Oxygen/Graphics/Common/Graphics.h`, `src/Oxygen/Platform/Platform.h`, and example startup code using `CHECK_F`, `DCHECK_F`, and `LOG_F`.

## Cross-Cutting Concerns

**Logging:** Logging is centralized around Oxygen/loguru-style macros and helper setup, visible in `Examples/main.cpp`, `src/Oxygen/Base/Logging.h`, and `src/Oxygen/EditorInterface/Api.h`.

**Validation:** Validation is split by layer: build-time/registration validation in `CMakeLists.txt` and module setup, runtime contract checks in headers such as `src/Oxygen/Core/EngineModule.h` and `src/Oxygen/Graphics/Common/Graphics.h`, plus loader/config validation in `src/Oxygen/Loader/GraphicsBackendLoader.h` and `src/Oxygen/Content/README.md`.

**Authentication:** Not applicable / not detected in the engine runtime architecture mapped from `src/Oxygen/` and `Examples/`.

---

*Architecture analysis: 2026-04-03*
