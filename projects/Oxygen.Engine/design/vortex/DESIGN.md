# Vortex Renderer Design

Status: `stable entry point — detailed LLDs live in lld/`

This document is the entry point for Vortex renderer design. It captures
cross-cutting design decisions, vocabulary types, architectural invariants, and
the data-flow model that span multiple subsystems. Per-subsystem and per-stage
low-level designs live in individual LLD documents under [`lld/`](lld/README.md).

It assumes the stable conceptual model defined in
[ARCHITECTURE.md](./ARCHITECTURE.md).

Related documents:

| Document | Purpose |
| --- | --- |
| [PRD.md](./PRD.md) | Product requirements |
| [ARCHITECTURE.md](./ARCHITECTURE.md) | Stable conceptual architecture |
| [PLAN.md](./PLAN.md) | Phased execution plan |
| [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) | Authoritative file placement |
| [IMPLEMENTATION-STATUS.md](./IMPLEMENTATION-STATUS.md) | Tracker |
| [lld/README.md](lld/README.md) | LLD package index and reserved future LLDs |

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

## 1. Design Summary

Vortex organizes around:

- a desktop scene renderer orchestrator (`SceneRenderer`) that
  owns frame-stage dispatch
- a `SceneTextures` product that carries GBuffer, depth, color, and velocity
  data across the frame
- capability-family subsystem services that own domain-specific GPU work
- a Vortex-owned Renderer Core substrate (frame loop, views, composition,
   facades, publication, upload) that may retain only architecture-neutral
   concepts after they are requalified against the UE5.7 parity target
- a shared forward light data service that lives inside the Lighting subsystem

The design should be read through the scene-renderer mental model:

- the scene renderer owns the frame structure and dispatches subsystems
- subsystems are services, not frame-structure owners
- passes are the lowest-level execution units
- everything else (publication, upload, composition) is substrate

## 2. LLD Document Map

All per-subsystem and per-stage designs live in dedicated LLD documents.
See [`lld/README.md`](lld/README.md) for the full index. The key mappings:

| Topic | LLD |
| --- | --- |
| Phase 1 substrate migration | [`substrate-migration-guide.md`](lld/substrate-migration-guide.md) |
| ScenePrep refactor and publication contract | [`sceneprep-refactor.md`](lld/sceneprep-refactor.md) |
| SceneTextures four-part contract | [`scene-textures.md`](lld/scene-textures.md) |
| SceneRenderBuilder + SceneRenderer shell | [`scene-renderer-shell.md`](lld/scene-renderer-shell.md) |
| Depth prepass (stage 3) | [`depth-prepass.md`](lld/depth-prepass.md) |
| Base pass (stage 9) | [`base-pass.md`](lld/base-pass.md) |
| Deferred lighting (stage 12) | [`deferred-lighting.md`](lld/deferred-lighting.md) |
| Shader contracts & directory layout | [`shader-contracts.md`](lld/shader-contracts.md) |
| InitViews (stage 2) | [`init-views.md`](lld/init-views.md) |
| LightingService | [`lighting-service.md`](lld/lighting-service.md) |
| PostProcessService | [`post-process-service.md`](lld/post-process-service.md) |
| ShadowService | [`shadow-service.md`](lld/shadow-service.md) |
| EnvironmentLightingService | [`environment-service.md`](lld/environment-service.md) |
| Examples/Async migration | [`migration-playbook.md`](lld/migration-playbook.md) |
| DiagnosticsService | [`diagnostics-service.md`](lld/diagnostics-service.md) |
| Translucency (stage 18) | [`translucency.md`](lld/translucency.md) |
| Occlusion / HZB (stage 5) | [`occlusion.md`](lld/occlusion.md) |
| Multi-view composition | [`multi-view-composition.md`](lld/multi-view-composition.md) |
| Offscreen rendering | [`offscreen-rendering.md`](lld/offscreen-rendering.md) |

## 3. SceneRenderer Overview

The scene renderer is owned by `Renderer` as a delegate — not a replacement.
Renderer retains ownership of the frame loop, `RenderContext` allocation, view
management, upload/staging, publication substrate, and composition execution.
The scene renderer dispatches stage modules and subsystem services in 23-stage
order (ARCHITECTURE.md §6).

See [`lld/scene-renderer-shell.md`](lld/scene-renderer-shell.md) for the full
class shape, dispatch skeleton, and bootstrap via `SceneRenderBuilder`.

### 3.1 Ownership Rules

1. The scene renderer is created by the Renderer during initialization, based
   on the active `CapabilitySet`.
2. The scene renderer owns `SceneTextures`, all stage module instances, and all
   subsystem service instances.
3. The Renderer delegates frame-phase work to the scene renderer after its own
   substrate-level work (context allocation, view management, upload staging).
4. The scene renderer dispatches stage modules and subsystem services in the
   correct frame-stage order. Neither stage modules nor subsystems control
   their own dispatch order.

### 3.2 Mode Selection

The scene renderer supports per-view mode selection:

```cpp
enum class ShadingMode : std::uint8_t {
  kDeferred,   // GBuffer base pass + deferred lighting (default)
  kForward,    // Forward shading using shared light data
};
```

Mode selection is per `CompositionView`. The scene renderer branches at the
base pass and lighting stages; all other stages (depth, occlusion, shadows,
environment, post-process) are shared.

## 4. SceneTextures Overview

`SceneTextures` is a concrete class owned by the scene renderer — not an
abstract interface. It carries GBuffer, depth, color, velocity, and auxiliary
data across the frame. See [`lld/scene-textures.md`](lld/scene-textures.md)
for the complete four-part contract (config, setup mode, bindings, extracts).

### 4.1 GBuffer Index Vocabulary

```cpp
enum class GBufferIndex : std::uint8_t {
  kNormal = 0,        // World normal (encoded)
  kMaterial = 1,      // Metallic, specular, roughness
  kBaseColor = 2,     // Base color
  kCustomData = 3,    // Custom data / shading model
  kShadowFactors = 4, // Precomputed shadow factors (optional)
  kWorldTangent = 5,  // World tangent (optional)
};
```

### 4.2 GBuffer Format Baseline

| Buffer | Format | Content |
| --- | --- | --- |
| GBufferNormal | `R10G10B10A2_UNORM` | Encoded world normal |
| GBufferMaterial | `R8G8B8A8_UNORM` | Metallic, specular, roughness, shading model ID |
| GBufferBaseColor | `R8G8B8A8_SRGB` | Base color, AO |
| GBufferCustomData | `R8G8B8A8_UNORM` | Custom data (subsurface, cloth, etc.) |

GBufferE/F are reserved — deferred to after the initial deferred path works.

## 5. Frame Orchestration

The scene renderer's `OnRender` dispatches the 23-stage frame structure from
ARCHITECTURE.md §6. See [`lld/scene-renderer-shell.md`](lld/scene-renderer-shell.md)
for the complete dispatch skeleton.

### 5.1 Dispatch Rules

1. Stage numbers correspond to ARCHITECTURE.md §6.2.
2. Stage modules are dispatched via `Execute(RenderContext&, SceneTextures&)`.
   Subsystem services are dispatched via their domain-specific methods.
3. Skipped stages (4, 7, 11, 13, 14, 16, 17, 19, 20) are reserved stubs.
   They become stage module or service dispatches when implemented.
4. Null subsystem pointers mean the stage is a no-op with zero overhead.
5. The scene renderer owns stage ordering; it does not delegate ordering to
   stage modules or subsystems.

### 5.2 Per-View vs Per-Frame Stages

| Per-frame | Per-view |
| --- | --- |
| Shadow depth rendering | Depth prepass |
| Light grid build | Occlusion / HZB |
| | Base pass |
| | Deferred lighting |
| | Translucency |
| | Post-processing |

See [`lld/multi-view-composition.md`](lld/multi-view-composition.md) for the
complete per-view iteration model and mixed-mode frame support.

## 6. Subsystem Service Contract Model

All subsystem services follow the same lifecycle contract:

```cpp
class ISubsystemService {
public:
  virtual ~ISubsystemService() = default;
  virtual void Initialize(graphics::IGraphics& gfx, const RendererConfig& config) = 0;
  virtual void OnFrameStart(const FrameContext& frame) = 0;
  virtual void Shutdown() = 0;
};
```

Subsystems extend this with domain-specific execution methods. There is no
forced common `ExecutePerViewPasses` interface — each subsystem exposes the
methods that match its place in the frame structure.

Per-subsystem detailed designs:

| Service | Stage(s) | LLD |
| --- | --- | --- |
| LightingService | 6, 12 | [`lighting-service.md`](lld/lighting-service.md) |
| ShadowService | 8 | [`shadow-service.md`](lld/shadow-service.md) |
| EnvironmentLightingService | 15 | [`environment-service.md`](lld/environment-service.md) |
| PostProcessService | 22 | [`post-process-service.md`](lld/post-process-service.md) |
| DiagnosticsService | overlay | [`diagnostics-service.md`](lld/diagnostics-service.md) |

## 7. Base Pass and Deferred Lighting

The base pass (stage 9) writes to all active GBuffer targets simultaneously
using MRT. The deferred lighting pass (stage 12) reads GBuffer SRVs and
outputs to SceneColor.

See [`lld/base-pass.md`](lld/base-pass.md) for the MRT write contract,
material shader interface, and velocity completion sub-pass.

See [`lld/deferred-lighting.md`](lld/deferred-lighting.md) for the
directional fullscreen plus bounded-volume local-light deferred-light contract,
and the shader contracts that support it.

## 8. Inherited Substrate Adaptation

The following substrate carries over with mechanical changes only (namespace,
export macros, include paths). See
[`lld/substrate-migration-guide.md`](lld/substrate-migration-guide.md) for the
complete migration checklist.

| Substrate | Adaptation |
| --- | --- |
| Frame loop lifecycle | Unchanged — Renderer owns it |
| RenderContext | Unchanged — authoritative execution context |
| Publication | Baseline stays in Renderer Core; subsystems add per-view data |
| Composition | Queued model unchanged; `CompositionSubmission` is sole handoff |
| Non-runtime facades | Architecturally unchanged; operate against Vortex types |
| Upload/staging | Unchanged |

See [`lld/offscreen-rendering.md`](lld/offscreen-rendering.md) for the
three non-runtime facade shapes adapted for Vortex.

## 9. Capability Declaration

Vortex retains the capability-family vocabulary from modular-renderer and adds
scene-renderer-level capabilities:

```cpp
enum class RendererCapabilityFamily : std::uint32_t {
  kScenePreparation,
  kGpuUploadAndAssetBinding,
  kLightingData,
  kShadowing,
  kEnvironmentLighting,
  kFinalOutputComposition,
  kDiagnosticsAndProfiling,
  kDeferredShading,          // new: GBuffer + deferred lighting
};
```

The `kDeferredShading` family gates the scene renderer's deferred path. When
absent, the scene renderer falls back to forward-only mode.

## 10. Shader Module Organization

See [`lld/shader-contracts.md`](lld/shader-contracts.md) for the complete
shader directory layout, per-file contracts, HLSL code shapes, and
EngineShaderCatalog registration table.

### 10.1 Shader Module Rules

1. Shader families mirror UE5 ownership boundaries while preserving Oxygen's
   build pipeline and bindless ABI (`ARCHITECTURE.md §10`).
2. `src/Oxygen/Core/Bindless/` remains the engine-global ABI/bootstrap layer.
   Vortex shader files must consume it; they must not replace it.
3. `ShaderCatalogBuilder.h` and `EngineShaderCatalog.h` remain the only
   engine-owned source of truth for shader discovery and permutation expansion.
4. `Contracts/Definitions/` holds shared numeric/layout vocabulary only:
   enums, flags, packing constants, and other cross-language definitions.
5. `Contracts/` holds stable renderer-facing shader contracts and typed routing
   helpers. It is not a dumping ground for service-local policy.
6. `Shared/` is intentionally narrow. Helpers belong there only if they are
   truly renderer-wide and stable across multiple owners.
7. Family-local `*Common` or helper files stay beside the owning stage/service
   family under `Stages/` or `Services/`.
8. `.hlsl` files are entrypoints or multi-entry compute families.
   `.hlsli` files are include-only libraries and contracts.
9. `Materials/` is a bounded renderer-owned adaptation layer for material
   evaluation/output packing. Materials do not define alternate renderer
   routing contracts.
10. Every engine-owned Vortex entrypoint must appear in `EngineShaderCatalog.h`.
    No filesystem globbing or second shader registry is allowed.
11. Permutation identity must reuse Oxygen's canonical request rules from
    `src/Oxygen/Graphics/Common/Shaders.cpp`.
12. ShaderBake remains the only compilation path for Vortex shaders.
13. No shader file depends on legacy `Oxygen.Renderer` shader paths.

## 11. Cross-Subsystem Data Flow

### 11.1 Data Product Dependencies

| Consumer | Products Consumed | Producer |
| --- | --- | --- |
| Occlusion / HZB | SceneDepth | Depth prepass |
| Shadow depth | Light list, view data | LightingService, InitViews |
| Base pass | Shadow maps (optional) | ShadowService |
| Deferred lighting | GBufferNormal/Material/BaseColor/CustomData, SceneDepth, shadow data, and only an explicitly documented ambient-bridge subset when that Phase 4 exception is enabled | Base pass, ShadowService, EnvironmentLightingService (ambient bridge only), future IndirectLightingService for canonical indirect environment evaluation |
| Translucency | SceneColor, SceneDepth, forward light data | Prior stages, LightingService |
| Post-process | SceneColor, SceneDepth, Velocity | Prior stages |
| Diagnostics | Any SceneTextures product | Prior stages |

### 11.2 Data Flow Rules

1. Dependencies are data-product dependencies, not ownership dependencies.
2. Products flow through `SceneTextures` or `RenderContext` bindings.
3. No subsystem holds a back-pointer to the scene renderer or to another
   subsystem.
4. The scene renderer ensures correct execution order so that consumed products
   are valid when needed.

## 12. Design Decisions and Rationale

### 12.1 Fullscreen Deferred Lighting (Phase 1)

Decision: use directional fullscreen deferred lighting plus one-pass bounded-volume local-light deferred lighting for the initial phase.

Rationale:

- simplest correct approach
- well-understood GPU behavior
- tiled/clustered deferred adds complexity without changing the architectural
  shape
- optimization can be introduced later without changing subsystem contracts

### 12.2 SceneTextures as Concrete Class (Not Interface)

Decision: `SceneTextures` is a concrete class owned by the scene renderer,
not an abstract interface.

Rationale:

- there is only one desktop scene renderer
- the texture set is defined by the frame structure, not by subsystem choice
- abstracting it adds indirection without enabling meaningful polymorphism

### 12.3 Subsystem Services as Concrete Classes

Decision: each subsystem service is a concrete class, not an abstract
interface.

Rationale:

- passes and pipelines are engine-authored production code
- test isolation is achieved through the existing facade infrastructure
- abstract service interfaces would add vtable cost with no known second
  implementation

Exception: if a future subsystem has a genuine need for backend polymorphism
(e.g., conventional shadows vs VSM), the polymorphism lives inside the service
as an internal strategy, not at the service-to-renderer boundary.

### 12.4 No Global Clustered Deferred

Decision: clustered deferred shading is not part of the phase-1 desktop
lighting plan.

Rationale:

- UE5 deprecated its clustered deferred path
  (`ClusteredDeferredShadingPass.cpp:37`)
- directional fullscreen plus bounded-volume local-light deferred lighting is
  the standard Phase 3/4 approach
- clustered forward data remains available for forward consumers

## 13. Design Closure

The Vortex design is shaped around:

- `SceneRenderer` as a delegate owned by `Renderer`
- `SceneTextures` as the canonical texture product owned by the scene renderer
- subsystem services with concrete classes and domain-specific execution methods
- GBuffer base pass with MRT output replacing the legacy forward ShaderPass
  contract
- directional fullscreen plus bounded-volume local-light deferred lighting as
  the initial approach
- shared forward light data inside LightingService for translucency consumers
- inherited substrate (facades, composition, publication, upload) adapted
  mechanically
- shader modules organized by subsystem, mirroring UE5 ownership boundaries

All per-subsystem and per-stage designs are captured in the 18 LLD documents
under [`lld/`](lld/README.md). See [PLAN.md](./PLAN.md) for phased execution
and [IMPLEMENTATION-STATUS.md](./IMPLEMENTATION-STATUS.md) for progress
tracking.
