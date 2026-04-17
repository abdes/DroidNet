# EnvironmentLightingService LLD

**Phase:** 4D - Migration-Critical Services
**Deliverable:** D.12
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`EnvironmentLightingService` is the capability-family owner for:

- **Stage 14** (reserved): future volumetrics / heterogeneous volumes / clouds
- **Stage 15** (active): sky background, sky-atmosphere composition, height
  fog, distance fog, and related environment composition

The service also owns environment probe / IBL state, but those probe products
are **not** defined as same-frame outputs of the Stage-15 render passes. They
are persistent or change-driven environment products prepared when needed and
published per view when valid.

This preserves the stage order:

- Stage 12 remains direct lighting
- Stage 13 remains the future canonical indirect-light owner
- Stage 15 remains active environment composition

### 1.2 Why The Split Matters

If environment probe publication were defined as a same-frame Stage-15 output,
Stage 12 would depend on data that does not exist yet. Vortex therefore splits
the environment family into:

1. persistent / change-driven probe state owned by the service
2. per-view published `EnvironmentFrameBindings`
3. Stage-15 render work for sky/atmosphere/fog composition

### 1.3 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 8 - subsystem service ownership
- [ARCHITECTURE.md](../ARCHITECTURE.md) Section 6.2 - stages 14 and 15
- [PLAN.md](../PLAN.md) Section 6 - Phase 4D scope and Stage-15 ownership
- [indirect-lighting-service.md](./indirect-lighting-service.md) - future
  Stage-13 owner that retires any temporary Phase 4 ambient bridge

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
`-- Services/
    `-- Environment/
        |-- EnvironmentLightingService.h
        |-- EnvironmentLightingService.cpp
        |-- Internal/
        |   |-- SkyRenderer.h/.cpp
        |   |-- AtmosphereRenderer.h/.cpp
        |   |-- FogRenderer.h/.cpp
        |   `-- IblProcessor.h/.cpp
        |-- Passes/
        |   |-- SkyPass.h/.cpp
        |   |-- AtmosphereComposePass.h/.cpp
        |   |-- FogPass.h/.cpp
        |   `-- IblProbePass.h/.cpp
        `-- Types/
            |-- EnvironmentProbeState.h
            |-- EnvironmentFrameBindings.h
            |-- SkyParams.h
            `-- FogParams.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class EnvironmentLightingService : public ISubsystemService {
 public:
  explicit EnvironmentLightingService(Renderer& renderer);
  ~EnvironmentLightingService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Publishes one per-view environment payload from persistent probe state.
  void PublishEnvironmentBindings(RenderContext& ctx) const;

  /// Stage 15: sky background, atmosphere composition, and fog composition.
  void RenderSkyAndFog(RenderContext& ctx,
                       const SceneTextures& scene_textures);

 private:
  Renderer& renderer_;
  std::unique_ptr<SkyRenderer> sky_;
  std::unique_ptr<AtmosphereRenderer> atmosphere_;
  std::unique_ptr<FogRenderer> fog_;
  std::unique_ptr<IblProcessor> ibl_;
  EnvironmentProbeState probe_state_;
};

}  // namespace oxygen::vortex
```

### 2.3 Persistent Probe State vs Per-View Publication

`EnvironmentLightingService` owns persistent probe state. It does **not** store
one singleton `EnvironmentFrameBindings` payload as the canonical contract.

```cpp
struct EnvironmentProbeBindings {
  uint32_t environment_map_srv{kInvalidIndex};
  uint32_t irradiance_map_srv{kInvalidIndex};
  uint32_t prefiltered_map_srv{kInvalidIndex};
  uint32_t brdf_lut_srv{kInvalidIndex};
  uint32_t probe_revision{0};
};

struct EnvironmentProbeState {
  EnvironmentProbeBindings probes;
  uint32_t flags{0};
  bool valid{false};
};

struct EnvironmentEvaluationParameters {
  float ambient_intensity{1.0f};
  float average_brightness{1.0f};
  float blend_fraction{0.0f};
  uint32_t flags{0};  // sky-visible, ambient-bridge-eligible, etc.
};

struct EnvironmentFrameBindings {
  EnvironmentProbeBindings probes;
  EnvironmentEvaluationParameters evaluation;
};

struct EnvironmentAmbientBridgeBindings {
  uint32_t irradiance_map_srv{kInvalidIndex};
  float ambient_intensity{1.0f};
  float average_brightness{1.0f};
  float blend_fraction{0.0f};
  uint32_t flags{0};
};
```

### 2.4 Per-View Publication

`EnvironmentFrameBindings` is published per view through `ViewFrameBindings`.

Its canonical long-lived consumer is future `IndirectLightingService` at
Stage 13.

Before Stage 13 exists, Phase 4 may expose only an explicitly documented
ambient-bridge subset to `LightingService`:

```cpp
EnvironmentAmbientBridgeBindings
```

That bridge:

- may consume only:
  - `irradiance_map_srv`
  - `ambient_intensity`
  - `average_brightness`
  - `blend_fraction`
  - explicit bridge flags
- must remain opt-in and explicitly documented
- must not redefine Stage 12 as the indirect-light owner

Reflection, AO, skylight shadowing, and other canonical indirect-light controls
remain Stage-13-owned.

Phase 4 replaces the current Phase 3 interim environment-binding shape with the
target contracts in Sections 2.3 and 2.4. The CPU-side structs, HLSL-side
counterparts, and `ViewFrameBindings` slot-routing update must land together.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Scene | environment map / sky parameters | sky background and environment state |
| Scene | atmosphere parameters | Stage-15 sky-atmosphere composition |
| Scene | fog parameters | fog rendering |
| SceneTextures | SceneColor (RTV/UAV as needed) | Stage-15 composition target |
| SceneTextures | SceneDepth (SRV) | atmosphere / fog depth-aware composition |
| Environment source changes | probe refresh trigger | refresh persistent probe state when required |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Stage-15 sky/atmosphere/fog contribution | SceneColor | direct composition |
| `EnvironmentFrameBindings` | future `IndirectLightingService` (Stage 13) | published through `ViewFrameBindings` |
| `EnvironmentAmbientBridgeBindings` | `LightingService` only when explicitly enabled | published through `ViewFrameBindings` |

No broader Phase 4 claim is made that arbitrary lighting or translucency
families consume the full environment payload. Those consumers must be named
explicitly when their contracts exist.

### 3.3 Execution Flow

```text
EnvironmentLightingService::OnFrameStart(frame)
  |
  `- Refresh EnvironmentProbeState when the environment source changes
       `- Generate / update irradiance and prefiltered probe products
       `- Keep persistent probe state ready for per-view publication

Renderer Core / current-view publication
  `- environment_->PublishEnvironmentBindings(ctx)
       `- Materialize one EnvironmentFrameBindings payload for the current view

EnvironmentLightingService::RenderSkyAndFog(ctx, scene_textures)
  |
  |- Background sky pass
  |    `- Writes sky background only where the current depth convention says
  |       the pixel is background / far scene
  |
  |- Atmosphere composition pass
  |    `- Applies atmosphere contribution for the current view
  |    `- Not limited to background-only pixels
  |
  |- Fog pass
  |    `- Reads SceneDepth
  |    `- Applies height fog + distance fog
  |    `- Composites fog into SceneColor
  |
  `- Stage 15 finishes with sky/atmosphere/fog composition
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Environment cubemap / source texture | Persistent (scene) | scene-owned input resource |
| Irradiance map | Persistent | regenerated on environment change / warm-up |
| Prefiltered specular map | Persistent | regenerated on environment change / warm-up |
| BRDF integration LUT | Persistent (global) | initialized once |
| Sky / atmosphere / fog PSOs | Persistent | cached by the service |

Persistent probe state and per-view publication are separate concerns:

- `EnvironmentProbeState` is long-lived service-owned state
- `EnvironmentFrameBindings` is published per view
- Stage-15 render work is neither of those things

## 5. Shader Contracts

### 5.1 Background Sky Pass

```hlsl
// Services/Environment/SkyPass.hlsl

TextureCube EnvironmentMap : register(t0);

bool IsFarBackground(float scene_depth, ViewFrameBindingData view) {
  // Must respect the active renderer depth convention.
  return EvaluateFarBackgroundMask(scene_depth, view);
}

float4 SkyPassPS(FullscreenVSOutput input) : SV_Target {
  float scene_depth = SceneDepth.Sample(PointClamp, input.uv).r;
  if (!IsFarBackground(scene_depth, ViewBindings)) {
    discard;
  }

  float3 view_dir = ReconstructViewDirection(input.uv, InvViewProjection);
  float3 sky = EnvironmentMap.Sample(LinearClamp, view_dir).rgb;
  return float4(sky, 1.0);
}
```

The architectural point is the helper: Stage 15 must not bake non-reverse-Z
assumptions such as `depth == 1.0` into its contract language or examples.

### 5.2 Atmosphere Composition Pass

```hlsl
// Services/Environment/AtmosphereComposePass.hlsl

float4 AtmosphereComposePS(FullscreenVSOutput input) : SV_Target {
  float raw_depth = SceneDepth.Sample(PointClamp, input.uv).r;
  float3 world_pos
    = ReconstructWorldPosition(input.uv, raw_depth, InvViewProjection);

  float3 atmosphere = EvaluateAtmosphere(world_pos, CameraPosition, Atmosphere);
  return float4(atmosphere, 1.0f);
}
```

This pass may apply aerial-perspective-style contribution over opaque scene
color. Stage 15 is therefore not a background-only sky stage.

### 5.3 Fog Pass

```hlsl
// Services/Environment/FogPass.hlsl

float4 FogPassPS(FullscreenVSOutput input) : SV_Target {
  float raw_depth = SceneDepth.Sample(PointClamp, input.uv).r;
  float3 world_pos
    = ReconstructWorldPosition(input.uv, raw_depth, InvViewProjection);

  float dist = length(world_pos - CameraPosition);
  float dist_fog = 1.0 - exp(-FogDensity * max(dist - FogStartDistance, 0));
  float height_fog = exp(-FogHeightFalloff * max(world_pos.y, 0));
  float fog = saturate(dist_fog * height_fog * FogMaxOpacity);
  return float4(FogColor.rgb, fog);
}
```

### 5.4 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexSkyPassVS` | vs_6_0 | fullscreen triangle |
| `VortexSkyPassPS` | ps_6_0 | background sky |
| `VortexAtmosphereComposeVS` | vs_6_0 | fullscreen triangle |
| `VortexAtmosphereComposePS` | ps_6_0 | atmosphere composition |
| `VortexFogPassVS` | vs_6_0 | fullscreen triangle |
| `VortexFogPassPS` | ps_6_0 | height + distance fog |
| `VortexIblIrradianceCS` | cs_6_0 | irradiance convolution |
| `VortexIblPrefilterCS` | cs_6_0 | specular prefilter |

## 6. Stage Integration

### 6.1 Dispatch Contract

- current-view publication:
  `environment_->PublishEnvironmentBindings(ctx)` before any consumer that
  needs the published environment payload
- Stage 14: reserved no-op in Phase 4D
- Stage 15:
  `environment_->RenderSkyAndFog(ctx, scene_textures)`

Stage 15 remains the sky/atmosphere/fog composition stage. Probe publication is
not a Stage-15-only side effect.

### 6.2 Null-Safe Behavior

When `environment_` is null:

- no sky / atmosphere / fog composition runs
- no environment probe state is refreshed
- no per-view environment payload is published
- `LightingService` must not assume an ambient bridge exists

### 6.3 Capability Gate

Requires `kEnvironmentLighting`.

## 7. Testability Approach

1. **Background sky:** verify sky appears only on far-background pixels under
   the active depth convention.
2. **Atmosphere composition:** verify atmosphere contributes over the scene and
   is not treated as background-only.
3. **Fog validation:** verify fog opacity matches the expected distance / height
   behavior.
4. **Per-view publication:** inspect the published `EnvironmentFrameBindings`
   payload for multiple views and verify it is derived from persistent probe
   state rather than a singleton global payload.
5. **RenderDoc:** inspect Stage-15 ordering and SceneColor evolution after sky,
   atmosphere, and fog passes.

## 8. Open Questions

None for the Phase 4D baseline.

Deferred work is already bounded:

- canonical indirect environment evaluation -> future Stage 13 owner in
  [indirect-lighting-service.md](./indirect-lighting-service.md)
- volumetrics / heterogeneous volumes / clouds -> future Stage 14 activation
