# ShadowService LLD

**Phase:** 4C - Migration-Critical Services
**Deliverable:** D.11
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`ShadowService` is the Stage-8 owner for **directional-first conventional
shadow-map production** in Phase 4C.

Phase 4C covers:

- directional conventional shadow-map setup
- directional shadow depth rendering at Stage 8
- per-view publication of the directional conventional shadow product
- later consumption of that published directional shadow product by Stage 12

Phase 4C does **not** claim:

- spot-light conventional shadows
- point-light conventional shadows
- local-light conventional shadow publication
- VSM activation

Those remain future `ShadowService` work and are not part of the truthful Phase
4C baseline.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 6 (`LightingService::BuildLightGrid`) | directional-light authority and forward-light publication already resolved |
| **This** | **Stage 8 - Shadow Depths** | conventional directional shadow production |
| Successor | Stage 9 (`BasePass`) | base pass does not consume the shadow product directly |

The published directional shadow product is consumed later by:

- Stage 12 deferred direct lighting
- Stage 18 translucency when that family activates

### 1.3 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 8 - subsystem service ownership
- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 6.2 - Stage 8 ownership
- [PLAN.md](../PLAN.md) Section 6 - Phase 4C directional-first scope
- [shadow-local-lights.md](./shadow-local-lights.md) - future local-light
  conventional shadow expansion

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
`-- Services/
    `-- Shadows/
        |-- ShadowService.h
        |-- ShadowService.cpp
        |-- Internal/
        |   |-- CascadeShadowSetup.h/.cpp
        |   |-- ConventionalShadowTargetAllocator.h/.cpp
        |   `-- ShadowCasterCulling.h/.cpp
        |-- Passes/
        |   |-- ShadowDepthPass.h/.cpp
        |   `-- CascadeShadowPass.h/.cpp
        |-- Types/
        |   |-- FrameShadowInputs.h
        |   |-- ShadowFrameBindings.h
        |   `-- ShadowCascadeBinding.h
        `-- Vsm/
            `-- Internal/   <- reserved and empty in Phase 4C
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

struct PreparedViewShadowInput {
  ViewId view_id;
  observer_ptr<const PreparedSceneFrame> prepared_scene;
  observer_ptr<const CompositionView> composition_view;
};

struct FrameShadowInputs {
  const FrameLightSelection& frame_light_set;
  std::span<const PreparedViewShadowInput> active_views;
};

class ShadowService : public ISubsystemService {
 public:
  explicit ShadowService(Renderer& renderer);
  ~ShadowService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 8: render directional conventional shadow depths and publish one
  /// per-view directional shadow payload for each active view.
  void RenderShadowDepths(const FrameShadowInputs& inputs);

  /// CPU-side inspection hook for tests/diagnostics of one view's directional
  /// shadow publication.
  [[nodiscard]] auto InspectShadowData(ViewId view_id) const
    -> const DirectionalShadowFrameData*;

  /// VSM support check. Returns false in Phase 4C.
  [[nodiscard]] auto HasVsm() const -> bool;

 private:
  Renderer& renderer_;
  std::unique_ptr<CascadeShadowSetup> cascade_setup_;
  std::unique_ptr<ConventionalShadowTargetAllocator> allocator_;
  std::unique_ptr<ShadowDepthPass> depth_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 Published Directional Shadow Contract

`ShadowFrameBindings` is the canonical GPU-facing publication seam for Phase 4C.
In Phase 4C it describes a **directional conventional shadow product**. The
published contract is intentionally consumer-neutral: it does not freeze one
atlas packing layout, one DSV-view scheme, or one later VSM migration ABI.

```cpp
struct ShadowCascadeBinding {
  glm::mat4 light_view_projection;
  float split_near{0.0f};
  float split_far{0.0f};
  glm::vec4 sampling_metadata0{0.0f};  // service-defined consumer metadata
  glm::vec4 sampling_metadata1{0.0f};  // service-defined consumer metadata
};

struct ShadowFrameBindings {
  static constexpr uint32_t kMaxCascades = 4;

  uint32_t conventional_shadow_surface_handle{kInvalidIndex};
  uint32_t cascade_count{0};
  uint32_t technique_flags{0};
  uint32_t sampling_contract_flags{0};

  ShadowCascadeBinding cascades[kMaxCascades];
};

struct DirectionalShadowFrameData {
  ShadowFrameBindings bindings;
  glm::uvec2 backing_resolution{0, 0};
  uint32_t storage_flags{0};
};
```

### 2.4 Directional-Light Authority

Phase 4C does not run an independent directional-light election.

`ShadowService` consumes the per-frame directional-light selection already
resolved earlier in frame execution. If no selected directional light exists
for a view, the published `ShadowFrameBindings` payload for that view is empty.

### 2.5 Per-View Publication

The service publishes one `ShadowFrameBindings` payload per active view through
`ViewFrameBindings`.

This is distinct from any frame-shared depth-resource allocation:

- backing depth resources may be frame-shared or implementation-specific
- the published payload remains per-view and multi-view-safe
- CPU inspection is keyed by view identity and is not the GPU-facing contract

`PreparedSceneFrame` remains the Stage-2 prepared-scene packet. The shadow
publication seam begins at Stage 8 and is not silently smuggled into
`PreparedSceneFrame` as a second long-lived shadow ABI.

Phase 4 replaces the current Phase 3 interim shadow-binding shape with the
target contract in Section 2.3. The CPU struct, HLSL-side sampling contract,
and `ViewFrameBindings` routing must evolve together; the old slot-based
interim shape is not the long-lived Phase 4 contract.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Renderer Core frame-light selection | selected directional light | authoritative light source for the directional baseline consumed by Stage 8 |
| Active prepared views | per-view frusta and publication targets | cascade split computation and per-view publication |
| Scene / prepared-scene draw source | shadow-casting geometry references | directional shadow depth draws |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| `ShadowFrameBindings` | `LightingService` (Stage 12) | Published through `ViewFrameBindings` |
| `ShadowFrameBindings` | `TranslucencyModule` (Stage 18) | Published through `ViewFrameBindings` when that stage activates |
| CPU inspection view of `DirectionalShadowFrameData` | Tests / diagnostics | `InspectShadowData(ViewId)` only |

Phase 4C publishes **directional** shadow data only. Local-light conventional
shadow bindings remain future work and are not exposed through the Phase 4C
payload.

### 3.3 Execution Flow

```text
ShadowService::RenderShadowDepths(frame_shadow_inputs)
  |
  |- Resolve selected directional light from the frame-light selection
  |
  |- For each active view:
  |    |- Compute directional cascade splits
  |    |- Compute light-space matrices and consumer-facing addressing metadata
  |    |- Allocate / reference conventional backing resources
  |    |- Cull shadow casters to the directional cascade frusta
  |    |- Render depth-only directional shadow draws
  |    `- Publish one ShadowFrameBindings payload for that view
  |
  `- Keep CPU inspection state keyed by ViewId only for diagnostics/tests
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Conventional shadow depth backing resource | Persistent or frame-persistent implementation detail | initial implementation may use atlas-backed storage |
| Shadow depth DSV views | Persistent / reused | implementation detail |
| Per-view directional cascade metadata | Per frame | published through `ShadowFrameBindings` |
| Shadow depth PSO | Persistent | shared with masked shadow variants |

### 4.1 Storage Policy

Phase 4C may start with one simple conventional backing layout, but that choice
is **not** the long-lived public ABI.

The design must allow:

- an atlas-backed initial implementation for cascades
- dedicated cascade targets if that proves cleaner
- later local-light conventional targets without inheriting the directional
  storage ABI
- later VSM activation without inheriting the conventional storage ABI

The public contract therefore exposes:

- per-cascade transforms
- split distances
- consumer-facing sampling metadata
- technique/capability flags

It does **not** freeze atlas UV math or a single 2D storage shape as the
architectural contract.

## 5. Shader Contracts

### 5.1 Shadow Depth Shader

```hlsl
// Services/Shadows/ShadowDepth.hlsl

cbuffer ShadowViewConstants : register(b0) {
  float4x4 LightViewProjection;
  float DepthBias;
  float NormalBias;
};

struct ShadowVSOutput {
  float4 position : SV_Position;
};

ShadowVSOutput ShadowDepthVS(float3 pos : POSITION,
                             uint instance_id : SV_InstanceID) {
  float4x4 world = LoadInstanceTransform(instance_id);
  float4 world_pos = mul(world, float4(pos, 1.0));
  ShadowVSOutput output;
  output.position = mul(LightViewProjection, world_pos);
  output.position.z += DepthBias;
  return output;
}
```

### 5.2 Directional Shadow Sampling Contract

Lighting consumes the published directional conventional shadow product through
`ShadowFrameBindings`. The architectural contract does not require consumers to
know whether the underlying storage is atlas-backed, dedicated-per-cascade, or
otherwise.

```hlsl
// Contracts/ShadowData.hlsli

float SampleDirectionalShadow(
  float3 world_pos, uint cascade_index, ShadowFrameBindingData shadow) {
  float4 light_space_pos
    = mul(shadow.cascades[cascade_index].light_view_projection,
          float4(world_pos, 1.0));
  return SamplePublishedDirectionalShadowSurface(
    shadow.conventional_shadow_surface_handle,
    shadow.cascades[cascade_index].sampling_metadata0,
    shadow.cascades[cascade_index].sampling_metadata1,
    light_space_pos);
}
```

### 5.3 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexShadowDepthVS` | vs_6_0 | directional conventional shadow depth rendering |
| `VortexShadowDepthMaskedPS` | ps_6_0 | alpha-tested directional shadow path |

## 6. Stage Integration

### 6.1 Dispatch Contract

`shadows_->RenderShadowDepths(frame_shadow_inputs)` runs at Stage 8.

The service may perform frame-shared allocation/cache work internally, but the
published payload remains per-view and multi-view-safe.

### 6.2 Null-Safe Behavior

When `shadows_` is null:

- no conventional shadow depths are produced
- deferred lighting applies no directional shadow attenuation
- published `ShadowFrameBindings` payloads are empty

### 6.3 Capability Gate

Requires `kShadowing`.

## 7. Testability Approach

1. **Directional cascade validation:** single directional light, known scene ->
   verify cascade splits cover the camera frustum correctly.
2. **Directional shadow visual:** ground plane under the selected directional
   light -> visible occluder shadow.
3. **Per-view publication:** inspect `ShadowFrameBindings` for multiple views ->
   verify view-specific matrices/addressing metadata remain isolated.
4. **RenderDoc:** inspect conventional shadow backing resources and verify
   Stage-8 ordering relative to Stage 12.

## 8. Open Questions

None for the Phase 4C baseline.

The intentionally deferred work already has named later owners:

- spot-light / point-light conventional shadows ->
  [shadow-local-lights.md](./shadow-local-lights.md)
- VSM activation -> later `ShadowService` expansion
