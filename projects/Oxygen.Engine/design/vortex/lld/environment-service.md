# EnvironmentLightingService LLD

**Phase:** 4D — Migration-Critical Services
**Deliverable:** D.12
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`EnvironmentLightingService` — the capability-family service owning:

- **Stage 14** (reserved): volumetrics / heterogeneous volumes / clouds
- **Stage 15** (active): sky/atmosphere rendering, height fog, distance
  fog, and related environment composition

Environment-owned probe / IBL products remain part of the same service, but
they are **not** defined as same-frame outputs of the stage-15 sky/fog pass.
They are persistent or change-driven environment products that the service
prepares when needed and publishes per view when valid. This avoids the
impossible stage-order dependency where stage 12 would otherwise need data
that stage 15 had not produced yet.

All environment-family work is grouped under this single service. Stage 14
is a reserved no-op slot in Phase 4D that prevents architectural shock when
volumetrics activate in Phase 7D.

### 1.2 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — subsystem services
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stages 14, 15

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── Services/
    └── Environment/
        ├── EnvironmentLightingService.h
        ├── EnvironmentLightingService.cpp
        ├── Internal/
        │   ├── SkyRenderer.h/.cpp
        │   ├── FogRenderer.h/.cpp
        │   └── IblProcessor.h/.cpp
        ├── Passes/
        │   ├── SkyPass.h/.cpp
        │   ├── FogPass.h/.cpp
        │   └── IblProbePass.h/.cpp
        └── Types/
            ├── EnvironmentFrameBindings.h
            ├── SkyParams.h
            └── FogParams.h
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

  /// Stage 15: sky, atmosphere, fog composition. Per-view execution.
  void RenderSkyAndFog(RenderContext& ctx,
                        const SceneTextures& scene_textures);

 private:
  Renderer& renderer_;
  std::unique_ptr<SkyRenderer> sky_;
  std::unique_ptr<FogRenderer> fog_;
  std::unique_ptr<IblProcessor> ibl_;
  EnvironmentFrameBindings environment_bindings_;
};

}  // namespace oxygen::vortex
```

### 2.3 EnvironmentFrameBindings

```cpp
struct EnvironmentProbeBindings {
  uint32_t environment_map_srv{kInvalidIndex};   // Cubemap SRV
  uint32_t irradiance_map_srv{kInvalidIndex};    // Diffuse irradiance SRV
  uint32_t prefiltered_map_srv{kInvalidIndex};   // Specular prefiltered SRV
  uint32_t brdf_lut_srv{kInvalidIndex};          // BRDF integration LUT SRV
};

struct EnvironmentEvaluationParameters {
  float ambient_intensity{1.0f};
  float average_brightness{1.0f};
  float blend_fraction{0.0f};
  uint32_t flags{0};  // dynamic/static, sky-visible, bridge-eligible, etc.
};

struct EnvironmentFrameBindings {
  EnvironmentProbeBindings probes;
  EnvironmentEvaluationParameters evaluation;
};
```

The split is intentional. Probe handles and samplers evolve on a different axis
from evaluation policy. UE 5.7 also effectively separates those concerns:
processed / captured probe resources are one layer, while brightness, dynamic
state, and shadowing / occlusion behavior are another. Keeping both layers in
one typed payload gives Vortex the same flexibility without importing UE's
entire parameter surface.

### 2.4 Per-View Publication

`EnvironmentFrameBindings` is published per-view through
`ViewFrameBindings`. The canonical long-lived consumer is future
`IndirectLightingService` at stage 13. If Phase 4 temporarily samples the
published ambient probe data in stage 12, that is a narrowly bounded migration
bridge and must be treated as temporary prose-visible debt rather than a
redefinition of stage ownership.

The bridge, when enabled, may consume only the stable ambient subset of
`EnvironmentEvaluationParameters`. Reflection, SSAO, skylight shadowing, and
other canonical indirect-light controls remain reserved for stage 13.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Scene | Environment map / sky parameters | Sky rendering |
| Scene | Fog parameters (height, density, color) | Fog rendering |
| SceneTextures | SceneColor (RTV) | Sky renders into SceneColor |
| SceneTextures | SceneDepth (SRV) | Depth-based fog |
| Environment state | Environment source changes / warm-up flags | Probe refresh when required |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Sky contribution | SceneColor | Direct render |
| Fog contribution | SceneColor | Composite over scene |
| `EnvironmentFrameBindings` | Future `IndirectLightingService` (stage 13) | Published through `ViewFrameBindings` |
| `EnvironmentFrameBindings` | Forward lighting / translucency consumers | Published through `ViewFrameBindings` |
| `EnvironmentFrameBindings` | Phase 4 ambient bridge, if explicitly enabled | Published through `ViewFrameBindings` |

### 3.3 Execution Flow

```text
EnvironmentLightingService::OnFrameStart(frame)
  │
  └─ Environment probe refresh (change-driven / amortized)
        └─ Generate irradiance / prefiltered probe products when required
        └─ Publish EnvironmentFrameBindings when valid

EnvironmentLightingService::RenderSkyAndFog(ctx, scene_textures)
  │
  ├─ Sky pass:
  │     └─ Render sky dome or procedural atmosphere
  │     └─ Write to SceneColor at depth=1.0 (far plane pixels only)
  │
  ├─ Fog pass:
  │     └─ Fullscreen pass reading SceneDepth
  │     └─ Apply height fog + distance fog
  │     └─ Blend fog color into SceneColor
  │
  └─ Stage 15 finishes with sky/fog composition only
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Environment cubemap | Persistent (scene) | Loaded from scene assets |
| Irradiance map | Persistent (scene) | Regenerated on environment change or warm-up |
| Prefiltered specular map | Persistent (scene) | Mip chain for roughness; amortizable across frames |
| BRDF integration LUT | Persistent (global) | Precomputed once at init |
| Sky/fog PSOs | Persistent | Cached |

## 5. Shader Contracts

### 5.1 Sky Pass

```hlsl
// Services/Environment/SkyPass.hlsl

#include "../../Shared/FullscreenTriangle.hlsli"

TextureCube EnvironmentMap : register(t0);

float4 SkyPassPS(FullscreenVSOutput input) : SV_Target {
  // Reconstruct view direction from UV
  float3 viewDir = ReconstructViewDirection(input.uv, InvViewProjection);
  // Only render where depth == far plane (no geometry)
  float depth = SceneDepth.Sample(PointClamp, input.uv).r;
  if (depth < 1.0) discard;  // Geometry present, skip sky

  float3 sky = EnvironmentMap.Sample(LinearClamp, viewDir).rgb;
  return float4(sky, 1.0);
}
```

### 5.2 Fog Pass

```hlsl
// Services/Environment/FogPass.hlsl

cbuffer FogConstants : register(b1) {
  float4 FogColor;
  float FogDensity;
  float FogHeightFalloff;
  float FogStartDistance;
  float FogMaxOpacity;
};

float4 FogPassPS(FullscreenVSOutput input) : SV_Target {
  float rawDepth = SceneDepth.Sample(PointClamp, input.uv).r;
  float3 worldPos = ReconstructWorldPosition(input.uv, rawDepth, InvViewProjection);

  // Distance fog
  float dist = length(worldPos - CameraPosition);
  float distFog = 1.0 - exp(-FogDensity * max(dist - FogStartDistance, 0));

  // Height fog
  float heightFog = exp(-FogHeightFalloff * max(worldPos.y, 0));

  float fog = saturate(distFog * heightFog * FogMaxOpacity);
  return float4(FogColor.rgb, fog);  // Alpha blend fog color
}
```

### 5.3 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexSkyPassVS` | vs_6_0 | Fullscreen triangle |
| `VortexSkyPassPS` | ps_6_0 | Environment cubemap sample |
| `VortexFogPassVS` | vs_6_0 | Fullscreen triangle |
| `VortexFogPassPS` | ps_6_0 | Height + distance fog |
| `VortexIblIrradianceCS` | cs_6_0 | Irradiance convolution (compute) |
| `VortexIblPrefilterCS` | cs_6_0 | Specular prefilter (compute) |

## 6. Stage Integration

### 6.1 Dispatch Contract

- Stage 14: reserved no-op (volumetrics, Phase 7D)
- Stage 15: `environment_->RenderSkyAndFog(ctx, scene_textures)`

Environment probe publication may be refreshed during lifecycle work such as
`OnFrameStart()`. Stage 15 remains the sky/fog composition stage only.

The reserved stage-14 family remains one Environment-owned umbrella, but it is
expected to contain multiple internal steps when activated (for example
volumetric fog, heterogeneous volumes, cloud rendering, and related
composition work), not one monolithic future pass.

### 6.2 Null-Safe Behavior

When null: no sky, no fog, and no published environment probe data. SceneColor
shows only the lighting families that remain active.

### 6.3 Capability Gate

Requires `kEnvironmentLighting`.

## 7. Testability Approach

1. **Sky rendering:** Set environment cubemap → verify sky appears at far-
   plane pixels only (no sky bleeding through geometry).
2. **Fog validation:** Place camera at known distance → verify fog opacity
   matches expected exponential falloff.
3. **Environment publication:** Inspect the published
   `EnvironmentFrameBindings` payload → verify probe SRVs are valid when the
   environment is active.
4. **RenderDoc:** Frame 10, inspect SceneColor after stage 15 — sky should
   be visible in background, fog should attenuate distant geometry.

## 8. Open Questions

1. **Procedural atmosphere:** Phase 4D may use a simple environment cubemap
   only. Procedural atmosphere (Bruneton model, Mie/Rayleigh scattering) is
   a Phase 7+ enhancement.
2. **Volumetric fog:** Stage 14 is reserved. Heterogeneous volume rendering
   (3D fog volume, volumetric lighting) deferred to Phase 7D.
