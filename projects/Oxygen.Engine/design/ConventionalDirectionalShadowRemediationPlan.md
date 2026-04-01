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

## 0. Current State (2026-04-02)

Current task focus:

- Active phase is now `CSM-4`.
- Effective `2026-04-02`, the authoritative comparison baseline for later
  phases is the frame-`350` at `50 fps` `CSM-2` package, not the archived
  pre-`CSM-2` conventional package.
- The next acceptable outputs are:
  - `CSM-4` GPU caster culling and compaction implementation
  - fresh canonical capture plus sequential RenderDoc analysis for `CSM-4`
  - compacted draw-count evidence compared against the locked `CSM-2`
    baseline
- `CSM-1`, `CSM-2`, and `CSM-3` are implemented and validated.
- `CSM-2` is the locked baseline package for future phases. It does not
  compare against an older capture package because it is the new authoritative
  baseline.
- `CSM-3` has a fresh frame-`350` capture package, sequential RenderDoc
  analysis, and an explicit baseline comparison against the locked `CSM-2`
  package.
- `CSM-3` was then optimized and revalidated on the same canonical frame-`350`
  capture shape; receiver-mask occupancy stayed unchanged while receiver-mask
  GPU time dropped materially.
- No later phase may be treated as complete until its code, document updates,
  and validation evidence all satisfy this plan's gates.
- The rejected receiver-object-bounds culling path stays rejected and is not
  part of the active task.
- No production claim, timing claim, or phase-complete claim is valid if it
  depends on ignored script errors, missing validation, or parallel RenderDoc
  analysis runs.
- The archived pre-`CSM-2` conventional package is reference-only invalidation
  context and is no longer a timing or structural baseline for future phases.

## 1. Authoritative Baseline Package (`CSM-2`)

### 1.1 Scene Identity

Canonical scene for this plan:

- live settings file swapped by the harness: `Examples/RenderScene/demo_settings.json`
- saved benchmark settings asset: `Examples/RenderScene/demo_settings.benchmark.json`
- key: `e74ea312-0b6f-928f-8864-e3f44af2a8b7`
- scene: `/.cooked/Scenes/NewSponza_Main_glTF_003.oscene`

Benchmark settings swap protocol:

- before every capture, rename the current live file to
  `Examples/RenderScene/demo_settings.sav`
- copy `Examples/RenderScene/demo_settings.benchmark.json` to
  `Examples/RenderScene/demo_settings.json`
- run the benchmark capture against that copied file
- after the run, always restore `Examples/RenderScene/demo_settings.sav` back to
  `Examples/RenderScene/demo_settings.json`
- a capture is invalid if the harness does not restore the original live
  settings file

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

Canonical frame-`350` capture:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\Examples\RenderScene\Run-ConventionalShadowBaseline.ps1 `
  -Frame 350 `
  -RunFrames 471 `
  -Fps 50 `
  -Output `
  out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50
```

Canonical sequential analysis:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\Examples\RenderScene\Analyze-ConventionalShadowCsm2.ps1 `
  -CapturePath `
  out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50_frame350.rdc `
  -OutputStem `
  out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50
```

### 1.3 Canonical Baseline Artifacts

Authoritative baseline artifacts:

- capture:
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50_frame350.rdc`
- benchmark log:
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50.benchmark.log`
- RenderDoc timing reports:
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50.shadow_timing.txt`
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50.shader_timing.txt`
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50.screen_hzb_timing.txt`
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50.receiver_analysis_timing.txt`
- RenderDoc receiver-analysis report:
  `out/build-ninja/analysis/conventional_shadow_csm2_validation/release_frame350_csm2_fps50.receiver_analysis_report.txt`

### 1.4 Authoritative Structural Baseline

These values are the current authoritative `CSM-2` baseline because they are
consistent across the benchmark log and the hardened sequential RenderDoc
reports:

| Metric | Value | Evidence |
| --- | ---: | --- |
| Shadowed directional lights | `1` | benchmark log |
| Raster jobs / cascades | `4` | receiver-analysis report `job_count` |
| Shadow work events | `1608` | RenderDoc timing report |
| Shadow draws | `1604` | `1608 - 4` |
| Shadow clears | `4` | receiver-analysis jobs / one clear per job |
| Average draws per job | `401` | `1604 / 4` |
| Conventional shadow draw records | `401` | benchmark log |
| Retained shadow casters | `405` | benchmark log |
| Visible receiver bounds | `35` | benchmark log |
| Receiver-analysis valid jobs | `2` | receiver-analysis report |
| Receiver-analysis sampled jobs | `2` | receiver-analysis report |

Canonical stabilization facts:

- scene build frame: `181`
- last texture repoint frame: `255`
- capture frame: `350`
- post-scene-build stabilization: `169` frames / `6.057 s`
- post-last-repoint stabilization: `95` frames / `1.893 s`

### 1.5 Timing Baseline Status

The authoritative `CSM-2` baseline timing package produced by the hardened
sequential run on `2026-04-02` is:

- `ConventionalShadowRasterPass`: `21.657248 ms`
- `ScreenHzbBuildPass`: `0.419232 ms`
- `ConventionalShadowReceiverAnalysisPass`: `0.553856 ms`
- `ShaderPass`: `9.286432 ms`

Current receiver-analysis baseline status from the same package:

- `job_count`: `4`
- `valid_job_count`: `2`
- `sampled_job_count`: `2`
- `min_full_area_ratio`: `0.000321`
- `max_full_area_ratio`: `0.245325`
- `min_full_depth_ratio`: `0.026405`
- `max_full_depth_ratio`: `0.387870`

Conclusion:

- the `CSM-2` package above is now the authoritative baseline for later phases
- the old conventional package is archived only and is no longer a comparison
  target
- `CSM-2` timing is now recorded from a hardened sequential analysis package
- `CSM-2` receiver analysis now proves near-cascade tightening versus the full
  rect
- `CSM-2` is the authoritative baseline and therefore has no baseline-compare
  requirement against older stale captures
- active implementation work now moves to `CSM-4`

### 1.6 Per-Phase Evidence Rule

Every future phase must produce all of the following:

1. Release build evidence.
2. Fresh frame-`350` capture of the canonical benchmark.
3. Sequential RenderDoc Python-script analysis.
4. A comparison against the authoritative `CSM-2` baseline package in this
   document.
5. Updated design status in this document.

Evidence is invalid if any of the following occurred during capture or
analysis:

- PowerShell or Python script errors were ignored
- a script continued after a failed command without explicit handling
- multiple RenderDoc analyses were run in parallel
- the benchmark-settings swap protocol was not fully restored

Structural phases may close without a shadow-GPU-time reduction only if they
show:

- no unexplained structural regression versus baseline
- the expected new GPU-visible data product is present in the capture and is
  analyzed by a Python RenderDoc script

Performance phases must additionally show:

- normalized timing comparison versus the authoritative `CSM-2` baseline
  capture
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

## 4. Non-Negotiable Rules

### 4.1 Execution Guardrails

1. This document's scope, phase gates, and evidence requirements are binding.
   If implementation or validation work no longer fits this document, update
   the document first before making new completion claims.
2. No shortcuts.
   Do not bypass a gate, skip a required artifact, or substitute ad hoc
   evidence for the required capture-and-analysis package.
3. RenderDoc execution is strictly sequential.
   Do not run multiple RenderDoc captures, UI inspections, or Python-script
   analyses in parallel.
4. PowerShell scripts must be hardened.
   Capture and analysis scripts must fail fast, propagate non-zero exit codes,
   validate required paths and tools, and always restore the benchmark
   settings file on every exit path.
5. RenderDoc Python scripts are treated as fallible code, not trusted oracles.
   Every run must check for script errors, incomplete output, and analysis
   mismatches; phase progress is blocked until those errors are fixed.

### 4.2 Design Rules

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

- complete

Goals:

- harden the canonical capture and analysis toolchain so validation failures
  stop immediately and surface actionable errors
- normalize the canonical baseline timing analysis so later phases can claim
  real GPU-time deltas
- replace receiver-object-bounds thinking with actual visible receiver samples

Tasks:

1. Harden the canonical PowerShell and RenderDoc Python tooling used by this
   phase:
   - fail fast on command, tool, or file errors
   - reject parallel RenderDoc analysis execution
   - verify the benchmark-settings swap is restored on all exit paths
   - surface Python analysis errors as blocking failures
2. Repair the RenderDoc baseline timing analysis so it reports a single
   authoritative `ConventionalShadowRasterPass` and `ShaderPass` duration for
   the canonical frame-`350` capture.
3. Add a GPU receiver-analysis pass that consumes the main-view depth path,
   not `visible_receiver_bounding_spheres`.
4. Use actual visible depth samples to derive, per cascade:
   - sample count
   - tight light-space XY bounds
   - tight light-space depth min/max
   - required dilation margin for filter / bias safety
5. Publish `ConventionalShadowReceiverAnalysis` as a renderer-owned GPU buffer.
6. Add a RenderDoc Python analysis script that reads back the receiver-analysis
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

- capture and analysis scripts are hardened, error-checked, and executed
  sequentially
- canonical baseline timing reports are normalized and regenerated from the
  canonical capture
- receiver analysis is derived from actual visible samples, not object bounds
- frame-`350` capture and analysis show that at least one near cascade is
  materially tighter than the full cascade-fit rect
- no unexplained structural regression versus the baseline package

Required phase evidence:

- Release build
- fresh frame-`350` capture
- receiver-analysis RenderDoc report
- doc update in this file

### CSM-3. Light-Space Receiver Mask Construction

Status:

- complete

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

- canonical frame-`350` capture shows a sparse receiver mask for at least one
  near cascade
- aggregate occupied-tile ratio is materially below full-map coverage on the
  canonical benchmark
- no unexplained shadow-path timing regression versus normalized baseline

Required phase evidence:

- Release build
- fresh frame-`350` capture
- RenderDoc receiver-mask report
- explicit baseline comparison

Current validation evidence:

- implementation files:
  - `src/Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.h`
  - `src/Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.cpp`
  - `src/Oxygen/Renderer/Types/ConventionalShadowReceiverMask.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/ConventionalShadowReceiverMask.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/ConventionalShadowReceiverMask.hlsl`
  - `src/Oxygen/Renderer/Pipeline/ForwardPipeline.cpp`
  - `Examples/RenderScene/Analyze-ConventionalShadowCsm3.ps1`
  - `Examples/RenderScene/AnalyzeRenderDocConventionalReceiverMask.py`
  - `Examples/RenderScene/CompareConventionalShadowCsm3Baseline.py`
- synthetic validation:
  - `Oxygen.Renderer.ConventionalShadowReceiverAnalysisPass.Tests.exe`
    `ConventionalShadowReceiverAnalysisPassTest.ProducesSampleDrivenReceiverBoundsTighterThanFullCascadeFit`
    passed
  - `Oxygen.Renderer.ConventionalShadowReceiverMaskPass.Tests.exe`
    `ConventionalShadowReceiverMaskPassTest.ProducesSparseReceiverTileMaskForSampledCascade`
    passed
- Release validation:
  - `cmake --build out/build-ninja --config Release --target`
    `Oxygen.Examples.RenderScene.exe`
    `Oxygen.Graphics.Direct3D12.ShaderBake.exe`
    `oxygen-renderer`
    succeeded
- live validation artifacts:
  - capture:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50_frame350.rdc`
  - benchmark log:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.benchmark.log`
  - timing reports:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.shadow_timing.txt`
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.shader_timing.txt`
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.screen_hzb_timing.txt`
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.receiver_analysis_timing.txt`
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.receiver_mask_timing.txt`
  - receiver-analysis report:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.receiver_analysis_report.txt`
  - receiver-mask report:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.receiver_mask_report.txt`
  - baseline comparison:
    `out/build-ninja/analysis/conventional_shadow_csm3_validation/release_frame350_csm3_optimized_fps50.baseline_compare.txt`

Optimization follow-up validated on `2026-04-02`:

- `CSM-3` no longer performs per-pixel dilation in `CS_Analyze`.
- `CS_Analyze` now writes only undilated raw occupancy.
- `CS_DilateMasks` performs tile-space dilation and writes the base mask.
- hierarchy construction counts occupied hierarchy tiles while it builds them.
- `CS_Finalize` consumes counted occupancy instead of rescanning the full base
  and hierarchy masks.
- canonical frame-`350` live validation preserved receiver-mask semantics:
  job counts, sampled jobs, occupied tile counts, and per-job occupancy ratios
  match the pre-optimization validated `CSM-3` package.

Current live `CSM-3` facts from the fresh canonical package:

- scene build frame: `210`
- last texture repoint frame: `284`
- capture frame: `350`
- post-scene-build stabilization: `140` frames / `5.870 s`
- post-last-repoint stabilization: `66` frames / `1.337 s`
- receiver-mask timing:
  - `ConventionalShadowReceiverMaskPass`: `0.441344 ms`
  - `work_event_count`: `6`
  - `CS_Analyze`: `0.408736 ms`
  - `CS_DilateMasks`: `0.011328 ms`
  - `CS_BuildHierarchy`: `0.007936 ms`
  - `CS_Finalize`: `0.005760 ms`
- receiver-mask occupancy:
  - `job_count`: `4`
  - `valid_job_count`: `2`
  - `sampled_job_count`: `2`
  - `sparse_job_count`: `2`
  - `total_occupied_tile_count`: `2211 / 65536`
  - `aggregate_occupied_tile_ratio`: `0.033737`
  - `total_hierarchy_occupied_tile_count`: `168 / 4096`
  - `aggregate_hierarchy_occupied_tile_ratio`: `0.041016`
  - `sampled_aggregate_occupied_tile_ratio`: `0.067474`
  - `sampled_aggregate_hierarchy_occupied_tile_ratio`: `0.082031`
  - job `0` occupied-tile ratio: `0.133606`
  - job `1` occupied-tile ratio: `0.001343`
  - jobs `2` and `3` are empty
- optimization delta versus the previously validated `CSM-3` package:
  - receiver-mask pass GPU time: `5.258752 ms -> 0.441344 ms`
  - receiver-mask delta: `-4.817408 ms` (`-91.607438%`)
  - `CS_Analyze`: `4.260032 ms -> 0.408736 ms`
  - `CS_Finalize`: `2.275968 ms -> 0.005760 ms`
- baseline comparison against locked `CSM-2`:
  - shadow work events unchanged: `1608`
  - shader work events unchanged: `33`
  - `ConventionalShadowRasterPass` delta: `+0.272960 ms`
  - `ShaderPass` delta: `+0.156928 ms`
  - `ScreenHzbBuildPass` delta: `-0.001536 ms`
  - `ConventionalShadowReceiverAnalysisPass` delta: `+0.477376 ms`
  - `ConventionalShadowReceiverMaskPass`: `0.441344 ms`
  - auxiliary shadow-path delta versus locked `CSM-2`: `+0.917184 ms`

Conclusion:

- the canonical frame-`350` capture shows sparse receiver occupancy for both
  sampled cascades
- aggregate occupied-tile coverage is materially below full-map coverage on the
  canonical benchmark
- the optimized implementation preserves the same receiver-mask occupancy
  results as the previous validated `CSM-3` package
- the optimization removed the prior `CSM-3` bottleneck by moving dilation to
  tile space and by eliminating full-mask scans from finalize
- structural work counts match the locked `CSM-2` baseline
- the remaining `CSM-3` cost is explicit, bounded, and materially lower than
  the previous validated `CSM-3` package
- active implementation work now moves to `CSM-4`

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

- canonical frame-`350` capture proves non-trivial real-scene rejection
- no job on the canonical benchmark remains at full-input eligibility
- average eligible draws/job is materially below the baseline `401`
- no unexplained CPU-path regression versus baseline benchmark log

Required phase evidence:

- Release build
- fresh frame-`350` capture
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
- fresh frame-`350` capture
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
- Release frame-`350` capture proves improvement against the canonical
  baseline
- docs updated
- old invalidated approach remains documented as rejected

## 7. Immediate Next Work

The next meaningful execution order from the recovered state is:

1. Implement `CSM-4` GPU caster culling and compaction from the locked
   `CSM-2` receiver-analysis output plus the completed `CSM-3` receiver mask.
2. Capture and analyze the canonical benchmark at frame `350` and `50 fps`.
3. Prove non-trivial real-scene caster rejection and reduced eligible
   draws/job versus the baseline replay shape.
4. Only then proceed to `CSM-5` counted-indirect conventional raster.

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
