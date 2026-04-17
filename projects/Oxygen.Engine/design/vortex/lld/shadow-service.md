# ShadowService LLD

**Phase:** 4C — Migration-Critical Services
**Deliverable:** D.11
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`ShadowService` — the stage-8 owner responsible for shadow depth map
production. Phase 4C implements conventional cascaded shadow maps for
directional lights first, with spot-light shadows optional and point-light
shadow strategy explicitly deferred pending a dedicated design decision. The
service produces typed shadow publications consumed by deferred lighting at
stage 12.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 6 (BuildLightGrid) — light data available | |
| **This** | **Stage 8 — Shadow Depths** | Shadow map production |
| Successor | Stage 9 (BasePass) — BasePass does not consume shadows directly | |

Shadows are consumed at stage 12 (deferred lighting) and stage 18
(translucency forward lighting).

### 1.3 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — subsystem services
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage 8

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── Services/
    └── Shadows/
        ├── ShadowService.h
        ├── ShadowService.cpp
        ├── Internal/
        │   ├── CascadeShadowSetup.h/.cpp
        │   ├── ShadowAtlasAllocator.h/.cpp
        │   └── ShadowCulling.h/.cpp
        ├── Passes/
        │   ├── ShadowDepthPass.h/.cpp
        │   └── CascadeShadowPass.h/.cpp
        ├── Types/
        │   ├── ShadowFrameData.h
        │   └── ShadowCascadeData.h
        └── Vsm/
            └── Internal/               ← reserved (empty in Phase 4C)
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class ShadowService : public ISubsystemService {
 public:
  explicit ShadowService(Renderer& renderer);
  ~ShadowService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 8: render shadow depth maps. Per-frame execution.
  void RenderShadowDepths(RenderContext& ctx);

  /// CPU-side inspection hook for tests and diagnostics.
  [[nodiscard]] auto InspectShadowData() const -> const ShadowFrameData&;

  /// VSM support check. Returns false in Phase 4C.
  [[nodiscard]] auto HasVsm() const -> bool;

 private:
  Renderer& renderer_;
  ShadowFrameData shadow_data_;
  std::unique_ptr<CascadeShadowSetup> cascade_setup_;
  std::unique_ptr<ShadowAtlasAllocator> atlas_allocator_;
  std::unique_ptr<ShadowDepthPass> depth_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 ShadowFrameData

```cpp
struct ShadowCascadeData {
  glm::mat4 view_projection;       // Light-space VP matrix
  float split_near;                 // Cascade near distance
  float split_far;                  // Cascade far distance
  glm::vec4 shadow_bounds;          // Atlas region (x, y, width, height)
};

struct ShadowFrameData {
  // Directional light cascades
  static constexpr uint32_t kMaxCascades = 4;
  uint32_t cascade_count{0};
  ShadowCascadeData cascades[kMaxCascades];

  // Shadow atlas
  uint32_t atlas_srv{kInvalidIndex};         // Bindless SRV index
  uint32_t atlas_width{2048};
  uint32_t atlas_height{2048};

  // Per-light shadow data for local lights
  struct LocalLightShadow {
    glm::mat4 view_projection;
    glm::vec4 atlas_region;       // UV rect in atlas
    float bias;
    float normal_bias;
  };
  std::vector<LocalLightShadow> local_shadows;
};
```

### 2.4 Per-View Publication

ShadowFrameData is published per-view through `ShadowFrameBindings` in
`ViewFrameBindings`, making shadow data accessible to lighting and later
translucency shaders. GPU consumers use the publication seam; CPU inspection
uses `InspectShadowData()` only for tests / diagnostics.

`PreparedSceneFrame` remains the Stage-2 prepared-scene packet and
intentionally does not carry shadow caster bounds, receiver bounds, or
conventional shadow draw records. Shadow payload enters the per-view
publication stack only once `ShadowService` has produced it at stage 8.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Scene | Light list (directional, point, spot) | Shadow casters |
| Scene | Geometry (shadow-casting meshes) | Draw commands |
| Views | Camera frustums | Cascade split computation |
| GeometryUploader | Vertex/index buffers | Mesh data |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| `ShadowFrameBindings` | LightingService (stage 12) | Published through `ViewFrameBindings` |
| `ShadowFrameBindings` | TranslucencyModule (stage 18) | Published through `ViewFrameBindings` when that stage activates |
| CPU inspection view of `ShadowFrameData` | Tests / diagnostics | Via `InspectShadowData()` only |

### 3.3 Execution Flow

```text
ShadowService::RenderShadowDepths(ctx)
  │
  ├─ Compute cascade splits for directional light
  │     └─ PSSM (Practical Split Scheme Maps) split distances
  │     └─ Per-cascade: compute light VP matrix, atlas region
  │
  ├─ Allocate conventional shadow targets
  │     └─ Spot lights: optional single-perspective allocation in Phase 4C
  │     └─ Point lights: deferred pending explicit storage strategy
  │
  ├─ for each cascade / local shadow:
  │     ├─ Set viewport to atlas region
  │     ├─ Set render target: shadow atlas (depth-only DSV)
  │     ├─ Cull geometry to light frustum
  │     └─ Draw shadow-casting geometry (depth-only)
  │
  └─ Populate ShadowFrameData and publish per-view ShadowFrameBindings
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Shadow atlas texture | Persistent | Reallocated if resolution changes |
| Shadow depth DSV | Persistent | Same texture, different view per region |
| Per-cascade VP matrices | Per frame | Computed from camera + light |
| Shadow depth PSO | Persistent | Shared with depth prepass variant |

### 4.1 Allocator Policy

Phase 4C may start with one simple atlas allocator, but the **contract**
must not freeze that shape. The design must allow:

- one simple atlas as an initial implementation
- dedicated CSM targets or atlas partitions for cascades
- a non-atlas point-light path (for example cubemap depth targets) if that
  becomes the selected strategy
- future VSM payloads without inheriting the conventional-atlas ABI

The fixed texture layout is therefore an implementation choice, not part of
the long-lived interface contract.

## 5. Shader Contracts

### 5.1 Shadow Depth Shader

```hlsl
// Services/Shadows/ShadowDepth.hlsl
// Minimal VS for shadow depth rendering.

cbuffer ShadowViewConstants : register(b0) {
  float4x4 LightViewProjection;
  float DepthBias;
  float NormalBias;
};

struct ShadowVSOutput {
  float4 position : SV_Position;
};

ShadowVSOutput ShadowDepthVS(float3 pos : POSITION, uint instanceId : SV_InstanceID) {
  float4x4 world = LoadInstanceTransform(instanceId);
  float4 worldPos = mul(world, float4(pos, 1.0));
  ShadowVSOutput output;
  output.position = mul(LightViewProjection, worldPos);
  output.position.z += DepthBias;
  return output;
}
```

### 5.2 Shadow Sampling (in Lighting Shaders)

```hlsl
// Contracts/ShadowData.hlsli

float SampleShadowCascade(float3 worldPos, uint cascadeIndex,
                            ShadowFrameBindingData shadow) {
  float4 lightSpacePos = mul(shadow.cascade_vp[cascadeIndex], float4(worldPos, 1.0));
  float2 shadowUV = lightSpacePos.xy / lightSpacePos.w * 0.5 + 0.5;
  shadowUV.y = 1.0 - shadowUV.y;

  // Offset into atlas region
  shadowUV = shadowUV * shadow.cascade_bounds[cascadeIndex].zw
           + shadow.cascade_bounds[cascadeIndex].xy;

  float shadowDepth = ShadowAtlas.SampleCmpLevelZero(
    ShadowSampler, shadowUV, lightSpacePos.z / lightSpacePos.w);
  return shadowDepth;
}
```

### 5.3 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexShadowDepthVS` | vs_6_0 | Depth-only shadow rendering |
| `VortexShadowDepthMaskedPS` | ps_6_0 | Alpha-tested shadow |

## 6. Stage Integration

### 6.1 Dispatch Contract

`shadows_->RenderShadowDepths(ctx)` at stage 8. The service may perform
frame-shared allocation / cache work, but the published shadow payload remains
per-view. Directional-shadow sharing across compatible views is an optimization
only, not the default semantic.

### 6.2 Null-Safe Behavior

When null: no shadow maps produced. Deferred lighting renders without
shadow terms (fully lit). Published `ShadowFrameBindings` are empty.

### 6.3 Capability Gate

Requires `kShadowing`.

## 7. Testability Approach

1. **Cascade validation:** Single directional light, known scene → verify
   cascade splits cover camera frustum correctly.
2. **Shadow visual:** Ground plane under directional light → visible shadow
   from occluder.
3. **Conventional target inspection:** RenderDoc frame 10, inspect the chosen
   shadow targets. Verify cascade regions contain valid depth data.
4. **Shadow terms:** Compare lit/shadowed pixels in SceneColor — shadowed
   regions should be darker by shadow attenuation factor.

## 8. Open Questions

1. **Point light shadows:** Explicitly deferred beyond the Phase 4C baseline
   until the storage strategy is chosen.
2. **VSM integration:** `Shadows/Vsm/` directory exists but is empty.
   Virtual shadow maps are Phase 7C scope.
