# PostProcessService LLD

**Phase:** 4B — Migration-Critical Services
**Deliverable:** D.10
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`PostProcessService` — the stage-22 owner responsible for HDR → LDR tone
mapping, auto-exposure, bloom, and the AA/TSR-facing slot. This is the
final rendering pass before composition and the minimum requirement for
visible screen output.

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

## 3. Post-Process Chain

### 3.1 Execution Order

```text
PostProcessService::Execute(ctx, scene_textures)
  │
  ├─ 1. Auto-exposure (if enabled)
  │     └─ Read SceneColor → compute luminance histogram
  │     └─ Adapt exposure from previous frame
  │     └─ Output: exposure value (float)
  │
  ├─ 2. Bloom (if enabled)
  │     └─ Downsample SceneColor (bright pass)
  │     └─ Gaussian blur chain (4-6 levels)
  │     └─ Upsample and composite
  │     └─ Output: bloom texture (same resolution as SceneColor)
  │
  └─ 3. Tonemap (always)
        └─ Read SceneColor + bloom + exposure
        └─ Apply tonemap operator (ACES or Filmic)
        └─ Output: LDR result to final output target
```

### 3.2 Phase 4B Minimum

Tonemap is the only hard requirement for visible output. Auto-exposure and
bloom are implemented if straightforward to carry from legacy. If not
feasible in Phase 4B, tonemap alone with fixed exposure is sufficient.

## 4. Data Flow and Dependencies

### 4.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| SceneTextures | SceneColor (SRV) | HDR lit scene |
| SceneTextures | SceneDepth (SRV) | Depth-aware effects (DOF in future) |
| SceneTextures | Velocity (SRV) | TAA/TSR motion vectors (future) |
| Previous frame | Adapted exposure | Temporal exposure adaptation |

### 4.2 Outputs

| Product | Target | Notes |
| ------- | ------ | ----- |
| Tonemapped LDR output | Back buffer or composition target | Final visible output |
| Adapted exposure | Persistent state | For next frame's adaptation |

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
| Bloom mip chain (4-6 levels) | Per frame | Half-res, quarter-res, etc. |
| Exposure histogram buffer | Per frame | 256-bin histogram |
| Adapted exposure buffer | Persistent | Temporal adaptation state |
| Tonemap PSO | Persistent | Cached |
| Bloom PSOs (down/up) | Persistent | Cached |

## 7. Stage Integration

### 7.1 Dispatch Contract

`post_process_->Execute(ctx, scene_textures)` at stage 22.

### 7.2 Null-Safe Behavior

When null: no tone mapping occurs. SceneColor HDR values are presented
directly (will appear washed-out/overexposed without tonemapping).

### 7.3 Capability Gate

Requires `kFinalOutputComposition`. Always active when SceneRenderer
produces output.

## 8. Testability Approach

1. **Tonemap validation:** Render constant-color scene (HDR value 2.0) →
   verify tonemap output matches expected ACES curve value.
2. **Exposure adaptation:** Render scene across frames with changing
   brightness → verify exposure adapts over time.
3. **Bloom validation:** Place bright emissive object → verify bloom glow
   around object in final output.
4. **RenderDoc:** Frame 10, inspect post-process pass inputs (SceneColor)
   and output (LDR back buffer).

## 9. Open Questions

1. **Tonemap operator selection:** ACES vs Filmic vs AgX. Phase 4B uses
   ACES; selection can be config-driven later.
2. **TAA integration:** Temporal anti-aliasing requires SceneVelocity and
   history buffers. Deferred to Phase 5 or 7. The slot exists in
   PostProcessService but is not populated.
