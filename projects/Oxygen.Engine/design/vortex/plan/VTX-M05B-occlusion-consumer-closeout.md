# VTX-M05B Occlusion Consumer Closeout

**Status:** `in_progress`
**Milestone:** `VTX-M05B - Occlusion Consumer Closeout`
**Scope owner:** Vortex SceneRenderer Stage 5 occlusion consumer
**Primary LLD:** [../lld/occlusion.md](../lld/occlusion.md)
**Supporting LLD:** [../lld/hzb.md](../lld/hzb.md)

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
- Every status update must remain in the single VTX-M05B row of
  `IMPLEMENTATION_STATUS.md`.

## 3. Current State

| Area | Current state | M05B action |
| --- | --- | --- |
| Screen HZB | Current/previous closest/furthest HZB production and bindings exist. | Reuse as the producer; request furthest HZB for occlusion. |
| Occlusion stage directory | Placeholder directories only. | Add the Vortex-native module, types, pass wrapper, and shader. |
| Prepared scene | Draw metadata, render items, matrices, and bounding spheres exist. | Build candidates keyed to prepared draw indices. |
| Consumers | Base pass filters pass masks and `main_view_visible`; no HZB occlusion mask. | Add visibility-mask consumption without leaking diagnostics/options. |
| Diagnostics | M05A ledger, manifest, and debug panel exist. | Publish compact occlusion counters and fallback reasons. |

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

Current evidence:

- [../lld/occlusion.md](../lld/occlusion.md) now defines `OcclusionModule` as
  the HZB consumer/visibility publisher over the existing `ScreenHzbModule`.
- [../PLAN.md](../PLAN.md), [../ARCHITECTURE.md](../ARCHITECTURE.md), and
  [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) no longer claim
  that `OcclusionModule` owns generic HZB generation.
- Validation passed on 2026-04-26:
  `rg` consistency scan for M05B/occlusion ownership references;
  `git diff --check`.
- Committed as `cd57ac692 docs: plan vortex occlusion consumer closeout`.

Tasks:

- Update [../lld/occlusion.md](../lld/occlusion.md) so it reflects the landed
  `ScreenHzbModule` producer and the missing occlusion consumer.
- Create this dedicated M05B plan.
- Update the single VTX-M05B row in
  [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

Validation:

- `rg` consistency scan for M05B and occlusion ownership references.
- `git diff --check`.

Remaining gap:

- No open Slice A gap.

### Slice B - Visibility Result Substrate

**Status:** `validated`

Current evidence:

- `OcclusionConfig`, `OcclusionFallbackReason`, `OcclusionFrameResults`,
  `OcclusionStats`, and `OcclusionModule` exist.
- `RenderContext::ViewSpecific` exposes a per-view `occlusion_results` pointer.
- `SceneRenderer` constructs the module when scene-prep/deferred capabilities
  are active and records Stage 5 occlusion diagnostics facts. The default
  module config is disabled, so this slice publishes conservative invalid
  results without changing draw behavior.
- Focused build/test validation passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OcclusionModule --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.OcclusionModule" --output-on-failure`
  with 5/5 tests passing.
- Neighboring SceneRenderer regression validation passed on 2026-04-26 after
  rebuilding stale test executables:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(OcclusionModule|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure`
  with 54/54 tests passing across 3 test programs.

Tasks:

- Add occlusion result, stats, and fallback-reason types.
- Add `OcclusionModule` shell and per-view result storage.
- Publish an invalid/all-visible result when disabled or unsupported.
- Add focused tests for result indexing, disabled behavior, and enum
  `to_string` coverage.

Validation:

- Focused Vortex build.
- Unit tests for result substrate and fallback semantics.

Remaining gap:

- No open Slice B substrate gap. HZB GPU testing/readback remains Slice C.

### Slice C - HZB Occlusion Tester Pass

**Status:** `in_progress`

Current evidence:

- `OcclusionModule` now builds fixed-capacity structured candidate/result
  buffers, submits `Vortex.Stage5.OcclusionTest`, and consumes a later
  `GpuBufferReadback` when available. Missing/pending readback remains
  conservatively visible.
- `Vortex/Stages/Occlusion/OcclusionTest.hlsl` implements a furthest-HZB sphere
  test aligned with the existing Screen HZB binding contract.
- The shader is registered in `EngineShaderCatalog.h`.
- Validation passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OcclusionModule --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.OcclusionModule" --output-on-failure`
  with 6/6 tests passing;
  `cmake --build out\build-ninja --config Debug --target Oxygen.Graphics.Direct3D12.ShaderBake --parallel 4`;
  `Oxygen.Graphics.Direct3D12.ShaderBake.exe rebuild --workspace-root F:\projects\DroidNet\projects\Oxygen.Engine --build-root F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\shader-bake-validation --out F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\shader-bake-validation\shaders.bin --mode dev`;
  ShaderBake inspect confirmed `Vortex/Stages/Occlusion/OcclusionTest.hlsl`.

Tasks:

- Add the fixed-capacity HZB occlusion tester resources and pass wrapper.
- Add `Vortex/Stages/Occlusion/OcclusionTest.hlsl`.
- Use furthest HZB bindings from `ScreenHzbModule` and structured
  bounds/results buffers for the first implementation.
- Implement readback latency and first-frame visible fallback.
- Wire shader catalog and ShaderBake entries.

Validation:

- ShaderBake/catalog validation.
- Focused tests or GPU-proof harness for capacity, fallback, and result decode.
- CDB/D3D12 debug-layer audit for the pass.

Remaining gap:

- Real D3D12 readback/result decode and debug-layer evidence are still required
  before Slice C can be marked `validated`.

### Slice D - Consumer Integration

**Status:** `in_progress`

Current evidence:

- `BasePassMeshProcessor` consumes `OcclusionFrameResults` as an optional
  prepared-draw filter. Missing or invalid occlusion results remain all-visible.
- `BasePassModule` passes the current view's occlusion results into solid and
  wireframe-overlay base-pass command building and records the number of
  otherwise-eligible base-pass draws culled by occlusion.
- Validation passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure`
  with 33/33 tests passing.

Tasks:

- Consume `OcclusionFrameResults` in base-pass opaque/masked command building.
- Integrate depth/shadow consumers only where the current prepared-draw
  visibility contract is valid for the consuming view.
- Record before/after draw counts when a consumer skips occluded draws.

Validation:

- Focused tests proving filtered draws are skipped and visible fallback keeps
  all draws.
- Runtime proof in a controlled occluder/occludee scene.

Remaining gap:

- Runtime/capture proof is still required before Slice D can be marked
  `validated`.

### Slice E - Diagnostics And Capture Surface

**Status:** `planned`

Tasks:

- Publish occlusion candidate/tested/visible/occluded/overflow counters.
- Record fallback reason, current furthest HZB availability, previous result
  validity, and consumer draw-count deltas.
- Surface the facts through the M05A diagnostics ledger/manifest without adding
  a large new UI.

Validation:

- DiagnosticsService focused tests if new APIs are added.
- Capture manifest/analyzer proof that occlusion facts are exported.

### Slice F - Runtime Proof And Closeout

**Status:** `planned`

Tasks:

- Run focused builds/tests.
- Run ShaderBake when shader catalog changes.
- Capture a controlled runtime scene that proves occlusion skips hidden draws
  and keeps visible geometry.
- Run CDB/D3D12 debug-layer validation.
- Update the VTX-M05B ledger row with concise implementation and validation
  evidence.

Validation:

- The milestone remains `in_progress` until all proof artifacts are recorded.

## 7. Exit Gate

VTX-M05B may be marked `validated` only when:

- implementation exists for the result substrate, HZB tester, readback/fallback
  behavior, diagnostics, and proven consumers
- required docs and this plan are current
- focused tests, ShaderBake/catalog validation, runtime/capture proof, and
  D3D12 debug-layer evidence are recorded
- the single ledger row states the remaining gap as closed
