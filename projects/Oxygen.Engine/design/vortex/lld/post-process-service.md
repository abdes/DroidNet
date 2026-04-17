# PostProcessService LLD

**Phase:** 4B — Migration-Critical Services
**Deliverable:** D.10
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`PostProcessService` - the Stage-22 owner responsible for the per-view
post-processing family:

- temporal AA / TSR-facing post work
- exposure ownership (global eye adaptation plus future local exposure)
- bloom
- HDR -> display tonemapping
- related per-view post histories

The service writes into a SceneRenderer-supplied post target. It does **not**
own presentation, extraction, or handoff policy; those remain with
SceneRenderer stage 21 / stage 23 and Renderer Core composition ownership.

That post target is delivered through the same scene-renderer-owned runtime
surfaces as other scene products: it is resolved from the current
`RenderContext` / `SceneTextures` publication state by SceneRenderer before
Stage 22 executes rather than invented privately inside the service.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 21 (ResolveSceneColor) | |
| **This** | **Stage 22 — PostProcess** | Tonemap, bloom, exposure, AA |
| Successor | Stage 23 (PostRenderCleanup) — extraction/handoff | |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — subsystem service contracts
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage 22

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── Services/
    └── PostProcess/
        ├── PostProcessService.h
        ├── PostProcessService.cpp
        ├── Internal/
        │   ├── ExposureCalculator.h/.cpp
        │   └── BloomChain.h/.cpp
        ├── Passes/
        │   ├── TonemapPass.h/.cpp
        │   ├── BloomPass.h/.cpp
        │   └── ExposurePass.h/.cpp
        └── Types/
            └── PostProcessConfig.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

struct PostProcessConfig {
  bool enable_bloom{true};
  bool enable_auto_exposure{true};
  float fixed_exposure{1.0f};       // Used when auto-exposure disabled
  float bloom_intensity{0.5f};
  float bloom_threshold{1.0f};
};

class PostProcessService : public ISubsystemService {
 public:
  explicit PostProcessService(Renderer& renderer);
  ~PostProcessService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Stage 22: full post-process chain. Per-view execution.
  void Execute(RenderContext& ctx,
               const SceneTextures& scene_textures);

  void SetConfig(const PostProcessConfig& config);

 private:
  Renderer& renderer_;
  PostProcessConfig config_;
  std::unique_ptr<TonemapPass> tonemap_pass_;
  std::unique_ptr<BloomPass> bloom_pass_;
  std::unique_ptr<ExposurePass> exposure_pass_;
};

}  // namespace oxygen::vortex
```

### 2.3 Exposure Ownership Boundary

Exposure has two different ownership concerns:

1. **view-to-view source resolution** for the current frame remains part of the
   current-view / view-lifecycle contract owned outside post processing
2. **post-owned exposure work** begins once Stage 22 executes:
   - eye adaptation
   - local exposure when activated
   - exposure histories and post-owned adaptation intermediates

`PostProcessService` therefore owns exposure processing and histories, but it
does not take over the broader view-lifecycle problem of selecting or
materializing the current view.

## 3. Post-Process Chain

### 3.1 Execution Order

```text
PostProcessService::Execute(ctx, scene_textures)
  │
  ├─ 0. Optional temporal upscaler / TAA slot
  │     └─ Consume resolved SceneColor + SceneVelocity + histories
  │     └─ Write updated temporal history when enabled
  │
  ├─ 1. Exposure work
  │     └─ Compute luminance histogram or use fixed-exposure fallback
  │     └─ Update EyeAdaptation state for the current view
  │     └─ Build local-exposure intermediates when that path is active
  │
  ├─ 2. Bloom (if enabled)
  │     └─ Downsample bright scene signal
  │     └─ Gaussian blur chain (4-6 levels)
  │     └─ Upsample and composite
  │     └─ Output: filtered bloom texture at service-chosen resolution
  │
  └─ 3. Tonemap (always)
        └─ Read post-temporal scene signal + bloom + exposure state
        └─ Apply tonemap operator (ACES or Filmic)
        └─ Output: LDR result to the SceneRenderer-supplied post target
```

### 3.2 Phase 4B Minimum

Tonemap is the only hard requirement for visible output. Phase 4B may start
with tonemap plus fixed-exposure fallback. Auto-exposure and bloom are added
if straightforward. The temporal slot, exposure ownership boundary, and
history ownership are fixed in the design even if temporal AA / TSR and local
exposure remain partially inactive in the first implementation slice.

## 4. Data Flow and Dependencies

### 4.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| SceneTextures | SceneColor (SRV) | HDR resolved scene signal |
| SceneTextures | SceneDepth (SRV) | Depth-aware effects (DOF in future) |
| SceneTextures | Velocity (SRV) | TAA / TSR motion vectors when enabled |
| Current-view publication | exposure source / current-view context | view-lifecycle-owned input into Stage 22 exposure work |
| Previous frame | EyeAdaptation / temporal histories | Post-owned history state |

### 4.2 Outputs

| Product | Target | Notes |
| ------- | ------ | ----- |
| Tonemapped LDR output | SceneRenderer-supplied post target | Consumed by composition / offscreen handoff |
| EyeAdaptation state | Persistent per-view history | For next frame's adaptation |
| Local-exposure intermediates | Per-frame post-owned internal products | Optional inputs to bloom / tonemap when enabled |
| TemporalAA / TSR histories | Persistent per-view history | Updated only when temporal path is active |

## 5. Shader Contracts

### 5.1 Tonemap Pass

```hlsl
// Services/PostProcess/Tonemap.hlsl

#include "../../Shared/FullscreenTriangle.hlsli"

Texture2D SceneColor : register(t0);
Texture2D BloomTexture : register(t1);
SamplerState LinearClamp : register(s0);

cbuffer TonemapConstants : register(b0) {
  float Exposure;
  float BloomIntensity;
};

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x) {
  float a = 2.51;
  float b = 0.03;
  float c = 2.43;
  float d = 0.59;
  float e = 0.14;
  return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

FullscreenVSOutput TonemapVS(uint vid : SV_VertexID) {
  return FullscreenTriangleVS(vid);
}

float4 TonemapPS(FullscreenVSOutput input) : SV_Target {
  float3 hdr = SceneColor.Sample(LinearClamp, input.uv).rgb;
  float3 bloom = BloomTexture.Sample(LinearClamp, input.uv).rgb;

  float3 color = hdr + bloom * BloomIntensity;
  color *= Exposure;
  color = ACESFilm(color);

  // Linear to sRGB (if output is sRGB)
  return float4(color, 1.0);
}
```

### 5.2 Bloom Downsample/Upsample

```hlsl
// Services/PostProcess/BloomDownsample.hlsl
// 13-tap box filter downsample (avoids aliasing)

// Services/PostProcess/BloomUpsample.hlsl
// Tent filter upsample + accumulation
```

### 5.3 Exposure Histogram

```hlsl
// Services/PostProcess/ExposureHistogram.hlsl (compute)
// Builds luminance histogram from SceneColor
// Dispatch: ceil(width/16) × ceil(height/16)
```

### 5.4 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexTonemapVS` | vs_6_0 | Fullscreen triangle |
| `VortexTonemapPS` | ps_6_0 | ACES tonemap |
| `VortexBloomDownsamplePS` | ps_6_0 | Bright pass + downsample |
| `VortexBloomUpsamplePS` | ps_6_0 | Tent filter upsample |
| `VortexExposureHistogramCS` | cs_6_0 | Luminance histogram (compute) |

## 6. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Bloom mip chain (4-6 levels) | Per frame | Service-chosen filtered resolutions |
| Exposure histogram buffer | Per frame | 256-bin histogram |
| EyeAdaptation buffer | Persistent per view | Temporal adaptation state |
| Local-exposure intermediates | Per frame / persistent per view as required | Post-owned once the local-exposure path activates |
| TemporalAA history | Persistent per view | Reserved in Phase 4B, active when temporal path lands |
| TSR history | Persistent per view | Future post-owned history family |
| Tonemap PSO | Persistent | Cached |
| Bloom PSOs (down/up) | Persistent | Cached |

## 7. Stage Integration

### 7.1 Dispatch Contract

`post_process_->Execute(ctx, scene_textures)` at stage 22.

Stage 21 remains a thin optional resolve owned by SceneRenderer. Stage 22 must
therefore accept either the resolved scene signal or the original scene color
when no separate resolve work was needed.

### 7.2 Null-Safe Behavior

When null: SceneRenderer routes the resolved scene signal directly to its
composition / offscreen target with no post-family work. Presentation and
handoff ownership still remain outside the service.

### 7.3 Capability Gate

Requires `kFinalOutputComposition`. Always active when SceneRenderer
produces output.

## 8. Testability Approach

1. **Tonemap validation:** Render constant-color scene (HDR value 2.0) →
   verify tonemap output in the post target matches the expected ACES curve
   value.
2. **Exposure adaptation:** Render scene across frames with changing
   brightness → verify EyeAdaptation state adapts over time.
3. **Bloom validation:** Place bright emissive object → verify bloom glow
   around object in the post target.
4. **RenderDoc:** Frame 10, inspect post-process pass inputs (SceneColor)
   and output (LDR post target).

## 9. Open Questions

1. **Tonemap operator selection:** ACES vs Filmic vs AgX. Phase 4B uses
   ACES; selection can be config-driven later.
2. **Initial temporal path:** TAA vs TSR for the first active temporal
   implementation. The stage-22 ownership and history contract are fixed now;
   only the first active algorithm choice is deferred.
