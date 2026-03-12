# Directional VSM Performance Recovery Plan

Date: March 12, 2026
Status: `in_progress`
Scope: directional virtual shadow maps only

## 1. Purpose

Directional VSM is now functionally stable, but it is still too expensive in
medium and large scenes. User validation reports that the current path can
drive the engine below 10 FPS. This document is the authoritative plan for
recovering directional VSM performance without reintroducing parallel legacy
paths.

This plan replaces ad-hoc performance notes spread across the milestone docs.
Those docs should summarize status and link here; detailed performance work
should be tracked here.

## 2. Review Summary

The current performance problem is not one bug. It is a stack of cost centers
that reinforce each other.

### 2.1 Dominant Cost: Brute-Force Page Raster Replay

`VirtualShadowPageRasterPass.cpp` currently:

- loops over every resolved page
- clears that page
- replays every shadow-caster partition for that page

That means raster cost scales roughly with:

- `resolved_page_count * shadow_caster_partition_count`

This is the single most important cost to remove first.

### 2.2 Missing Data for Page-Local Culling

`DrawMetadata` does not carry shadow-caster bounds, so the raster pass has no
cheap way to reject draws that cannot overlap the current virtual page.

The scene-prep layer already has world-space caster bounds
(`RenderItemData.world_bounding_sphere`), but that information is not carried
through the prepared draw metadata path that virtual page raster consumes.

### 2.3 Page Overproduction Before Raster

The request shader is already sparse: it requests the footprint-selected clip
plus optional finer prefetch only.

The larger inflation happens after request generation in the backend:

- coarse backbone coverage
- feedback dilation / guard bands
- receiver bootstrap
- motion-time reinforcement

That union still produces far more touched pages than the sparse request set
actually demands in medium and large scenes.

### 2.4 Fixed-Size Request/Resolve Readback Overhead

`VirtualShadowRequestPass` and `VirtualShadowResolvePass` still clear, copy,
and read back fixed-capacity buffers every frame. That is not the dominant
scene-scale cost once raster explodes, but it is still an unnecessary steady-
state tax on the current critical path.

### 2.5 Runtime Evidence Already Matches the Diagnosis

Existing `out/build-vs` runtime logs already show the mismatch:

- request feedback stays comparatively modest
- resolved schedule counts stay lower than final raster work
- raster still executes hundreds or thousands of virtual pages

Example patterns already captured in historical logs:

- requested pages around `130`
- scheduled pages around `91`
- raster still executing `637`, `1531`, or `1536` pages

That is enough evidence to justify a performance-first plan even before adding
new instrumentation.

## 3. Constraints

- Use `out/build-vs` only.
- Build focused targets only.
- Do not reintroduce a second directional shadow architecture.
- Reuse existing prepared-scene and pass infrastructure where possible.
- Do not claim performance completion without before/after evidence.

## 4. Performance Exit Gate

Directional VSM performance is not considered recovered until all of the
following are true:

- directional VSM no longer collapses the renderer into single-digit FPS in
  the agreed medium and large validation scenes
- requested, scheduled, resolved, and rastered page counts stay tightly
  coupled in steady-state and under camera motion
- raster submissions scale with page-local caster overlap, not total scene
  shadow-caster count
- request/resolve no longer depend on full steady-state fixed-buffer readback
  for the live path
- the docs record concrete before/after captures from `build-vs`

## 5. Frozen Execution Order

### Step 1. Establish the Baseline

Goal:

- capture authoritative per-scene cost before optimization work begins

Required outputs:

- requested page count
- scheduled page count
- resolved page count
- rastered page count
- page clear count
- shadow-caster draw submission count
- CPU time for request, resolve, and raster preparation
- scene-level frame-time evidence from the `build-vs` runtime capture

Current measurement boundary:

- per-pass GPU time for request, resolve, and page raster is not part of Step 1
  yet because the current renderer does not expose timestamp-query
  infrastructure through the cross-backend graphics layer
- Step 1 therefore establishes the baseline with authoritative page/work counts,
  pass CPU preparation timings, and scene-level runtime timings from
  `RenderScene`
- if later optimization work needs per-pass GPU timings, that becomes a
  separate graphics-infrastructure prerequisite rather than an implicit part of
  this baseline step

Validation:

- capture a reproducible baseline run for the current staged `RenderScene`
  scene in `build-vs`
- record the data in this document or in a linked evidence section
- expand the same capture method to additional validation scenes during the
  final before/after validation gate; Sponza is intentionally not part of this
  first automated baseline

Why this comes first:

- without baselines, later “improvements” cannot be trusted

Step 1 status, March 12, 2026: `completed`

Evidence for the current staged `RenderScene` scene:

- scene: `/.cooked/Scenes/physics_domains.oscene` from
  `Examples/RenderScene/demo_settings.json`
- command:
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 120 --fps 100 --vsync false --directional-shadows virtual-only`
- log:
  `out/build-vs/directional-vsm-step1-baseline-redo-20260312.log`
- capture conditions:
  - current staged `RenderScene` scene only
  - `virtual-only`
  - `2560x1400` depth footprint
  - no Sponza capture in this step
- settled window:
  - frames `101-120`
  - one stable directional address-space hash:
    `0x4aa2f0adc785a7d0`
  - requested pages: `436-441`, average `438.30`
  - scheduled pages: `436-441`, average `438.30`
  - resolved/rastered pages: `740-743`, average `740.95`
  - page clears: `740-743`, average `740.95`
  - shadow-caster draw submissions: `6660-6687`, average `6668.55`
  - request prepare CPU time: `124-152 us`, average `132.05 us`
  - resolve prepare CPU time: `2361-3277 us`, average `2595.20 us`
  - raster prepare CPU time: `41-162 us`, average `51.35 us`
- worst spikes in the same run:
  - frame `6`: `1536` rastered pages, `1536` page clears, `13824` shadow
    draws
  - frame `72`: `1467` rastered pages, `1467` page clears, `13203` shadow
    draws
  - resolve prepare spikes reached `1,234,936-1,258,161 us` on frames `6-9`
- scene-level renderer timings from the same run:
  - average `view_render_ms=117.727`
  - average `render_graph_ms=104.639`
  - last-frame `view_render_ms=44.017`
  - last-frame `render_graph_ms=37.757`
  - inference: the run-average `view_render_ms` is roughly `8.5 FPS`

What the baseline proves:

- the expensive path is not request generation; request and schedule counts
  stay in the mid-`400`s once the scene settles
- resolved/rastered pages remain much higher than requested/scheduled pages in
  steady-state
- the raster pass is still replaying far too many shadow-caster draws per
  settled page set
- large transient spikes still exist before the scene settles, so both the
  raster replay cost and the page-overproduction path remain active problems

### Step 2. Remove the Dominant Raster Replay Cost

Goal:

- make virtual page raster submit only draws that can overlap the target page

Required implementation direction:

- extend prepared-scene / draw-metadata data with shadow-caster bounds
- derive page-local caster or draw ranges from that data
- make `VirtualShadowPageRasterPass` consume those compact page-local ranges
  instead of replaying every shadow-caster partition for every page

Explicit non-goal:

- do not create a second legacy raster path; the resolved-page contract remains
  the only raster contract

Exit evidence:

- raster draw submission count must drop with page-local overlap
- focused regressions must prove pages do not lose valid casters after culling

Step 2 status, March 12, 2026: `completed`

Implementation summary:

- per-draw world bounding spheres now propagate through the prepared-scene
  path and remain aligned with draw ordering / instancing
- `VirtualShadowPageRasterPass` now performs conservative page-local culling
  against those prepared per-draw bounds instead of replaying every
  shadow-caster partition for every resolved page
- the resolved-page contract remains the only raster contract; this step did
  not introduce a second legacy raster path

Evidence for the current staged `RenderScene` scene:

- scene: `/.cooked/Scenes/physics_domains.oscene` from
  `Examples/RenderScene/demo_settings.json`
- focused test build:
  `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.DrawMetadataEmitter.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- focused tests:
  `out/build-vs/bin/Debug/Oxygen.Renderer.DrawMetadataEmitter.Tests.exe --gtest_filter=*BoundingSphere*:*VirtualShadowRasterCulling*`
- runtime build:
  `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
- runtime command:
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe --frames 120 --fps 100 --vsync false --directional-shadows virtual-only`
- log:
  `out/build-vs/directional-vsm-step2-baseline-20260312.log`
- capture conditions:
  - current staged `RenderScene` scene only
  - `virtual-only`
  - `2560x1400` depth footprint
  - no Sponza capture in this step
- settled window:
  - frames `101-120`
  - one stable directional address-space hash:
    `0x877a39bb54734da5`
  - requested pages: `439-444`, average `441.60`
  - scheduled pages: `439-444`, average `441.60`
  - resolved/rastered pages: `413-424`, average `420.75`
  - shadow-caster draw submissions: `1448-1480`, average `1465.80`
  - page-local culls: `2269-2336`, average `2320.95`
  - request prepare CPU time: `120-156 us`, average `131.45 us`
  - resolve prepare CPU time: `2495-3590 us`, average `2816.70 us`
  - raster prepare CPU time: `29-38 us`, average `32.20 us`
- compared to Step 1 over the same settled window:
  - rastered pages dropped from `740.95` to `420.75` (`-43.2%`)
  - shadow draw submissions dropped from `6668.55` to `1465.80` (`-78.0%`)
  - raster prepare CPU time dropped from `51.35 us` to `32.20 us`
    (`-37.3%`)
  - end-to-end 120-frame run duration dropped from `20,731 ms` to
    `19,123 ms` (`-7.8%`) when measured from `Starting frame loop` to
    `exit code: 0`

What Step 2 proves:

- the dominant brute-force raster replay cost was real and this step removed a
  large part of it
- the steady-state page counts now track much more closely to requested /
  scheduled demand, so the raster pass is no longer the main amplifier of page
  cost in the current staged scene
- performance is still not production-ready because large transient page
  spikes remain and the backend still overproduces candidate pages before the
  raster stage; Step 3 is still required

### Step 3. Reduce Page Overproduction Before Raster

Goal:

- stop feeding the raster pass a page set that is much larger than the real
  sparse demand

Required implementation direction:

- replace the unconditional coarse-backbone and motion-reinforcement policy
  with a budgeted policy
- invert the current page-vs-caster overlap test: project each caster
  footprint onto the clip-page lattice, flag the covered page spans once per
  caster, and intersect that mask with receiver/request-driven demand instead
  of scanning every caster for every candidate page
- tighten feedback dilation / guard bands
- cap current-frame reinforcement against measured demand
- add a hard per-frame page-update budget with explicit prioritization

Exit evidence:

- requested, scheduled, and rastered page counts must converge substantially
  compared to the baseline

### Step 4. Remove Full-Buffer Readback from the Steady-State Path

Goal:

- stop paying fixed-capacity request/resolve clear-copy-readback cost every
  frame in the live directional VSM path

Required implementation direction:

- compact sparse requested pages on GPU
- keep residency/update scheduling on the resolve path
- reduce CPU readback to the minimum required for debug or explicit validation

Exit evidence:

- live path no longer requires whole-buffer readback to drive the next frame's
  steady-state behavior

### Step 5. Specialize Caching for Dynamic Pressure

Goal:

- keep moving casters from forcing broad reraster of stable pages

Required implementation direction:

- static-vs-dynamic cache/update policy split
- cheaper update path for many small page writes where the full default raster
  path is overkill

Exit evidence:

- dynamic-heavy scenes no longer exhibit the current large update spikes under
  modest motion

## 6. Secondary CPU Micro-Optimizations

These are legitimate follow-up optimizations, but they are not the first
priority because they do not change the current dominant cost structure by
themselves.

- consider replacing `std::unordered_map` / `std::unordered_set` in the hot
  residency path with a flatter open-addressing container if profiling proves
  those lookups are a measurable CPU bottleneck after the structural fixes
- persist and reuse per-view scratch vectors such as selected-page masks and
  feedback-seed masks instead of allocating them anew inside the frame loop
- if mapped-page tallying is still part of the live cost after structural
  fixes, keep that count incrementally during page-table assignment or restrict
  the full-array tally to debug/introspection builds

One caution:

- do not replace canonical shadow-caster sorting with a commutative hash alone
  unless the invalidation design changes at the same time; the current
  canonical ordering is also used to pair previous/current caster bounds for
  spatial-delta invalidation, so a pure order-independent hash is not a safe
  drop-in replacement

### Step 6. Close with Before/After Validation

Goal:

- close the performance plan with evidence, not anecdotes

Required validation:

- rerun the agreed validation scenes in `build-vs`
- record before/after counters
- record FPS or frame-time improvement
- confirm no correctness regressions were introduced

This step is not complete until the evidence is recorded in docs.

## 7. Recommended Immediate Next Slice

The first useful implementation slice is:

1. baseline capture
2. prepared-scene / draw-metadata extension for caster bounds
3. page-local raster culling in `VirtualShadowPageRasterPass` (`completed`)

That is the shortest path to a visible performance win because it attacks the
largest structural cost first.

## 8. Status

Current status:

- functional directional VSM validation is reported complete
- Step 1 baseline capture is complete for the current staged `RenderScene`
  scene
- Step 2 page-local raster culling is complete with measured reductions in
  rastered pages and shadow draw submissions
- Step 3 page-production tightening / budgeting is the next implementation
  task
- this plan is the active authoritative performance plan for the next
  directional VSM work
