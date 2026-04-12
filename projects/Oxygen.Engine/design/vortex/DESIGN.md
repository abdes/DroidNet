# Vortex Renderer Design

Status: `initial design baseline`

This document captures the concrete solution shapes for the Vortex renderer:
subsystem service contracts, SceneTextures design, GBuffer layout, frame
orchestration mechanics, deferred lighting, shared forward light data, and
inherited substrate adaptation. It assumes the stable conceptual model defined
in [ARCHITECTURE.md](./ARCHITECTURE.md).

Related:

- [PRD.md](./PRD.md)
- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [PLAN.md](./PLAN.md)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md)

## 1. Design Summary

Vortex organizes around:

- a desktop scene renderer orchestrator (`SceneRenderer`) that
  owns frame-stage dispatch
- a `SceneTextures` product that carries GBuffer, depth, color, and velocity
  data across the frame
- capability-family subsystem services that own domain-specific GPU work
- inherited Renderer Core substrate (frame loop, views, composition, facades,
  publication, upload) adapted mechanically from the legacy renderer
- a shared forward light data service that lives inside the Lighting subsystem

The design should be read through the scene-renderer mental model:

- the scene renderer owns the frame structure and dispatches subsystems
- subsystems are services, not frame-structure owners
- passes are the lowest-level execution units
- everything else (publication, upload, composition) is substrate

## 2. SceneRenderer

### 2.1 Top-Level Shape

```cpp
class SceneRenderer {
public:
  explicit SceneRenderer(
    Renderer& renderer,
    SceneTexturesConfig config);

  // Per-frame hooks, called from Renderer frame loop
  void OnFrameStart(const FrameContext& frame);
  void OnPreRender(const FrameContext& frame);
  void OnRender(RenderContext& ctx);
  void OnCompositing(RenderContext& ctx);
  void OnFrameEnd(const FrameContext& frame);

private:
  Renderer& renderer_;
  SceneTextures scene_textures_;

  // Stage modules — bounded frame-stage execution units
  std::unique_ptr<InitViewsModule> init_views_;
  std::unique_ptr<DepthPrepassModule> depth_prepass_;
  std::unique_ptr<OcclusionModule> occlusion_;
  std::unique_ptr<BasePassModule> base_pass_;
  std::unique_ptr<TranslucencyModule> translucency_;
  // reserved: distortion_, light_shaft_bloom_

  // Subsystem services — null when capability is absent
  std::unique_ptr<LightingService> lighting_;
  std::unique_ptr<ShadowService> shadows_;
  std::unique_ptr<EnvironmentLightingService> environment_;
  std::unique_ptr<PostProcessService> post_process_;
  std::unique_ptr<DiagnosticsService> diagnostics_;
  // reserved: geometry_virtualization_, material_composition_,
  // indirect_lighting_, water_
};
```

### 2.2 Ownership Rules

1. The scene renderer is created by the Renderer during initialization, based
   on the active `CapabilitySet`.
2. The scene renderer owns `SceneTextures`, all stage module instances, and all
   subsystem service instances.
3. The Renderer delegates frame-phase work to the scene renderer after its own
   substrate-level work (context allocation, view management, upload staging).
4. The scene renderer dispatches stage modules and subsystem services in the
   correct frame-stage order. Neither stage modules nor subsystems control
   their own dispatch order.

### 2.3 Relationship to Renderer

The Renderer continues to own:

- frame loop lifecycle (attach, start, render, compositing, end, detach)
- `RenderContext` allocation and materialization
- view registration and canonical runtime state
- upload/staging services
- publication substrate
- composition planning, queueing, target resolution, and compositing execution
- non-runtime facades

The scene renderer is a delegate, not a replacement for the Renderer. Renderer
calls into the scene renderer at the appropriate frame phases. The scene
renderer calls into subsystem services and produces scene-view composition
intent, but Renderer retains ownership of composition queueing, target
resolution, and compositing execution.

### 2.4 Mode Selection

The scene renderer supports per-view mode selection:

- `ShadingMode::Deferred` — GBuffer base pass + deferred lighting (default)
- `ShadingMode::Forward` — forward shading using shared light data (optional)

```cpp
enum class ShadingMode : std::uint8_t {
  kDeferred,
  kForward,
};
```

Mode selection is per `CompositionView`. The scene renderer branches at the
base pass and lighting stages; all other stages (depth, occlusion, shadows,
environment, post-process) are shared.

## 3. SceneTextures Product

### 3.1 Allocation Shape

```cpp
struct SceneTexturesConfig {
  glm::uvec2 extent;
  bool enable_velocity{true};
  bool enable_custom_depth{false};
  std::uint32_t gbuffer_count{4};  // A-D by default, E-F optional
};

class SceneTextures {
public:
  explicit SceneTextures(
    graphics::IGraphics& gfx,
    const SceneTexturesConfig& config);

  // Core products — always valid after construction
  [[nodiscard]] auto GetSceneColor() const -> const TextureHandle&;
  [[nodiscard]] auto GetSceneDepth() const -> const TextureHandle&;
  [[nodiscard]] auto GetPartialDepth() const -> const TextureHandle&;

  // GBuffer products
  [[nodiscard]] auto GetGBuffer(GBufferIndex index) const
    -> const TextureHandle&;

  // Optional products — may be null
  [[nodiscard]] auto GetVelocity() const -> const TextureHandle*;
  [[nodiscard]] auto GetCustomDepth() const -> const TextureHandle*;
  [[nodiscard]] auto GetStencil() const -> const TextureHandle&;
  [[nodiscard]] auto GetScreenSpaceAO() const -> const TextureHandle*;

  // Lifecycle
  void Resize(glm::uvec2 new_extent);
  void RebuildWithGBuffers();  // Called after base pass
};
```

### 3.2 GBuffer Index Vocabulary

```cpp
enum class GBufferIndex : std::uint8_t {
  kA = 0,  // World normal (encoded)
  kB = 1,  // Metallic, specular, roughness
  kC = 2,  // Base color
  kD = 3,  // Custom data / shading model
  kE = 4,  // Precomputed shadow factors (optional)
  kF = 5,  // World tangent (optional)
};
```

### 3.3 GBuffer Format Baseline

Phase-1 GBuffer format targets:

| Buffer | Format | Content |
| --- | --- | --- |
| GBufferA | `R10G10B10A2_UNORM` | Encoded world normal |
| GBufferB | `R8G8B8A8_UNORM` | Metallic, specular, roughness, shading model ID |
| GBufferC | `R8G8B8A8_SRGB` | Base color, AO |
| GBufferD | `R8G8B8A8_UNORM` | Custom data (subsurface, cloth, etc.) |

GBufferE and GBufferF are deferred to after the initial deferred path works.
Their slots are reserved in the `SceneTextures` allocation.

### 3.4 SceneTextures Lifecycle in Frame

1. **Frame start:** SceneTextures exist from construction. Resize if viewport
   changed.
2. **Depth prepass:** writes to `SceneDepth` and optionally `PartialDepth`.
3. **Base pass:** writes to GBufferA–D + `SceneColor` (emissive accumulation).
4. **RebuildWithGBuffers():** marks GBuffer products as valid for downstream
   reads. This is not a re-allocation; it is a state transition.
5. **Deferred lighting → post-process:** all passes read from SceneTextures
   as needed.

## 4. Frame Orchestration Design

### 4.1 OnRender Dispatch

The scene renderer's `OnRender` method dispatches stages in UE5-aligned order.
This is the concrete implementation of the 23-stage frame structure from
ARCHITECTURE.md §6.

Illustrative shape:

```cpp
void SceneRenderer::OnRender(RenderContext& ctx) {
  // Stage 2: InitViews — visibility, culling, command generation
  init_views_->Execute(ctx, scene_textures_);

  // Stage 3: Depth prepass + early velocity
  depth_prepass_->Execute(ctx, scene_textures_);

  // Stage 4: reserved — GeometryVirtualizationService

  // Stage 5: Occlusion / HZB
  occlusion_->Execute(ctx, scene_textures_);

  // Stage 6: Forward light data
  if (lighting_) lighting_->BuildLightGrid(ctx);

  // Stage 7: reserved — MaterialCompositionService::PreBasePass

  // Stage 8: Shadow depth
  if (shadows_) shadows_->RenderShadowDepths(ctx);

  // Stage 9: Base pass — GBuffer MRT + velocity completion
  base_pass_->Execute(ctx, scene_textures_);

  // Stage 10: Rebuild scene textures (~50 lines, inline)
  scene_textures_.RebuildWithGBuffers();

  // Stage 11: reserved — MaterialCompositionService::PostBasePass

  // Stage 12: Deferred direct lighting
  if (lighting_) lighting_->RenderDeferredLighting(ctx, scene_textures_);

  // Stage 13: reserved — IndirectLightingService::Execute
  // Stage 14: reserved — EnvironmentLightingService volumetrics stages

  // Stage 15: Sky / atmosphere / fog
  if (environment_) environment_->RenderSkyAndFog(ctx, scene_textures_);

  // Stage 16: reserved — WaterService::Execute
  // Stage 17: reserved — post-opaque extensions

  // Stage 18: Translucency — forward-lit
  translucency_->Execute(ctx, scene_textures_);

  // Stage 19: reserved — DistortionModule::Execute
  // Stage 20: reserved — LightShaftBloomModule::Execute

  // Stage 21: Resolve scene color (~180 lines, file-separated method)
  ResolveSceneColor(ctx);

  // Stage 22: Post processing
  if (post_process_) post_process_->Execute(ctx, scene_textures_);

  // Stage 23: SceneRenderer-owned cleanup/extraction using Renderer Core helpers
  PostRenderCleanup(ctx);

  // Diagnostics overlay
  if (diagnostics_) diagnostics_->Execute(ctx, scene_textures_);
}
```

### 4.2 Dispatch Rules

1. Stage numbers correspond to ARCHITECTURE.md §6.2.
2. Stage modules are dispatched via `Execute(RenderContext&, SceneTextures&)`.
   Subsystem services are dispatched via their domain-specific methods.
3. Skipped stages (4, 7, 11, 13, 14, 16, 17, 19, 20) are reserved stubs.
   They become stage module or service dispatches when implemented.
4. Null subsystem pointers mean the stage is a no-op with zero overhead.
5. The scene renderer owns stage ordering; it does not delegate ordering to
   stage modules or subsystems.
6. Each dispatch receives `RenderContext` and, where needed, `SceneTextures`.

### 4.3 Per-View vs Per-Frame Stages

Some stages execute once per frame, others once per view:

| Per-frame | Per-view |
| --- | --- |
| Shadow depth rendering | Depth prepass |
| Light grid build | Occlusion / HZB |
| | Base pass |
| | Deferred lighting |
| | Translucency |
| | Post-processing |

The scene renderer iterates views for per-view stages while calling per-frame
stages once.

## 5. Subsystem Service Contracts

All subsystem services follow the same lifecycle contract pattern.

### 5.1 Base Service Lifecycle

```cpp
class ISubsystemService {
public:
  virtual ~ISubsystemService() = default;

  virtual void Initialize(
    graphics::IGraphics& gfx,
    const RendererConfig& config) = 0;

  virtual void OnFrameStart(const FrameContext& frame) = 0;
  virtual void Shutdown() = 0;
};
```

Subsystems extend this with domain-specific execution methods. There is no
forced common `ExecutePerViewPasses` interface — each subsystem exposes the
methods that match its place in the frame structure.

### 5.2 LightingService

```cpp
class LightingService : public ISubsystemService {
public:
  // Stage 6: build clustered light grid
  void BuildLightGrid(RenderContext& ctx);

  // Stage 12: fullscreen deferred lighting
  void RenderDeferredLighting(
    RenderContext& ctx,
    const SceneTextures& scene_textures);

  // Access to shared forward light data
  [[nodiscard]] auto GetForwardLightData() const
    -> const ForwardLightData*;
};
```

Forward light data shape follows UE5's `FForwardLightData` — a six-`float4`
local-light record:

```cpp
struct ForwardLocalLight {
  glm::vec4 position_and_inv_radius;
  glm::vec4 color_id_falloff_and_ray_bias;
  glm::vec4 direction_and_extra_data;
  glm::vec4 spot_angles_and_source_radius;
  glm::vec4 tangent_ies_and_specular_scale;
  glm::vec4 rect_data_and_shadow_data;
};
```

### 5.3 ShadowService

```cpp
class ShadowService : public ISubsystemService {
public:
  // Stage 8: render shadow depth maps
  void RenderShadowDepths(RenderContext& ctx);

  // Query shadow data for lighting
  [[nodiscard]] auto GetShadowData() const -> const ShadowFrameData&;

  // VSM support (optional, feature-gated)
  [[nodiscard]] auto HasVsm() const -> bool;
};
```

### 5.4 EnvironmentLightingService

```cpp
class EnvironmentLightingService : public ISubsystemService {
public:
  // Stage 14 reserved: volumetrics / heterogeneous volumes / clouds

  // Stage 15: sky atmosphere, fog, IBL
  void RenderSkyAndFog(
    RenderContext& ctx,
    const SceneTextures& scene_textures);

  // Access IBL data for lighting consumers
  [[nodiscard]] auto GetIblData() const -> const IblFrameData*;
};
```

### 5.5 PostProcessService

```cpp
class PostProcessService : public ISubsystemService {
public:
  // Stage 22: tonemap, bloom, exposure, AA
  void Execute(
    RenderContext& ctx,
    const SceneTextures& scene_textures);
};
```

### 5.6 DiagnosticsService

```cpp
class DiagnosticsService : public ISubsystemService {
public:
  // Per-frame configuration, logging, and profiler lifecycle
  void OnFrameStart(const FrameContext& frame) override;

  // Publish diagnostics-owned bindings (debug buffers, counters, optional views)
  void PublishBindings(RenderContext& ctx);

  // Fullscreen/debug visualization passes and overlays
  void Execute(
    RenderContext& ctx,
    const SceneTextures& scene_textures);

  // Dedicated GPU debug primitive lifecycle
  void ClearGpuDebugBuffers(RenderContext& ctx);
  void DrawGpuDebugPrimitives(RenderContext& ctx);

  // Typed shader-debug mode selection
  void SetShaderDebugMode(ShaderDebugMode mode);

  // Panel and sink registration
  void RegisterPanel(std::unique_ptr<DiagnosticsPanel> panel);
  void AddTimelineSink(std::shared_ptr<GpuTimelineSink> sink);

  // Fine-grained GPU trace control is routed through the unified profiling path;
  // backend-specific Tracy objects remain below this service boundary.
  void SetGpuProfilingEnabled(bool enabled);
};
```

DiagnosticsService owns the diagnostics-only CPU/GPU seam for Vortex:

- typed debug-mode routing and compatibility checks
- publication of `DebugFrameBindings` and related diagnostics payloads
- dedicated debug shaders and visualization passes
- GPU debug primitive resource management and presentation
- GPU timeline telemetry sinks and operator-facing panels
- renderer-facing profiling enablement and policy, with Tracy as the required
  fine-grained tracing backend behind the graphics backend seam

It does not own frame ordering, production scene-texture allocation, or other
services' domain logic. Producer stages and services may emit diagnostics data,
but DiagnosticsService owns the shared diagnostics infrastructure and
presentation path.

Profiling note:

- renderer/pass code continues to instrument GPU work through the engine's
  unified GPU scope API
- `DiagnosticsService` consumes the coarse telemetry side of that system
- backend-specific Tracy context creation, collection, and destruction remain
  below the service boundary in Graphics/Direct3D12 or future backend peers

## 6. Base Pass Design

### 6.1 GBuffer Write Contract

The base pass writes to all active GBuffer targets simultaneously using MRT
(multiple render targets):

| RT Slot | Output | Source |
| --- | --- | --- |
| 0 | GBufferA | Encoded world normal |
| 1 | GBufferB | Metallic, specular, roughness, shading model ID |
| 2 | GBufferC | Base color + AO |
| 3 | GBufferD | Custom data |
| 4 | SceneColor (emissive) | Material emissive contribution |
| DS | SceneDepth | Depth/stencil |

### 6.2 Base Pass Configuration

```cpp
struct BasePassConfig {
  bool write_velocity{true};
  bool early_z_pass_done{true};  // skip depth writes if prepass ran
  ShadingMode shading_mode{ShadingMode::kDeferred};
};
```

When `shading_mode` is `kForward`, the base pass writes to SceneColor directly
using forward shading instead of writing GBuffers. The depth/stencil output
remains the same.

### 6.3 Material Shader Contract

Base pass materials in deferred mode must output:

```hlsl
struct GBufferOutput {
  float4 GBufferA : SV_Target0;  // normal
  float4 GBufferB : SV_Target1;  // metallic/specular/roughness
  float4 GBufferC : SV_Target2;  // base color
  float4 GBufferD : SV_Target3;  // custom
  float4 Emissive : SV_Target4;  // emissive → SceneColor
};
```

This replaces the legacy `ShaderPass` single-target forward output contract.

## 7. Deferred Lighting Stage Design

### 7.1 Approach

Phase-1 deferred lighting uses fullscreen pass-per-light for simplicity:

- one fullscreen quad per directional light
- one stencil-bounded sphere per point light
- one stencil-bounded cone per spot light

This is the simplest correct deferred lighting approach. Tiled or clustered
deferred optimizations are explicitly deferred to later phases.

### 7.2 Deferred Light Pixel Shader Contract

The deferred light shader reads from GBuffer SRVs and outputs to SceneColor:

```hlsl
float4 DeferredLightPS(float2 uv : TEXCOORD0) : SV_Target {
  // Read from GBuffers via SceneTextures bindings
  float3 normal = DecodeNormal(GBufferA.Sample(sampler, uv));
  float3 baseColor = GBufferC.Sample(sampler, uv).rgb;
  float metallic = GBufferB.Sample(sampler, uv).r;
  float roughness = GBufferB.Sample(sampler, uv).b;
  float depth = SceneDepth.Sample(sampler, uv).r;

  // Reconstruct world position from depth
  float3 worldPos = ReconstructWorldPosition(uv, depth);

  // Evaluate BRDF + shadow
  return float4(EvaluateLight(worldPos, normal, baseColor,
                              metallic, roughness), 1.0);
}
```

### 7.3 Lighting Stage Inputs

| Input | Source |
| --- | --- |
| GBufferA–D | SceneTextures, written by base pass |
| SceneDepth | SceneTextures, written by depth prepass |
| Shadow data | ShadowService products |
| IBL data | EnvironmentLightingService products |
| Light list | LightingService internal state |

## 8. Inherited Substrate Adaptation

### 8.1 Mechanical Adaptation

The following substrate carries over with mechanical changes only:

| Change type | Scope |
| --- | --- |
| Namespace | `oxygen::engine` / `oxygen::renderer` → `oxygen::vortex` |
| Export macros | `OXGN_RNDR_*` → `OXGN_VRTX_*` |
| Include paths | `Oxygen/Renderer/...` → `Oxygen/Vortex/...` |
| Include guards | Updated if not using `#pragma once` |

### 8.2 Non-Runtime Facades

The three non-runtime facades carry over architecturally unchanged:

- `ForSinglePassHarness()` — one pass, validated context
- `ForRenderGraphHarness()` — one caller graph, validated context
- `ForOffscreenScene()` — scene-derived offscreen rendering

Their semantic boundaries, staging models, validation contracts, and
finalization contracts are stable per the modular-renderer DESIGN.md.

Adaptation for Vortex:

- facades operate against the Vortex `RenderContext` and `Renderer`
- `ForOffscreenScene()` can optionally use the scene renderer in forward mode
  for lightweight offscreen scenarios
- no new facades are introduced in the initial Vortex release

### 8.3 Composition Model

The queued composition model from the modular-renderer phase-1 design carries
over:

- `CompositionSubmission` remains the sole final handoff
- renderer queues multiple submissions per frame
- each submission remains single-target
- renderer drains the queue deterministically during late composition execution

### 8.4 Publication Model

Publication follows the modular-renderer phase-1 split:

- **Baseline publication** (Renderer Core): ViewConstants, core view-frame
  routing, core draw-frame routing, baseline view-color publication
- **Subsystem publication** (per-service): each subsystem service produces
  domain-specific per-view data through `PerViewStructuredPublisher`

### 8.5 RenderContext Adaptation

`RenderContext` adapts for the deferred path:

- pass-type registry starts empty; subsystem services extend it when registered
- `SceneTextures` bindings are available as a first-class binding set for
  deferred-consuming passes
- the context remains the authoritative execution context per modular-renderer
  design rules

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

### 10.1 Directory Structure

```
src/Oxygen/Core/Bindless/
├── BindlessHelpers.hlsl
└── Generated.BindlessAbi.hlsl

src/Oxygen/Graphics/Direct3D12/Shaders/
├── ShaderCatalogBuilder.h
├── EngineShaderCatalog.h
└── Vortex/
    ├── Contracts/
    │   ├── Definitions/
    │   │   ├── SceneDefinitions.hlsli
    │   │   ├── LightDefinitions.hlsli
    │   │   ├── LightGridDefinitions.hlsli
    │   │   └── ShadowDefinitions.hlsli
    │   ├── SceneTextures.hlsli
    │   ├── SceneTextureBindings.hlsli
    │   ├── GBufferLayout.hlsli
    │   ├── GBufferHelpers.hlsli
    │   ├── ViewFrameBindings.hlsli
    │   ├── LightData.hlsli
    │   ├── LightGridData.hlsli
    │   ├── ShadowData.hlsli
    │   ├── EnvironmentData.hlsli
    │   └── MaterialPayloads.hlsli
    ├── Shared/
    │   ├── FullscreenTriangle.hlsli
    │   ├── PositionReconstruction.hlsli
    │   ├── PackUnpack.hlsli
    │   ├── BRDFCommon.hlsli
    │   ├── DeferredShadingCommon.hlsli
    │   ├── ForwardLightingCommon.hlsli
    │   └── DebugVizCommon.hlsli
    ├── Materials/
    │   ├── MaterialTemplateAdapter.hlsli
    │   ├── MaterialEvaluation.hlsli
    │   └── GBufferMaterialOutput.hlsli
    ├── Stages/
    │   ├── InitViews/
    │   ├── DepthPrepass/
    │   ├── Occlusion/
    │   ├── BasePass/
    │   ├── Translucency/
    │   ├── Distortion/
    │   └── LightShaftBloom/
    └── Services/
        ├── Lighting/
        ├── Shadows/
        ├── Environment/
        ├── PostProcess/
        ├── Diagnostics/
        ├── MaterialComposition/
        ├── IndirectLighting/
        ├── Water/
        └── GeometryVirtualization/
```

### 10.2 Shader Module Rules

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
12. ShaderBake remains the only compilation path for Vortex shaders. Runtime
    continues to load the final archive only.
13. No shader file depends on legacy `Oxygen.Renderer` shader paths or
    compatibility wrappers.

## 11. Cross-Subsystem Data Flow

### 11.1 Data Product Dependencies

| Consumer | Products Consumed | Producer |
| --- | --- | --- |
| Occlusion / HZB | SceneDepth | Depth prepass |
| Shadow depth | Light list, view data | LightingService, InitViews |
| Base pass | Shadow maps (optional) | ShadowService |
| Deferred lighting | GBufferA–D, SceneDepth, shadow data, IBL | Base pass, ShadowService, EnvironmentService |
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

Decision: use pass-per-light fullscreen/stencil deferred for initial phase.

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
- pass-per-light deferred with stencil bounds is the standard approach
- clustered forward data remains available for forward consumers

## 13. Design Closure

The Vortex design is shaped around:

- `SceneRenderer` as a delegate owned by `Renderer`
- `SceneTextures` as the canonical texture product owned by the scene renderer
- subsystem services with concrete classes and domain-specific execution methods
- GBuffer base pass with MRT output replacing the legacy forward ShaderPass
  contract
- fullscreen pass-per-light deferred lighting as the initial approach
- shared forward light data inside LightingService for translucency consumers
- inherited substrate (facades, composition, publication, upload) adapted
  mechanically
- shader modules organized by subsystem, mirroring UE5 ownership boundaries

What remains is implementation planning (PLAN.md), not design uncertainty.
