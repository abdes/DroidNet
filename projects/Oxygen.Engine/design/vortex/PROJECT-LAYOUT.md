# Vortex Project Layout

> **Module:** `Oxygen.Vortex`
> **Location:** `src/Oxygen/Vortex/`
> **CMake target:** `oxygen::vortex`

This document is the authoritative reference for the Vortex module's directory
structure, file placement rules, and organizational conventions. Every developer
working on Vortex should read this document before adding files.

Related:

- [ARCHITECTURE.md](./ARCHITECTURE.md) — architectural model, frame structure,
  ownership rules
- [DESIGN.md](./DESIGN.md) — concrete solution shapes and service contracts
- [PLAN.md](./PLAN.md) — implementation slices and migration plan
- [PRD.md](./PRD.md) — product requirements

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
   code. It is not production, not a reference implementation, not a fallback,
   and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
   system that targets maximum parity with UE5.7, grounded in
   `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
   `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
   explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
   explicit human approval records the accepted gap and the reason the parity
   gate cannot close.

## 1. Directory Tree

Files under this structure are illustrative only. Folder structure is
authoritative. It is up to implementation phases to place files under the
proper structure, following the rules and guidelines in this document.

```text
src/Oxygen/Vortex/
│
├── CMakeLists.txt                     ← single target: oxygen::vortex
├── api_export.h                       ← OXGN_VRTX_API / OXGN_VRTX_NDAPI
├── Errors.h                           ← renderer-core public error surface
├── FacadePresets.h                    ← helper presets for non-runtime facades
├── PreparedSceneFrame.h/.cpp          ← frame-prepared scene payload
├── RenderContext.h                    ← authoritative pass-execution context
├── Renderer.h/.cpp                    ← renderer-core substrate
├── RendererTag.h                      ← module tag / typed identity
├── SceneCameraViewResolver.h/.cpp     ← scene camera → view resolution
├── CompositionView.h                  ← per-view composition descriptor
├── RendererCapability.h               ← capability-family bitmask vocabulary
├── RenderMode.h                       ← wireframe / solid / debug enum
│
│   ── Renderer Core ──────────────────────────────────────────────────────
│
├── Internal/                          ← renderer-private (never in public API)
│   │  BlueNoiseData.h                    blue-noise texture data
│   │  CompositionPlanner.h/.cpp          composition planning infrastructure
│   │  CompositionViewImpl.h/.cpp         composition view realization
│   │  FrameViewPacket.h/.cpp             per-frame view packets
│   │  PerViewStructuredPublisher.h       per-view transient structured buffer publisher
│   │  RenderContextMaterializer.h        facade staging → validated context
│   │  RenderContextPool.h                frame-slotted RenderContext pool
│   │  RenderScope.h                      RAII context-pointer scope guard
│   │  ViewLifecycleService.h/.cpp        view lifecycle management
│   │  ...
│
│   ── Scene Renderer Layer ───────────────────────────────────────────────
│
├── SceneRenderer/                     ← desktop frame orchestrator layer
│   │  SceneRenderer.h/.cpp
│   │  SceneRenderBuilder.h/.cpp
│   │  SceneTextures.h/.cpp
│   │  ShadingMode.h
│   │  DepthPrePassPolicy.h               depth pre-pass mode enum
│   │  ResolveSceneColor.cpp              file-separated method (~180 lines)
│   │  PostRenderCleanup.cpp              file-separated method (~200-300 lines)
│   ├── Internal/                      ← scene-renderer-private helpers
│   │     FramePlanBuilder.h/.cpp          frame planning for desktop stages
│   │     ViewRenderPlan.h                 per-view render plan
│   └── Stages/                        ← stage modules (bounded frame-stage units)
│         ├── InitViews/               stage module: visibility, culling (~6.5k UE5 lines)
│         │     InitViewsModule.h/.cpp
│         │     Passes/
│         │     Types/
│         ├── DepthPrepass/            stage module: depth-only, partial velocity (~1.35k)
│         │     DepthPrepassModule.h/.cpp
│         │     Passes/
│         │     Types/
│         ├── Occlusion/               stage module: HZB, occlusion queries (~2.3k)
│         │     OcclusionModule.h/.cpp
│         │     Passes/
│         │     Types/
│         ├── BasePass/                stage module: GBuffer MRT, base-pass mesh proc (~3.35k)
│         │     BasePassModule.h/.cpp
│         │     Passes/
│         │     Types/
│         ├── Translucency/            stage module: forward-lit translucency (~1.77k)
│         │     TranslucencyModule.h/.cpp
│         │     Passes/
│         │     Types/
│         ├── Distortion/              reserved stage module (~1.1k)
│         │     DistortionModule.h/.cpp
│         │     Passes/
│         └── LightShaftBloom/         reserved stage module (~610)
│               LightShaftBloomModule.h/.cpp
│               Passes/
│
│   ── Pass Base Classes ──────────────────────────────────────────────────
│
├── Passes/
│   │  RenderPass.h/.cpp                  base: lifecycle, bindings, draw helpers
│   │  ComputeRenderPass.h/.cpp           base: compute PSO management
│   │  GraphicsRenderPass.h/.cpp          base: graphics PSO, render targets
│
│   ── Capability-Family Services ─────────────────────────────────────────
│
├── Environment/                       service: atmosphere, sky, IBL
│   │  EnvironmentLightingService.h
│   ├── Internal/
│   ├── Passes/
│   └── Types/
│
├── Shadows/                           service: conventional + VSM shadows
│   │  ShadowService.h
│   ├── Internal/
│   ├── Passes/
│   ├── Types/
│   └── Vsm/
│       ├── Internal/
│       ├── Passes/
│       └── Types/
│
├── Lighting/                          service: light data, deferred shading, culling
│   │  LightingService.h
│   ├── Internal/
│   ├── Passes/
│   └── Types/
│
├── PostProcess/                       service: tonemap, exposure, bloom
│   │  PostProcessService.h
│   ├── Passes/
│   └── Types/
│
├── Diagnostics/                       service: GPU debug, profiling, ImGui
│   │  DiagnosticsService.h
│   ├── Internal/
│   ├── ImGui/
│   └── Passes/
│
│   ── Reserved Capability-Family Services ────────────────────────────────
│
├── MaterialComposition/               reserved service: DBuffer decals, material classification
├── IndirectLighting/                  reserved service: GI, reflections, SSR, SSAO
├── Water/                             reserved service: single-layer water, caustics
├── GeometryVirtualization/            reserved service: Nanite-equivalent geometry
│
│   ── Requalified Vortex Subsystems ───────────────────────────────────────
│
├── ScenePrep/                         scene traversal → render items
├── Resources/                         geometry uploader, material/texture binders
├── Upload/                            ring-buffer staging, atlas, upload coordinator
│
│   ── Cross-Cutting Types ────────────────────────────────────────────────
│
├── Types/                             shared POD structs (no domain-specific types)
│
│   ── Tests ──────────────────────────────────────────────────────────────
│
└── Test/
    ├── Fakes/
    └── Fixtures/
```

## 2. `Internal/` Convention

Every `Internal/` directory, at any nesting level, contains files that are
**forbidden from appearing in public API headers**. The rule is structural:

- A public header (`OXYGEN_VORTEX_HEADERS` in CMake) must never
  `#include` an `Internal/` path from outside its own domain.
- A CI header-dependency linter can enforce this by scanning public
  header `#include` directives for `Internal/` segments.

Note: `Internal/` is stricter than a `detail` namespace. A `detail` namespace
marks symbols as not-for-external-use, but those symbols still ship in public
headers. Files under `Internal/` are never included by public headers at all —
they are invisible outside their owning domain at the `#include` level.

### 2.1 Domain-Scoped Internal

Each capability-family service has its own `Internal/` directory. Files in
`Lighting/Internal/` are private to the Lighting service. Files in the
top-level `Internal/` are private to Renderer Core.

A file in `Lighting/Internal/` must not be included by `Shadows/` headers,
and vice versa. Cross-domain dependencies flow through public service
interfaces, not through internal implementation files.

## 3. Capability-Family Service Pattern

Each domain sub-directory follows the same template:

| Sub-path | Contents |
| - | - |
| `Xxx/XxxService.h` | Public contract: lifecycle methods, accessors |
| `Xxx/Internal/` | Managers, backends, provider interfaces |
| `Xxx/Passes/` | Concrete render/compute passes |
| `Xxx/Types/` | POD structs published via `PerViewStructuredPublisher` |

The renderer orchestrator owns a `unique_ptr<XxxService>` for each
capability family that is present in the active `CapabilitySet`. More
precisely: `SceneRenderer` owns subsystem service instances;
`Renderer` owns substrate services only. Services are created conditionally;
their absence is a null pointer.

### 3.1 Adding a New Service

To add a new capability-family service:

1. Create the domain directory at the Vortex root (e.g., `NewDomain/`).
2. Create `NewDomain/NewDomainService.h` with the public contract.
3. Create `NewDomain/Internal/`, `NewDomain/Passes/`, `NewDomain/Types/`
   as needed.
4. Add files to the appropriate CMake source lists in `CMakeLists.txt`.
5. Wire the service into `SceneRenderer` at the correct
   frame stage.

### 3.2 Adding a New Pass

To add a new pass within an existing service:

1. Create the pass header and source in `Xxx/Passes/`.
2. Derive from `RenderPass`, `ComputeRenderPass`, or `GraphicsRenderPass`.
3. Register the pass in the owning service's initialization.
4. Add to CMake source lists.

### 3.3 Adding a New Stage Module

To add a new stage module:

1. Create `SceneRenderer/Stages/NewStage/` with `NewStageModule.h/.cpp`.
2. Create `Passes/`, `Types/` subdirectories as needed.
3. Implement `Execute(RenderContext&, SceneTextures&)` as the entry point.
4. Wire the module into `SceneRenderer` at the correct frame stage.
5. Add files to the appropriate CMake source lists.

Stage modules follow the same `Internal/` visibility rules as services:
files in `Stages/Xxx/Internal/` are private to that stage module.

## 4. Single CMake Target

Everything compiles into one library target (`oxygen::vortex`). Domain
sub-directories are organizational boundaries, not link-time boundaries.
This avoids premature build-system complexity while keeping code
ownership clear.

### 4.1 CMake Source List Convention

The `CMakeLists.txt` organizes sources into grouped variables:

- `OXYGEN_VORTEX_HEADERS` — public headers
- `OXYGEN_VORTEX_PRIVATE_SOURCES` — implementation files and private
  compilation units

Files are listed grouped by directory, in the same order as the directory
tree above.

### 4.2 Export Macros

| Macro | Purpose |
| - | - |
| `OXGN_VRTX_API` | DLL export/import for public symbols |
| `OXGN_VRTX_NDAPI` | `[[nodiscard]]` + DLL export/import |
| `OXGN_VRTX_EXPORTS` | Define symbol set by CMake during build |
| `OXGN_VRTX_STATIC` | Guard for static-library builds |

These are defined in `api_export.h`.

## 5. File Placement Decision Flowchart

When adding a new file, use this decision process:

1. **Is it part of the scene-renderer layer?**
   - Owns desktop frame ordering, `SceneTextures`, or shading-mode dispatch
     → `SceneRenderer/`

2. **Is it a type/struct with no domain-specific logic?**
   - Used by multiple subsystems → `Types/`
   - Used by one subsystem only → `Xxx/Types/`

3. **Is it a render or compute pass?**
   - Base class → `Passes/`
   - Concrete pass → `Xxx/Passes/`

4. **Is it an internal implementation detail?**
   - Renderer core internal → `Internal/`
   - Scene-renderer internal → `SceneRenderer/Internal/`
   - Domain service internal → `Xxx/Internal/`

5. **Is it a public service contract?**
   - `Xxx/XxxService.h` at the domain root

6. **Is it a renderer-core public API?**
   - Root of `src/Oxygen/Vortex/`

## 6. Dependency Hygiene Rules

The layout exists to preserve separation of concerns. Directory boundaries are
not cosmetic.

### 6.1 Layer Dependency Direction

Allowed dependency direction:

`Renderer Core` → `SceneRenderer` → `Subsystem Service` → `Pass`

Shared products and data may also flow through:

- `RenderContext`
- `SceneTextures`
- `Types/`
- `Xxx/Types/`

Forbidden dependency direction:

- `Subsystem Service` → `Renderer Core` implementation details
- `Pass` → owning service internals outside constructor/config inputs
- `Renderer Core` → subsystem service headers
- one subsystem domain depending on another subsystem's `Internal/`
- any public header depending on any `Internal/` header outside its own domain

### 6.2 Circular Dependency Prevention

To prevent circular dependencies:

1. Prefer forward declarations in headers; include concrete headers in `.cpp`
   files.
2. Public service headers expose contracts, not private helper types.
3. Cross-subsystem interaction happens through published data products,
   `SceneTextures`, or stable public contracts, never through back-pointers.
4. `SceneRenderer` owns service dispatch order; services do not
   call back into the scene renderer to trigger other services.
5. If two domains start sharing the same POD/config/product, move that type to
   `Types/` rather than letting domains include each other.

### 6.3 Separation-of-Concerns Rules

Keep concerns physically separated:

- `Renderer Core` owns frame/session lifetime, facades, publication substrate,
  view assembly, view registration, upload/staging, composition planning, and
  composition queue only.
- `SceneRenderer/` owns desktop frame ordering, `SceneTextures`, shading-mode
  branching, desktop scene configuration policy, and subsystem dispatch only.
- `XxxService` owns domain-specific GPU work, managers, passes, and domain data
  products only.
- `Passes/` base classes do not accumulate domain policy.

### 6.4 Public Header Pollution Rules

Public headers should minimize transitive surface area:

1. A root public header must not include subsystem `Internal/` headers.
2. A service public header must not include another service's public header
   unless that dependency is part of the stable contract and documented.
3. A scene-renderer public header may depend on service forward declarations,
   but service implementation details stay in `.cpp` files.
4. If a header needs too many unrelated includes, that is a placement smell;
   split the contract or move shared types to `Types/`.

## 7. Direct3D12 Shader Tree

This section defines the **target** organization for
`src/Oxygen/Graphics/Direct3D12/Shaders/`. It is the authoritative file-home
contract for the shader migration. The current filesystem still diverges from
this target in several places, so shader-tree restructuring remains
`in_progress` until catalog paths, include paths, and validation evidence land
together.

### 7.1 Shader Root Layout

```text
src/Oxygen/Graphics/Direct3D12/Shaders/
│
├── CMakeLists.txt                     ← engine-owned build integration
├── README.md                          ← operator/build notes
├── EngineShaders.h/.cpp               ← engine-owned runtime archive loader
├── EngineShaderCatalog.h              ← authoritative catalog registration
├── ShaderCatalogBuilder.h             ← compile-time catalog builder
├── Ui/                                ← engine-owned non-Vortex UI shaders
│     ImGui.hlsl
│
└── Vortex/                            ← all Vortex shader contracts + implementation
    │
    ├── RendererCore/                 ← Renderer Core-owned shader families
    │   └── Compositing/                 copy/blend/composition execution
    │
    ├── Contracts/                    ← stable published structs + binding accessors
    │   ├── Definitions/                 scene/light/material/shared enums + ABI constants
    │   ├── Draw/                        draw metadata, vertex/material payloads
    │   ├── View/                        view/view-history/view-color contracts
    │   ├── Scene/                       scene textures, GBuffer, HZB contracts
    │   ├── Lighting/                    published lighting-frame contracts
    │   ├── Environment/                 published environment-frame contracts
    │   ├── Shadows/                     published shadow/VSM contracts
    │   └── Diagnostics/                published debug-frame contracts
    │
    ├── Shared/                       ← truly renderer-wide helpers with no single owner
    ├── Materials/                    ← material template adapters and shared material helpers
    │
    ├── Stages/                       ← stage-module-owned shader families
    │   ├── DepthPrepass/
    │   ├── Occlusion/                   stage-5 HZB / occlusion ownership
    │   ├── BasePass/
    │   ├── Translucency/                forward-lit special-case material path
    │   ├── Distortion/                  reserved
    │   └── LightShaftBloom/            reserved
    │
    └── Services/                     ← subsystem-service-owned shader families
        ├── Lighting/
        ├── Shadows/
        │   ├── Conventional/
        │   └── Vsm/
        ├── Environment/
        ├── PostProcess/
        ├── Diagnostics/
        ├── MaterialComposition/        reserved
        ├── IndirectLighting/           reserved
        ├── Water/                      reserved
        └── GeometryVirtualization/     reserved
```

### 7.2 Root Rules

The shader root exists to make runtime ownership obvious.

1. No Vortex shader family may live directly under
   `Shaders/Forward/`, `Shaders/Depth/`, `Shaders/Lighting/`,
   `Shaders/Renderer/`, `Shaders/Compositing/`, or `Shaders/Common/`.
2. `Ui/` is the only permitted non-Vortex shader family root today because it
   is engine-owned rather than Vortex-owned.
3. Renderer-Core-owned shader entrypoints belong under
   `Vortex/RendererCore/`, not under `Vortex/Services/PostProcess/`.
   Composition is a Renderer Core responsibility, not a post-process service.
4. Shared structs, frame bindings, descriptor-routing helpers, and typed
   accessors belong under `Vortex/Contracts/`, not under a generic
   `Renderer/` bucket.
5. Truly renderer-wide math/helper code belongs under `Vortex/Shared/`.
   Family-local common code stays with its stage or service owner.
6. Reserved or inactive families may exist only under their final owner
   directory. Inactive code must not remain parked in anonymous legacy roots.

### 7.3 Current Root-to-Owner Mapping

| Current family/root | Target owner/home |
| - | - |
| `Forward/ForwardMesh_*.hlsl`, `Forward/ForwardDebug_PS.hlsl`, `Forward/ForwardWireframe_PS.hlsl`, `Forward/ForwardPbr.hlsli` | `Vortex/Stages/Translucency/` |
| `Forward/ForwardMaterialEval.hlsli` | `Vortex/Materials/` |
| `Depth/DepthPrePass.hlsl` | replace with `Vortex/Stages/DepthPrepass/DepthPrepass.hlsl`, then remove old file |
| `Lighting/LightCulling.hlsl`, `Lighting/ClusterLookup.hlsli` | `Vortex/Services/Lighting/` |
| `Lighting/IblFiltering.hlsl` | environment-owned family; if retained, move under `Vortex/Services/Environment/`, otherwise remove after parity review |
| `Compositing/Compositing_*.hlsl` | `Vortex/RendererCore/Compositing/` |
| `Compositing/ToneMap_*.hlsl`, `Compositing/AutoExposure_*.hlsl` | replace with `Vortex/Services/PostProcess/`, then remove old files |
| `Renderer/GroundGrid_*.hlsl` | replace with `Vortex/Services/PostProcess/GroundGrid.hlsl`, then remove old files |
| `Renderer/ScreenHzbBuild.hlsl` | replace with `Vortex/Stages/Occlusion/ScreenHzbBuild.hlsl`, then remove old file |
| `Renderer/GpuDebug*.hlsl`, `Renderer/Debug*.hlsli` | `Vortex/Services/Diagnostics/` |
| `Renderer/ConventionalShadow*.hlsl`, `Renderer/ConventionalShadow*.hlsli` | `Vortex/Services/Shadows/Conventional/` if retained, otherwise remove |
| `Renderer/Vsm/*` | `Vortex/Services/Shadows/Vsm/` if retained, otherwise remove |
| `Renderer/*.hlsli` draw/view/frame-binding payloads | split into `Vortex/Contracts/`, `Vortex/Shared/`, or the owning stage/service family |
| `Common/*.hlsli` | move live reusable helpers to `Vortex/Shared/`; remove unused leftovers |

### 7.4 Shader-Cleanup Rules

Shader-tree cleanup is not a blind move-only exercise.

1. A file is **live** if current production code requests it directly, or if a
   catalog-registered live entrypoint reaches it through includes.
2. A file is **reserved** only if it already belongs to a named Vortex owner in
   architecture or LLD documents and the file can be placed under that final
   owner without inventing a second legacy path.
3. A file is **dead** if it is not runtime-referenced, is not needed by any
   retained catalog family, and does not map cleanly to a named Vortex owner.
   Dead files should be deleted rather than re-homed.
4. Duplicate legacy and Vortex implementations must not coexist long term.
   When a Vortex-owned replacement exists, callers move first and the legacy
   duplicate is then removed.
5. New shader work must register only the final target path in
   `EngineShaderCatalog.h`; catalog aliases or doubled registrations are not
   allowed.
