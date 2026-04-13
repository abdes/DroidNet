# Occlusion Module LLD

**Phase:** 5C — Remaining Services
**Deliverable:** D.16
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`OcclusionModule` — the stage-5 owner responsible for hierarchical Z-buffer
(HZB) generation, occlusion query batching and testing, temporal HZB
handoff, and feedback passes. It uses depth data from the depth prepass
(stage 3) to cull occluded geometry, reducing draw-call counts for
subsequent stages.

### 1.2 Classification

OcclusionModule is a **stage module** with per-view persistent history
state (HZB textures are stored on view state, not inline in the module).

### 1.3 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 3 (DepthPrepass) — SceneDepth written | |
| Predecessor | Stage 4 (GeometryVirtualization — reserved) | |
| **This** | **Stage 5 — Occlusion/HZB** | |
| Successor | Stage 6 (LightGrid) | |

### 1.4 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage 5
- UE5 reference: `RenderOcclusion` family (~2.3 k lines)

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── Occlusion/
            ├── OcclusionModule.h
            ├── OcclusionModule.cpp
            ├── Internal/
            │   ├── HzbGenerator.h/.cpp
            │   └── OcclusionQueryPool.h/.cpp
            ├── Passes/
            │   ├── HzbBuildPass.h/.cpp
            │   └── OcclusionTestPass.h/.cpp
            └── Types/
                └── OcclusionResult.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class OcclusionModule {
 public:
  explicit OcclusionModule(Renderer& renderer);
  ~OcclusionModule();

  OcclusionModule(const OcclusionModule&) = delete;
  auto operator=(const OcclusionModule&) -> OcclusionModule& = delete;

  /// Stage 5 entry point. Per-view execution.
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  /// Query whether a primitive is visible (after Execute).
  [[nodiscard]] auto IsVisible(uint32_t node_index) const -> bool;

 private:
  Renderer& renderer_;
  std::unique_ptr<HzbGenerator> hzb_generator_;
  std::unique_ptr<OcclusionQueryPool> query_pool_;
};

}  // namespace oxygen::vortex
```

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| SceneTextures | SceneDepth (SRV) | HZB source |
| InitViewsModule | Visible primitive lists | Occlusion test candidates |
| Previous frame | Temporal HZB | Two-phase occlusion test |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| HZB texture (mip chain) | OcclusionModule (internal), SSR (future) | Per-view GPU texture |
| Occlusion results | BasePassModule, LightingService | Refined visibility lists |
| Temporal HZB | Next frame | Per-view history state |

### 3.3 HZB Generation

1. Read SceneDepth (full resolution).
2. Build mip chain: each level = max of 2×2 block from previous level.
3. Result: hierarchical depth pyramid for fast AABB depth testing.

### 3.4 Occlusion Testing

Two-phase approach:

1. **Previous-frame HZB test:** Test AABB against previous frame's HZB.
   Fast, but may have false positives (previously occluded objects that
   moved into view).
2. **Current-frame refinement:** Objects that pass previous-frame test are
   drawn in depth prepass. After depth prepass, objects that failed
   previous-frame test are re-tested against current HZB.

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| HZB texture (mip chain) | Per view (persistent) | Stored in view history |
| Previous-frame HZB | Per view (persistent) | Swapped each frame |
| Occlusion query heap | Persistent | D3D12 query heap, recycled |
| HZB build PSO | Persistent | Compute or pixel shader |

## 5. Shader Contracts

### 5.1 HZB Build Shader

```hlsl
// Stages/Occlusion/HzbBuild.hlsl (compute)

Texture2D<float> InputDepth : register(t0);
RWTexture2D<float> OutputMip : register(u0);

[numthreads(8, 8, 1)]
void HzbBuildCS(uint3 dtid : SV_DispatchThreadID) {
  float2 uv = (float2(dtid.xy) + 0.5) / OutputDimensions;
  // Sample 4 texels from input, take max (conservative depth)
  float d0 = InputDepth.Load(int3(dtid.xy * 2, 0));
  float d1 = InputDepth.Load(int3(dtid.xy * 2 + int2(1,0), 0));
  float d2 = InputDepth.Load(int3(dtid.xy * 2 + int2(0,1), 0));
  float d3 = InputDepth.Load(int3(dtid.xy * 2 + int2(1,1), 0));
  OutputMip[dtid.xy] = max(max(d0, d1), max(d2, d3));
}
```

### 5.2 Occlusion Test Shader

```hlsl
// Stages/Occlusion/OcclusionTest.hlsl (compute)

// Test AABB screen-space bounds against HZB
// Output: visibility flag per object
```

### 5.3 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexHzbBuildCS` | cs_6_0 | HZB mip chain build |
| `VortexOcclusionTestCS` | cs_6_0 | AABB vs HZB test |

## 6. Stage Integration

### 6.1 Dispatch Contract

`occlusion_->Execute(ctx, scene_textures)` at stage 5.

### 6.2 Null-Safe Behavior

When null: no occlusion culling. All primitives from InitViews pass through
to downstream stages. Functionally correct but potentially slower for
complex scenes.

### 6.3 Capability Gate

Requires `kScenePreparation`. Optional feature — scenes with low geometry
count may disable occlusion for simpler dispatch.

## 7. Testability Approach

1. **HZB validation:** Known scene with one large occluder → verify HZB mip
   chain contains correct max-depth values.
2. **Occlusion test:** Place object fully behind occluder → verify
   `IsVisible()` returns false.
3. **Draw call reduction:** Complex scene with many occluded objects →
   measure draw call count with/without occlusion module.
4. **Temporal stability:** Moving camera → verify no visible popping
   (temporal HZB prevents single-frame visibility gaps).

## 8. Open Questions

1. **Two-phase vs single-phase:** Two-phase occlusion (previous + current
   HZB) adds complexity. Phase 5C may start with single-phase
   (current-frame HZB only) and add temporal refinement later if popping
   is observed.
2. **GPU-driven indirect draw:** When integrated with GPU-driven rendering
   pipeline (Phase 7+), occlusion results feed into indirect draw argument
   buffers rather than CPU visibility lists.
