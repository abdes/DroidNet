# EX05.1 — Exposure performance

Status: `validated`

| Field     | Summary                                                                               |
| --------- | ------------------------------------------------------------------------------------- |
| Outcome   | FP32 production policy and qualified GPU performance; CPU targets remain a follow-up. |
| Remaining | Extensions: [VX-CPU-01](../../../OPEN_ITEMS.md#p2--engineering-follow-ups).           |
| Evidence  | [Validation record](validation.md)                                                    |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

Tasks, budgets, dependencies and results are recorded in
[Milestone record](#tasks-and-outcome).

- Close the delivered profiling, H1/H2 scan, rejected H3, H4/H5 reuse and R091
  lifecycle-accounting checkpoints. Their final matrix coverage belongs to 13.
- Implement EX051-10A: lease FP32 SceneColor independently so a checked-half
  fallback does not retain depth, GBuffer, velocity or custom-depth attachments.
  Both color reuse and attachment-family reuse must honor readers and GPU fences.
- Replace the performance reference in EX051-04 with `fp32-only`: FP32 storage,
  P=1, normal exposure/events/history and current-frame protection, with no FP16
  candidate scans, FP16-only certificates or eligibility-only status jobs.
  Preserve the existing `fp32` format-only control for numerical diagnostics.
- EX051-05 runs four production/FP32-only pairs in one Release binary: C01 at
  1080p and 4K, C02 at 1080p, and I02 at 1080p. Eight timed runs decide whether
  FP16 helps accepted-half, temporal and retained-FP32 operation. Repeat a pair
  once only if timing variation prevents the decision.
- EX051-09 owns the explicit FP32/admission/FP16/recovery state machine, retry
  triggers and certificate validity. Preserve two actual consecutive eligible
  frames, GPU-owned P/S, current-frame protection and immutable histories.
  Fold exposure-related temporal precision work from 08 into this item.
- EX051-13A/13B are a approved joint follow-up to the measured CPU budget
  failure: shared recording ownership at the participating Vortex stages, plus
  D3D12 root-signature/binding reuse. The tracker and PostProcess owner carry the
  concrete scope and gates. Migrate affected APIs cleanly, remove superseded
  entry points, and preserve actual-submission publication and GPU-fence safety.
  Do not migrate unrelated Oxygen callers merely to make everything batch.
  Keep logging OFF, GPU timing unchanged and use Tracy; no further custom timing
  mechanism, map rewrite or shader/root-constant ABI change is authorized.
- EX051-11 uses existing profiling to isolate active exposure CPU work from
  queue waits. Correct only the identified preparation/submission bottleneck.
  The approved bounded candidates are EX051-11A (one recorder/submission
  for adjacent final range plus histogram/solve, preserving early boundaries)
  and EX051-11B (attribute publication and evaluate one immutable histogram
  constant record for clear and accumulation). Their concrete contracts,
  checks and accept/reject conditions are tracked in
  [PostProcessService](validation.md#cpu-attribution-and-integrated-qualification)
  and the existing item table. They authorize no general submission framework,
  unsafe descriptor reuse or budget relaxation.
  11B was retained because one immutable publication per view removes duplicate
  staging/descriptor work. The measured total CPU timings overlap; it provides
  no demonstrated frame-time gain.
- EX051-12 integrates correctness. EX051-13 alone runs final production
  acceptance: eight recipes at two resolutions, three runs per cell, with the
  fixed transition schedule attached to the 1080p I02 runs and one presentation
  check. EX051-14 reconciles the results and owner documents.

An item closes with its named delivery and accept/reject evidence. Rejected
experiments close without a production change. Final performance acceptance
does not keep completed implementation items open. Store raw results once;
analyses and the checkpoint manifest link them by path/hash.

**Gate:** EX051-GATE passes with native timing distributions, controlled cost
attribution, bounded resources, independently verified correctness and current
owner evidence. Existing replay cost measurements alone do not close it.

**Closed 2026-09-21:** the full native GPU matrix, correctness, resource and
presentation gates pass. Joint 13A/B reduces measured active I02 1080p CPU
p95/p99 by 27.0%/29.6%, to 0.514935/0.625473 ms. The measured CPU cost is the accepted delivery point; further optimization is tracked as VX-CPU-01. This accepted disposition closes 13/14/GATE; the original CPU limits
remain future goals, not passed results. Broader CPU/scaling qualification
belongs to that deferred work. No further benchmark or production correction
is required in Slice 5.1.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 5.1 — Exposure performance | validated | Closed 2026-09-21: format/policy, independent SceneColor ownership, CPU corrections, correctness and final GPU acceptance complete. Accepted operating point; CPU-budget follow-up: VX-CPU-01. | [Current work](../README.md), [final CPU decision](validation.md#shared-recording-and-binding-correction) |

## Tasks and outcome

**Slice status: validated, closed 2026-09-21 with accepted measured CPU cost. Execution state: [Current work](../README.md).**

#### Delivery order and ownership

Remaining order: **03 inventory -> 10A SceneColor ownership -> 04 FP32-only
baseline -> 05 format-benefit decision -> 09 precision policy -> 11 CPU
attribution -> 12 correctness integration -> 13 final acceptance -> 14 closeout**.

EX051-01/02/06/07/10 are complete within their named scopes. EX051-08 is
superseded by the temporal precision work in 09. An implementation item closes
with its own correctness checks, measured result and accept/reject decision.
Only 13 owns the final repeated performance matrix. GATE evaluates that evidence.
A completed or rejected experiment does not wait for 13 to close.

**Closeout:** 13A/B is validated and committed as `22cea346b`. Saved Tracy
context switches establish active I02 1080p p95/p99 of
**0.514935/0.625473 ms**, improved **27.0%/29.6%**. The measured cost is the accepted operating point; further optimization is tracked separately.
EX051-13/14/GATE are closed. The completed GPU matrix, event scripts, outputs,
presentation and copy attribution remain accepted. No more collection or
production optimization is scheduled in Slice 5.1; do not reopen historical
remaining-work lists.

Use the existing renderer, profiling APIs, native workloads and GPU status
reports. Keep FP32 lighting accumulation, exposure equations, current-frame range
protection, image/meter tolerances, immutable leases and GPU-fence retirement.
FP16 is an optional storage optimization for resolved color and per-view
sky/AP/fog products. The FP32-only baseline and precision policy are specified in
[PostProcessService](../../../lld/post-process-service.md#slice-51-fp32-baseline-and-precision-policy).
SceneColor fallback ownership is specified in
[SceneTextures](../../../lld/scene-textures.md#ex051-10a-independent-scenecolor-fallback-ownership).

#### Acceptance contract

Keep numerical baseline `6092dc0d6` and its manifests. Use RTX 3080 / Ryzen 9950X
as the reference system and record adapter/LUID, driver, clocks, power settings,
compiler/shader options and runtime identities. The primary target is 1080p/60.
4K qualifies scaling. A secondary view is half the main width and height.

| Metric on frozen workloads                                                    | One 1920x1080 view  | 1920x1080 main + 960x540 secondary | 4K scaling requirement                                                                 |
| ----------------------------------------------------------------------------- | ------------------- | ---------------------------------- | -------------------------------------------------------------------------------------- |
| Native GPU frame, steady p95 / p99                                            | <=14.0 / <=16.67 ms | <=14.0 / <=16.67 ms                | Report both percentiles and maximum; no 60 fps claim required                          |
| Uncapped complete frame interval, steady p99                                  | <=16.67 ms          | <=16.67 ms                         | Report CPU/GPU/presentation limits separately                                          |
| Attributed exposure + precision GPU work, p95 / p99                           | <=0.50 / <=0.75 ms  | <=0.65 / <=1.00 ms                 | p95 <=2.00 / <=2.60 ms respectively, and <=4.5 times corresponding 1080p p95 + 0.05 ms |
| Exposure CPU preparation/submission, p95 / p99                                | <=0.10 / <=0.20 ms  | <=0.15 / <=0.30 ms                 | Active CPU p95 <=1.25 times 1080p + 0.02 ms; report waits separately                   |
| Warm exposure transitions: additional GPU work over matched steady state, p99 | <=1.00 ms           | <=1.30 ms                          | Report scaling and worst transition; preserve event-frame semantics                    |

Attribute metering, qualification, reductions, producer error bookkeeping,
exposure-related copies and CPU preparation to exposure. Report ordinary fog,
base rendering and whole-frame time separately. Compute per-frame interval unions
before percentiles; never add nested scope times or independent percentiles.

Memory accounting includes live, in-flight, retained and cached allocations,
outputs and reduction scratch. Repeated fixed-descriptor lifecycle cycles must
stabilize, steady-state creation churn must be zero, and matching lifecycle peaks
must not grow by more than 5% without an approved tradeoff. Record placement bytes
and cached-family populations separately. Baseline/candidate comparisons use the
same executed workload and common correctness fixes, identified by source hashes.

The numeric targets and their published context were established in the
[approved baseline manifest](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/baseline-manifest.json).
Keep workload quality and numeric targets fixed. An unrelated rendering
bottleneck requires a separate owner decision.

#### EX051-04 runtime controls

| Selector     | Behavior                                                                                                                                                                                                                              | Use                                                                                            |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `production` | Validated 09 default: FP32/P1, normal exposure and current range protection; no automatic half admission.                                                                                                                             | Production operating mode; historical D01-D04 used the previous dynamic policy at `5f5aa9e85`. |
| `fp32`       | Existing format-only diagnostic control; qualified P and certification remain enabled.                                                                                                                                                | Existing numerical/format diagnostics.                                                         |
| `fp32-only`  | Qualified all-FP32 path with P=1. Preserve metering, adaptation, events, source sharing, temporal rendering and current-frame range checks. Omit FP16 candidate scans, FP16-only error certificates and eligibility-only status jobs. | Performance and memory reference for 05.                                                       |
| `qualified`  | Existing checked FP16/FP32 qualification selected explicitly for diagnostics. Two actual eligible frames and all current checks remain required.                                                                                      | Preserves certified producer/consumer coverage; not automatic production policy.               |

Extend `OXYGEN_EXPOSURE_BASELINE_PRECISION` with `fp32-only`. Use runtime selection
in one Release binary. A mode change resets only precision qualification and
rebases or rebuilds affected radiance histories; it preserves exposure gain and
pending authored transitions. Transition acknowledgements remain active.

#### EX051-05 fixed decision benchmark

Run these four pairs after 10A and 04. Each pair compares `production` with
`fp32-only` on identical scene, quality, camera path and simulation delta time.

| Pair | Existing workload                                     | Main / secondary      | Decision                                                |
| ---- | ----------------------------------------------------- | --------------------- | ------------------------------------------------------- |
| D01  | C01: controlled Manual, deferred, temporal off        | 1920x1080 / 960x540   | Does admitted FP16 improve total frame cost and memory? |
| D02  | C01                                                   | 3840x2160 / 1920x1080 | Does the benefit scale with resolution?                 |
| D03  | C02: controlled Manual, deferred, temporal on         | 1920x1080 / 960x540   | Does dynamic precision help temporal rendering?         |
| D04  | I02: moving indoor/outdoor Auto, forward, temporal on | 1920x1080 / 960x540   | What does admission cost when production remains FP32?  |

**Four pairs, eight timed runs, one Release binary.** Keep the existing opt-in
entry points `ExposureProfilingOverheadTest.DISABLED_ReleaseControlledBaseline`
and `ExposureIndoorOutdoorBenchmarkTest.DISABLED_ReleaseIndoorOutdoorBaseline`.
Use `OXYGEN_EXPOSURE_BASELINE_CASE`, `OXYGEN_EXPOSURE_TIMING_WIDTH`,
`OXYGEN_EXPOSURE_BASELINE_PRECISION`, `OXYGEN_EXPOSURE_BASELINE_FRAMES` and a unique
`OXYGEN_EXPOSURE_BASELINE_RUN`. No source swapping or rebuild between controls.
`OXYGEN_EXPOSURE_BASELINE_FRAMES=warmup` runs the same warmup without a timed
population and writes its throughput to the normal unique run manifest. Use
both controls' warmup results to select one common frame count with duration
headroom; use an integer for each of the eight timed runs. This is an untimed
calibration of the existing entry points, not another performance decision run.

- Warm for at least 300 frames and 10 seconds. I02 warmup includes a complete
  1,200-frame path cycle. Both controls use the same simulation timebase.
- Choose one common measured frame count per pair from the faster control's
  warmup throughput. Collect at least 1,800 frames and 30 seconds per condition;
  I02 populations contain whole path cycles.
- Collect native optimized Release data with VSync, caps, dynamic resolution,
  frame generation, RenderDoc, debug layer and GPU validation disabled.
- Reuse `GpuTimelineProfiler` and `CpuProfileScope`. Reject incomplete,
  overflowed or cancelled recordings. Keep graphics-queue timing and CPU waits
  identifiable; do not treat the graphics timeline as a cross-queue critical path.
- Record complete-frame and GPU-frame distributions, qualification/gradient
  time, memory, format occupancy and output correctness. Capture existing
  rejection reports and P at fixed untimed checkpoints, outside the sample window.
- Report p50/p95/p99/max with nearest-rank percentiles and all sampled frames.
  Preserve slow frames. Record thermal/clock variation.
- Repeat a pair once only when timing variation prevents its decision. If the
  repeated result is still ambiguous, record no demonstrated benefit and stop.

D01/D02 decide whether to retain further FP16 optimization work. D03 selects the
next temporal precision design. D04 defines the cost to remove from the FP32
steady state. EX051-05 records those decisions before 09 changes production policy.
The existing format-only control does not substitute for `fp32-only` in these pairs.

#### EX051-13 final acceptance matrix

Use these frozen recipes in `production` at 1080p and 4K. Run each cell three
independent times after the final implementation: **16 cells, 48 steady runs**.
Each run meets the warmup/sample protocol above. FP32 diagnostic controls remain
in 05 and are not a second 48-run acceptance matrix.
Every acceptance run must meet its cell's targets. Report each run and the
aggregate; do not replace a failed run with a pooled percentile.

| Recipe / `OXYGEN_EXPOSURE_BASELINE_CASE` | Views | Exposure      | Path     | Temporal fog | Existing test entry point / scene                                                                                                  |
| ---------------------------------------- | ----: | ------------- | -------- | ------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| C01                                      |     2 | Manual EV0    | Deferred | Off          | `ExposureProfilingOverheadTest.DISABLED_ReleaseControlledBaseline`; emissive triangle 0.25, vacuum atmosphere, zero-extinction fog |
| C02                                      |     2 | Manual EV0    | Deferred | On           | Same controlled entry point and scene                                                                                              |
| M01                                      |     1 | Auto          | Deferred | Off          | `ExposureProfilingOverheadTest.DISABLED_ReleaseMixedBaseline`; static MultiView mixed scene                                        |
| M02                                      |     2 | Auto          | Deferred | On           | Same mixed entry point and scene                                                                                                   |
| M03                                      |     2 | Manual EV14.5 | Forward  | Off          | Same mixed entry point and scene                                                                                                   |
| M04                                      |     1 | Auto          | Forward  | On           | Same mixed entry point and scene                                                                                                   |
| I01                                      |     1 | Auto          | Deferred | On           | `ExposureIndoorOutdoorBenchmarkTest.DISABLED_ReleaseIndoorOutdoorBaseline`; mixed scene plus six-piece shadowed enclosure          |
| I02                                      |     2 | Auto          | Forward  | On           | Same indoor/outdoor entry point and scene                                                                                          |

**EX051-03 executable inventory.** All entries are in
[`ExposurePerformance_bench.cpp`](../../../../../src/Oxygen/Vortex/Benchmarks/ExposurePerformance_bench.cpp)
and run from `out/build-ninja/bin/Release/Oxygen.Vortex.Exposure.Benchmarks.exe`
with `--gtest_also_run_disabled_tests --gtest_filter=<entry-point>`.
[`ExposureBaselineSetup.cpp`](../../../../../src/Oxygen/Vortex/Benchmarks/ExposureBaselineSetup.cpp)
owns the selectors, view count, path and temporal settings. Set width to 1920 or
3840, precision to the item-required control, frames to the paired sample count,
and run ID to a unique name using the five environment variables listed in 05.
`fp32-only` is implemented and qualified in 04.

Common controls in
[`ExposureBaselineScenario.h`](../../../../../src/Oxygen/Vortex/Benchmarks/ExposureBaselineScenario.h):
16,666,667 ns simulation dt, three frame slots, Average metering, key 12.5,
None tone mapper, gamma 1, jitter off, AP width 64/depth 32/range 96 km/two
samples per slice, fog history-miss supersampling 4 and directional shadows on.
Other exposure fields use the existing `scene::ExposureSettings` defaults;
the run manifest records their resolved values. Views/handles are 500 and 501;
secondary dimensions are half the main. Controlled cameras retain aspect 1/FOV
1 radian; mixed cameras use aspect 16:9/FOV 45 degrees. Caller outputs are FP32.
The indoor fixture enables the shadowing capability; the other two do not.

[`PopulateMixedExposureBenchmarkScene`](../../../../../src/Oxygen/Vortex/Test/Fixtures/ExposureBenchmarkScene.h)
owns procedural asset identities, five opaque/emissive/masked/translucent/ground
surfaces, lights and nonzero atmosphere/fog. I01/I02 add the existing enclosure.
[`ExposureBaselineRendering.cpp`](../../../../../src/Oxygen/Vortex/Benchmarks/ExposureBaselineRendering.cpp)
owns the 1,200-frame path: 300 exterior hold, 300 smoothstep entry, 300 interior
hold, 300 smoothstep exit; secondary phase is +600. Its warmup already enforces
300 frames/10 seconds (whole 1,200-frame cycles for I01/I02). Asset readiness,
draws, actual formats, shadows and temporal history are checked by that harness.
GPU JSON, CPU CSV and the run manifest are emitted once; untimed endpoint images
and placement snapshots are separate from sampled frames. Existing
[native checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/native-benchmark-checkpoint-manifest.json)
and [H5 checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h5-closeout-manifest.json)
remain prior evidence, not new runs or final acceptance.

Append the same bounded event schedule to the three 1080p I02 runs: seed/cut,
Manual/Auto switch, brightness step, sharing/source loss, resize, remove/re-add
and delayed acknowledgement. Record startup and event windows separately from
steady samples. Qualify warm-transition cost against its matched steady window.
The fixed event script is owned by the
[PostProcessService event inventory](../../../lld/post-process-service.md#slice-51-event-operation-inventory).
It maps public operations and the existing test-only acknowledgement-delay seam;
wiring that script into the existing I02 harness and executing it belong to 13.
Memory expectations are in the
[SceneTextures inventory](#workload-memory-inventory).
Verify normal 60 Hz presentation once through an
existing presented-output path. Do not rerun this matrix at intermediate items.

#### Tasks and exit evidence

| ID         | Work item / owner                                                                      | Status     | Depends on           | Validation                                                       |
| ---------- | -------------------------------------------------------------------------------------- | ---------- | -------------------- | ---------------------------------------------------------------- |
| EX051-01   | Acceptance contract / rendering owner                                                  | validated  | Scope review         | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-02   | Native profiling / Graphics + Diagnostics                                              | validated  | 01                   | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-03   | Recipe, event and memory inventory / existing workload owners                          | validated  | 01                   | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-04   | FP32-only baseline / PostProcess + Environment                                         | validated  | 02, 10A              | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-05   | Format-benefit decision / rendering owner                                              | validated  | 03, 04               | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-06   | Bound-publication reduction experiment / shader owners                                 | validated  | 05 checkpoint        | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-07   | Maximum reuse and scan fusion / ExposurePass + producers                               | validated  | 05 checkpoint        | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-08   | Standalone fog optimization                                                            | superseded | —                    | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-09   | Precision operating policy / PostProcess + Environment                                 | validated  | 05                   | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-10   | Copy/reuse/retirement correction / SceneTextures + Graphics                            | validated  | Measured checkpoints | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-10A  | Independent SceneColor fallback ownership / SceneTextures                              | validated  | 10                   | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-11   | CPU attribution and bounded corrections / PostProcess + Graphics callers               | validated  | 05, 09               | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-11A  | Adjacent exposure recorder ownership / PostProcess + ExposurePass                      | validated  | 11 attribution       | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-11B  | Exposure constant and descriptor publication / ExposurePass + existing upload owner    | validated  | 11A decision         | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-12   | Integrated correctness / existing owners                                               | validated  | 04, 09, 10A, 11      | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-13   | Final performance acceptance / rendering owner                                         | validated  | 12                   | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-14   | Owner and evidence closeout                                                            | validated  | 13                   | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-13A  | Shared recording ownership / SceneRenderer + PostProcess + participating Vortex stages | validated  | 13 CPU diagnosis     | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-13B  | Root-signature and binding reuse / Graphics D3D12                                      | validated  | 13 CPU diagnosis     | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |
| EX051-GATE | Slice acceptance                                                                       | validated  | 12, 13, 14           | [Evidence](validation.md#tasks-and-exit-evidence--task-evidence) |

#### Checkpoint and evidence rules

1. Use focused Debug checks for a changed contract and the broader owning Debug
   gate at item closure. The final slice receives the owning Release correctness
   gate. An unchanged accepted checkpoint does not trigger another run.
2. Each new production hypothesis names its change, expected observable result,
   smallest check and stopping condition in 05 before implementation. Keep coupled
   C++/HLSL layouts and callers in one independently buildable commit.
3. Commit an accepted correction or a rejected experiment's retained regression
   and decision before starting another correction. Global acceptance remains in
   13/GATE; it does not reopen completed implementation items.
4. Freeze source/runtime inputs during native collection. Use one runtime control
   selector for D01-D04 and no competing GPU work. Instrumentation stays frozen;
   no new overhead campaign is scheduled.
5. Store each raw result once. Analyses contain derived tables and references to
   input paths/hashes. One checkpoint manifest links commands, revisions, raw
   results and the decision. Do not embed raw native payloads again in audits or
   summaries. Preserve existing evidence and failed-run records.

#### Deferred CPU optimization — later milestone

| Follow-up                                                       | Status               | Validation                                                                          |
| --------------------------------------------------------------- | -------------------- | ----------------------------------------------------------------------------------- |
| Active exposure CPU reduction and broader scaling qualification | deferred, 2026-09-21 | [Evidence](validation.md#deferred-cpu-optimization--later-milestone--task-evidence) |

## Workload memory inventory

EX051-03 inventories existing descriptors and evidence; it introduces no new
allocation measurement. All eight recipes use the same scene/post-process
owners. C01/C02 have two views; M01/M04/I01 have one; M02/M03/I02 have two.
Temporal histories participate in C02/M02/M04/I01/I02. I01/I02 additionally
enable actual shadow rendering. Actual retained generations and placement,
rather than a fixed multiplier per view, determine the total.

| Population / owner                                              | Descriptor-based expectation                                                                                                                                                                                                                   | Existing observation / retirement rule                                                                                                                                                  |
| --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Live scene attachments / `SceneTextures`                        | FP32 accumulation: 16 bytes/texel; depth, partial depth, four GBuffers and enabled velocity/custom depth retain the formats in section 5.1. Main/secondary dimensions follow the recipe.                                                       | Deduplicate native identities; count placement separately from raw texels. Family cache and leased counts are distinct.                                                                 |
| Resolved color and queued fallback / `ResolveSceneColor`        | 8 bytes/texel for admitted half resolve, 16 for FP32 resolve; a conditional extraction additionally retains original FP32 color and its immutable P/state/report.                                                                              | EX051-10A removes the measured whole-family fallback retention below. Queued readers and descriptor retirement remain mandatory and are qualified by its checkpoint.                    |
| Depth extracts / `ExtractSceneDepth`                            | Resolved and previous depth can alias one artifact; count one native allocation, not two logical outputs.                                                                                                                                      | H4/H5 and the lifecycle fixture verify both delayed aliases after source-family reuse.                                                                                                  |
| Environment current products and history / environment passes   | Sky-view dimensions use the existing quality descriptor; AP is 64 x 64 x 32 for steady recipes. Fog dimensions come from the existing viewport/grid resolver. Qualified radiance uses 8 bytes/texel, FP32 uses 16.                             | Temporal recipes retain actual previous fog products, stored P and certificates until their readers/fences finish. Reprojection on does not imply half admission.                       |
| Shared canonical atmosphere cache                               | One shared 256 x 64 and one 32 x 32 RGBA32F table: 136 KiB raw per generation.                                                                                                                                                                 | Count each unique cache generation once across views; include placement alignment and overlapping retired generations.                                                                  |
| Exposure states, reports and reduction scratch / `ExposurePass` | Histogram allocation is `kHistogramWordCount * sizeof(uint32_t)` when metering needs it; frame/state/status/conversion records and structured constant publishers use their declared buffer descriptors. No full-resolution telemetry texture. | Count actual leased generations and cached buffers, including idle allocations. Status readback reuse is bounded by frame slots; a retained consumer can extend its frame/state leases. |
| Caller outputs and fixture transport                            | One full and optional half-size FP32 output; lifecycle fixture also has two 1 x 1 delayed outputs and depth readbacks.                                                                                                                         | Keep named fixture/transport allocations separate from engine placement; diagnostic inspection is excluded from the creation trace.                                                     |
| Other rendering products                                        | Mixed scene assets, HZB and I01/I02 shadow surfaces use their own descriptors and native identities.                                                                                                                                           | Included when observed after trace start; do not attribute ordinary rendering resources to exposure or call a partial trace total device residency.                                     |

`ExposureBaselineScenario::Snapshot` supplies untimed before/after native texture
and buffer placement and creation counts for steady recipes. Zero warm creation
churn is the expectation; snapshots are not lifecycle high-water marks.
`ExposureAllocationScenario` supplies creation-time texture/buffer/combined/
engine/HDR peaks over three cut/resize/remove/re-add cycles, with delayed color
and depth readers. The existing 4K temporal-off entry point required by 10A is
`ExposureLightingGpuTest.DISABLED_ProductionHdrAllocationAccounting`, with
`OXYGEN_EXPOSURE_TIMING_WIDTH=3840` and precision `production`.

The [current Release table](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-current-Release-memory-table.json)
indexes eight raw cases by source hash. The
[matched 4K production audit](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-3840-production-Release.json)
and the other matched audits linked in EX051-10 close the common-fix comparison;
the earlier table's pending-comparison prose is superseded. Their trace is
queue-drained except for explicit delayed consumers; it is neither a worst-case
concurrency bound nor heap commitment/residency. Fixed-descriptor populations
must stabilize after reader/fence retirement. The measured unrelated-attachment
excess below is a correction target, not an acceptable permanent cache budget.
Final timings/memory acceptance belong to EX051-13, and post-10A placement must
be measured before claiming its reduction.

## Supporting records

- [validation](validation.md)
