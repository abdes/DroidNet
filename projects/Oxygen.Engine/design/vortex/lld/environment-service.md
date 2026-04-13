# EnvironmentLightingService LLD

**Phase:** 4D — Migration-Critical Services
**Deliverable:** D.12
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`EnvironmentLightingService` — the capability-family service owning:

- **Stage 14** (reserved): volumetrics / heterogeneous volumes / clouds
- **Stage 15** (active): sky/atmosphere rendering, height fog, distance
  fog, and IBL (image-based lighting) data production

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
            ├── IblFrameData.h
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

  /// Stage 15: sky, atmosphere, fog, IBL. Per-view execution.
  void RenderSkyAndFog(RenderContext& ctx,
                        const SceneTextures& scene_textures);

  /// Access IBL data for lighting consumers.
  [[nodiscard]] auto GetIblData() const -> const IblFrameData*;

 private:
  Renderer& renderer_;
  std::unique_ptr<SkyRenderer> sky_;
  std::unique_ptr<FogRenderer> fog_;
  std::unique_ptr<IblProcessor> ibl_;
  IblFrameData ibl_data_;
};

}  // namespace oxygen::vortex
```

### 2.3 IblFrameData

```cpp
struct IblFrameData {
  uint32_t environment_map_srv{kInvalidIndex};   // Cubemap SRV
  uint32_t irradiance_map_srv{kInvalidIndex};    // Diffuse irradiance SRV
  uint32_t prefiltered_map_srv{kInvalidIndex};   // Specular prefiltered SRV
  uint32_t brdf_lut_srv{kInvalidIndex};          // BRDF integration LUT SRV
  float ambient_intensity{1.0f};
};
```

### 2.4 Per-View Publication

`IblFrameData` is published per-view through `EnvironmentFrameBindings`
in `ViewFrameBindings`. Deferred lighting at stage 12 reads IBL data for
ambient contribution.

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Scene | Environment map / sky parameters | Sky rendering |
| Scene | Fog parameters (height, density, color) | Fog rendering |
| SceneTextures | SceneColor (RTV) | Sky renders into SceneColor |
| SceneTextures | SceneDepth (SRV) | Depth-based fog |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Sky contribution | SceneColor | Direct render |
| Fog contribution | SceneColor | Composite over scene |
| IblFrameData | LightingService (stage 12) | Via `GetIblData()` |
| IblFrameData | ForwardLighting consumers | Via ViewFrameBindings publication |

### 3.3 Execution Flow

```text
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
  └─ IBL update (if environment changed):
        └─ Generate irradiance map from environment cubemap
        └─ Generate prefiltered specular map
        └─ BRDF LUT is precomputed (persistent)
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Environment cubemap | Persistent (scene) | Loaded from scene assets |
| Irradiance map | Persistent (scene) | Regenerated on environment change |
| Prefiltered specular map | Persistent (scene) | Mip chain for roughness |
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

### 6.2 Null-Safe Behavior

When null: no sky, no fog, no IBL. SceneColor shows only direct lighting
against a black background. Deferred lighting has no ambient contribution.

### 6.3 Capability Gate

Requires `kEnvironmentLighting`.

## 7. Testability Approach

1. **Sky rendering:** Set environment cubemap → verify sky appears at far-
   plane pixels only (no sky bleeding through geometry).
2. **Fog validation:** Place camera at known distance → verify fog opacity
   matches expected exponential falloff.
3. **IBL integration:** White metallic sphere → verify specular reflections
   from environment map appear.
4. **RenderDoc:** Frame 10, inspect SceneColor after stage 15 — sky should
   be visible in background, fog should attenuate distant geometry.

## 8. Open Questions

1. **Procedural atmosphere:** Phase 4D may use a simple environment cubemap
   only. Procedural atmosphere (Bruneton model, Mie/Rayleigh scattering) is
   a Phase 7+ enhancement.
2. **Volumetric fog:** Stage 14 is reserved. Heterogeneous volume rendering
   (3D fog volume, volumetric lighting) deferred to Phase 7D.
