# Directional VSM Redesign Plan

Date: March 12, 2026
Status: `proposed`
Scope: directional virtual shadow maps only

Cross-references: [directional_vsm_architecture_review.md](directional_vsm_architecture_review.md) |
[virtual_shadow_map_backend.md](virtual_shadow_map_backend.md) |
[implementation_plan.md](implementation_plan.md)

## 1. Purpose

This document defines the replacement design for Oxygen directional VSM after
the March 12, 2026 architecture review.

It is explicitly inspired by the way Unreal splits page marking, page
management, cache invalidation, page-space reuse, per-page draw-command build,
and projection.

This is not an implementation-complete claim. It is the proposed redesign
contract that should replace the current frozen publication-led path.

## 1.1 Execution Tasks

Active execution order as of March 18, 2026:

1. `completed` Freeze a no-regression baseline with fresh evidence for:
   BurgerPiz parity captures, the close-ground clipmap-edge repro captures, and
   the heavy moving-camera RenderScene benchmark.
2. `completed` Split the monolithic directional resolve path into
   phased shaders and passes with no intentional behavior change. Exit gate:
   output-equivalent captures/logs and no PSO regressions.
3. `completed` Replace the brute-force draw x scheduled-page expansion with a
   UE-style per-page draw-command build path. Exit gate:
   lower resolve/build cost with matching rendered output.
4. `in_progress` Replace the current full-screen request pass with UE-style visible
   page marking and pruning. Exit gate:
   lower request-pass cost with matching rendered output.
5. `pending` Remove CPU-assisted dirty-page invalidation and fold dirty-page
   marking into the GPU per-page build path. Exit gate:
   lower CPU upload and invalidation cost with matching rendered output.
6. `pending` Remove temporary compatibility scaffolding and revalidate the
   full VSM path against the frozen baseline scenes and benchmark.

Task 1 evidence, March 18, 2026:

- fresh build state used for the baseline:
  - `cmake --build out/build-ninja --config Release --target oxygen-examples-renderscene --parallel 8`
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-renderscene oxygen-cooker-importtool oxygen-cooker-inspector --parallel 8`
- fresh full-window `nircmdc.exe savescreenshotwin` captures:
  - current BurgerPiz camera, virtual:
    `out/build-ninja/benchmarks/directional-vsm/task1-burgerpiz-current-virtual-only-20260318-004304.bmp`
  - current BurgerPiz camera, conventional:
    `out/build-ninja/benchmarks/directional-vsm/task1-burgerpiz-current-conventional-20260318-004304.bmp`
  - close-ground BurgerPiz repro, virtual:
    `out/build-ninja/benchmarks/directional-vsm/task1-burgerpiz-close-ground-virtual-only-20260318-004304.bmp`
  - close-ground BurgerPiz repro, conventional:
    `out/build-ninja/benchmarks/directional-vsm/task1-burgerpiz-close-ground-conventional-20260318-004304.bmp`
  - matching runtime logs:
    `out/build-ninja/benchmarks/directional-vsm/task1-burgerpiz-*-20260318-004304.stderr.log`
- fresh heavy moving-camera benchmark run:
  - runner:
    `powershell -ExecutionPolicy Bypass -File Examples\RenderScene\benchmark_directional_vsm.ps1`
  - archived log:
    `out/build-vs/benchmarks/directional-vsm/directional-vsm-benchmark-20260318-004420.log`
  - latest metadata:
    `out/build-vs/directional-vsm-benchmark-latest.json`
  - result:
    `wall_ms=7473`, `approx_fps=16.06`
  - renderer averages from the archived log:
    `sceneprep=0.748 ms`, `view_render=18.310 ms`,
    `render_graph=17.183 ms`, `env_update=0.518 ms`,
    `compositing=0.334 ms`

Task 1 measurement note:

- the current frozen benchmark runner no longer surfaces the old
  request/schedule/raster page counters in its archived log, and this task did
  not add new instrumentation to recover them
- task 1 is therefore closed as a fresh screenshot + wall-clock baseline for
  the current redesign execution, not as a full page-counter telemetry pass

Task 2 evidence, March 18, 2026:

- phased resolve split landed in code:
  - pass wiring:
    `src/Oxygen/Renderer/Passes/VirtualShadowResolvePass.h`
    `src/Oxygen/Renderer/Passes/VirtualShadowResolvePass.cpp`
  - shared/common shader body:
    `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolveCommon.hlsli`
  - phase wrappers:
    `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolve.hlsl`
    `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolveBuildClearArgs.hlsl`
    `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolveBuildDrawArgs.hlsl`
  - shader catalog registration:
    `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
- fresh builds after the split:
  - `cmake --build out/build-ninja --config Release --target oxygen-examples-renderscene --parallel 8`
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-renderscene --parallel 8`
- fresh current-camera full-window `nircmdc.exe savescreenshotwin` captures:
  - virtual:
    `out/build-ninja/benchmarks/directional-vsm/task2-burgerpiz-current-virtual-only-20260318-010446.bmp`
  - conventional:
    `out/build-ninja/benchmarks/directional-vsm/task2-burgerpiz-current-conventional-20260318-010524.bmp`
  - matching runtime logs:
    `out/build-ninja/benchmarks/directional-vsm/task2-burgerpiz-current-*-20260318-01*.stderr.log`
- current-camera capture comparison against the task 1 baseline, sampled over
  the full window except the top-right FPS overlay:
  - task 1 virtual vs task 2 virtual:
    `sampled_mean_abs_rgb=0.3389`
  - task 1 conventional vs task 2 conventional:
    `sampled_mean_abs_rgb=0.0005`
  - task 2 virtual vs task 2 conventional:
    `sampled_mean_abs_rgb=0.9110`
  - task 1 virtual vs task 1 conventional:
    `sampled_mean_abs_rgb=0.9108`
- fresh heavy moving-camera benchmark rerun:
  - runner:
    `powershell -ExecutionPolicy Bypass -File Examples\RenderScene\benchmark_directional_vsm.ps1`
  - archived log:
    `out/build-vs/benchmarks/directional-vsm/directional-vsm-benchmark-20260318-010548.log`
  - latest metadata:
    `out/build-vs/directional-vsm-benchmark-latest.json`
  - result:
    `wall_ms=6174`, `approx_fps=19.44`
- the fresh task 2 BurgerPiz logs and the benchmark log do not contain:
  `COM Error`, `Failed to create compute pipeline state`, or
  `RenderGraph execution for view`

Task 2 validation closeout:

- task 2 has been validated and accepted as complete
- this document had temporarily drifted out of sync with the validated state
  before this update

Task 3 validation closeout, March 18, 2026:

- the first per-page draw-command build slice is still the active code path in:
  `src/Oxygen/Renderer/Passes/VirtualShadowResolvePass.h`
  `src/Oxygen/Renderer/Passes/VirtualShadowResolvePass.cpp`
  `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolveCommon.hlsli`
- the first task 3 runtime failure was caused by a constant-buffer packing
  mismatch after adding one more scalar field before the resolve matrices; that
  packing bug was corrected in both the C++ pass constants and the HLSL
  constants block
- task 3 has been validated and accepted as complete after the packing fix
- task 4 work proceeds from this validated task 3 baseline

## 2. Replacement Design Goals

The redesign must satisfy all of the following:

- stable motion-time correctness without stale whole-snapshot republish
- page-table encoded fallback, not publication-heuristic fallback
- clipmap continuity through page-space reuse and remap
- page-centric invalidation and allocation
- explicit coarse/detail content policy
- pass and shader boundaries that match domain ownership
- no monolithic backend function owning clipmap setup, request policy,
  invalidation, allocation, raster scheduling, and publication at once

## 3. Authoritative Contract Changes

### 3.1 Clipmaps become stable cache identities

Directional VSM clipmaps must be treated as stable panning cache identities.

The CPU side should only own:

- snapped clipmap setup
- per-level page-space offsets to the previous frame
- per-level reuse validity / guardband invalidation
- upload of stable clipmap constants and next-data mapping

The CPU side must stop deciding motion-time continuity by publishing or
withholding whole old snapshots.

### 3.2 The page table becomes the shading truth

The page table must stop being just:

- physical tile x
- physical tile y
- valid bit
- requested-this-frame bit

It needs a real sampling contract.

Minimum redesign:

- physical address
- current-LOD valid
- any-LOD-valid
- fallback LOD offset

Recommended supporting contract:

- keep page-content state in a separate page-flags buffer, not in the page
  table entry itself
- keep the page-table entry compact and shading-focused

### 3.3 Page flags become explicit

Introduce a page-flags buffer modeled after the Unreal pattern.

Minimum flags:

- allocated
- dynamic uncached
- static uncached
- detail geometry
- used this frame

Optional later additions:

- dirty
- invalidation cause bits
- hierarchical coverage bits

This replaces the current implicit multi-band behavior with a simple and
auditable coarse/detail contract.

## 4. Pass Redesign

The current backend/pass split is wrong because too much is buried inside
`BuildDirectionalVirtualViewState()` and `ResolvePendingPageResidency()`.

The replacement pass graph should be:

### Pass 0. Directional Clipmap Setup

CPU-owned, light/view setup only.

Responsibilities:

- snapped clipmap transforms
- stable clipmap IDs
- previous-to-current page-space offsets
- per-level reuse validity
- upload clipmap constants / next-data mapping

Must not do:

- page selection
- fallback publication policy
- allocation
- raster scheduling

### Pass 1. Visible Page Marking

GPU compute.

Inputs:

- main depth / visible receiver inputs
- stable clipmap projection constants

Responsibilities:

- mark detail page requests from visible samples
- use small local dilation only

Must not do:

- coarse safety budgeting
- CPU feedback reinterpretation
- whole-frame bootstrap heuristics

### Pass 2. Coarse Page Marking

GPU compute, explicit separate pass.

Responsibilities:

- mark coarse pages for directional systems that require guaranteed coverage
- keep the coarse path binary and bounded

This replaces today's blend of:

- coarse backbone
- coarse safety publish fallback
- receiver bootstrap as a pseudo-fallback

### Pass 3. Cache Invalidation

GPU compute.

Responsibilities:

- invalidate pages based on moved / revealed / WPO / dirty primitives
- optionally use page-space HZB tests later
- write dirty and invalidation flags against physical pages

This must be separate from allocation.

### Pass 4. Physical Page Update / Reuse

GPU compute.

Responsibilities:

- carry previous physical pages forward through page-space remap
- apply invalidation results
- preserve valid pages in place
- update page table for reused pages

This is the authoritative continuity stage.

### Pass 5. New Page Allocation

GPU compute.

Responsibilities:

- allocate physical pages only for requested unmapped pages
- consume available/LRU page lists
- clear old page-table ownership when a page is reallocated

This replaces CPU-side allocation embedded in backend resolve.

### Pass 6. Hierarchical Page-Flag / Mip Propagation

GPU compute.

Responsibilities:

- propagate mapped/coarse/detail state up the hierarchy
- generate the coarse-valid / fallback-visible information shading needs

This is where fallback becomes a page-table/page-flag property, not a
publication heuristic.

### Pass 7. Per-Page Draw-Command Build

GPU compute.

Responsibilities:

- intersect visible shadow casters against valid requested pages
- emit compact per-page draw instance lists
- mark dirty pages touched by material invalidation or moving content

This replaces CPU-authored `resolved_raster_pages` as the authoritative raster
schedule source.

### Pass 8. Virtual Page Raster

Raster pass(es).

Responsibilities:

- consume per-page draw lists only
- rasterize only pages marked uncached/dirty
- support multiple caster domains through the already prepared draw metadata

### Pass 9. Shadow Projection / Shading

Shader sampling pass.

Responsibilities:

- consume stable page table + page flags + clipmap constants
- perform per-entry fallback through page-table data
- never depend on stale whole-snapshot republish

### Pass 10. Optional HZB / Dirty-Page Maintenance

GPU compute, optional follow-up stage.

Responsibilities:

- update per-page HZB or equivalent page summaries
- clear dirty flags after the frame's update work

## 5. Shader Redesign

The shader split should follow the pass split.

Recommended Oxygen shader structure:

- `VirtualShadowPageAccess.hlsli`
  - page-table decode
  - any-LOD-valid / fallback decode
  - physical address translation
- `VirtualShadowClipmapCommon.hlsli`
  - stable directional clipmap transforms
  - page-space offset helpers
- `VirtualShadowRequestMarking.hlsl`
  - visible-sample detail marking
- `VirtualShadowCoarseMarking.hlsl`
  - explicit coarse page marking
- `VirtualShadowPageFlags.hlsl`
  - hierarchical flag generation and propagation
- `VirtualShadowInvalidation.hlsl`
  - page invalidation from moved/dirty primitives
- `VirtualShadowPageManagement.hlsl`
  - physical page reuse and allocation
- `VirtualShadowBuildPerPageDrawCommands.hlsl`
  - per-page draw-list construction
- `VirtualShadowProjectionDirectional.hlsli`
  - final directional sample logic using the page-access helpers

The current Oxygen design is too monolithic because sampling, fallback policy,
and backend publication assumptions are entangled.

## 6. Current Components To Remove or Retire

The redesign should remove these as authoritative mechanisms:

- `last_coherent_shadow_instances`
- `last_coherent_directional_virtual_metadata`
- `last_coherent_page_table_entries`
- `use_last_coherent_publish_fallback`
- `receiver_bootstrap` as a main page-source
- `current_frame_reinforcement` as a main page-source
- `feedback_refinement` as a main continuity mechanism
- CPU-authored coarse-safety publication fallback as the main shading safety net

Some temporary bridging may exist during migration, but none of these should
survive as the final directional VSM continuity contract.

## 7. Migration Order

If implementation resumes, the order should be:

1. Introduce the new stable page-table and page-flags contracts.
2. Split directional clipmap setup from page selection/publication logic.
3. Add explicit GPU coarse-page marking and visible-sample detail marking.
4. Add dedicated invalidation and physical-page update passes.
5. Move allocation to the dedicated GPU page-management stage.
6. Replace CPU-authored raster schedule with GPU per-page draw-command build.
7. Replace shader sampling with page-table fallback decoding.
8. Remove the last coherent publication fallback path and the old bootstrap /
   reinforcement continuity mechanisms.

## 8. Exit Criteria For The Redesign

The redesign is only acceptable if all of the following are true:

- motion-time correctness no longer depends on stale full-metadata republish
- ocean / large-flat-receiver boiling from multi-band quality transitions is
  gone or materially reduced
- page churn under camera motion is dominated by visible sample changes, not by
  bootstrap heuristics
- wrong-page flashing is gone under aggressive motion
- stable-state correctness remains intact
- performance optimization can proceed against this contract without reopening
  the architecture again

## 9. Validation

Validation for this redesign document:

- code-inspection based
- derived from the Unreal and Oxygen file references captured in
  `directional_vsm_architecture_review.md`
- no code implementation landed
- no builds or tests run

Remaining gap:

- implementation has not been started against this redesign
