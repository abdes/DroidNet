# VTX-M05B — Occlusion consumer closeout

Status: `validated`

| Field     | Summary                                                                                                                                                                                                 |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | GPU HZB occlusion, delayed readback, conservative fallback and draw consumers.                                                                                                                          |
| Remaining | Extensions: [VX-OCC-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-OCC-02](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-CULL-01](../../OPEN_ITEMS.md#p2--engineering-follow-ups). |
| Evidence  | [Validation record](validation.md)                                                                                                                                                                      |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M02

## Delivered scope

HZB occlusion consumer over the landed generic Screen HZB with visibility publication, readback latency, conservative fallbacks, base-pass consumers, diagnostics, and proof aligned to the UE5.7 occlusion/HZB source review recorded in the milestone plan.

## Scope and acceptance

Scope:

- Occlusion consumer over the already-landed generic Stage 5 Screen HZB:
  visibility-state publication, HZB-test batching, readback latency,
  conservative fallbacks, and consumers in base pass and other compatible draw
  builders where proven, with UE5.7 source mapping recorded in the detailed
  milestone plan.

Dependency note:

- Environment local fog depends on generic Screen HZB publication, not on this
  full occlusion closure.

**Status:** `validated`
**Milestone:** `VTX-M05B - Occlusion Consumer Closeout`
**Scope owner:** Vortex SceneRenderer Stage 5 occlusion consumer
**Primary LLD:** [../lld/occlusion.md](../../lld/occlusion.md)
**Supporting LLD:** [../lld/hzb.md](../../lld/hzb.md)

## 1. Goal

Implement the UE5.7-shaped HZB occlusion consumer over the existing Vortex
`ScreenHzbModule`. The milestone closes when Vortex can publish conservative
per-view prepared-draw visibility, consume it in the relevant draw command
builders, and prove that occluded draws are skipped without hiding visible
geometry.

## 2. Implementation Policy

- `ScreenHzbModule` remains the only owner of generic closest/furthest HZB
  production, publication, and HZB history.
- `OcclusionModule` owns candidate extraction, HZB tests, readback latency,
  conservative fallback, visibility publication, and occlusion diagnostics.
- First frame, missing HZB, invalid readback, overflow, or disabled stage means
  visible, not culled.
- Do not add a hardware-query path, Nanite-style instance culling, GPU Scene, or
  indirect-draw compaction in M05B.
- Use fixed-capacity structured buffers for candidate bounds and visibility
  results. This is the Oxygen divergence from UE5.7's fixed texture tables and
  keeps the same latency/fallback behavior while reusing `GpuBufferReadback`.
- Do not claim draw-reduction closure until a test or capture proves downstream
  consumers actually skipped draws because of occlusion.

## 4. UE5.7 References

Each implementation slice must re-check the relevant source before claiming
parity:

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
  - previous-result visibility consumption
- `Shaders/Private/HZBOcclusion.usf`
  - box projection and furthest-HZB visibility test

## 5. Non-Goals

- No `Oxygen.Renderer` fallback.
- No general editor showflag or visibility-debug framework.
- No Nanite, GPU Scene, instance-culling, or indirect-draw argument compaction.
- No shadow light-view occlusion claim unless the implemented visibility
  contract is proven correct for that consumer.
- No hardware occlusion query path unless this plan and the LLD are updated
  first with an accepted reason.

## 6. Implementation Slices

### Slice A - Architecture And Plan Authority

**Status:** `validated`

Tasks:

- Update [../lld/occlusion.md](../../lld/occlusion.md) so it reflects the landed
  `ScreenHzbModule` producer and the missing occlusion consumer.
- Create this dedicated M05B plan.
- Update the single VTX-M05B row in
  [Milestone record](../../PLAN.md).

[Checks](validation.md#slice-a---architecture-and-plan-authority--checks).

Remaining gap:

- No open Slice A gap.

### Slice B - Visibility Result Substrate

**Status:** `validated`

Tasks:

- Add occlusion result, stats, and fallback-reason types.
- Add `OcclusionModule` shell and per-view result storage.
- Publish an invalid/all-visible result when disabled or unsupported.
- Add focused tests for result indexing, disabled behavior, and enum
  `to_string` coverage.

[Checks](validation.md#slice-b---visibility-result-substrate--checks).

Remaining gap:

- No open Slice B substrate gap. HZB GPU testing/readback remains Slice C.

### Slice C - HZB Occlusion Tester Pass

**Status:** `in_progress`

Tasks:

- Add the fixed-capacity HZB occlusion tester resources and pass wrapper.
- Add `Vortex/Stages/Occlusion/OcclusionTest.hlsl`.
- Use furthest HZB bindings from `ScreenHzbModule` and structured
  bounds/results buffers for the first implementation.
- Implement readback latency and first-frame visible fallback.
- Wire shader catalog and ShaderBake entries.

[Checks](validation.md#slice-c---hzb-occlusion-tester-pass--checks).

Remaining gap:

- Real D3D12 readback/result decode and debug-layer evidence are still required
  before Slice C can be marked `validated`.

### Slice D - Consumer Integration

**Status:** `in_progress`

Tasks:

- Consume `OcclusionFrameResults` in base-pass opaque/masked command building.
- Integrate depth/shadow consumers only where the current prepared-draw
  visibility contract is valid for the consuming view.
- Record before/after draw counts when a consumer skips occluded draws.

[Checks](validation.md#slice-d---consumer-integration--checks).

Remaining gap:

- Runtime/capture proof is still required before Slice D can be marked
  `validated`.

### Slice E - Diagnostics And Capture Surface

**Status:** `in_progress`

Tasks:

- Publish occlusion candidate/tested/visible/occluded/overflow counters.
  Implemented in the `Vortex.OcclusionFrameResults` product descriptor.
- Record fallback reason, current furthest HZB availability, previous result
  validity, and consumer draw-count deltas. Implemented through compact
  `Vortex.OcclusionFrameResults` and `Vortex.BasePassDrawCommands`
  descriptors.
- Surface the facts through the M05A diagnostics ledger/manifest without adding
  a large new UI. Stable counter descriptors export to the capture manifest;
  transient `bindless:` descriptor indices remain filtered out.

[Checks](validation.md#slice-e---diagnostics-and-capture-surface--checks).

Remaining gap:

- Runtime capture proof still has to show real occlusion counters and base-pass
  cull deltas in an application frame before Slice E can be marked
  `validated`.

### Slice F - Runtime Proof And Closeout

**Status:** `in_progress`

Tasks:

- Add a clean runtime enable path for proof. Implemented through archived
  renderer CVars `vtx.occlusion.enable` (default off) and
  `vtx.occlusion.max_candidate_count`.
- Run focused builds/tests.
- Run ShaderBake when shader catalog changes.
- Capture a controlled runtime scene that proves occlusion skips hidden draws
  and keeps visible geometry.
- Run CDB/D3D12 debug-layer validation.
- Update the VTX-M05B ledger row with concise implementation and validation
  evidence.

[Checks](validation.md#slice-f---runtime-proof-and-closeout--checks).

## 7. Exit Gate

VTX-M05B may be marked `validated` only when:

- implementation exists for the result substrate, HZB tester, readback/fallback
  behavior, diagnostics, and proven consumers
- required docs and this plan are current
- focused tests, ShaderBake/catalog validation, runtime/capture proof, and
  D3D12 debug-layer evidence are recorded
- the single ledger row states the remaining gap as closed

## Supporting records

- [validation](validation.md)
