---
phase: 03-deferred-core
verified: 2026-04-15T18:10:00+04:00
status: passed
score: 6/6 must-haves verified
overrides_applied: 0
gaps: []
deferred:
  - truth: "RenderDoc runtime validation of the deferred-core output"
    addressed_in: "Phase 4"
    evidence: "Phase 03 closeout continues to defer the first truthful runtime RenderDoc frame-10 capture until Async and DemoShell migrate to Vortex in Phase 4."
---

# Phase 3: Deferred Core Verification Report

**Phase Goal:** deliver a truthful deferred-core baseline with real Stage 2/3/9/10/12 behavior, automated closeout proof, and runtime RenderDoc validation explicitly deferred to Phase 04.
**Verified:** 2026-04-15T18:10:00+04:00
**Status:** passed
**Re-verification:** yes

## Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Vortex shader contracts and entrypoints compile through `ShaderBake` and the engine shader catalog. | ✓ VERIFIED | `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4` exited `0`, including the deferred-light stencil-mark shader additions. |
| 2 | Stage 2 is a truthful InitViews contract with prepared-scene publication and active-view rebinding. | ✓ VERIFIED | `SceneRenderer.cpp` still dispatches Stage 2 through `InitViewsModule`, and the deferred-core test suite continues to pass `InitViewsPublishesPreparedSceneFrameForEverySceneView` and `InitViewsKeepsTheActiveViewPreparedFrameBoundToCurrentView`. |
| 3 | Stage 3 is a real depth/partial-velocity render stage. | ✓ VERIFIED | `DepthPrepassModule.cpp` now acquires a graphics command recorder, binds a real framebuffer, records draw calls from prepared-scene metadata, and copies `SceneDepth` into `PartialDepth`; the suite passes `DepthPrepassRecordsRealDrawWorkFromPreparedMetadata`. |
| 4 | Stage 9 writes deferred attachments through a real base-pass render path. | ✓ VERIFIED | `BasePassModule.cpp` records graphics work against `BasePassGBuffer.hlsl`, binds GBufferA-D plus `SceneColor`/`SceneDepth`, and the proof surface still passes `BasePassPromotesGBuffersAtStage10` and `BasePassCompletesVelocityForDynamicGeometry`. |
| 5 | Stage 12 reads GBuffers and records real fullscreen/stencil-bounded deferred-light draws into `SceneColor`. | ✓ VERIFIED | `SceneRenderer.cpp` now acquires a graphics recorder for deferred lighting, uploads per-light constants, records directional fullscreen draws plus point/spot stencil-mark and lighting passes, and the suite passes `DeferredLightingConsumesPublishedGBuffers`, `DeferredLightingAccumulatesIntoSceneColor`, and `DeferredLightingUsesStencilBoundedLocalLights` with draw/pipeline assertions. |
| 6 | The repo-owned Phase 03 closeout proof passes again against the remediated renderer behavior. | ✓ VERIFIED | `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10` exited `0`; the generated report records `stage_2_order=pass`, `stage_3_order=pass`, `stage_9_order=pass`, `stage_12_order=pass`, `gbuffer_contents=pass`, `scene_color_lit=pass`, and `stencil_local_lights=pass`. |

## Behavioral Spot-Checks

| Behavior | Command | Result |
| --- | --- | --- |
| Renderer + shader build | `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4` | PASS |
| Deferred-core/publication suites | `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)\.Tests$" --output-on-failure` | PASS |
| Repo-owned closeout runner | `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10` | PASS |

## Requirement Coverage

| Requirement | Status | Evidence |
| --- | --- | --- |
| `SHDR-01` | ✓ SATISFIED | ShaderBake/catalog build is green with the full deferred-core shader family. |
| `DEFR-01` | ✓ SATISFIED | Stage 3 and Stage 9 now record actual render work and the proof surface observes that path. |
| `DEFR-02` | ✓ SATISFIED | Stage 12 now records actual deferred-light draws and the closeout runner passes against that implementation. |

## Deferred Validation

Runtime RenderDoc capture is still deferred to Phase 04. That deferral is explicit and remains truthful because the first migrated runtime surface has not landed yet; it is no longer a blocker for Phase 03 completion.
