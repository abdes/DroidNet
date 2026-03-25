# VSM Shadow Rasterizer Implementation Plan

Status: `in_progress`
Audience: engineer implementing Phase F of the VSM pipeline
Scope: Stage 12 shadow rasterization, from page-job preparation through raster submission, dirty tracking, reveal handling, and static-feedback hooks

Cross-references:

- `VirtualShadowMapArchitecture.md` — authoritative architecture spec
- `VsmImplementationPlan.md` — parent remaining-work plan

## Summary Tracking

This document decomposes parent Phase F into bounded execution slices. Parent
Phase F remains `in_progress` until every slice below is implemented and
validated.

| Status | Slice | Deliverable | Exit Gate |
| --- | --- | --- | --- |
| ☑ | F0 | Pass scaffolding and deterministic page-job preparation | `VsmShadowRasterizerPass` exists, consumes current allocation products, expands raster page jobs deterministically, and has focused automated coverage |
| ☑ | F1 | Baseline depth submission into physical VSM pages | Dynamic-slice depth writes land in the correct physical page rects for known test geometry without touching unrelated pages |
| ☐ | F2 | GPU instance culling and compact draw-list generation | Instance culling consumes draw bounds plus previous-frame screen HZB and emits compact per-page draw arguments |
| ☐ | F3 | Static recache, reveal tracking, and invalidation feedback | Static-only rerender routing, reveal-forced redraw, and primitive-to-page feedback all exist without breaking the merge contract |
| ☐ | F4 | Point-light face routing, orchestration hardening, and validation sweep | Directional + local-light paths render correctly, targeted tests pass, and parent Phase F exit gate evidence is complete |

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

### F3 — Static recache, reveal tracking, and invalidation feedback

Scope:

- support static-only rerender jobs into slice 1 without changing the later
  merge contract
- add newly-visible primitive reveal forcing
- record primitive/page overlap feedback for later invalidation refinement

### F4 — Point-light face routing and validation sweep

Scope:

- point-light per-face routing without widening public remap-key contracts
- renderer hardening and targeted logging
- phase-level validation sweep and updated parent-plan evidence

## 3. Current Slice Status

`F0` and `F1` are now implemented and validated. The next active slice is
`F2`.

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

## 4. Expected Source Layout

Planned files for the early slices:

- `src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h`
- `src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.h`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.cpp`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowRasterJobs_test.cpp`
- `src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowRasterizerPass_test.cpp`

Later slices are expected to add:

- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmInstanceCulling.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowDepth_VS.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowDepth_PS.hlsl`

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
ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "VsmShadowRaster"
```

Maximum-verbosity sanity runs:

```powershell
out\build-ninja\bin\Debug\Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests.exe --gtest_filter=VsmShadowRasterJobsTest.* -v 9
out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_filter=VsmShadowRasterizerPassGpuTest.* -v 9
```

Latest validation evidence (`2026-03-25`):

- build passed for `Oxygen.Renderer.Oxygen.Renderer.VirtualShadows.Tests.Tests`
- build passed for `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests`
- `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "VsmShadowRaster"` passed with:
  - `VsmShadowRasterJobsTest.BuildShadowRasterPageJobsBuildsDeterministicJobsForPreparedPages`
  - `VsmShadowRasterJobsTest.BuildShadowRasterPageJobsSkipsPagesWithoutProjectionOrValidPhysicalCoord`
  - `VsmShadowRasterizerPassGpuTest.PrepareResourcesBuildsPreparedPagesAndRegistersPass`
- direct `-v 9` sanity runs passed and the logs were coherent:
  - CPU job-prep logs only emitted the expected contract-path warnings for
    invalid physical page, missing projection, and not-ready frame inputs
  - GPU pass logs showed `VsmShadowRasterizerPass` preparing `1` page job,
    beginning texture-state tracking from `Common`, transitioning the shadow
    pool to `DepthWrite`, then entering execute/pipeline setup in that order

## 6. Known Risks

1. `VsmPageAllocationFrame` does not currently publish a finalized CPU-side page
   flag array, so early slices must use conservative job selection derived from
   allocation decisions rather than pretending the full dirty/rerender policy is
   already available.
2. The renderer still exposes only the conventional directional shadow policy at
   the `ShadowManager` seam, so Phase F implementation must avoid implying full
   renderer integration before parent Phase K.
3. Static recache and reveal handling depend on new CPU/GPU contracts; they are
   intentionally deferred instead of being hidden inside the initial raster
   scaffold.

## 7. Exit Criteria for Parent Phase F

Parent Phase F is only complete when all of the following are true:

- shadow depth is rasterized into the correct physical VSM pages
- previous-frame screen HZB is consumed by GPU culling from frame 1 onward
- point-light face routing works
- static recache routing works when a static slice exists
- reveal-forced rerender exists
- invalidation feedback capture exists
- validation evidence is recorded in `VsmImplementationPlan.md`
