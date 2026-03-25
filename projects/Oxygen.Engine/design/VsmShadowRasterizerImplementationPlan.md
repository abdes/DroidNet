# VSM Shadow Rasterizer Implementation Plan

Status: `completed`
Audience: engineer implementing Phase F of the VSM pipeline
Scope: Stage 12 shadow rasterization, from page-job preparation through raster submission, dirty tracking, reveal handling, and static-feedback hooks

Cross-references:

- `VirtualShadowMapArchitecture.md` — authoritative architecture spec
- `VsmImplementationPlan.md` — parent remaining-work plan

## Summary Tracking

This document decomposes parent Phase F into bounded execution slices. All
slices `F0` through `F4` are now implemented and validated.

| Status | Slice | Deliverable | Exit Gate |
| --- | --- | --- | --- |
| ☑ | F0 | Pass scaffolding and deterministic page-job preparation | `VsmShadowRasterizerPass` exists, consumes current allocation products, expands raster page jobs deterministically, and has focused automated coverage |
| ☑ | F1 | Baseline depth submission into physical VSM pages | Dynamic-slice depth writes land in the correct physical page rects for known test geometry without touching unrelated pages |
| ☑ | F2 | GPU instance culling and compact draw-list generation | Instance culling consumes draw bounds plus previous-frame screen HZB and emits compact per-page draw arguments |
| ☑ | F3 | Static recache, dirty publication, reveal tracking, and invalidation feedback | Static-only rerender routing, dirty outputs, reveal-forced redraw, and primitive-to-page feedback all exist without breaking the merge contract |
| ☑ | F4 | Point-light face routing, orchestration hardening, and validation sweep | Directional + local-light paths render correctly, targeted tests pass, and parent Phase F exit gate evidence is complete |

## 1. Why Phase F Was Split

Phase F is not just one graphics pass. The parent plan currently groups
multiple cross-cutting concerns under one heading:

- page-job/view construction from `VsmProjectionData`
- GPU instance culling with previous-frame screen HZB
- compact indirect draw generation
- depth rasterization into physical page rects
- dirty metadata updates
- primitive reveal tracking
- static-slice recache routing
- static invalidation feedback capture

The first three items alone span new CPU contracts, GPU buffers, and render
submission. The last three widen invalidation and cache semantics beyond a
single-pass implementation. That scope is too broad for one evidence-backed
change set, so this plan executes it in slices with explicit gates.

## 2. Slice Boundaries

### F0 — Pass scaffolding and deterministic page-job preparation

Scope:

- add `VsmShadowRasterizerPass`
- define its frame input contract
- expand current-frame page-allocation decisions plus per-map projections into
  deterministic raster page jobs
- compute physical page viewport/scissor from the physical pool layout
- keep job selection conservative for the first slice:
  - include `kAllocateNew` and `kInitializeOnly`
  - defer broader dirty/reveal-driven resubmission until later slices

Out of scope:

- issuing real depth draws
- GPU culling
- dirty metadata writes
- static recache or reveal handling

### F1 — Baseline depth submission into physical pages

Scope:

- bind the physical VSM shadow texture array as the depth target
- iterate prepared page jobs and raster opaque/masked shadow-caster draws into
  the target physical rects
- use page-local view/projection data per job
- establish the first correctness test that verifies writes land in the intended
  page tile

### F2 — GPU instance culling and compact draw-list generation

Scope:

- add `VsmInstanceCulling.hlsl`
- consume draw bounds, draw metadata, per-page jobs, and previous-frame
  screen-space HZB
- emit compact indirect draw commands, preferably via
  `IndirectCommandLayout::kDrawWithRootConstant`
- route the raster pass to those compact commands instead of brute-force
  submission

### F3 — Static recache, dirty publication, reveal tracking, and invalidation feedback

Scope:

- support static-only rerender jobs into slice 1 without changing the later
  merge contract
- publish rendered-page dirty results needed by later merge/HZB stages
- add newly-visible primitive reveal forcing
- record primitive/page overlap feedback for later invalidation refinement

### F4 — Point-light face routing and validation sweep

Scope:

- point-light per-face routing without widening public remap-key contracts
- renderer hardening and targeted logging
- phase-level validation sweep and updated parent-plan evidence

## 3. Current Slice Status

`F0` through `F4` are now implemented and validated. Parent Phase `F` is now
complete; the next parent-plan phase is `G`.

Delivered in `F0`:

- `VsmShadowRasterizerPass.h/.cpp`
- a narrow CPU helper that turns frame decisions plus projections into prepared
  raster jobs
- focused automated tests for job selection and physical page rect generation
- focused GPU pass coverage that validates pass preparation and
  `RenderContext` registration against a real shadow-pool texture
- parent-plan reference to this sub-plan

Evidence backing `F0`:

- code is in the renderer build
- focused tests ran and passed
- the pass fixed its own shadow-texture resource tracking instead of relying on
  implicit upstream state
- remaining gaps to `F1` are listed instead of implied away

Delivered in `F1`:

- `VsmShadowRasterizerPass` now reuses the shared `DepthPrePass` depth-only
  raster path instead of inventing a parallel submission stack
- per-page VSM jobs upload page-local `ViewConstants` derived from the prepared
  shadow projection while preserving the published bindless view-frame slot
- dynamic-slice raster submission binds per-page DSVs, viewport/scissor rects,
  and emits opaque/masked shadow-caster draws into the physical shadow pool
- `VsmPhysicalPagePoolManager` now owns the `ResourceRegistry` contract for VSM
  shadow/HZB pool resources, including unregister on recreate/reset/destruction
- focused GPU coverage verifies both physical-page depth writes and resource
  registry lifetime behavior

Evidence backing `F1` on `2026-03-25`:

- built `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` and
  `Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests` in
  `out/build-ninja` (`Debug`)
- ran `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "VsmShadowRaster|VsmPhysicalPagePoolGpuLifecycle"` with `100% tests passed, 0 tests failed out of 14`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmPhysicalPagePoolGpuLifecycleTest.*:VsmShadowRasterizerPassGpuTest.* -v 9`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmShadowRasterJobsTest.* -v 9`
- max-verbosity logs showed the expected flow: pool-resource registration,
  `Common -> DepthWrite`, `VsmShadowRasterizerPass` prepare summary,
  `execute prepared_pages=1 eligible_pages=1 emitted=1`, and registered pool
  resources being unregistered on reset/destruction
- the GPU correctness test verified depth changed inside the target physical
  page while a neighboring page remained at the cleared depth

Delivered in `F2`:

- `VsmShadowRasterizerPass` now classifies eligible dynamic page jobs, builds
  GPU-ready page-job payloads, and dispatches compute culling per active shadow
  partition instead of brute-force replaying every draw for every page
- new shader ABI types (`VsmShaderRasterPageJob`,
  `VsmShaderIndirectDrawCommand`) and HLSL includes define the shared CPU/GPU
  contract for per-page compaction
- `VsmInstanceCulling.hlsl` consumes prepared page jobs, draw metadata, draw
  bounds, and previous-frame screen HZB to write compact indirect commands plus
  per-page command counts
- `CommandRecorder` now exposes counted indirect execution cleanly at the API
  layer, with Direct3D12 and headless/fake implementations updated together
- focused GPU coverage verifies both the compacted indirect buffers and the
  resulting page-local depth writes, including the previous-frame HZB culling
  path

Evidence backing `F2` on `2026-03-25`:

- built `Oxygen.Graphics.Common.Commander.Tests`,
  `Oxygen.Renderer.GpuTimelineProfiler.Tests`,
  `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests`, and
  `Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests` in
  `out/build-ninja` (`Debug`)
- ran `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\\.Graphics\\.Common\\.Commander\\.Tests|Oxygen\\.Renderer\\.GpuTimelineProfiler\\.Tests|VsmShadowRaster|VsmPhysicalPagePoolGpuLifecycle"` with `100% tests passed, 0 tests failed out of 17`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Graphics.Common.Commander.Tests.exe -v 9`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.GpuTimelineProfiler.Tests.exe -v 9`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmShadowRasterJobsTest.* -v 9`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmPhysicalPagePoolGpuLifecycleTest.*:VsmShadowRasterizerPassGpuTest.* -v 9`
- max-verbosity logs were sane for the integrated `F2` path: prepare summary,
  `prepared instance culling`, counted-indirect execute summary, and previous
  frame HZB availability all appeared in the expected order
- the compact-draw GPU test verified per-page command counts and indirect
  command payloads before confirming that only the expected physical pages
  received depth writes
- the HZB GPU test verified `ScreenHzbBuildPass` published a previous-frame
  pyramid (`previous_available=true`) and that the VSM compute path compacted
  the page to zero draws while the target shadow page remained at the cleared
  depth
- extra probe note: `Oxygen.Renderer.ScreenHzb.Tests.exe --gtest_filter=ScreenHzbBuildGpuTest.PreviousFrameOutputTracksPriorPyramidAcrossFrames -v 9`
  still throws an SEH access violation during the standalone test body's
  seed-depth setup, so it is not used as `F2` evidence; the integrated VSM HZB
  test above passed and remains the gating coverage for this slice

Delivered in `F3`:

- `VsmShadowRasterizerPass` now routes `kStaticOnly` page jobs into the static
  shadow slice while leaving the dynamic slice untouched for the later merge
  contract
- `VsmPublishRasterResults.hlsl` now publishes rendered-page dirty flags into
  the per-frame dirty buffer and folds raster results into
  `VsmPhysicalPageMeta` by setting `is_dirty` / `used_this_frame`, clearing
  `view_uncached` plus the matching invalidation bit, and recording
  `last_touched_frame`
- reveal forcing now derives from previous-frame visible shadow primitives plus
  `DrawMetadata` visibility flags, bypasses HZB rejection for newly visible
  primitives, and publishes `kRevealForced` when that rerender path is taken
- static primitive/page overlap feedback is now built from prepared page jobs
  and draw bounds, and `VsmCacheManager` now publishes both visible primitives
  and static primitive/page feedback into the extracted retained frame snapshot
- focused GPU coverage now verifies dynamic dirty publication, static-slice
  recache, reveal-forced rerender, and the cache-manager publication contract

Evidence backing `F3` on `2026-03-26`:

- built `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` and
  `Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests` in
  `out/build-ninja` (`Debug`)
- ran `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\\.Renderer\\.VirtualShadowGpuLifecycle\\.Tests|Oxygen\\.Renderer\\.Oxygen\\.Renderer\\.VirtualShadows\\.Tests\\.Tests"` with `100% tests passed, 0 tests failed out of 2`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmShadowRasterizerPassGpuTest.*:VsmPhysicalPagePoolGpuLifecycleTest.* -v 1`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmCacheManagerOrchestrationTest.* -v 1`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmShadowRasterizerPassGpuTest.*:VsmPhysicalPagePoolGpuLifecycleTest.* -v 9`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmCacheManagerOrchestrationTest.* -v 9`
- low-verbosity logs were clean: no warnings, no failures, and no
  `Reusing partition ... without observed retirement` staging warning
- max-verbosity logs were sane for the integrated `F3` path: the static-slice
  test logged `static_pages=1` during prepare and `static_feedback=1` during
  culling/execute, the HZB path logged `previous_hzb_available=true` with zero
  dirty flags after compaction, the reveal path logged `reveal_candidates=1`
  before a counted indirect rerender, and the cache-manager logs showed
  `published visible shadow primitives` plus
  `published static primitive/page feedback` before extraction
- the GPU tests verified dynamic dirty publication into the page dirty buffer,
  physical-page metadata updates, static-only rerender into slice `1`,
  reveal-forced dirty flag publication, and visible/static-feedback extraction
  surfaces

Delivered in `F4`:

- `VsmPageRequestProjection` now describes routed subregions inside an owning
  virtual map instead of assuming one projection equals one full map
- new shared routing helpers in `VsmProjectionRouting.h/.cpp` keep page-request
  generation and raster-job expansion on one contract for global-page mapping,
  page-table indexing, and projection-local page selection
- `BuildShadowRasterPageJobs` now resolves shared-map point-light face routes
  without widening public remap-key contracts, rejects overlapping routes
  deterministically, and preserves the selected face-local page for page crop
  math
- `VsmShadowRasterizerPass` now reports routed/cube-face page counts in its
  debug prepare summary and consumes the selected face-local page when building
  shadow view constants and per-page crop matrices
- focused CPU coverage now verifies shared-map request routing plus raster-job
  face selection and overlap rejection
- focused GPU coverage now verifies a shared-map point-light face route writes
  depth into the intended physical page while a neighboring page remains at the
  cleared depth

Evidence backing `F4` on `2026-03-26`:

- built `Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests` and
  `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` in `out/build-ninja`
  (`Debug`)
- ran `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\\.Renderer\\.VirtualShadowGpuLifecycle\\.Tests|Oxygen\\.Renderer\\.Oxygen\\.Renderer\\.VirtualShadows\\.Tests\\.Tests"` with `100% tests passed, 0 tests failed out of 2`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmPageRequestGenerationTest.*:VsmShadowRasterJobsTest.* -v 9`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmPageRequestGeneratorGpuTest.*:VsmShadowRasterizerPassGpuTest.* -v 1`
- ran `out\\build-ninja\\bin\\Debug\\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmPageRequestGeneratorGpuTest.*:VsmShadowRasterizerPassGpuTest.* -v 9`
- low-verbosity GPU logs were clean: no warnings, no failures, and `[  PASSED  ] 8 tests.`
- max-verbosity GPU logs were sane for the new routed path: the point-face test
  logged `prepare map_count=2 prepared_pages=1 ... routed_pages=1 cube_face_pages=1`
  before counted-indirect execution, and the targeted physical page was written
  while its neighbor stayed at the cleared depth
- max-verbosity CPU logs only emitted the expected contract-path warnings for
  intentionally invalid inputs (`missing projection route` and
  `overlapping projection routes`) while the positive routing tests passed

## 4. Expected Source Layout

Implemented files through `F4`:

- `src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h`
- `src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.h`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.cpp`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.cpp`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.cpp`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h`
- `src/Oxygen/Graphics/Common/CommandRecorder.h`
- `src/Oxygen/Graphics/Direct3D12/CommandRecorder.h`
- `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp`
- `src/Oxygen/Graphics/Headless/CommandRecorder.h`
- `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/DrawMetadata.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmInstanceCulling.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmIndirectDrawCommand.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmPageRequestProjection.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmPageRequestGenerator.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmPublishRasterResults.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmRasterPageJob.hlsli`
- `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.cpp`
- `src/Oxygen/Renderer/Types/DrawMetadata.h`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowRasterJobs_test.cpp`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmPageRequestGeneration_test.cpp`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmPageRequestGeneratorPass_test.cpp`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowRasterizerPass_test.cpp`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmCacheManagerOrchestration_test.cpp`
- `src/Oxygen/Graphics/Common/Test/Commander_test.cpp`
- `src/Oxygen/Renderer/Test/GpuTimelineProfiler_test.cpp`

Dedicated VSM depth shaders are not planned while the shared `DepthPrePass`
path remains the contract.

## 5. Build and Test Commands

Repository root:

```powershell
Set-Location H:\projects\DroidNet\projects\Oxygen.Engine
```

Current-slice build commands:

```powershell
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests" --parallel 4
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests" --parallel 4
```

Current-slice test commands:

```powershell
ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\.Renderer\.VirtualShadowGpuLifecycle\.Tests|Oxygen\.Renderer\.Oxygen\.Renderer\.VirtualShadows\.Tests\.Tests"
```

Maximum-verbosity sanity runs:

```powershell
out\build-ninja\bin\Debug\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmPageRequestGenerationTest.*:VsmShadowRasterJobsTest.* -v 9
out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmPageRequestGeneratorGpuTest.*:VsmShadowRasterizerPassGpuTest.* -v 9
```

Latest validation evidence (`2026-03-26`):

- build passed for `Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests`
- build passed for `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests`
- `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\\.Renderer\\.VirtualShadowGpuLifecycle\\.Tests|Oxygen\\.Renderer\\.Oxygen\\.Renderer\\.VirtualShadows\\.Tests\\.Tests"` passed with `2/2` tests green
- direct `-v 9` CPU routing sanity run passed for
  `VsmPageRequestGenerationTest.*:VsmShadowRasterJobsTest.*`
- direct `-v 1` GPU sanity run passed for
  `VsmPageRequestGeneratorGpuTest.*:VsmShadowRasterizerPassGpuTest.*`
- direct `-v 9` GPU sanity run passed for the same filter
- the low-verbosity GPU logs were clean: no warnings, no failures, and no
  staging partition-reuse warning
- the max-verbosity logs were coherent:
  - the point-face path logged `routed_pages=1 cube_face_pages=1` during
    prepare before writing depth into the routed physical page
  - static recache logged `static_pages=1` during prepare and
    `static_feedback=1` during culling/execute before depth landed in slice `1`
  - the integrated HZB path logged `previous_hzb_available=true` and kept the
    target page at cleared depth with zero dirty bits
  - the reveal path logged `reveal_candidates=1` and published
    `kRevealForced | kDynamicRasterized`
  - CPU routing logs only warned for the intentional invalid-route coverage

## 6. Known Risks

1. `VsmPageAllocationFrame` does not currently publish a finalized CPU-side page
   flag array, so early slices must use conservative job selection derived from
   allocation decisions rather than pretending the full dirty/rerender policy is
   already available.
2. The renderer still exposes only the conventional directional shadow policy at
   the `ShadowManager` seam, so Phase F implementation must avoid implying full
   renderer integration before parent Phase K.
3. Higher-level renderer orchestration beyond this raster-phase seam is still
   deferred to parent Phase `K`; this plan must not imply end-to-end VSM
   renderer integration before those seams land.

## 7. Exit Criteria for Parent Phase F

Parent Phase F is only complete when all of the following are true:

- shadow depth is rasterized into the correct physical VSM pages
- previous-frame screen HZB is consumed by GPU culling from frame 1 onward
- point-light face routing works
- static recache routing works when a static slice exists
- reveal-forced rerender exists
- invalidation feedback capture exists
- validation evidence is recorded in `VsmImplementationPlan.md`

All exit criteria above are now satisfied on `2026-03-26`.
