# LightingService LLD

**Phase:** 4A - Migration-Critical Services
**Deliverable:** D.9
**Status:** `ready`

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

## 1. Scope and Context

### 1.1 What This Covers

`LightingService` is the capability-family owner for two distinct Phase 4A
responsibilities:

- **Stage 6**: frame-scope forward-light preparation plus per-view publication
  of the forward-light family as shared supporting data
- **Stage 12**: per-view deferred **direct** lighting using the canonical
  Vortex contract:
  directional fullscreen draws plus bounded-volume point/spot local-light
  draws into `SceneColor`

Phase 4A migrates the live Phase 3 inline Stage-12 implementation into a
service owner, adds the Stage-6 forward-light family required by later forward
consumers, and removes the temporary Phase 3 `SV_VertexID` local-light proxy
generation from the canonical runtime path.

### 1.2 What It Replaces

The Phase 3 inline
`SceneRenderer::RenderDeferredLighting(ctx, scene_textures)` implementation
moves into `LightingService::RenderDeferredLighting(...)`.

What does **not** change:

1. Stage 12 remains the canonical deferred **direct**-lighting stage.
2. Stage 6 remains shared supporting data, not the deferred-lighting root.
3. Canonical indirect environment evaluation remains future Stage 13 work.
4. Any Phase 4 ambient bridge is transitional, opt-in, and ambient-only.

### 1.3 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 8 - subsystem service contracts
- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 6.2 - stages 6 and 12
- [PLAN.md](../PLAN.md) Section 6 - Phase 4A scope and exit criteria
- UE5.7 reference families:
  - `PrepareForwardLightData(...)`
  - `ComputeLightGrid(...)`
  - `RenderLights(...)`

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
`-- Services/
    `-- Lighting/
        |-- LightingService.h
        |-- LightingService.cpp
        |-- Internal/
        |   |-- LightGridBuilder.h/.cpp
        |   |-- ForwardLightPublisher.h/.cpp
        |   |-- DeferredLightPacketBuilder.h/.cpp
        |   `-- LightSelectionResolver.h/.cpp
        |-- Passes/
        |   |-- DeferredLightPass.h/.cpp
        |   `-- LightGridBuildPass.h/.cpp
        `-- Types/
            |-- FrameLightingInputs.h
            |-- ForwardLightFrameBindings.h
            |-- ForwardLocalLightRecord.h
            |-- DirectionalLightForwardData.h
            `-- LightGridMetadata.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

struct PreparedViewLightingInput {
  ViewId view_id;
  observer_ptr<const PreparedSceneFrame> prepared_scene;
  observer_ptr<const CompositionView> composition_view;
};

struct FrameLightSelection {
  observer_ptr<const DirectionalLightProxy> selected_directional_light;
  std::span<const observer_ptr<const LocalLightProxy>> local_lights;
  uint64_t selection_epoch{0};
};

struct FrameLightingInputs {
  const FrameLightSelection& frame_light_set;
  std::span<const PreparedViewLightingInput> active_views;
};

class LightingService : public ISubsystemService {
 public:
  explicit LightingService(Renderer& renderer);
  ~LightingService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 6: frame-scope forward-light preparation.
  /// Builds shared local-light storage once for the frame and publishes the
  /// per-view forward-light package for each active prepared view.
  void BuildLightGrid(const FrameLightingInputs& inputs);

  /// Stage 12: deferred direct lighting for the current view.
  /// Consumes the same renderer-owned frame light selection used by Stage 6.
  void RenderDeferredLighting(
    RenderContext& ctx, const SceneTextures& scene_textures,
    const FrameLightSelection& frame_light_set);

  /// CPU inspection hook for tests/diagnostics of the published forward-light
  /// package for one view.
  [[nodiscard]] auto InspectForwardLightBindings(ViewId view_id) const
    -> const ForwardLightFrameBindings*;

 private:
  Renderer& renderer_;
  std::unique_ptr<LightGridBuilder> light_grid_builder_;
  std::unique_ptr<ForwardLightPublisher> publisher_;
  std::unique_ptr<DeferredLightPacketBuilder> deferred_packets_;
  std::unique_ptr<DeferredLightPass> deferred_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 Frame-Shared Lighting Inputs

`LightingService` does not own authoritative scene light proxies. It consumes a
renderer-owned `FrameLightSelection` built earlier in frame execution.

The `FrameLightSelection` builder lives outside `LightingService`. It is
constructed by Renderer Core / scene-renderer frame setup before Stage 6 from
scene-published light proxies and the current frame's prepared-view context.
`LightingService` consumes that result; it does not perform the authoritative
scene-light gather/sort itself.

The frame-light selection is the canonical source for:

- the selected directional light for the current frame
- the local-light set considered visible/relevant for the frame
- the stable selection epoch used by tests/diagnostics

Stage 6 and Stage 12 both consume this same frame-light selection. Stage 6 may
cache derived data from it, but Stage 6 does **not** become the authoritative
owner of Stage-12 deferred-light dispatch.

### 2.4 ForwardLocalLight Record

The local-light storage record is frame-shared GPU data, not the whole
consumer-facing publication contract.

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

### 2.5 Published Forward-Light Package

`ForwardLightFrameBindings` is the stable per-view consumer-facing lighting
payload. It must be richer than the raw local-light storage buffer because
future consumers need directional-light data, clustered-access metadata, and
view-relative lighting routing without reaching into service internals.

```cpp
struct DirectionalLightForwardData {
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  float source_radius{0.0f};

  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float volumetric_scattering_intensity{0.0f};

  float specular_scale{1.0f};
  float diffuse_scale{1.0f};
  uint32_t shadow_flags{0};
  uint32_t light_function_atlas_index{kInvalidIndex};

  uint32_t cascade_count{0};
  uint32_t reserved0{0};
  uint32_t reserved1{0};
  uint32_t reserved2{0};
};

struct ForwardLightFrameBindings {
  // Shared frame storage
  uint32_t local_light_buffer_srv{kInvalidIndex};
  uint32_t light_view_data_srv{kInvalidIndex};

  // Per-view clustered access package
  uint32_t grid_metadata_buffer_srv{kInvalidIndex};
  uint32_t grid_indirection_srv{kInvalidIndex};
  uint32_t directional_light_indices_srv{kInvalidIndex};

  glm::ivec3 grid_size{0};
  float reserved_grid0{0.0f};
  glm::vec3 grid_z_params{0.0f};
  float reserved_grid1{0.0f};

  uint32_t num_grid_cells{0};
  uint32_t max_culled_lights_per_cell{0};
  uint32_t directional_light_count{0};
  uint32_t local_light_count{0};

  uint32_t has_directional_light{0};
  uint32_t affects_translucent_lighting{0};
  uint32_t flags{0};
  uint32_t reserved_flags{0};

  glm::vec4 pre_view_translation_offset{0.0f};

  DirectionalLightForwardData directional{};
};
```

### 2.6 Directional-Light Authority

The selected directional-light payload is part of the published per-view
forward-light package. `LightingService` is responsible for shaping that
payload for consumers, but the service does not invent a second scene-light
ownership system. The selected directional light remains sourced from the
renderer-owned frame-light selection described in Section 2.3.

### 2.7 Per-View Publication

The service publishes `ForwardLightFrameBindings` through
`LightingFrameBindings`, which is then routed through `ViewFrameBindings`.
Consumers access the forward-light family only through the published per-view
binding stack.

Phase 4 replaces the current Phase 3 interim lighting-binding shape with the
target contract in Section 2.5. The CPU struct, the HLSL-side counterpart, and
the `ViewFrameBindings` slot-routing update must land together; this is not an
optional side-by-side binding family.

Per-view publication is distinct from frame-shared build work:

- Stage 6 builds shared local-light storage once per frame
- Stage 6 publishes one `ForwardLightFrameBindings` payload per active view
- Stage 12 consumes the current view plus the same frame-light selection used
  by Stage 6

## 3. Data Flow and Dependencies

### 3.1 Stage 6 - BuildLightGrid

| Input | Source | Purpose |
| ----- | ------ | ------- |
| Frame light set / sorted visible lights | Renderer Core light-gather result | Shared light source for Stage 6 and Stage 12 |
| Active prepared views | Renderer Core + InitViews publication | Per-view frusta, Z slicing, and publication targets |

| Output | Consumer | Delivery |
| ------ | -------- | -------- |
| `ForwardLocalLightRecord` buffer | Published forward-light family | Shared storage owned by `LightingService` |
| `ForwardLightFrameBindings` | Stage 18 translucency, forward-only materials, diagnostics, later lighting-adjacent families | Published through `LightingFrameBindings` / `ViewFrameBindings` |
| Frame-light selection cache (optional) | Stage 12 | Derived cache of the renderer-owned frame light set, not a Stage-6-owned deferred-light contract |

Stage 6 is frame-scope work. It does not run on one current-view `RenderContext`
and then pretend the result is global. The build consumes the frame light set
plus the active prepared-view list and publishes per-view lighting payloads
from that frame-scope build.

### 3.2 Stage 12 - RenderDeferredLighting

| Input | Source | Purpose |
| ----- | ------ | ------- |
| GBufferNormal/Material/BaseColor/CustomData (SRV) | SceneTextures (from stage 10) | Material data for BRDF |
| SceneDepth (SRV) | SceneTextures | Position reconstruction |
| `ShadowFrameBindings` | ShadowService publication | Directional shadow attenuation terms in Phase 4C |
| Frame light set / sorted visible lights | Renderer Core light-gather result (or a cache of that same result) | Canonical Stage-12 per-light direct-light iteration |
| `EnvironmentAmbientBridgeBindings` | Environment service publication | Optional ambient-only migration bridge with an explicitly bounded payload |

| Output | Target | Blend Mode |
| ------ | ------ | ---------- |
| SceneColor | SceneTextures | Additive (ONE, ONE) |

Stage 12 remains the canonical deferred **direct**-lighting stage. It consumes
the same renderer-owned frame-light selection used by Stage 6, but the
clustered forward-light family never becomes the authoritative deferred-light
dispatch source.

### 3.3 Sequence Diagram

```text
Renderer Core builds frame light selection
  `- selected directional light + visible local lights

SceneRenderer::OnRender(...)
  |- Stage 6: lighting_->BuildLightGrid(frame_lighting_inputs)
  |            `- Builds shared local-light storage once
  |            `- Publishes one ForwardLightFrameBindings payload per view
  |- ...stages 7-11...
  `- Stage 12: lighting_->RenderDeferredLighting(
                 ctx, scene_textures, frame_light_set)
                 `- Derives per-light draw packets from the same frame light set
                 `- Records one directional fullscreen draw plus bounded-volume
                    point/spot draws
                 `- Accumulates into SceneColor
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Forward-local-light structured buffer | Per frame | Upload ring or frame allocator |
| Light-grid metadata / indirection buffers | Per frame | Per-view clustered access data |
| Derived Stage-12 draw packets | Per frame | Transient direct-light draw parameters |
| Light volume geometry (sphere, cone) | Persistent | Canonical permanent owner; replaces the temporary Phase 03 procedural `SV_VertexID` path |
| Deferred-light PSOs | Persistent | Cached by the service |

Authoritative light state remains outside `LightingService`. The service owns
only:

- shared proxy-geometry resources (sphere/cone)
- PSO caches and service-local GPU resources
- transient per-frame draw packets derived from the current frame light set

The service does **not** become the persistent owner of scene light proxies.
Position, radius, cone angles, attenuation, shadow identifiers, and
view-relative transforms are imported each frame from renderer-owned prepared
scene / light-proxy data.

## 5. Shader Contracts

### 5.1 Light Grid Compute Shader

```hlsl
// Services/Lighting/LightGridBuild.hlsl
// Builds one clustered light grid per published view.
// Dispatch shape is derived from the per-view grid dimensions carried in
// ForwardLightFrameBindings.
```

### 5.2 Deferred Light Shaders

The deferred-light family remains the canonical direct-light path. The
forward-light buffers published at Stage 6 are for forward consumers and later
lighting-adjacent families; they do not replace the explicit per-light
deferred-light constants used by Stage 12.

```hlsl
// Services/Lighting/DeferredLightingCommon.hlsli
// Direct-light evaluation helpers for:
// - directional fullscreen deferred lighting
// - bounded-volume point deferred lighting
// - bounded-volume spot deferred lighting

cbuffer DeferredLightConstants : register(b1) {
  float4 LightPositionAndRadius;
  float4 LightColorAndIntensity;
  float4 LightDirectionAndFalloff;
  float4 SpotAngles;
}

// Stage-6-owned publication helpers remain family-local:
//   Services/Lighting/LightGridCommon.hlsli
//   Services/Lighting/ForwardLightingCommon.hlsli
//   Services/Lighting/DeferredLightingCommon.hlsli
//   Services/Lighting/DeferredShadingCommon.hlsli
```

### 5.3 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexLightGridBuildCS` | cs_6_0 | Compute: clustered grid build |

Phase 3 deferred-light entrypoints remain canonical for Stage 12 in Phase 4A.

### 5.4 Geometry Ownership Cutover

Phase 4A is not truthful until the retained Phase 03 procedural point/spot
proxy-generation shortcut is removed from the canonical runtime path.

The permanent `LightingService` implementation owns:

- persistent sphere proxy geometry for point lights
- persistent cone proxy geometry for spot lights
- upload / lifetime / cache identity for those resources

## 6. Stage Integration

### 6.1 Dispatch Contract

- Stage 6: `lighting_->BuildLightGrid(frame_lighting_inputs)` - frame-scope
  build followed by per-view publication
- Stage 12:
  `lighting_->RenderDeferredLighting(ctx, scene_textures, frame_light_set)` -
  per-view deferred direct lighting

### 6.2 Null-Safe Behavior

When `lighting_` is null:

- Stage 6 publishes no forward-light package
- Stage 12 records no deferred direct-light draws
- `SceneColor` retains only prior stage contributions such as base-pass
  emissive

When the selected directional light is absent, the published payload sets
`has_directional_light=0`, but the local-light portion of the forward-light
family remains valid if local lights are present.

### 6.3 Capability Gate

Requires `kLightingData` plus `kDeferredShading`.

## 7. Migration from Phase 3 Inline

1. Move the Phase 3 Stage-12 body into
   `LightingService::RenderDeferredLighting(...)`.
2. Replace the temporary Phase 03 procedural point/spot proxy generation with
   persistent `LightingService`-owned sphere/cone geometry.
3. Add frame-scope `BuildLightGrid(...)` and publish the forward-light family
   through `LightingFrameBindings` / `ViewFrameBindings`.
   This replaces the interim Phase 3 lighting-binding shape rather than
   coexisting beside it.
4. Preserve Stage 12 as the canonical per-light deferred direct-light owner.
5. If a Phase 4 ambient bridge is enabled, bind only the explicitly approved
   `EnvironmentAmbientBridgeBindings` payload.
6. Keep the future Stage-13 indirect-light owner explicit; Stage 12 does not
   absorb it.

## 8. Testability Approach

1. **Frame-scope build vs per-view publication:** mock multiple views and one
   shared frame light set -> verify one shared local-light storage build and
   one published forward-light package per view.
2. **Directional payload publication:** inspect the published
   `ForwardLightFrameBindings` for a view -> verify directional-light fields,
   counts, and grid metadata are valid.
3. **Deferred-lighting parity:** render the same scene through Phase 3 inline
   Stage 12 and Phase 4A `LightingService` -> require pixel-identical direct
   lighting.
4. **Geometry ownership cutover:** verify the canonical runtime path no longer
   relies on `SV_VertexID` procedural point/spot proxy generation.
5. **RenderDoc:** inspect Stage 6 buffer contents and Stage 12 draw calls.

## 9. Open Questions

None. The remaining deferred boundaries are already named:

- ambient-bridge retirement -> future `IndirectLightingService`
- local-light conventional shadows -> future `ShadowService` expansion
