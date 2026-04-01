# Conventional Directional Shadow Remediation Plan

Status: `in_progress`
Audience: renderer engineers remediating Oxygen's conventional directional
shadow path
Scope: turn Oxygen's current conventional directional shadow implementation into
a production-grade cascaded shadow-map path that is viable as a shipping
fallback, without leaving behind a second temporary architecture or pushing the
problem back into ScenePrep

This document is intentionally discovery-heavy. It records the baseline
artifacts, dead ends, exact commands, and design constraints needed to restart
this work later without redoing the slow investigation.

## 0. Current Snapshot (2026-04-01)

Truthful status:

- `CSM-1` is implemented and validated.
- No shadow GPU-time reduction from later phases is implemented yet; the
  current work establishes the shadow-draw contract and telemetry without
  changing raster behavior.
- The benchmark hydration and analysis scaffolding needed for this plan is now
  implemented and validated.
- The canonical baseline is a fresh Release `NewSponza` capture at frame `320`
  with exactly one authoritative scene sun and sequential RenderDoc analysis
  using an isolated automation config root.
- Canonical baseline metrics:
  - `ConventionalShadowRasterPass`: `21.747520 ms`
  - `ShaderPass`: `4.440512 ms`
  - ratio: `4.90x`
  - shadow work events: `1608`
  - shadow draws: `1604`
  - shadow clears: `4`
  - unique shadow jobs/slices: `4`
  - shadow viewport/scissor: `4096 x 4096`
- Runtime state at the canonical capture:
  - `405` retained shadow casters
  - `18` visible receivers
  - `1` shadowed directional light
  - the authoritative directional is the scene node `SUN`
- Capture stabilization window for the canonical baseline:
  - scene build frame: `124`
  - last texture repoint frame: `198`
  - capture request frame: `320`
  - post-scene-build stabilization: `196` frames / `8.638 s`
  - post-last-repoint stabilization: `122` frames / `4.263 s`
- Inference from the baseline capture plus runtime log:
  - `1604 / 4 = 401` draws per shadow job on average
  - this is still effectively a replay of almost the entire `405`-caster set
    for every shadow job

Current validation evidence:

- Release build command succeeded:
  - `cmake --build out/build-ninja --parallel 8 --config Release --target Oxygen.Examples.RenderScene.exe Oxygen.Graphics.Direct3D12.ShaderBake.exe`
- Fresh Release smoke run succeeded without the previously observed
  `LightCulling.hlsl:CS [CLUSTERED=1]` failure:
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_smoke_post_rebuild.stderr.log`
- Canonical Release capture:
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional_frame320.rdc`
- Canonical benchmark log:
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.benchmark.log`
- Canonical sequential RenderDoc reports:
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.shadow_timing.txt`
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.shader_timing.txt`
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.shadow_parent_focus.txt`
- RenderDoc automation scripts:
  - `Examples/RenderScene/Invoke-RenderDocUiAnalysis.ps1`
  - `Examples/RenderScene/Analyze-ConventionalShadowBaseline.ps1`
- Validated sequential analysis command:
  - `powershell -ExecutionPolicy Bypass -File .\Examples\RenderScene\Analyze-ConventionalShadowBaseline.ps1 -CapturePath out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional_frame320.rdc -OutputStem out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional`
- DemoShell sun-policy validation:
  - `cmake --build out/build-ninja --parallel 8 --config Release --target Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests`
  - `out/build-ninja/bin/Release/Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests.exe`
  - result: `9` tests passed
- `CSM-1` validation:
  - `cmake --build out/build-ninja --parallel 8 --config Release --target Oxygen.Renderer.ConventionalShadowDrawRecords.Tests Oxygen.Renderer.ConventionalShadowRasterPassConfig.Tests Oxygen.Examples.RenderScene.exe`
  - `out/build-ninja/bin/Release/Oxygen.Renderer.ConventionalShadowDrawRecords.Tests.exe`
    - result: `2` tests passed
  - `out/build-ninja/bin/Release/Oxygen.Renderer.ConventionalShadowRasterPassConfig.Tests.exe`
    - result: `3` tests passed
  - fast `Track A` runtime validation:
    - `powershell -ExecutionPolicy Bypass -File .\Examples\RenderScene\Run-ConventionalShadowBaseline.ps1 -Frame 256 -Output out/build-ninja/analysis/conventional_shadow_csm1_validation/release_frame256_csm1`
    - artifact:
      `out/build-ninja/analysis/conventional_shadow_csm1_validation/release_frame256_csm1.benchmark.log`
    - validated production telemetry at steady state:
      `conventional shadow draw records=401`
      with partition distribution
      `#0:Opaque|ShadowCaster@[0,331)=331, #1:DoubleSided|Opaque|ShadowCaster@[331,384)=53, #2:Opaque|ShadowCaster|MainViewVisible@[384,401)=17`
    - this matches the previously captured frame-`256` shadow workload:
      `1604` shadow draws / `4` jobs = `401` draws per job

Artifacts that must not be reused as closure evidence:

- `out/build-ninja/analysis/conventional_shadow_regression/*`
  - old debug capture
  - contains `invalid camera depth span ...; skipping directional shadows`
  - not representative of a valid shadow frame
- `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame180_conventional_frame180.rdc`
  - captured during the earlier Release shader mismatch failure
- `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame220_conventional_frame220.rdc`
  - user-reviewed as still inside scene-streaming churn
  - keep for history only, not as baseline evidence
- `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame500_conventional_frame500.rdc`
  - captured after streaming settled, but invalid for closure because
    RenderScene persisted a synthetic-sun override and benchmarked two
    shadowed directional lights
- `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_conventional*`
  - earlier ad hoc capture/analyze path
  - keep for history only
  - superseded by the fresh frame-`320` baseline captured with the stabilized
    benchmark and sequential analysis scripts
- `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame256_conventional*`
  - useful fast-iteration capture path only
  - not the canonical baseline for this plan
  - timing is materially different despite matching structural workload, so it
    must not replace the explicit frame-`320` baseline without fresh review

## 1. Canonical Baseline Package

### 1.1 Scene Identity

Canonical scene for this plan:

- file: `Examples/RenderScene/demo_settings.json`
- key: `e74ea312-0b6f-928f-8864-e3f44af2a8b7`
- scene: `/.cooked/Scenes/NewSponza_Main_glTF_003.oscene`
- source index:
  `H:\projects\DroidNet\projects\Oxygen.Engine\Examples\RenderScene\.cooked\container.index.bin`

The benchmark scene is already configured in `RenderScene`, but benchmark runs
must not trust the persisted environment state blindly. Before every capture:

- save the current `Examples/RenderScene/demo_settings.json`
- apply the benchmark sun policy in `demo_settings.json`
- run the benchmark
- restore the original `demo_settings.json`

Benchmark sun policy for this plan:

- if the active scene has a directional light tagged `is_sun_light`, use that
- else if the active scene has a directional light node named `SUN`, promote it
  to the scene sun and mark it shadow-casting plus environment-contributing
- else if the active scene has at least one directional light, promote the
  first directional light to the scene sun and mark it shadow-casting plus
  environment-contributing
- else use a synthetic sun, and when synthetic override is active it must be
  the only shadow-casting `is_sun_light` node

### 1.2 Build And Smoke Commands

Release build:

```powershell
cmake --build out/build-ninja --parallel 8 --config Release --target `
  Oxygen.Examples.RenderScene.exe `
  Oxygen.Graphics.Direct3D12.ShaderBake.exe
```

Release smoke:

```powershell
out/build-ninja/bin/Release/Oxygen.Examples.RenderScene.exe `
  -v=0 `
  --frames 120 `
  --fps 30 `
  --vsync false `
  --directional-shadows conventional
```

Validated smoke log:

- `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_smoke_post_rebuild.stderr.log`

### 1.3 Canonical Baseline Capture Command

Use the benchmark runner with an explicit frame `320` output stem. The runner
already backs up and restores `demo_settings.json`, forces the authoritative
scene-sun policy, validates the positive and negative sun-selection logs, and
records the stabilization window in the benchmark log.

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\Examples\RenderScene\Run-ConventionalShadowBaseline.ps1 `
  -Frame 320 `
  -Output `
  out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional
```

Canonical capture artifacts:

- capture:
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional_frame320.rdc`
- benchmark log:
  `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional.benchmark.log`

Canonical stabilization facts from the benchmark log:

- scene build:
  `21:24:57.860`, frame `124`
- last texture repoint:
  `21:25:02.235`, frame `198`
- capture request:
  `21:25:06.498`, frame `320`
- post-last-repoint slack:
  `122` frames / `4.263 s`

Fast-iteration capture path:

- the benchmark runner defaults to frame `256`
- that path is intentionally kept for quicker profiling iteration
- it is not the canonical baseline for this plan
- when using the fast path, keep its artifacts separate from baseline artifacts

### 1.4 RenderDoc Analysis Commands

Run analysis sequentially through the automation wrapper so RenderDoc uses an
isolated config root instead of the interactive user profile.

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\Examples\RenderScene\Analyze-ConventionalShadowBaseline.ps1 `
  -CapturePath `
  out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame320_refresh_conventional_frame320.rdc
```

The wrapper emits:

- `*.shadow_timing.txt`
- `*.shader_timing.txt`
- `*.shadow_parent_focus.txt`

### 1.5 Baseline Snapshot

Canonical frame-`320` baseline:

| Metric | Value | Evidence |
| --- | ---: | --- |
| `ConventionalShadowRasterPass` GPU time | `21.747520 ms` | `release_frame320_refresh_conventional.shadow_timing.txt` |
| `ShaderPass` GPU time | `4.440512 ms` | `release_frame320_refresh_conventional.shader_timing.txt` |
| Shadow/Shading ratio | `4.90x` | derived from the two timing reports |
| Shadow work events | `1608` | `release_frame320_refresh_conventional.shadow_timing.txt` |
| Shadow draws | `1604` | derived from report line classification |
| Shadow clears | `4` | derived from report line classification |
| Unique shadow jobs | `4` | extracted from `Job[..].Slice[..]` scopes |
| Shader draws | `17` | `release_frame320_refresh_conventional.shader_timing.txt` |
| Retained shadow casters | `405` | `release_frame320_refresh_conventional.benchmark.log` |
| Visible receivers | `18` | `release_frame320_refresh_conventional.benchmark.log` |
| Shadowed directional lights | `1` | `release_frame320_refresh_conventional.benchmark.log` |
| Shadow viewport/scissor | `4096 x 4096` | `release_frame320_refresh_conventional.shadow_parent_focus.txt` |

Baseline interpretation:

- The authoritative single-sun benchmark is now correct and reproducible.
- The pass is still the dominant GPU cost relative to the main shading pass.
- The workload shape is the real problem:
  - `405` retained casters
  - `4` raster jobs
  - `401` draws per job on average
- The focus report confirms that each inspected job slice still rasterizes a
  full `4096 x 4096` viewport into one layer of `DirectionalShadowDepthArray`.
- The root-cause target for remediation remains unchanged:
  the pass is replaying almost the entire caster population for every cascade.

Non-baseline but preserved for context:

- `release_frame256_conventional_*`
  - validated fast-iteration capture
  - same structural workload (`1604` draws, `4` clears, `4` jobs,
    `405` retained casters, `18` visible receivers)
  - materially different timing, so preserved for investigation only
- `release_frame500_conventional_*`
  - invalidated by the pre-fix two-directional-light benchmark state

## 2. Root-Cause Summary

The current failure mode is architectural, not cosmetic.

What the current code does:

- `ConventionalShadowBackend` uses receiver bounds and caster bounds to size
  the orthographic cascade and tighten depth range.
- `ConventionalShadowRasterPass` then loops over every raster job and replays
  every `kShadowCaster` partition range.
- ScenePrep keeps off-frustum shadow casters alive as `shadow_only_submeshes`,
  which is correct in principle but currently too broad.
- The result is `shadow jobs x almost all shadow casters`.

Concrete evidence in the current codebase:

- `src/Oxygen/Renderer/Passes/ConventionalShadowRasterPass.cpp`
  - loops jobs first, then loops every shadow-caster partition
  - emits the entire partition range for each job
- `src/Oxygen/Renderer/ScenePrep/Extractors.h`
  - keeps non-main-view shadow casters as `shadow_only_submeshes`
  - explicitly notes GPU Hi-Z as future work, not current reality
- `src/Oxygen/Renderer/Internal/ConventionalShadowBackend.cpp`
  - sizes cascade extents and depth range from receiver/caster bounds
  - does not publish a per-job compacted draw list

In other words:

- the backend already computes enough information to size a better shadow
  volume,
- but the raster path never converts that information into per-job draw
  eligibility,
- so the GPU still rasterizes almost the same set of casters into every slice.

## 3. Non-Negotiable Decisions

1. This remediation ends with one final conventional directional-shadow path.
   No long-lived "temporary CPU culling path" and no second shadow backend.
2. ScenePrep must not gain an `O(cascades x full-scene)` per-submesh shadow
   filter. That would move the bottleneck instead of fixing it.
3. Reuse existing renderer data and infrastructure before adding new systems:
   - `PreparedSceneFrame.render_items`
   - `PreparedSceneFrame.shadow_caster_bounding_spheres`
   - `PreparedSceneFrame.visible_receiver_bounding_spheres`
   - draw bounds already emitted by `DrawMetadataEmitter`
   - existing `ExecuteIndirectCounted` public API
   - existing VSM counted-indirect command shape and resource lifecycle pattern
4. Offscreen casters remain allowed, but only if they can affect currently
   visible receivers for the shadow job being rasterized.
5. The raster pass must consume compacted per-job work. Replaying full
   partitions per job is the behavior being deleted.
6. Static and dynamic shadow casters must be budgeted separately. If static
   caching is needed to hit budget, it must be deliberate and measurable, not a
   hidden side path.
7. Multi-directional-light shadow policy must be explicit before closure:
   either Oxygen budgets more than one shadowed directional light on purpose,
   or it enforces one primary directional shadow caster. Silent multiplication
   is not acceptable.

## 4. Final Target Architecture

### 4.1 CPU/GPU Ownership Split

CPU responsibilities:

- keep current ScenePrep extraction model
- build per-frame shadow draw records once from existing prepared-frame data
- build per-light, per-cascade conservative shadow job descriptors
- choose the directional-light budget policy
- publish stable telemetry

GPU responsibilities:

- perform per-job fine eligibility testing for shadow casters
- compact eligible casters into per-job, per-partition indirect-command streams
- drive raster through counted indirect draws

What must not happen:

- no new per-cascade ScenePrep walk
- no CPU readback of GPU cull results
- no CPU-side per-job draw emission after compaction lands

### 4.2 Data Products

The final path should add the following renderer-owned data products:

- `ConventionalShadowCullJob`
  - one record per raster job
  - contains light-space culling planes or equivalent conservative volume,
    target slice, resolution, and any partition offsets needed by the compute
    stage
- `ConventionalShadowDrawRecord`
  - one record per shadow-caster draw
  - contains:
    - draw index / root-constant payload
    - partition slot or pass-mask class
    - world bounding sphere
    - static/dynamic bit
    - any geometry data needed to emit indirect draw arguments
- per-job eligibility and count buffers
- per-partition counted-indirect command buffers

These are renderer-internal data products. They are not new public runtime
systems.

### 4.3 Execution Shape

The target execution flow is:

1. ScenePrep and resource upload build the normal prepared frame.
2. `ConventionalShadowBackend` publishes raster jobs plus conservative
   receiver-driven culling descriptors.
3. A new compute stage tests each shadow draw record against each shadow job.
4. A scan/compaction stage produces counted indirect commands per partition.
5. `ConventionalShadowRasterPass` loops jobs and partitions but executes only
   the compacted indirect streams via `ExecuteIndirectCounted`.

This should reuse the existing counted-indirect pattern already proven by
`VsmShadowRasterizerPass`, specifically the `kDrawWithRootConstant` layout used
with `VsmShaderIndirectDrawCommand`.

### 4.4 What Is Intentionally Out Of Scope First

These items are not phase-1 requirements:

- screen-HZB-driven shadow-caster occlusion
- page-based or virtualized shadow memory
- any dependency on VSM cache invalidation

They may become later optimizations only if the baseline budgets remain unmet
after receiver-driven compaction and indirect raster are in place.

## 5. Performance Targets

Two benchmark tracks must be carried through the remediation:

- `Track A: Sponza-Authored`
  - the exact current `NewSponza` scene as captured
  - currently includes `2` shadowed directional lights and `8` shadow jobs
- `Track B: Sponza-PrimarySun`
  - same scene, but with the final product-approved primary-directional policy
  - this is required because many engines budget only one primary shadowed sun

Baseline for Track A is fixed by the authoritative capture in this document.

Final exit gates:

- `Track A`
  - shadow pass GPU time reduced by at least `65%` from baseline
  - target: `<= 15.5 ms`
  - average eligible draws per shadow job reduced from about `401` to `<= 128`
- `Track B`
  - shadow pass GPU time reduced by at least `75%` from Track A baseline
  - target: `<= 11.0 ms`
  - average eligible draws per shadow job `<= 96`

These are merge gates, not aspirations. If a phase misses its gate, the plan
stays `in_progress` and the next phase does not get to claim success.

## 6. Phased Remediation

### CSM-0. Baseline And Product Policy Lock

Goals:

- preserve the authoritative Release baseline package
- lock the benchmark tracks
- decide and document the shipping policy for more than one shadowed
  directional light

Tasks:

- keep the frame-500 baseline artifacts referenced in this document
- capture one normalized `Track B` baseline after the directional-light policy
  is decided
- add a small renderer telemetry dump for:
  - shadow job count
  - eligible draw count per job
  - compacted indirect count per partition

Exit gate:

- baseline artifacts are stable and reproducible
- the directional-light budget policy is documented
- both benchmark tracks are defined

### CSM-1. Shadow Draw Record Contract

Status:

- implemented

Concrete contract:

- `PreparedSceneFrame` now publishes:
  - `conventional_shadow_draw_records`
  - `bindless_conventional_shadow_draw_records_slot`
- `ConventionalShadowDrawRecord` is a `32`-byte shader-facing record with:
  - `world_bounding_sphere`
  - `draw_index`
  - `partition_index`
  - `partition_pass_mask`
  - `primitive_flags`
- the builder runs once per finalized view in renderer finalization, not per
  shadow job
- the builder derives draw-level records from finalized draw metadata,
  partitions, and draw-bounds spans so the record count matches the actual
  shadow raster population instead of the broader retained-item count
- renderer telemetry now logs per-view record counts, static/dynamic split,
  shadow-only count, and per-partition distribution

Goals:

- build one shadow-caster record stream per frame without touching ScenePrep's
  asymptotic cost
- make the shadow pass measurable before replacing the raster path

Tasks:

- publish a renderer-internal `ConventionalShadowDrawRecord` buffer
- derive it from existing prepared-frame data and draw metadata
- reuse:
  - `RenderItemData.world_bounding_sphere`
  - `RenderItemData.static_shadow_caster`
  - draw root-constant / draw-index identity already used by the renderer
- add tests proving:
  - shadow-only casters are retained
  - main-view visibility is not conflated with shadow eligibility
  - static/dynamic bits survive into the record stream

Exit gate:

- record count matches current shadow-caster draw population
- no new `ScenePrep` `O(jobs x scene)` work was introduced
- telemetry reports record counts and partition distribution

Validation evidence:

- unit coverage:
  - `src/Oxygen/Renderer/Test/ConventionalShadowDrawRecordBuilder_test.cpp`
- implementation files:
  - `src/Oxygen/Renderer/Types/ConventionalShadowDrawRecord.h`
  - `src/Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h`
  - `src/Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.cpp`
  - `src/Oxygen/Renderer/PreparedSceneFrame.h`
  - `src/Oxygen/Renderer/Renderer.h`
  - `src/Oxygen/Renderer/Renderer.cpp`
- fast Release runtime validation:
  - `out/build-ninja/analysis/conventional_shadow_csm1_validation/release_frame256_csm1.benchmark.log`
  - steady-state view telemetry reports `401` conventional shadow draw records
    against `405` retained caster bounds, which matches the known
    rasterized-draw workload rather than the broader retained-item count

### CSM-2. Receiver-Driven Conservative Job Volumes

Goals:

- turn the existing cascade-fit math into actual per-job culling descriptors

Tasks:

- extend `ConventionalShadowBackend` to publish per-job conservative culling
  data:
  - light-space XY receiver rect
  - conservative depth slab
  - extrusion length along light direction
  - target slice and per-job transform
- encode the volume so GPU testing is cheap:
  - sphere-vs-rect-slab or sphere-vs-planes
- keep CPU work `O(jobs)` and independent of shadow-caster count

Exit gate:

- per-job culling descriptors are published and validated
- no raster behavior change yet
- telemetry can report the job volume for debugging and captures

### CSM-3. GPU Eligibility And Compaction

Goals:

- move repeated per-job shadow eligibility work onto the GPU
- reduce the live shadow-caster population before raster

Tasks:

- add a compute pass that tests `ConventionalShadowDrawRecord` against
  `ConventionalShadowCullJob`
- use a 1D or 2D dispatch with one thread per `(job, draw)` pair
- initial kernel guidance:
  - threadgroup size: `64`
  - one bounds test per thread
  - job descriptors read from a small structured buffer / root constants
  - write job-local eligibility bits or counts
- add a scan/compaction stage that produces:
  - eligible-draw counts per job
  - per-job per-partition compacted indices

Validation gates:

- `Track A` eligible draws/job reduced to `<= 224`
- capture proves the compacted counts are smaller than the input population
- CPU time for the conventional path does not regress due to the new cull pass

### CSM-4. Counted-Indirect Conventional Raster

Goals:

- delete CPU replay of full shadow partitions
- raster only the compacted work

Tasks:

- convert `ConventionalShadowRasterPass` to consume counted indirect commands
- reuse the existing public `CommandRecorder::ExecuteIndirectCounted` API
- reuse the same packed root-constant + draw-arguments layout pattern already
  used by `VsmShadowRasterizerPass`
- keep PSO selection public-API-only, as already required by this codebase

Expected result:

- the pass still loops jobs and partitions,
- but it no longer emits one direct draw per retained shadow caster

Validation gates:

- no direct `EmitDrawRange` replay remains on the hot path
- RenderDoc shows counted indirect execution in the conventional pass
- `Track A` shadow GPU time reduced to `<= 22 ms`

### CSM-5. Static/Dynamic Split And Update Budget

Goals:

- stop paying the same price every frame for stable static casters if Phase 4
  is still over budget

Tasks:

- leverage the existing `static_shadow_caster` bit already flowing through
  renderer data
- split static and dynamic shadow draw records
- add stabilized-cascade reuse only if the camera/light policy makes it valid
- if cross-frame reuse is adopted, keep it renderer-owned and explicit:
  - no hidden persistent fallback path
  - no cache that silently diverges from live dynamic content

Important rule:

- this phase is mandatory if Phase 4 does not hit the final budget
- this phase may be skipped only if Phase 4 already satisfies the final exit
  gates on both benchmark tracks

### CSM-6. Shadow Material And LOD Specialization

Goals:

- remove expensive but low-value shadow work that remains after structural
  culling

Tasks:

- add shadow-only LOD policy where the content pipeline supports it
- add explicit masked-shadow budget controls
- allow simplified shadow material paths for masked casters when correctness is
  preserved
- keep validation on:
  - alpha-tested shadow correctness
  - mismatch counts
  - visible artifact review in the baseline capture scene

Exit gate:

- masked or specialized shadow work is measurable and justified
- no correctness regression is accepted without capture-backed approval

### CSM-7. Closure

Goals:

- prove the final path on the same baseline workflow used to expose the
  problem

Tasks:

- rerun Release build
- rerun both benchmark tracks
- regenerate late-frame RenderDoc captures
- update:
  - `src/Oxygen/Renderer/Docs/shadows.md`
  - any pass docs affected by the new cull/indirect path
  - this design document with final measured numbers

Closure gate:

- final budgets met
- docs updated
- tests and captures recorded
- old baseline artifacts retained but clearly marked superseded

## 7. Implementation Map

Primary files likely touched by this remediation:

- planning and publication:
  - `src/Oxygen/Renderer/Internal/ConventionalShadowBackend.cpp`
  - `src/Oxygen/Renderer/ShadowManager.cpp`
  - `src/Oxygen/Renderer/PreparedSceneFrame.h`
  - `src/Oxygen/Renderer/Renderer.cpp`
- raster execution:
  - `src/Oxygen/Renderer/Passes/ConventionalShadowRasterPass.h`
  - `src/Oxygen/Renderer/Passes/ConventionalShadowRasterPass.cpp`
- new compute path:
  - `src/Oxygen/Renderer/Passes/` new conventional shadow cull/command build
    pass
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/` new conventional shadow
    cull / compact shaders
- data preparation:
  - `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.cpp`
  - `src/Oxygen/Renderer/ScenePrep/RenderItemData.h`
- testing:
  - `src/Oxygen/Renderer/Test/` conventional shadow contract / live-scene /
    telemetry tests

Existing code that should be reused as reference instead of duplicated:

- `src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp`
- `src/Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h`
- `src/Oxygen/Graphics/Common/CommandRecorder.h`
- `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp`

## 8. Immediate Next Work

The next meaningful execution order is:

1. Lock the multi-directional-light product policy and capture `Track B`.
2. Add the shadow draw record contract and per-job telemetry.
3. Add receiver-driven job descriptors.
4. Implement GPU eligibility + compaction.
5. Convert conventional raster to counted indirect.

Do not start with cross-frame caching. The current baseline is dominated by
replaying almost all casters for all jobs; that structural waste must be
removed first.

## 9. References And Archived Discovery

Useful repo references:

- `examples/RenderScene/README.md`
- `src/Oxygen/Renderer/Docs/shadows.md`
- `design/LightCullingRemediationPlan.md`

Archived investigation artifacts kept for context:

- earlier failed Release shader run:
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_frame180_conventional.run.log`
- earlier shader-archive smoke runs:
  - `out/build-ninja/analysis/conventional_shadow_sponza_baseline/release_smoke_after_shader_refresh.stderr.log`
- old invalid conventional regression capture:
  - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_debuglayer.log`
  - `out/build-ninja/analysis/conventional_shadow_regression/renderscene_conventional_frame60.shadow_timing.txt`

These historical artifacts are useful for understanding the investigation, but
they are not acceptable final evidence for this remediation.
