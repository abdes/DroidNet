# Translucency Module LLD

**Phase:** 5B — Remaining Services
**Deliverable:** D.15
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`TranslucencyModule` — the stage-18 owner responsible for forward-lit
translucent rendering. Translucent objects (glass, particles, effects) are
rendered with forward lighting using the published forward-light family from
`LightingService`, composited over the deferred opaque result in
SceneColor.

### 1.2 Classification

TranslucencyModule is a **stage module** (not a full-lifecycle service).
It has no persistent state between frames and is dispatched per-view.

### 1.3 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 17 (reserved post-opaque extensions) | |
| **This** | **Stage 18 — Translucency** | Forward-lit translucent rendering |
| Successor | Stage 19 (Distortion — reserved) | |

### 1.4 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage 18
- Published forward-light family consumption from LightingService

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── Translucency/
            ├── TranslucencyModule.h
            ├── TranslucencyModule.cpp
            ├── TranslucencyMeshProcessor.h
            └── TranslucencyMeshProcessor.cpp
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class TranslucencyModule {
 public:
  explicit TranslucencyModule(Renderer& renderer);
  ~TranslucencyModule();

  TranslucencyModule(const TranslucencyModule&) = delete;
  auto operator=(const TranslucencyModule&) -> TranslucencyModule& = delete;

  /// Stage 18 entry point. Per-view execution.
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

 private:
  Renderer& renderer_;
  std::unique_ptr<TranslucencyMeshProcessor> mesh_processor_;
};

}  // namespace oxygen::vortex
```

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| InitViewsModule (stage 2) | Current-view `PreparedSceneFrame` payload | Translucent geometry refinement without scene re-traversal |
| LightingService | `LightingFrameBindings` publication | Forward lighting evaluation |
| ShadowService | `ShadowFrameBindings` publication | Shadow terms |
| EnvironmentLightingService | `EnvironmentFrameBindings` publication | Ambient contribution |
| SceneTextures | SceneColor (RTV) | Composite target |
| SceneTextures | SceneDepth (DSV, read) | Depth test (no write) |

### 3.2 Outputs

| Product | Target | Blend Mode |
| ------- | ------ | ---------- |
| SceneColor | SceneTextures::GetSceneColor() | Alpha blend (SRC_ALPHA, INV_SRC_ALPHA) |

### 3.3 Execution Flow

```text
TranslucencyModule::Execute(ctx, scene_textures)
  │
  ├─ Read current view prepared-scene payload from ctx
  │     ├─ Refine the prepared-scene payload to translucent participants only
  │     ├─ Sort back-to-front by distance_sq (painter's algorithm)
  │     │
  │     ├─ Set render targets:
  │     │     RTV = SceneColor (alpha blend)
  │     │     DSV = SceneDepth (depth read, no write)
  │     │
  │     └─ for each translucent draw command:
  │           ├─ Bind forward-lit PSO (per-material variant)
  │           ├─ Bind published lighting / shadow / environment bindings
  │           ├─ Bind material resources
  │           └─ DrawIndexedInstanced(...)
  └─ (SceneColor now contains opaque + translucent)
```

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Forward-lit PSOs | Persistent | Per-material variants, alpha blend |
| No new GPU textures | — | Uses existing SceneColor and SceneDepth |

## 5. Shader Contracts

### 5.1 Forward Translucency Shader

```hlsl
// Stages/Translucency/TranslucencyForward.hlsl

#include "../../Shared/BRDFCommon.hlsli"
#include "../../Shared/ForwardLightingCommon.hlsli"
#include "../../Contracts/SceneTextures.hlsli"

struct TranslucencyPSOutput {
  float4 color : SV_Target0;   // Alpha-blended to SceneColor
};

TranslucencyPSOutput TranslucencyForwardPS(ForwardVSOutput input) {
  MaterialSurface surface = EvaluateMaterial(input);

  // Forward lighting: iterate visible lights from the published forward-light package
  float3 lighting = EvaluateForwardLighting(
    surface, input.worldPos, CameraPosition,
    ForwardLightBindings);

  float3 ambient = EvaluateEnvironmentAmbient(surface, EnvironmentBindings);

  TranslucencyPSOutput output;
  output.color = float4(lighting + ambient + surface.emissive,
                          surface.opacity);
  return output;
}
```

### 5.2 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexTranslucencyVS` | vs_6_0 | Standard world-space VS |
| `VortexTranslucencyPS` | ps_6_0 | Forward-lit, alpha blend output |

## 6. Stage Integration

### 6.1 Dispatch Contract

`translucency_->Execute(ctx, scene_textures)` at stage 18.
Per-view execution.

### 6.2 Null-Safe Behavior

When null: no translucent rendering. Only opaque deferred result visible.

### 6.3 Capability Gate

Requires `kLightingData` (for published forward-light access).

## 7. Sorting

Translucent geometry must be sorted back-to-front per view to ensure
correct alpha blending. The mesh processor sorts by `distance_sq` from the
camera. This is a simple painter's algorithm; OIT (order-independent
transparency) is a future enhancement.

## 8. Testability Approach

1. **Alpha blend validation:** Translucent quad in front of opaque object →
   verify correct color blending in SceneColor.
2. **Forward lighting:** Translucent object under point light → verify
   correct lighting (specular highlight, diffuse).
3. **Depth test:** Translucent object behind opaque → verify not visible
   (depth read from prepass/basepass).
4. **RenderDoc:** Frame 10, inspect stage 18 draw calls. Verify
   back-to-front ordering and alpha blend state.

## 9. Open Questions

1. **Order-independent transparency:** OIT techniques (weighted blended OIT,
   per-pixel linked lists) are a Phase 7+ enhancement. Phase 5B uses
   painter's algorithm.
