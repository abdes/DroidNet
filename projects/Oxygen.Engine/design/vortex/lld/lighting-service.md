# LightingService LLD

**Phase:** 4A — Migration-Critical Services
**Deliverable:** D.9
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`LightingService` — the capability-family service owning two frame stages:

- **Stage 6**: clustered light-grid build and publication of the forward-light
  family as shared supporting data
- **Stage 12**: deferred direct lighting (fullscreen pass-per-light → SceneColor)

Phase 4A migrates the Phase 3 inline deferred lighting into this service and
adds the clustered light-grid that feeds forward consumers such as
translucency and later optional optimizations.

### 1.2 What It Replaces

The Phase 3 inline `SceneRenderer::RenderDeferredLighting(ctx, scene_textures)`
method moves into `LightingService::RenderDeferredLighting()`.

The important constraint is what does **not** change: stage 12 remains the
canonical correctness-first deferred direct-lighting stage using per-light
deferred dispatch. The new stage-6 forward-light publication family is shared
supporting data, not the new definition of deferred lighting.

### 1.3 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — subsystem service contracts
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stages 6, 12
- UE5 reference: `PrepareForwardLightData`, `ComputeLightGrid`, `RenderLights`

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── Services/
    └── Lighting/
        ├── LightingService.h
        ├── LightingService.cpp
        ├── Internal/
        │   ├── LightGridBuilder.h/.cpp
        │   └── ForwardLightDataProvider.h/.cpp
        ├── Passes/
        │   ├── DeferredLightPass.h/.cpp
        │   └── LightGridBuildPass.h/.cpp
        └── Types/
            ├── ForwardLightFrameBindings.h
            ├── ForwardLocalLightRecord.h
            └── LightGridMetadata.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class LightingService : public ISubsystemService {
 public:
  explicit LightingService(Renderer& renderer);
  ~LightingService() override;

  // ISubsystemService lifecycle
  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 6: build clustered light grid. Per-frame (shared data).
  void BuildLightGrid(RenderContext& ctx);

  /// Stage 12: deferred direct lighting. Per-view SceneColor accumulation.
  void RenderDeferredLighting(RenderContext& ctx,
                               const SceneTextures& scene_textures);

 private:
  Renderer& renderer_;
  std::unique_ptr<LightGridBuilder> light_grid_builder_;
  std::unique_ptr<ForwardLightDataProvider> forward_data_;
  std::unique_ptr<DeferredLightPass> deferred_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 ForwardLocalLight Record

Six `float4`-aligned structured record (96 bytes), matching the UE5-style
local-light record convention. This record is **not** the whole published
per-view forward-light contract; it is only the shared local-light storage
element:

```cpp
struct ForwardLocalLightRecord {
  glm::vec4 position_and_inv_radius;
  glm::vec4 color_id_falloff_and_ray_bias;
  glm::vec4 direction_and_extra_data;
  glm::vec4 spot_angles_and_source_radius;
  glm::vec4 tangent_ies_and_specular_scale;
  glm::vec4 rect_data_and_linkage;
};
```

### 2.4 Published Forward-Light Package

```cpp
struct ForwardLightFrameBindings {
  // Shared storage
  uint32_t local_light_buffer_srv{kInvalidIndex};

  // Per-view clustered access package
  uint32_t grid_metadata_buffer_srv{kInvalidIndex};
  uint32_t grid_indirection_srv{kInvalidIndex};

  // Directional-light selection for forward consumers
  uint32_t selected_directional_light_index{kInvalidIndex};
  uint32_t directional_light_count{0};
  uint32_t local_light_count{0};
  uint32_t flags{0};
};
```

### 2.5 Per-View Publication

The lighting service publishes `ForwardLightFrameBindings` through
`LightingFrameBindings`, which is then routed through `ViewFrameBindings`.
Consumers access the forward-light family through the published per-view
binding stack; they do not reach into `LightingService` internals directly.

## 3. Data Flow and Dependencies

### 3.1 Stage 6 — BuildLightGrid

| Input | Source | Purpose |
| ----- | ------ | ------- |
| Scene light list | Engine scene graph | Light positions, colors, types, radii |
| View frustums | Published CompositionViews | Cluster Z-slicing per view |

| Output | Consumer | Delivery |
| ------ | -------- | -------- |
| `ForwardLocalLightRecord` buffer | Published forward-light family | Shared storage owned by LightingService |
| `ForwardLightFrameBindings` | Stage 18 (translucency), forward-only materials, diagnostics | Published through `LightingFrameBindings` / `ViewFrameBindings` |
| Deferred-light draw list | Stage 12 (deferred lighting) | Internal service-owned per-light direct-light data |

### 3.2 Stage 12 — RenderDeferredLighting

| Input | Source | Purpose |
| ----- | ------ | ------- |
| GBufferNormal/Material/BaseColor/CustomData (SRV) | SceneTextures (from stage 10) | Material data for BRDF |
| SceneDepth (SRV) | SceneTextures | Position reconstruction |
| `ShadowFrameBindings` | ShadowService (stage 8 publication) | Shadow attenuation terms |
| Deferred-light draw list | Self | Canonical per-light direct-light parameters |
| `EnvironmentFrameBindings` | Environment service publication | Optional bounded ambient bridge only when that Phase 4 exception is explicitly enabled |

| Output | Target | Blend Mode |
| ------ | ------ | ---------- |
| SceneColor | SceneTextures | Additive (ONE, ONE) |

Stage 12 remains the canonical deferred **direct**-lighting stage. If Phase 4
needs some environment ambient visible before `IndirectLightingService` exists,
the only allowed shortcut is a narrowly documented ambient bridge that consumes
already-published environment probe data. That bridge is a migration aid, not a
redefinition of stage 12 ownership.

### 3.3 Sequence Diagram

```text
SceneRenderer::OnRender(ctx)
  ├─ Stage 6:  lighting_->BuildLightGrid(ctx)
  │              └─ Builds local-light record storage
  │              └─ Publishes per-view forward-light bindings
  │              └─ Prepares internal deferred-light draw data
  ├─ ...stages 7-11...
  └─ Stage 12: lighting_->RenderDeferredLighting(ctx, scene_textures)
                 └─ For each light: canonical fullscreen or bounded-volume deferred draw
                 └─ Accumulates into SceneColor
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Forward-local-light structured buffer | Per frame | Upload ring buffer |
| Light-grid metadata / indirection buffers | Per frame | Per-view clustered access data |
| Deferred-light draw payloads | Per frame | Internal direct-light parameters |
| Light volume geometry (sphere, cone) | Persistent | Canonical permanent owner; replaces the temporary Phase 03 procedural `SV_VertexID` local-light proxy generation |
| Deferred lighting PSOs | Persistent | Cached by renderer |

## 5. Shader Contracts

### 5.1 Light Grid Compute Shader

```hlsl
// Services/Lighting/LightGridBuild.hlsl
// Builds per-view clustered light grid via compute shader.
// Dispatch: (grid_x * grid_y * grid_z) / 64 thread groups
```

### 5.2 Deferred Light Shaders

Carried forward from Phase 3 (see [deferred-lighting.md](deferred-lighting.md) §5),
with the deferred-light family remaining the canonical direct-light path. The
forward-light buffers published at stage 6 are owned by the same service
family, but they are for forward consumers and future optional optimizations,
not a required replacement for per-light deferred-light payloads.

```hlsl
// Deferred-light shaders keep their explicit per-light payload contract.
cbuffer DeferredLightConstants : register(b1) {
  float4 LightPositionAndRadius;
  float4 LightColorAndIntensity;
  float4 LightDirectionAndFalloff;
  float4 SpotAngles;
}

// LightingService also owns family-local common files for forward-light
// publication and future optional clustered consumers:
//   Services/Lighting/LightGridCommon.hlsli
//   Services/Lighting/ForwardLightingCommon.hlsli
//   Services/Lighting/DeferredLightingCommon.hlsli
//   Services/Lighting/DeferredShadingCommon.hlsli
```

### 5.3 Catalog Additions (Phase 4A)

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexLightGridBuildCS` | cs_6_0 | Compute: clustered grid build |

Phase 3 deferred-light entrypoints remain canonical for stage 12 in Phase 4A.

### 5.4 Phase 4A Geometry Ownership Cutover

Phase 4A is required to remove the temporary Phase 03 procedural point/spot
proxy-generation path. The permanent `LightingService` implementation owns:

- persistent sphere proxy geometry for point lights
- persistent cone proxy geometry for spot lights
- the buffer lifetime, initialization/upload path, and cache identity for
  those proxies

Phase 4A completion is not truthful until the retained `SV_VertexID`
procedural proxy-generation shortcut from Phase 03 is gone from the canonical
Stage 12 runtime path.

## 6. Stage Integration

### 6.1 Dispatch Contract

- Stage 6: `lighting_->BuildLightGrid(ctx)` — per frame, before shadows
- Stage 12: `lighting_->RenderDeferredLighting(ctx, scene_textures)` — per view

### 6.2 Null-Safe Behavior

When `lighting_` is null: stages 6 and 12 are skipped. No light grid, no
deferred lighting. SceneColor retains only emissive from BasePass.

### 6.3 Capability Gate

Requires `kLightingData` + `kDeferredShading`.

## 7. Migration from Phase 3 Inline

1. Move `SceneRenderer::RenderDeferredLighting()` body into
   `LightingService::RenderDeferredLighting()`.
2. Replace the temporary Phase 03 procedural point/spot proxy generation with
   persistent `LightingService`-owned sphere/cone geometry.
3. Add `BuildLightGrid()` (new, stage 6) and publish the forward-light family
   through `LightingFrameBindings` / `ViewFrameBindings`.
4. Keep the deferred-light draw contract separate from the published
   forward-light package so stage 12 stays canonically per-light.
5. If Phase 4 enables the temporary ambient bridge, document it explicitly as
   environment-probe sampling from already-published persistent state.
6. Wire SceneRenderer dispatch: `lighting_->BuildLightGrid(ctx)` at stage 6,
   `lighting_->RenderDeferredLighting(ctx, scene_textures)` at stage 12.

## 8. Testability Approach

1. **Light grid validation:** Mock scene with known lights → verify grid
   cell occupancy matches expected light coverage.
2. **Deferred lighting comparison:** Same scene rendered with Phase 3 inline
   vs Phase 4A service → pixel-identical SceneColor output.
3. **Forward data publication:** Inspect the published
   `LightingFrameBindings` / `ViewFrameBindings` payload for the current view →
   verify counts and SRVs are valid.
4. **RenderDoc:** Frame 10, verify light grid buffer contents and deferred
   lighting draw calls.

## 9. Open Questions

None. The key Phase 4 design decision is already fixed here: the clustered
forward-light family is shared supporting data, while stage 12 remains the
canonical deferred direct-lighting path.
