# LightingService LLD

**Phase:** 4A — Migration-Critical Services
**Deliverable:** D.9
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`LightingService` — the capability-family service owning two frame stages:

- **Stage 6**: clustered light-grid build (`ForwardLightData` product)
- **Stage 12**: deferred direct lighting (fullscreen pass-per-light → SceneColor)

Phase 4A migrates the Phase 3 inline deferred lighting into this service
and adds the clustered light-grid that feeds both deferred (stage 12) and
forward (stage 18 translucency) consumers.

### 1.2 What It Replaces

The Phase 3 inline `SceneRenderer::RenderDeferredLighting(ctx, scene_textures)`
method moves
into `LightingService::RenderDeferredLighting()`. The Phase 3 per-light
CBV approach is replaced with a `ForwardLightData` structured buffer.

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
            ├── ForwardLightData.h
            ├── ForwardLocalLight.h
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

  /// Access shared forward light data for translucency and other consumers.
  [[nodiscard]] auto GetForwardLightData() const
      -> const ForwardLightData*;

 private:
  Renderer& renderer_;
  std::unique_ptr<LightGridBuilder> light_grid_builder_;
  std::unique_ptr<ForwardLightDataProvider> forward_data_;
  std::unique_ptr<DeferredLightPass> deferred_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 ForwardLocalLight Record

Six `float4`-aligned structured record (96 bytes), matching UE5
`FForwardLightData` layout:

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

### 2.4 ForwardLightData Product

```cpp
struct ForwardLightData {
  std::vector<ForwardLocalLight> local_lights;
  uint32_t directional_light_count{0};
  uint32_t local_light_count{0};

  // Light grid metadata (per-view)
  struct GridMetadata {
    uint32_t grid_x, grid_y, grid_z;     // cluster dimensions
    float z_near, z_far;                   // Z slicing range
    uint32_t total_cells;
  };
  GridMetadata grid;

  // GPU buffer handles
  uint32_t light_buffer_srv{kInvalidIndex};      // Structured buffer SRV
  uint32_t grid_indirection_srv{kInvalidIndex};  // Cell → light index SRV
};
```

### 2.5 Per-View Publication

ForwardLightData is published through `ViewFrameBindings` via
`PerViewStructuredPublisher`. Each view receives light grid indices routed
through the per-view binding stack.

## 3. Data Flow and Dependencies

### 3.1 Stage 6 — BuildLightGrid

| Input | Source | Purpose |
| ----- | ------ | ------- |
| Scene light list | Engine scene graph | Light positions, colors, types, radii |
| View frustums | Published CompositionViews | Cluster Z-slicing per view |

| Output | Consumer | Delivery |
| ------ | -------- | -------- |
| ForwardLightData | Stage 12 (deferred lighting) | Via `GetForwardLightData()` |
| ForwardLightData | Stage 18 (translucency) | Via `GetForwardLightData()` |
| Light grid SRV | All lighting consumers | Published in ViewFrameBindings |

### 3.2 Stage 12 — RenderDeferredLighting

| Input | Source | Purpose |
| ----- | ------ | ------- |
| GBufferA–D (SRV) | SceneTextures (from stage 10) | Material data for BRDF |
| SceneDepth (SRV) | SceneTextures | Position reconstruction |
| ShadowFrameData | ShadowService (stage 8) | Shadow attenuation terms |
| IblFrameData | EnvironmentService (stage 15) | Ambient contribution |
| ForwardLightData | Self (stage 6) | Light parameters |

| Output | Target | Blend Mode |
| ------ | ------ | ---------- |
| SceneColor | SceneTextures | Additive (ONE, ONE) |

### 3.3 Sequence Diagram

```text
SceneRenderer::OnRender(ctx)
  ├─ Stage 6:  lighting_->BuildLightGrid(ctx)
  │              └─ Builds ForwardLightData structured buffer
  │              └─ Publishes to ViewFrameBindings
  ├─ ...stages 7-11...
  └─ Stage 12: lighting_->RenderDeferredLighting(ctx, scene_textures)
                 └─ For each light: fullscreen/stencil-bounded draw
                 └─ Accumulates into SceneColor
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| ForwardLightData structured buffer | Per frame | Upload ring buffer |
| Light grid indirection buffer | Per frame | Per-view grid data |
| Light volume geometry (sphere, cone) | Persistent | Shared with Phase 3 |
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
now with ForwardLightData structured buffer access instead of per-light CBV:

```hlsl
// Access ForwardLocalLight from structured buffer
StructuredBuffer<ForwardLocalLight> LightBuffer : register(t0, space1);
uint LightIndex;  // from light grid or per-draw constant

ForwardLocalLight GetLight() {
  return LightBuffer[LightIndex];
}
```

### 5.3 Catalog Additions (Phase 4A)

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexLightGridBuildCS` | cs_6_0 | Compute: clustered grid build |

Phase 3 deferred light entrypoints unchanged.

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
2. Replace per-light CBV with `ForwardLightData` structured buffer reads.
3. Add `BuildLightGrid()` (new, stage 6).
4. Add `ForwardLightData` publication to ViewFrameBindings.
5. Wire SceneRenderer dispatch: `lighting_->BuildLightGrid(ctx)` at stage 6,
   `lighting_->RenderDeferredLighting(ctx, scene_textures)` at stage 12.

## 8. Testability Approach

1. **Light grid validation:** Mock scene with known lights → verify grid
   cell occupancy matches expected light coverage.
2. **Deferred lighting comparison:** Same scene rendered with Phase 3 inline
   vs Phase 4A service → pixel-identical SceneColor output.
3. **Forward data publication:** Query `GetForwardLightData()` → verify
   local light count and buffer SRV are valid.
4. **RenderDoc:** Frame 10, verify light grid buffer contents and deferred
   lighting draw calls.

## 9. Open Questions

None — lightingService is the most well-specified Phase 4 service. The
clustered grid approach follows established UE5/industry patterns.
