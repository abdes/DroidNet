# Occlusion Consumer LLD

**Phase:** 5C - Remaining Services
**Deliverable:** D.16
**Status:** `in_progress`

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

## 1. Scope And Context

### 1.1 What This Covers

`OcclusionModule` is the Stage 5 **HZB consumer and visibility publisher**. It
tests prepared-scene bounds against the furthest Screen HZB, reads back the
previous frame's results, and publishes conservative visibility for downstream
draw command builders.

This document covers:

- occlusion candidate extraction from the current `PreparedSceneFrame`
- UE5.7-shaped HZB occlusion tests over prepared draw bounds
- result latency, readback, and conservative fallback behavior
- per-view visibility publication for base/depth/shadow consumers
- minimal diagnostics and proof requirements for draw-reduction claims

This document does **not** own generic HZB generation. `ScreenHzbModule` owns
current/previous HZB texture creation, bindless publication, and per-view HZB
history as specified by [hzb.md](hzb.md). M05B must not duplicate that producer.

### 1.2 Classification

`OcclusionModule` is a Stage 5 module with per-view persistent history state.
The persistent state is the occlusion result/readback history and frame
validity, not the HZB textures themselves.

### 1.3 Stage Position

| Position | Stage | Notes |
| --- | --- | --- |
| Predecessor | Stage 3 DepthPrepass | Current `SceneDepth` exists when HZB is requested. |
| Predecessor | Stage 5 ScreenHzbModule | Builds current furthest HZB and exposes previous furthest HZB when available. |
| **This** | **Stage 5 OcclusionModule** | Tests candidates and publishes conservative visibility. |
| Successor | Base/depth/shadow draw command builders | May skip occluded prepared draw items once visibility is valid. |

### 1.4 UE5.7 Source Mapping

The parity reference is the HZB occlusion path, not a hardware-query-first
path:

- `Renderer/Private/HZB.cpp`
  - `InitHZBCommonParameter`
  - `GetHZBParameters`
  - `IsPreviousHZBValid`
- `Renderer/Private/DeferredShadingRenderer.cpp`
  - `RenderHzb`
  - `RenderOcclusion`
  - `FamilyPipelineState->bHZBOcclusion`
- `Renderer/Private/SceneRendering.h`
  - `FHZBOcclusionTester`
- `Renderer/Private/SceneOcclusion.cpp`
  - `FHZBOcclusionTester::AddBounds`
  - `FHZBOcclusionTester::Submit`
  - `FHZBOcclusionTester::MapResults`
  - `FHZBOcclusionTester::IsVisible`
- `Renderer/Private/SceneVisibility.cpp`
  - mapping previous-frame HZB results into current visibility decisions
- `Shaders/Private/HZBOcclusion.usf`
  - AABB projection and `IsVisibleHZB` test over the furthest HZB

### 1.5 Oxygen Divergences

Oxygen keeps the UE-shaped behavior but maps it to existing Vortex ownership:

- `ScreenHzbModule` already builds closest/furthest pyramids and publishes
  `ScreenHzbFrameBindings`; `OcclusionModule` consumes those products.
- The first M05B implementation targets prepared draw metadata and bounding
  spheres from `PreparedSceneFrame`; it does not add Nanite, GPU Scene, or
  instance-culling infrastructure.
- Hardware occlusion query heaps are not the first implementation path. UE5.7's
  HZB tester already uses GPU-produced visibility plus readback, which fits
  Vortex's Stage 5 HZB and diagnostics model better.
- UE5.7 stores candidate bounds/results in fixed-size textures. Oxygen uses
  fixed-capacity structured buffers for the first implementation because the
  existing graphics layer already provides structured UAV/SRV descriptors and
  `GpuBufferReadback`; the behavioral contract remains the UE-shaped one:
  fixed capacity, current-frame submission, previous-result consumption, and
  conservative visible fallback.
- First-frame, missing-HZB, invalid-readback, and overflow cases are
  conservative: publish visible, record the fallback reason, and keep rendering
  correct.
- No `Oxygen.Renderer` fallback is permitted.

## 2. Public Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── Occlusion/
            ├── OcclusionModule.h
            ├── OcclusionModule.cpp
            ├── OcclusionConfig.h
            ├── Internal/
            │   ├── HzbOcclusionTester.h
            │   └── HzbOcclusionTester.cpp
            ├── Passes/
            │   ├── OcclusionTestPass.h
            │   └── OcclusionTestPass.cpp
            └── Types/
                ├── OcclusionFrameResults.h
                └── OcclusionStats.h

src/Oxygen/Graphics/Direct3D12/
└── Shaders/
    └── Vortex/
        └── Stages/
            └── Occlusion/
                └── OcclusionTest.hlsl
```

The `ScreenHzbBuild.hlsl` shader remains owned by `ScreenHzbModule` even though
it lives in the `Vortex/Stages/Occlusion` shader folder for stage locality.

### 2.2 Module API

```cpp
namespace oxygen::vortex {

class OcclusionModule {
public:
  explicit OcclusionModule(Renderer& renderer);
  ~OcclusionModule();

  OcclusionModule(const OcclusionModule&) = delete;
  auto operator=(const OcclusionModule&) -> OcclusionModule& = delete;

  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  [[nodiscard]] auto GetCurrentResults() const -> const OcclusionFrameResults&;
  [[nodiscard]] auto GetStats() const -> const OcclusionStats&;

private:
  Renderer& renderer_;
};

} // namespace oxygen::vortex
```

`Execute` must read HZB availability through the current `RenderContext` and the
landed `ScreenHzbModule` publications. The API intentionally does not expose an
HZB builder.

### 2.3 Visibility Result Shape

The published result is keyed to prepared draw metadata indices, not scene
objects:

```cpp
struct OcclusionFrameResults {
  std::span<const std::uint8_t> visible_by_draw;
  std::uint32_t draw_count;
  bool valid;
  OcclusionFallbackReason fallback_reason;
};
```

Rules:

- `valid == false` means downstream consumers must treat every draw as visible.
- When `valid == true`, a zero byte means occluded and a non-zero byte means
  visible.
- The array length must match the prepared-scene draw metadata count for the
  view that produced it.
- Results are per view. Multi-view work must not share visibility arrays across
  views.

Every enum introduced for the occlusion API must use existing Oxygen enum
patterns, include `to_string` overloads, and use `src/Oxygen/Base/Macros.h`
flag helpers when flags are needed.

## 3. Data Flow

### 3.1 Inputs

| Source | Data | Purpose |
| --- | --- | --- |
| `PreparedSceneFrame` | draw metadata, render items, world matrices, bounding spheres | Candidate bounds and draw-index mapping. |
| `RenderContext::ViewSpecific` | current/previous HZB fields | HZB availability and view-local HZB dimensions. |
| `ScreenHzbFrameBindings` | HZB mapping parameters | UE-shaped HZB coordinate conversion. |
| `SceneTextures` | current depth product validity | Stage precondition and diagnostics evidence. |

### 3.2 Outputs

| Product | Consumer | Delivery |
| --- | --- | --- |
| `OcclusionFrameResults` | Base/depth/shadow draw builders | Per-view prepared draw visibility. |
| `OcclusionStats` | DiagnosticsService and capture manifests | Candidate/tested/occluded/fallback counts. |
| Current test readback | Next frame | Per-view persistent tester history. |

### 3.3 Execution Order

1. Map the previous frame's occlusion readback if it is available and valid for
   the same view.
2. Build the current frame's conservative visibility array from that previous
   result. Missing history means all visible.
3. Publish the visibility array before downstream draw command builders consume
   it.
4. If current furthest HZB is available, submit the current frame's candidate
   bounds for next-frame readback.
5. Record diagnostics counters and fallback reason.

This matches UE5.7's latency model: visibility uses previous results while the
current frame submits tests for later consumption.

## 4. HZB Occlusion Test

### 4.1 Candidate Selection

M05B starts with the prepared draw metadata already accepted by scene prep.
Candidates must have:

- a valid draw index
- a finite world-space bounding sphere or bounds proxy
- opaque or masked participation in the relevant pass
- an object size above the configured tiny-object threshold

Invalid, tiny, transparent-only, or overflowing candidates are treated visible
and counted by diagnostics.

### 4.2 GPU Test Shape

The UE5.7 reference stores bounds in fixed-size textures and writes a fixed-size
visibility texture. Oxygen maps those fixed tables to structured buffers:

- fixed maximum candidate count, initially UE-shaped at 256 x 256
- center/extent upload for candidate bounds
- result buffer with one visible/occluded value per candidate
- GPU-to-CPU buffer readback for next-frame `IsVisible` decisions
- unused/padded entries are visible

The shader projects bounds with current view matrices, rejects objects outside
the frustum, and tests the projected screen rectangle against the furthest HZB.
Under reversed-Z, the furthest pyramid is the correct conservative occlusion
surface for "hidden behind closer depth" decisions.

### 4.3 Fallback Policy

The fallback policy is part of correctness:

| Condition | Behavior |
| --- | --- |
| Stage disabled | Publish invalid results; consumers render all draws. |
| No prepared frame | Publish invalid results and no GPU test. |
| No current furthest HZB | Publish all visible, skip current submission. |
| No previous readback | Publish all visible, submit current test if possible. |
| Candidate count exceeds capacity | Test first capacity-limited batch; overflow visible and counted. |
| Readback failure | Publish all visible for that frame and mark previous result invalid. |

No fallback may cull geometry.

## 5. Consumer Integration

Downstream stages consume visibility by prepared draw index. The first required
consumers are:

- base pass opaque/masked command building
- any depth/shadow command path that rebuilds from the same prepared draw
  metadata and can safely preserve its pass semantics

Consumers must be simple:

```cpp
if (occlusion.valid && !occlusion.visible_by_draw[draw_index]) {
  continue;
}
```

The visibility payload must not leak rendering-mode, diagnostics, or UI options
into pass builders. Pass builders only see visibility.

Runtime enablement is explicit and conservative. `vtx.occlusion.enable`
defaults to `false` and is the only runtime on/off switch for Stage 5 HZB
occlusion testing plus consumer culling. `vtx.occlusion.max_candidate_count`
caps submitted prepared draws for proof and stress testing. There is no
compile-time `OXYGEN_WITH_*` gate for M05B; the code is part of Vortex and the
runtime switch controls behavior.

## 6. Diagnostics And Proof Surface

M05B diagnostics must be compact and useful:

- candidate count
- submitted/tested count
- visible/occluded count from mapped results
- overflow-visible count
- fallback reason
- current furthest HZB availability
- previous result validity
- draw counts before/after occlusion for consumers that actually skip draws

Diagnostics must integrate with the M05A `DiagnosticsService` pass/product
ledger and capture manifest. The runtime records `Vortex.OcclusionFrameResults`
with compact stable facts (`draws`, `candidates`, `submitted`, `visible`,
`occluded`, `overflow_visible`, `fallback`, `hzb`, `prev`, `valid`) and
`Vortex.BasePassDrawCommands` with `draws` plus `occlusion_culled`. Transient
bindless descriptor indices remain excluded from capture manifests; stable
counter descriptors are exported for external analysis scripts. A
draw-reduction claim is not allowed until a capture or focused test proves that
a consumer skipped draw commands because of occlusion.

## 7. Validation Gates

M05B cannot be marked `validated` until all gates are satisfied:

1. UE5.7 mapping evidence is current in this LLD and the detailed milestone
   plan.
2. Implementation exists for the occlusion result substrate, HZB tester,
   readback/fallback behavior, and at least the base-pass consumer.
3. Shader changes are in the Direct3D12 shader catalog and pass ShaderBake.
4. Focused tests prove conservative fallback, capacity overflow, result
   indexing, and consumer filtering.
5. Runtime/capture proof shows projected occlusion reducing draw submission in
   a controlled scene while preserving visible geometry.
6. D3D12 debug-layer/CDB validation records no relevant warnings or errors for
   the occlusion path.
7. `IMPLEMENTATION_STATUS.md` records one concise VTX-M05B ledger row with
   implementation files/areas, validation artifacts, and no hidden residual
   gap.

## 8. Non-Goals

- No `Oxygen.Renderer` reuse.
- No Nanite, GPU Scene, instance culling, or GPU-driven indirect draw
  compaction in M05B.
- No broad editor showflag system.
- No hardware-query path unless the HZB tester path proves insufficient and the
  design/plan are updated before implementation claims.
- No generic visibility framework outside the prepared-scene contract.

## 9. Open Questions

1. Whether shadow command consumers can share the first visibility mask without
   breaking light-view-specific culling. If not, M05B closes with base/deferred
   consumers and records shadow-specific occlusion as a later light-view task.
2. Whether candidate bounds should upgrade from bounding spheres to full AABB
   extents once the prepared-scene payload exposes stable per-draw boxes. The
   first implementation may conservatively derive extents from spheres.
