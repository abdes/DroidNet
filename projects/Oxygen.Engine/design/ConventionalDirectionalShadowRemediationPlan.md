# Conventional Directional Shadow Remediation Plan

Status: `in_progress`
Audience: renderer engineers remediating Oxygen's conventional directional
shadow path
Scope: turn Oxygen's current conventional directional shadow implementation into
a production-grade cascaded shadow-map path that is viable as a shipping
fallback, without leaving behind a second temporary architecture or pushing the
problem back into ScenePrep

This document is intentionally restartable. It records the baseline package,
the invalidated approach, the corrected architecture, the phase gates, and the
evidence standards required to continue this work later without redoing the
slow discovery.

## 0. Current State (2026-04-01)

Truthful status:

- The repository has been recovered to the state immediately after `CSM-1`.
- `CSM-1` is implemented and remains valid.
- The previously attempted `CSM-2` / `CSM-3` path was invalidated by
  RenderDoc evidence and rolled back.
- No shadow GPU-time reduction beyond the original conventional path currently
  exists in code.
- Every future phase must include:
  - Release build validation
  - a fresh frame-`320` RenderDoc capture of the canonical benchmark
  - sequential RenderDoc Python-script analysis
  - an explicit comparison against the canonical baseline package in this
    document

What survived rollback:

- the canonical benchmark harness and baseline artifacts
- the single-sun benchmark policy for `RenderScene`
- the `CSM-1` conventional shadow draw-record contract and tests

What did not survive rollback:

- receiver-sphere-based cull jobs
- sphere-vs-rect-slab `CSM-3` compute compaction
- any claim that the rolled-back `CSM-2` / `CSM-3` design was a viable
  production direction

## 1. Canonical Baseline Package

### 1.1 Scene Identity

Canonical scene for this plan:

- file: `Examples/RenderScene/demo_settings.json`
- key: `e74ea312-0b6f-928f-8864-e3f44af2a8b7`
- scene: `/.cooked/Scenes/NewSponza_Main_glTF_003.oscene`

Benchmark sun policy for this plan:

- if the active scene has a directional light tagged `is_sun_light`, use that
- else if the active scene has a directional light node named `SUN`, promote it
  to the scene sun and mark it shadow-casting plus environment-contributing
- else if the active scene has at least one directional light, promote the
  first directional light to the scene sun and mark it shadow-casting plus
  environment-contributing
- else use a synthetic sun, and when synthetic override is active it must be
  the only shadow-casting `is_sun_light` node

### 1.2 Canonical Build And Capture Commands

Release build:

```powershell
cmake --build out/build-ninja --parallel 8 --config Release --target `
  Oxygen.Examples.RenderScene.exe `
  Oxygen.Graphics.Direct3D12.ShaderBake.exe
```

Canonical frame-`320` capture:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\Examples\RenderScene\Run-ConventionalShadowBaseline.ps1 `
  -Frame 320 `
  -Output `
  out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional
```

Canonical sequential analysis:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\Examples\RenderScene\Analyze-ConventionalShadowBaseline.ps1 `
  -CapturePath `
  out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional_frame320.rdc
```

### 1.3 Canonical Baseline Artifacts

Authoritative baseline artifacts:

- capture:
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional_frame320.rdc`
- benchmark log:
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.benchmark.log`
- RenderDoc focus report:
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.shadow_parent_focus.txt`
- RenderDoc timing reports:
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.shadow_timing.txt`
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.shader_timing.txt`

### 1.4 Authoritative Structural Baseline

These values are the current authoritative baseline because they are consistent
across the benchmark log and the RenderDoc focus report:

| Metric | Value | Evidence |
| --- | ---: | --- |
| Shadowed directional lights | `1` | benchmark log |
| Raster jobs / cascades | `4` | benchmark log + RenderDoc event naming |
| Shadow work events | `1608` | RenderDoc timing report |
| Shadow draws | `1604` | RenderDoc timing report classification |
| Shadow clears | `4` | RenderDoc timing report classification |
| Average draws per job | `401` | `1604 / 4` |
| Retained shadow casters | `405` | benchmark log |
| Visible receiver items | `18` | benchmark log |
| Shadow viewport / scissor | `4096 x 4096` | RenderDoc focus report |

Canonical stabilization facts:

- scene build frame: `124`
- last texture repoint frame: `198`
- capture frame: `320`
- post-scene-build stabilization: `196` frames / `8.638 s`
- post-last-repoint stabilization: `122` frames / `4.263 s`

### 1.5 Timing Baseline Status

The currently stored pass-timing text files are not trustworthy closure
evidence for GPU timing deltas:

- `release_frame320_refresh_conventional.shadow_timing.txt` currently reports
  `507.133568 ms`
- `release_frame320_refresh_conventional.shader_timing.txt` currently reports
  `12.981472 ms`

Those numbers are inconsistent with the accepted baseline session and with the
later rolled-back `CSM-3` capture, which showed a structurally identical
workload but a `ConventionalShadowRasterPass` cost around `21.7 ms`.

Conclusion:

- structural baseline metrics above are authoritative today
- timing closure is blocked on normalization of the RenderDoc timing analysis
  so that a single canonical frame-`320` capture yields reproducible pass
  timings
- no future phase may claim a timing improvement until that normalization is
  done and the baseline timing report is regenerated from the canonical capture

This is now part of the new `CSM-2` scope.

### 1.6 Per-Phase Evidence Rule

Every future phase must produce all of the following:

1. Release build evidence.
2. Fresh frame-`320` capture of the canonical benchmark.
3. Sequential RenderDoc Python-script analysis.
4. A comparison against the canonical baseline package in this document.
5. Updated design status in this document.

Structural phases may close without a shadow-GPU-time reduction only if they
show:

- no unexplained structural regression versus baseline
- the expected new GPU-visible data product is present in the capture and is
  analyzed by a Python RenderDoc script

Performance phases must additionally show:

- normalized timing comparison versus the canonical baseline capture
- draw/job reduction or raster reduction consistent with the phase goal

## 2. Invalidated Approach And Why It Failed

The rolled-back approach must not be revived.

What it attempted:

- build per-cascade receiver volumes from `visible_receiver_bounding_spheres`
- use those volumes to publish `ConventionalShadowCullJob`
- test each `ConventionalShadowDrawRecord` bounding sphere against a light-space
  rect + depth slab
- compact surviving draw indices on the GPU

Why it was invalidated:

- RenderDoc analysis of the rolled-back `CSM-3` capture showed zero useful
  culling on the canonical benchmark:
  - artifact:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame320_csm3.shadow_culling_report.txt`
  - aggregate input draw records: `401`
  - aggregate compacted draws: `1604`
  - average eligible draws per job: `401`
  - aggregate compaction ratio: `1.000000`
- The same failure mode reproduced on a second large scene according to user
  validation.

Root bugs in the invalidated design:

1. Receiver truth was wrong.
   `visible_receiver_bounding_spheres` represented whole visible receiver
   objects, not the actual visible receiver footprint from the main view.
2. Receiver bounds were too coarse.
   Large world-space spheres on roofs, walls, and architectural pieces expanded
   the light-space receiver rects until they behaved like "almost the entire
   scene".
3. Full-split scale still leaked into the light setup.
   The published job geometry remained coupled to the full cascade slice size,
   so narrowing the receiver proxy did not actually produce a tight culling
   volume.
4. The culling predicate was too weak.
   Caster-sphere vs receiver-rect-slab tests are too conservative once both
   sides are whole-object spheres.
5. The test gate was insufficient.
   Synthetic GPU contract tests validated append/compaction mechanics, not that
   real-scene descriptors were selective enough.

The important conclusion is not "Sponza is special". The conclusion is that
receiver-object-sphere culling is the wrong contract for this problem.

## 3. Corrected Strategy

The new direction is to cull shadow casters against actual visible receiver
samples from the main view, not against visible receiver object bounds.

The strategy combines three proven ideas:

- Sample Distribution Shadow Maps:
  derive tight light-space bounds from the distribution of visible samples
  instead of from coarse frustum or object bounds.
  Reference:
  `Lauritzen, Sample Distribution Shadow Maps, Advances in Real-Time Rendering 2010`
  `https://advances.realtimerendering.com/s2010/Lauritzen-SDSM%28SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course%29.pdf`
- Receiver-mask-based caster culling:
  cull casters that cannot project onto visible receivers.
  Reference:
  `Bittner et al., Shadow Caster Culling for Efficient Shadow Mapping, I3D 2011`
  `https://www.cg.tuwien.ac.at/research/publications/2011/bittner-2011-scc/`
- GPU hierarchical visibility:
  use existing depth / HZB style visibility products and GPU-side hierarchical
  tests instead of pushing new per-cascade walks into CPU scene prep.
  Reference:
  `Hill and Collin, Practical, Dynamic Visibility for Games`
  `https://blog.selfshadow.com/publications/practical-visibility/`

What this means for Oxygen:

- Keep `CSM-1` as the authoritative draw-record stream.
- Use the main-view depth path as the receiver ground truth.
- Build per-cascade receiver analysis on GPU from visible samples.
- Build a conservative light-space receiver mask per cascade.
- Cull and compact casters against that mask on GPU.
- Feed conventional shadow raster through counted indirect draws only.

## 4. Non-Negotiable Design Rules

1. `CSM-1` stays.
   The conventional shadow draw-record contract is still the right foundation.
2. Do not put new `O(cascades x scene)` work into ScenePrep.
3. Do not reintroduce receiver-object-sphere cull jobs.
4. Do not depend on CPU readback for per-frame shadow culling.
5. Do not leave a CPU replay fallback on the hot raster path.
6. Reuse existing renderer systems first:
   - `DepthPrePass`
   - `ScreenHzbBuildPass`
   - `PreparedSceneFrame`
   - `ExecuteIndirectCounted`
   - existing VSM-style counted-indirect resource patterns
7. All new analysis must use RenderDoc Python scripts, not ad hoc runtime
   telemetry.

## 5. Target Architecture

### 5.1 CPU Responsibilities

- keep current ScenePrep extraction model
- keep `CSM-1` draw-record publication
- publish any static metadata the GPU needs to interpret draw records
- schedule shadow analysis / culling / raster passes
- never do per-cascade per-draw CPU filtering

### 5.2 GPU Responsibilities

- derive receiver-driven per-cascade bounds from actual visible depth samples
- build conservative receiver occupancy masks in light space
- cull shadow casters against those masks and compact surviving draw indices
- drive conventional shadow raster through counted indirect execution

### 5.3 Renderer Data Products

Existing valid product:

- `ConventionalShadowDrawRecord`
  - authoritative shadow-caster draw list
  - draw identity
  - partition identity
  - world bounding sphere
  - primitive flags

New products to add:

- `ConventionalShadowReceiverAnalysis`
  - one record per raster job
  - light-space XY min/max from actual visible samples
  - light-space depth min/max from actual visible samples
  - sample count
  - dilation margin expressed in texels or world units
  - flags describing fallback / empty / valid states
- `ConventionalShadowReceiverMask`
  - one sparse tile mask per raster job
  - base occupancy plus one or more OR-reduced hierarchy levels
- optional companion cull-bounds buffer if `CSM-1` spheres prove too coarse
  for mask overlap tests
  - this extends `CSM-1`; it does not invalidate it
- per-job / per-partition compacted draw-index buffers
- per-job / per-partition counted indirect argument buffers

### 5.4 Execution Flow

Target flow:

1. `DepthPrePass` produces main-view depth.
2. `ScreenHzbBuildPass` produces the main-view HZB.
3. `CSM-2` receiver analysis derives tight per-cascade bounds from visible
   samples.
4. `CSM-3` receiver mask construction marks occupied shadow tiles per cascade.
5. `CSM-4` caster culling tests draw records against receiver analysis and mask
   and compacts survivors.
6. `CSM-5` conventional raster executes only compacted work through counted
   indirect.

No CPU readback is allowed in this flow.

## 6. Phase Plan

### CSM-1. Shadow Draw Record Contract

Status:

- implemented
- retained after rollback

Purpose:

- publish one authoritative conventional shadow draw stream per prepared view
- avoid any new `ScenePrep x cascade` work
- provide stable draw identity for future GPU culling and indirect raster

Current validation evidence:

- implementation files:
  - `src/Oxygen/Renderer/Types/ConventionalShadowDrawRecord.h`
  - `src/Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h`
  - `src/Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.cpp`
  - `src/Oxygen/Renderer/PreparedSceneFrame.h`
  - `src/Oxygen/Renderer/Renderer.cpp`
- tests:
  - `src/Oxygen/Renderer/Test/ConventionalShadowDrawRecordBuilder_test.cpp`
- Release validation:
  - `out/build-ninja/bin/Release/Oxygen.Renderer.ConventionalShadowDrawRecords.Tests.exe`
  - result: `2` tests passed
- runtime validation:
  - `out/build-ninja/analysis/conventional_shadow_csm1_validation/release_frame256_csm1.benchmark.log`
  - steady-state record count: `401`

### CSM-2. Receiver Sample Foundation And Baseline Timing Normalization

Status:

- pending

Goals:

- normalize the canonical baseline timing analysis so later phases can claim
  real GPU-time deltas
- replace receiver-object-bounds thinking with actual visible receiver samples

Tasks:

1. Repair the RenderDoc baseline timing analysis so it reports a single
   authoritative `ConventionalShadowRasterPass` and `ShaderPass` duration for
   the canonical frame-`320` capture.
2. Add a GPU receiver-analysis pass that consumes the main-view depth path,
   not `visible_receiver_bounding_spheres`.
3. Use actual visible depth samples to derive, per cascade:
   - sample count
   - tight light-space XY bounds
   - tight light-space depth min/max
   - required dilation margin for filter / bias safety
4. Publish `ConventionalShadowReceiverAnalysis` as a renderer-owned GPU buffer.
5. Add a RenderDoc Python analysis script that reads back the receiver-analysis
   buffer from the capture and reports:
   - sample counts per job
   - projected area ratio relative to the full cascade-fit rect
   - depth-range ratio relative to the old full-slice range

Implementation guidance:

- use the existing main-view depth / HZB path, not new ScenePrep inputs
- start with a GPU reduction over visible samples; do not iterate CPU-side over
  render items
- prefer one thread per selected sample or per small screen tile
- use atomics or tiled reductions to produce one record per cascade

Exit gate:

- canonical baseline timing reports are normalized and regenerated from the
  canonical capture
- receiver analysis is derived from actual visible samples, not object bounds
- frame-`320` capture and analysis show that at least one near cascade is
  materially tighter than the full cascade-fit rect
- no unexplained structural regression versus the baseline package

Required phase evidence:

- Release build
- fresh frame-`320` capture
- refreshed baseline compare report
- receiver-analysis RenderDoc report
- doc update in this file

### CSM-3. Light-Space Receiver Mask Construction

Status:

- pending

Goals:

- stop treating the entire tight receiver rect as uniformly occupied
- capture the sparse distribution of visible receivers inside each shadow job

Tasks:

1. Build a light-space receiver tile mask per cascade from actual visible
   samples.
2. Start with a conservative base mask at `32 x 32` texel tiles for a
   `4096 x 4096` shadow map.
3. Dilate occupancy conservatively for filter footprint and bias margin.
4. Build at least one OR-reduced hierarchy level for fast overlap tests.
5. Add a RenderDoc Python analysis script that reports:
   - occupied tile count per job
   - occupied tile ratio versus full shadow-map coverage
   - hierarchy occupancy summaries

Implementation guidance:

- build from the `CSM-2` receiver-analysis transform and visible samples
- keep it GPU-only
- do not encode this as runtime debug telemetry

Exit gate:

- canonical frame-`320` capture shows a sparse receiver mask for at least one
  near cascade
- aggregate occupied-tile ratio is materially below full-map coverage on the
  canonical benchmark
- no unexplained shadow-path timing regression versus normalized baseline

Required phase evidence:

- Release build
- fresh frame-`320` capture
- RenderDoc receiver-mask report
- explicit baseline comparison

### CSM-4. GPU Caster Culling And Compaction

Status:

- pending

Goals:

- cull shadow casters against actual receiver occupancy, not receiver object
  bounds
- reduce the live shadow-caster population before raster

Tasks:

1. Add a GPU culling pass that tests each conventional shadow draw against:
   - the `CSM-2` receiver analysis bounds
   - the `CSM-3` receiver mask hierarchy
2. Use `CSM-1` draw records as the authoritative draw stream.
3. If sphere-only bounds are too coarse for mask overlap, add a companion
   cull-bounds buffer without invalidating `CSM-1`.
4. Compact surviving draws into per-job, per-partition index buffers.
5. Add a RenderDoc Python analysis script that reads those compacted counts and
   compares them to the canonical baseline `401` draws/job replay shape.

Implementation guidance:

- keep the broad phase cheap
- one thread per `(job, draw)` is acceptable for the first implementation
- use the mask hierarchy to avoid fine tests when no receiver tiles overlap

Exit gate:

- canonical frame-`320` capture proves non-trivial real-scene rejection
- no job on the canonical benchmark remains at full-input eligibility
- average eligible draws/job is materially below the baseline `401`
- no unexplained CPU-path regression versus baseline benchmark log

Required phase evidence:

- Release build
- fresh frame-`320` capture
- RenderDoc compacted-count report
- explicit baseline comparison

### CSM-5. Counted-Indirect Conventional Raster

Status:

- pending

Goals:

- delete CPU replay of full shadow partitions
- raster only compacted work

Tasks:

1. Convert `ConventionalShadowRasterPass` to consume compacted work through the
   existing public `ExecuteIndirectCounted` API.
2. Reuse the same public counted-indirect usage pattern already proven by the
   VSM raster path.
3. Keep PSO usage within public graphics APIs only.
4. Add RenderDoc analysis that proves:
   - counted indirect execution is used
   - raster draw count is reduced versus the canonical baseline

Exit gate:

- no full-partition direct replay remains on the hot path
- RenderDoc shows counted indirect execution in the conventional shadow pass
- normalized shadow-pass GPU time is materially lower than the normalized
  canonical baseline

Required phase evidence:

- Release build
- fresh frame-`320` capture
- RenderDoc indirect-raster report
- normalized timing compare versus baseline

### CSM-6. Static / Dynamic Update Budget

Status:

- pending

Goals:

- stop paying the same full price every frame if `CSM-5` is still over budget

Tasks:

1. Split static and dynamic shadow workloads explicitly.
2. Reuse the existing static-shadow-caster bit already flowing through the
   renderer.
3. Add deliberate, measurable update budgeting only if required by timing
   results.

Important rule:

- this phase is mandatory if `CSM-5` misses the final performance gate
- it may be skipped only if `CSM-5` already satisfies the final closure budget

### CSM-7. Shadow Material / LOD Specialization

Status:

- pending

Goals:

- remove remaining expensive low-value shadow work after structural waste is
  gone

Tasks:

1. Add shadow-only LOD policy where supported.
2. Add explicit masked-shadow budget controls.
3. Validate alpha-tested correctness against the canonical capture scene.

### CSM-8. Closure

Status:

- pending

Closure gate:

- normalized timing baseline repaired
- final conventional path uses receiver-sample analysis, receiver mask culling,
  and counted indirect raster
- Release frame-`320` capture proves improvement against the canonical
  baseline
- docs updated
- old invalidated approach remains documented as rejected

## 7. Immediate Next Work

The next meaningful execution order from the recovered state is:

1. Implement `CSM-2` baseline timing normalization.
2. Implement `CSM-2` receiver analysis from actual visible samples.
3. Capture and analyze the canonical benchmark at frame `320`.
4. Only then proceed to `CSM-3` receiver mask construction.

Do not restart with receiver object bounds. Do not attempt to tune the old
rect/slab method. That path has already failed and been rolled back.

## 8. References

Primary references for the corrected direction:

- Microsoft Learn:
  `Cascaded Shadow Maps`
  `https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps`
- Lauritzen:
  `Sample Distribution Shadow Maps`
  `https://advances.realtimerendering.com/s2010/Lauritzen-SDSM%28SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course%29.pdf`
- Bittner et al.:
  `Shadow Caster Culling for Efficient Shadow Mapping`
  `https://www.cg.tuwien.ac.at/research/publications/2011/bittner-2011-scc/`
- Hill and Collin:
  `Practical, Dynamic Visibility for Games`
  `https://blog.selfshadow.com/publications/practical-visibility/`
- GPU Gems 3:
  `Parallel-Split Shadow Maps on Programmable GPUs`
  `https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus`

## 9. Archived Invalidation Evidence

These artifacts are preserved to explain the rollback, not as closure evidence:

- rolled-back culling capture:
  `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame320_csm3_frame320.rdc`
- rolled-back culling report:
  `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame320_csm3.shadow_culling_report.txt`

They show that the rejected receiver-object-sphere approach compacted nothing on
the canonical benchmark and therefore is not an acceptable foundation for the
next phase.
