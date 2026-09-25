# CPU attribution and integrated qualification

The approved FP32 default was measured through the existing CPU profiler in an
isolated optimized Release build with Tracy enabled. One missing exposure-owner
scope was added, followed by nested Graphics acquisition/finalization and D3D12
binding/submission/wait scopes to diagnose the first result. Each frozen batch
passes its one Debug production smoke and one I02 native run; each captures
6,000 steady frames and verifies all 1,005 input hashes. These traces qualify CPU
attribution only, not production GPU performance or the final matrix.

The detailed trace gives the following **elapsed main-thread** intervals,
intersected with the exposure-owner interval union. Nested categories overlap;
their percentiles must not be added. Backend frame-start waits are outside this
union. Driver and scheduler time remain included, so these are not active-only
CPU measurements.

| Interval, two-view I02 at 1080p                   | p95 ms | p99 ms | Observation                                                                                      |
| ------------------------------------------------- | -----: | -----: | ------------------------------------------------------------------------------------------------ |
| Exposure owner union                              |  0.762 |  0.902 | Active-CPU target 0.15/0.30 ms is not demonstrated. Initial trace was 0.776/0.946 ms.            |
| Recorder acquisition                              |  0.128 |  0.154 | Eight acquisitions per frame.                                                                    |
| Recorder finalization                             |  0.265 |  0.325 | Includes eight immediate submissions and retirement bookkeeping.                                 |
| Native `ExecuteCommandLists`                      |  0.206 |  0.256 | Nested inside finalization; about 25% of aggregate exposure elapsed time.                        |
| Compute-pipeline binding                          |  0.066 |  0.080 | Twelve bindings per frame.                                                                       |
| Exposure outside acquisition/finalization/binding |  0.314 |  0.367 | Settings, state/resource tracking, publication, recording and profiling remain in this interval. |
| Explicit fence wait inside exposure               |      0 |      0 | No matching wait interval in any sampled exposure scope.                                         |

Subtracting native submission intervals frame by frame still leaves
0.563/0.662 ms p95/p99. This subtraction is diagnostic, not a replacement metric
or permission to exclude driver CPU work from acceptance. No isolated
exposure-bookkeeping correction has been demonstrated to close the gap. Removing
one adjacent range/solve submission would address only part of the eight
submissions; retaining event ordering, immutable records and submitted-work
acknowledgements remains mandatory.

**Agreed scope (scope review, 2026-09-20):** execute the two specific candidates
below as separate increments. EX051-11 already includes Graphics callers; the
earlier broad wording did not establish a need for a general submission rewrite.
The traces show elapsed costs, not a proven descriptor bottleneck or pure active
CPU cost. Keep 11 in progress until its measured decision is recorded; final
budgets remain in 13/GATE.

| Candidate | Concrete ownership/change                                                                                                                                                                                                                                                                                                                                 | Verification and stopping condition                                                                                                                                                                                                                                                                                                                                                                                                                             |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| EX051-11A | `SceneRenderer` currently invokes `CheckSceneColorRange` immediately before `PrepareSceneExposure`. Move the final range recording into the enclosing exposure operation. One recorder owns final range, histogram and solve, with one successful submission before state publication. Two-view I02 should fall from eight exposure command lists to six. | Prove exact recorder count and unchanged GPU work/order; focused Debug range/numerical, failure/retry, transition/generation, sharing and queued-reader cases, then owning exposure correctness. Compile affected Release paths and use the existing I02 trace controls for a matched CPU comparison. Retain only with correctness and demonstrated benefit; reject without broadening into other renderer stages if it does not help.                          |
| EX051-11B | `RecordState` called `UpdateHistogramConstants` before clear and again before accumulation with the same inputs. Attribute publication, then evaluate publishing one immutable record and binding its index for both dispatches. Evaluate any further constant/descriptor reuse only after its cost and safe ownership are established.                   | Compare publication/descriptor activity and CPU time; qualify histogram/mask, two-view, mode/event, lifetime and retained-reader behavior. Payloads, shader layouts and output budgets stay identical. For this candidate, the user explicitly directed consideration of clarity, maintainability and resource reductions when timing benefit is small or absent. Report each benefit separately; fewer publications do not establish a frame-time improvement. |

11A keeps frame P/state resolve and pre-environment range as separate submissions:
intervening rendering work makes them different ordering boundaries. Each GPU
phase keeps its existing timing scope and required transitions/UAV ordering.
Recording is not submission: failed acquisition, recording or submission must
not publish a solved state, mark a request submitted or acknowledge a transition.
Partially recorded resources remain safely retained. Existing explicit diagnostic
qualification, fallback and retry semantics remain covered.

11B uses `PerViewStructuredPublisher` / `TransientStructuredBuffer` as the
existing publication path. Rebinding a record within one recording does not
permit mutation of its payload. Persistent/reusable backing storage or descriptors
must wait for all queued readers and GPU retirement; a frame-slot index alone is
not a lifetime proof. No new scheduler, general command-batching framework,
cross-frame deferral or renderer-wide allocator redesign is approved by these
candidates. Numerical contracts, production FP32 policy, budgets and final matrix
are unchanged. Commit each candidate's accepted correction or rejected result
before the next production correction. Use the existing validation subagent and
frozen serial native batches; raw results remain in the single checkpoint tree.

**11A accepted:** final range and solve share one recorder when a new solve is
needed. Reusing an already-submitted same-frame solve still checks the current
accumulation independently; it cannot reuse an earlier range verdict. The normal
scene caller explicitly requires the final range guard. Standalone range/kernel
checks use the same recording helper.

All 228 native exposure cases and 88 owning CPU checks pass (latest unique
outcomes). Two existing fixtures needed their explicit diagnostic mode restored
after renderer reconstruction; the repeated fog-history fixture also needed the
backend frame boundaries that drive attachment retirement. The original failures,
CDB null-buffer diagnosis and affected-only reruns are preserved.

The matched optimized I02 trace records six exposure acquisitions/submissions in
every one of 6,000 frames, versus eight previously. Elapsed exposure CPU p95/p99
falls from 0.762/0.902 to 0.646/0.760 ms (15.2%/15.7%). The five candidate-cycle p95s
range from 0.599 to 0.669 ms, below the baseline range 0.744–0.782 ms. GPU phase
counts and memory placement are identical; steady texture/buffer creations remain
zero. All four endpoint pairs preserve gain and pass the existing image budgets;
three are identical, and the remaining maximum difference is 1.49e-7 with at most
one UNorm8 code. All 1,011 frozen inputs remain unchanged. See the
[11A decision table](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/recorder/decision-table.json).
This accepts the candidate, not the active-only CPU target or final GPU matrix.

**11B accepted for resource work and maintainability:** `PublishHistogramConstants`
now returns one immutable record per view. `RecordState` explicitly rebinds it
after both pipeline changes. This separates allocation/publication from binding
and avoids constructing and publishing the same payload again. The trace confirms
four publications per two-view frame become two: 12,000 fewer publications over
6,000 frames. The existing publisher makes one staging allocation and descriptor
lookup/create operation per publication, so both operation counts are halved for
these constants. Payload writes fall by 128 bytes per two-view frame; allocated
GPU texture/buffer placement is unchanged. No descriptor-heap capacity or resident
memory reduction is claimed, and no persistent cache or lifetime extension was
introduced.

Total exposure elapsed CPU p95/p99 changes from 0.640/0.804 to 0.664/0.775 ms;
cycle ranges overlap. **Total CPU/frame time is unchanged within measurement variation.**
The pre-change helper accounts for only 1.9% of aggregate exposure elapsed time
(0.011 ms p95). Its scope includes binding before the refactor; binding remains
inside the enclosing exposure interval after the refactor, so the narrower helper
timing alone is not an end-to-end saving. This does not justify a descriptor-store
redesign. Reduced resource operations and simpler ownership are the acceptance basis.

All 228 owning Debug cases and the instrumented Release run pass; all four
endpoint pairs meet the unchanged budgets and preserve gain. GPU phase counts,
six submissions per frame, memory placement and zero steady resource creation
are preserved. The initial baseline capture was invalidated by changed frozen
inputs; it remains preserved, and its authorized fresh-freeze retry passed with
1,013 unchanged inputs. The candidate passes with 1,014 unchanged inputs. The
retained source is byte-identical to that validated candidate. See the
[11B decision table](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/publication/decision-table.json).
The bounded 11 work is complete; proceed to 12 integration. Active-only CPU
budget proof and production performance acceptance remain open in 13/GATE.

The [CPU checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/checkpoint-manifest.json)
links the original and detailed raw traces, CPU exports, analyses, commands and
hashes and the accepted 11A/11B increments. EX051-12 is qualified below; 13–14
and final acceptance remain pending.

### EX051-12 integration coverage

The changed contracts map to existing owning cases below. No new test was
needed. Current Debug evidence was reused; the missing normal Release gate
(Tracy off) passed **371/371** enabled checks: 143 CPU and 228 native. All 979
frozen inputs stayed unchanged; eight disabled benchmarks were not run.

| Changed contract                                              | Existing coverage                                                                                                   | Evidence treatment                                                                                                                            |
| ------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Production FP32/P1 and explicit certification controls        | `Fp32Only_test.cpp`, `Fp32Reference_test.cpp`, DiagnosticsService defaults/revision checks                          | Reuse current Debug; qualify normal Release.                                                                                                  |
| Numerical gain, histogram, masks, curves and fixed/zero modes | `Metering_test.cpp`, `Masks_test.cpp`, `Transitions_test.cpp`, FP32-only fixed-mode cases                           | Existing native owner suite covers constant reuse; no new test needed. ExposureSettings equations are unchanged and retain accepted evidence. |
| Current range and temporal history                            | `RangeGuards_test.cpp`, `FogHistory_test.cpp`, FP32-only temporal/range case                                        | Both renderer-reconstruction fixture corrections and their original failures are preserved in11A.                                             |
| Events, sharing and stale packets                             | `ScenePrecision_test.cpp`, `SourceLoss_test.cpp`, `SceneLifecycle_test.cpp`, precision-control acknowledgement case | Retain accepted delayed-status/lifetime coverage; normal Release owner suite qualifies optimized paths.                                       |
| Independent SceneColor readers/fences                         | SceneTextures/RetainedTexturePool owner tests and `QueuedConsumers_test.cpp`                                        | Reuse10A Debug4K lifecycle evidence; qualify missing Release owner checks. No repeated lifecycle benchmark.                                   |
| Combined final range/solve and same-frame reuse               | `CombinedSceneRangePreservesSeedRetryInvalidMeterAndRetainedState`, existing preparation/failure/reuse tests        | Current Debug accepted; normal Release adds the missing optimized-build proof.                                                                |
| Runtime/public post-process integration                       | PostProcessService, SceneRendererDeferredCore, RendererPublicationSplit, DiagnosticsService                         | Reuse accepted Debug; one normal Release batch.                                                                                               |
| Shader catalog and layouts                                    | Accepted04 catalog tests and234-module Debug/Release archives                                                       | Shader and Scene source roots are unchanged from5f5aa9e85 through8c39ab62b; no catalog/settings rerun.                                        |

The [integration checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/integration12/checkpoint-manifest.json)
links exact commands, per-suite results, reused checkpoints and the unchanged
Scene/shader source audit. This closes correctness integration only; final
performance, event windows and presentation remain in 13/GATE.

### EX051-13 final collection and gate disposition

**Closed 2026-09-21:** the completed GPU/event/presentation evidence, accepted
13A/B correction and explicit acceptance of measured CPU cost close this gate.
See [the final CPU decision](#shared-recording-and-binding-correction).
The diagnostic sequence below is retained as historical evidence; its earlier
holds and proposed next steps are superseded by that closeout.

The final frozen production matrix is collected: **48 valid runs, 296,100 steady
frames**, all eight recipes at 1080p/4K with three runs each. Every run has at
least 1,800 frames and 38.887 seconds of steady data. All 981 frozen inputs stayed
unchanged. The first I02 attempt failed the test transport's delayed-status bound;
its evidence remains separate from the 48 valid runs. No native benchmark was
rerun for analysis or report generation.

The following values are the worst per-run GPU frame p95/p99 of three runs,
in milliseconds. They are not pooled percentiles.

| Recipe | 1080p p95 / p99 |    4K p95 / p99 |
| ------ | --------------: | --------------: |
| C01    |   3.799 / 5.011 |   7.214 / 8.386 |
| C02    |   3.826 / 5.061 |   7.236 / 8.773 |
| M01    |   5.903 / 7.461 | 16.937 / 18.339 |
| M02    |  8.474 / 10.181 | 22.568 / 24.690 |
| M03    |   5.919 / 7.181 | 15.267 / 16.668 |
| M04    |   4.405 / 5.275 | 11.669 / 13.712 |
| I01    |   5.977 / 6.946 | 17.379 / 18.765 |
| I02    |   6.411 / 7.779 | 15.163 / 16.990 |

Against the matching recorded pre-policy production baselines, conservative
GPU frame p95 reductions are **54.7% C01/1080p, 55.1% C01/4K, 63.1% C02/1080p
and 41.8% I02/1080p**. No before/after improvement is claimed for cells without
a matching recorded baseline. The approved C01 steady-memory increases remain
106/398.625 MiB at 1080p/4K; C02/I02 decrease by 1.125 MiB.

| Acceptance evidence                         | Recorded result                                                                                                                                                                                                                                |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Whole-frame 1080p targets                   | All 24 runs pass GPU p95/p99 and uncapped wall p99 targets; worst wall p99 is 12.035 ms.                                                                                                                                                       |
| Attributed exposure GPU work and 4K scaling | All 48 runs pass; all 24 corresponding 4K comparisons pass scaling. Exposure status copies are included. Stage-21 ordinary output snapshots are reported separately, as established below.                                                     |
| Warm transitions                            | Three full I02 scripts pass; worst per-operation additional GPU p99, including the entire resolve scope, is 0.392 ms against 1.30 ms. Startup is separate: 60 frames per run.                                                                  |
| Correctness/output                          | All native checks and exports pass. All 64 same-cell endpoint comparisons pass unchanged float and UNorm8 budgets; maximum float difference 7.75e-7, maximum gain difference 9.83e-7 stops.                                                    |
| Resource stability                          | Zero steady texture/buffer creation churn in every run. Placement snapshots and accepted 10/10A lifecycle evidence are retained. No new lifecycle campaign.                                                                                    |
| Presentation                                | One 180-frame VortexBasic launch with target 60 fps and VSync passes. Its captured EV14 output has gain 2^-14 and pre-storage RGB 0.25; replay numerical checks and visual inspection pass. This is not a measured monitor-refresh-rate claim. |

**Closure work:** the final harness omitted exposure-only CPU accounting. The
earlier proposal to treat missing CPU/copy evidence as qualification exceptions
is withdrawn. Budgets remain unchanged:

1. Active exposure CPU p95/p99 and 4K CPU scaling are not established by the
   whole-renderer CPU CSV or the instrumented elapsed owner intervals. The
   latter include driver/scheduling time and do not demonstrate the original
   0.15/0.30-ms two-view active-CPU limits.
2. **Copy attribution closed by source and recorded scopes.** Stage 21 publishes
   ordinary resolved-color and resolved-depth snapshots for Stage 22/23 even
   when `PreparedExposure` is absent. In production FP32, both copies remain
   that renderer handoff work. Exposure-specific checked conversion has its own
   `Exposure.ConvertSceneColor` scope and is absent in every recorded window.
   Exposure status copies have an `Exposure.StatusReadback` scope: all 21 recorded
   instances are already included in the exposure interval unions. The source
   audit matches the frozen implementation hashes and covers all 48 steady runs
   plus their startup/event windows. Therefore the existing exposure union is
   the attributed production metric; `ResolveSceneColor` remains separately
   reported ordinary renderer work. Adding it exceeds the exposure-only budget
   in 14 runs because it adds unrelated output snapshots, not because exposure
   copies were omitted. See the
   [ownership audit](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/copy-ownership-audit.json).

The user subsequently authorized creating/running the benchmark needed to close
the gap. Use the normal optimized Release build with Tracy disabled. Observe the
existing exposure CPU scopes without changing their ownership, and intersect
their per-frame union with Windows scheduler running intervals. Include driver
CPU work; report off-CPU time and explicit fence waits separately. Capture QPC
timestamps and thread/frame identities so scheduler and owner records share one
clock. Keep trace buffers bounded and reject dropped events or incomplete scopes.

Start with I02 at 1080p and 4K. Verify accounting and diagnose an actual target
failure before expanding to the remaining recipes and repetitions. The original
CPU limits and eight-recipe/two-resolution coverage remain authoritative. Use
the existing serial validation agent, frozen native batches and sampling rules.
The completed production GPU matrix, events, correctness and presentation are
reused; supplemental GPU output is not a replacement performance campaign.
Resolve Stage-21 copy attribution by source ownership and recorded scope/format
evidence. No production correction or target exception is implicit in this
measurement authorization. Close 13/14/GATE when the resulting evidence meets
their exits.

The measurement implementation adds `ScopedCpuScopeObserver` to the existing
CPU profiling coordinator without changing `CpuProfileScope` layout or Tracy/PIX
behavior. The opt-in baseline control `OXYGEN_EXPOSURE_BASELINE_CPU=1` preallocates
owner records before timing, preserves the established 11 owner selection and
exports QPC/frame/thread identities. `AnalyzeExposureCpu.py` intersects those
unions with scheduler running intervals, including driver execution, and reports
blocked/descheduled and explicit fence intervals separately. No profiling cost
is subtracted from the result. The collector rejects overflow, incomplete scopes,
missing frame/scheduler coverage and lost ETW events.

Measurement checks pass: seven Debug and seven Release observer/profile tests,
one Debug production smoke, and four synthetic interval/scheduling checks. An
80.635-ms scheduler probe resolves 80.622 ms blocked and 0.013 ms active with
matching 10-MHz QPC clocks and zero lost events/buffers; all 1,033 frozen inputs
remain equal. Raw checks and commands live in the existing
[CPU evidence directory](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/cpu).
This qualifies accounting; workload CPU budgets still require the new collection.

The first active CPU pair completed with 1,034 unchanged inputs and zero ETW
loss. I02/1080p measured 0.959/1.239 ms p95/p99; 4K p95 was 0.932 ms, passing
that pair's scaling bound. The requested identical 1080p rerun measured
0.508/0.619 ms. Both used the existing Release INFO logging default. The user
then required logging fully disabled for all subsequent measurements. The
`-v=OFF` run confirms zero engine log lines (933 bytes of test/startup output,
versus 41 MB previously) and measures 0.570/0.722 ms. All three 1080p results
exceed the unchanged 0.150/0.300-ms CPU targets; no remaining recipes are run
while this failure is unresolved. Earlier evidence stays intact.

The next bounded diagnostic uses `OXYGEN_EXPOSURE_BASELINE_CPU_DETAIL=1` to
record the existing nested Graphics acquisition/finalization, D3D12 binding/
submission and exposure publication scopes. It preserves the owner union,
sampling and numerical workload, with logging OFF. Nested category percentiles
are diagnostic and must not be summed. Identify a measured cause before any
production correction; this does not authorize a general submission rewrite.

That diagnostic's GPU serializer hit `export_queue_full` at frame 7779 and
exported 5,378/7,200 GPU frames. The native failure is retained; no GPU acceptance
uses it. Independent CPU export completed all 7,200 frames/381,600 scope records,
with zero ETW loss and all 1,035 frozen identities equal. Active CPU p95/p99 was
0.905/1.110 ms. Native `ExecuteCommandLists` alone measured 0.256/0.338 ms and
27.1% of aggregate exposure CPU execution; acquisition measured 0.155/0.196 ms.
Histogram publication was 0.011/0.017 ms and 1.2% of aggregate execution. These
nested measurements identify submission/acquisition as material costs; they do
not justify another constant-publication experiment.

The user rejected disabling GPU timestamps and further custom instrumentation.
Keep GPU timing unchanged and use the existing validated Tracy build/captures
to diagnose submission. All 30 runtime/shader identities still match the 11B
Tracy checkpoint, so this investigation needs no rebuild. Framewise subtraction
shows that engine submission work outside `ExecuteCommandLists` is only
0.0064/0.0125 ms p95/p99; 97.5% of aggregate submission time is inside the native
call. There are exactly six such calls per frame. Use Tracy's existing Windows
sampling/context-switch support with elevation and logging OFF to investigate
that call; do not add more timestamps or change production policy speculatively.

The [performance report](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/performance-report.md),
[per-run decision table](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/decision-table.json)
and [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/checkpoint-manifest.json)
reference raw results once by path/hash, source/runtime freezes, clock/thermal
telemetry and the presentation proof. One partial final telemetry row from a
stopped sampler is preserved and identified; no native sample was discarded.

## Shared recording and binding correction

The review accepted these two items together after reviewing the source
and the non-overlapping CPU costs. This extends the earlier bounded 11A/11B
scope; those completed decisions remain closed. The migration uses clean API
changes and migration, removal of superseded entry points, and no compatibility
wrappers or parallel legacy implementations. Unrelated Oxygen submission sites
need not adopt batching.

Implementation checkpoint: the shared SceneRenderer view owner and borrowed
stage APIs compile in Debug and Release. The permanent-state handoff regression
reported by MultiView was corrected: pass handoffs now use ordinary transitions,
while the command tracker retains its permanent-state invariant. The user
confirmed the demo correction; it is not rerun by this task. The auxiliary
recording local was renamed and C4456 is absent from both builds.

The final owning checks pass: **589 unique Debug checks** across 20 executables
and **403 Release checks** across the five affected Vortex owners, including
228 native exposure checks. Release ran the existing Tracy-enabled measurement
binaries; its 1,034 frozen inputs were unchanged. All eight disabled benchmarks
remained unrun in that correctness batch. Failed/discarded recording, committed
history, range-certificate retry, queued consumers and producer publication are
covered. Final clang-tidy analyzed 89 translation units with zero diagnostics
on changed code, zero parse failures and no new warning suppressions. Existing
unmodified-code diagnostics remain in the full logs as input to the
[bounded 5.2 fix selection](../EX05.2/README.md#tasks-and-outcome);
they are not an automatic backlog requiring a full-owner cleanup.

**Measured decision (2026-09-21): accept the joint correction.** One new I02
1080p Release run was compared with the existing Tracy baseline: 7,200 steady
frames each, identical controls, logging OFF, GPU timestamps enabled and the
custom CPU observer disabled. All 1,030 candidate input hashes stayed unchanged.
Existing Tracy zones conservatively charge the complete shared view acquisition
and finalization to exposure; nested intervals are counted once. No new runtime
timer or repeated baseline was needed.

| Elapsed CPU/frame metric               | Previous baseline | Joint candidate | Reduction |
| -------------------------------------- | ----------------: | --------------: | --------: |
| Active exposure mean                   |       0.482794 ms |     0.371156 ms |     23.1% |
| Active exposure p95                    |       0.705693 ms |     0.514935 ms |     27.0% |
| Active exposure p99                    |       0.888333 ms |     0.625473 ms |     29.6% |
| Elapsed exposure mean                  |       0.483860 ms |     0.371625 ms |     23.2% |
| Exposure p95                           |       0.708597 ms |     0.515917 ms |     27.2% |
| Exposure p99                           |       0.901528 ms |     0.630422 ms |     30.1% |
| Attributed acquisition p95             |       0.107153 ms |     0.069161 ms |     35.5% |
| Attributed finalization/submission p95 |       0.247008 ms |     0.130787 ms |     47.1% |
| Nested compute binding p95             |       0.070302 ms |     0.061307 ms |     12.8% |
| Whole-frame recording/submission p95   |       5.193100 ms |     3.958400 ms |     23.8% |
| Whole-frame wall p95                   |       7.660300 ms |     7.167600 ms |      6.4% |

Exposure recordings fall from six to two and all render-thread recordings from
40 to 10 in every measured frame. Compute binding calls remain 12 per frame;
reuse reduces work inside those calls. The nested rows are separate attribution
views, not additive percentile savings or isolated A/B experiments. All six
candidate cycle p95 values (0.481372–0.572314 ms) are below all six baseline
values (0.624460–0.764024 ms). GPU frame p95 improves 6.717440 to 6.436864 ms.
GPU phase counts and placement are identical, with zero steady buffer/texture
creation. All four endpoint pairs pass: displayed gain is exact, maximum float
error is 5.9604645e-8 and quantized output is identical.

The [decision table](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-corrections13/decision-table.json)
and [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-corrections13/checkpoint-manifest.json)
reference raw inputs by path/hash and preserve failed intermediate diagnoses.
This closes 13A/B's implementation and measured-benefit decisions. Offline
extraction through Tracy's own file reader and context-switch API now qualifies
active CPU from the same captures, without another engine run. The analysis
intersects each merged zone interval with the native render-thread running
intervals and checks complete coverage of every measured frame. Both active
CPU targets fail: **0.514935/0.625473 ms p95/p99 versus 0.15/0.30 ms**. Scheduler
gaps explain only 0.000982 ms of candidate p95; they are not the remaining cost.

**Operating-point decision (2026-09-21):** ship the measured joint correction
and retain the original CPU limits as optimization targets. I02 active CPU
p95/p99 is **0.514935/0.625473 ms**, above the original **0.15/0.30 ms** target.
The numerical, GPU/scaling, resource and presentation gates passed. Further
active-CPU reduction and broader CPU scaling qualification are
[VX-CPU-01](../../../OPEN_ITEMS.md#p2--engineering-follow-ups).

The reference diagnosis is 27.1% native submission, 16.7% acquisition, 7.6%
finalization outside native submission, and 9.8% compute binding. A hypothetical
six-to-two reduction of all acquisition/finalization cost exposes about 34.2%
of the measured total, not a demonstrated saving. No 45–60% prediction is an
acceptance criterion. Budgets and numerical tolerances stay fixed.

Implement these as one coherent joint correction with normal hooks; do not
interleave production edits with frozen collection. Logging remains fully OFF,
GPU timing remains enabled, and Tracy is the profiling tool. Do not add another
custom timing mechanism. First qualify the changed contracts, then measure I02
at the agreed checkpoint before expanding acceptance. Existing baseline payloads
remain unchanged; final candidate qualification follows only after implementation
is stable. Map/dense-handle rewrites, root-constant ABI changes and unrelated
renderer optimizations are outside these two approved items.

#### Established results and remaining design problem

- H1 maximum reuse and H2 scan fusion are retained. H3 bound-publication reduction
  was rejected and reverted. These experiments are closed.
- H4 depth sharing and H5 resource reuse are committed. The H5 I02 checkpoint
  removes 12 texture and two buffer creations per frame, with unchanged placement.
- R091 and lifecycle accounting are committed as `8903305`. Seven focused Debug
  cases and eight Release cases on each side pass. Matching pre-H5/current
  texture, buffer, combined, engine and HDR placement peaks are identical.
- The H5 I02 recording stays FP32 for all 3,600 sampled frames. Mean explicit
  exposure time is 2.146 ms: candidate qualification 1.377, gradients 0.328,
  scene-range checks 0.220, metering/adaptation 0.180 and other work 0.041 ms.
- Before 10A, conditional FP16 output retained a whole SceneTextures family for its FP32
  fallback. Temporal-off retirement retained an extra 302.25 MiB at 1080p and
  1,128.75 MiB at 4K; 216 and 806.25 MiB respectively are unrelated attachments.
  EX051-10A now fixes that ownership with independent color leases.
- The existing `fp32` control keeps certification enabled. Temporal production
  and that control differ in P at 13 per-view observations across seven phases.
  EX051-04 supplies the qualified FP32-only baseline; 05's results are below.

#### EX051-05 results and decision

Completed in one qualified Release binary from implementation `5f5aa9e85` on
the reference RTX 3080 / Ryzen 9950X system. Each pair used one common count
chosen from both untimed warmups; every condition exceeded 30 seconds. All
60,000 frames, including slow frames, remain in the native records. No recording
was incomplete, overflowed or cancelled. All 648 frozen identities stayed equal.

| Pair            | Frames / condition | GPU frame p95: production / FP32-only (ms) | FP32-only minus production steady placement (MiB) | Measured decision                                                    |
| --------------- | -----------------: | -----------------------------------------: | ------------------------------------------------: | -------------------------------------------------------------------- |
| D01: C01, 1080p |              9,600 |                              8.385 / 5.322 |                                          +106.000 | Admitted half saves memory but has no frame-time benefit.            |
| D02: C01, 4K    |              5,100 |                             16.084 / 9.444 |                                          +398.625 | Memory saving scales; the time penalty grows to 6.640 ms.            |
| D03: C02, 1080p |              9,300 |                             10.359 / 5.158 |                                            -1.125 | Both stay FP32; reject recurring temporal admission work.            |
| D04: I02, 1080p |              6,000 |                             11.015 / 8.232 |                                            -1.125 | Both stay FP32; remove recurring admission work from this operation. |

GPU metrics are graphics-queue frame spans, not a cross-queue critical path.
Placement is the complete traced steady snapshot, including outputs/cache/
in-flight resources, not heap commitment or a lifecycle peak. The C01 increases
are 13.1% and 13.6% of production's traced placement; this is an explicit policy
tradeoff requiring the 09 agreement, not an assumed memory-budget exemption.

All ten endpoint pairs meet the existing 0.5% + 2e-5 float and one-code-value
image budgets. C01/C02 are byte-identical; I02's largest float delta is
2.742e-6, its largest independent UNorm8 delta is one, and endpoint gain differs
by at most 3.830e-6 stops. All eight runs have zero sampled texture and buffer
creations. Within-run block p95 ranges are separated for every pair, so variation
does not prevent these decisions and no pair is repeated.

**Recommendation for 09:** make FP32/P1 the production default and keep checked
FP16 qualification available only as an explicit diagnostic. This ends further
production FP16 optimization in this slice, subject to user agreement on the
memory tradeoff. The complete state/validity table is in the PostProcess owner.
At this checkpoint: for example, I02's FP32-only explicit-exposure
p95 is 0.683 ms before attributing resolve work, above the 0.650 ms target.
11 must separate active CPU/submission work from waits; 12/13 own integrated
correctness and final budgets. No target or image tolerance is changed.

[One derived decision report, raw paths/hashes and all distributions](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/decision-table.json)
and [checkpoint manifest](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/checkpoint-manifest.json).

## Tasks and exit evidence — task evidence

| ID         | Work item / owner                                                                      | Status     | Depends on           | Delivery and exit evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| ---------- | -------------------------------------------------------------------------------------- | ---------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| EX051-01   | Acceptance contract / rendering owner                                                  | validated  | Scope review         | Hardware, invariants, budgets and eight recipes are frozen in the [baseline manifest](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/baseline-manifest.json).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| EX051-02   | Native profiling / Graphics + Diagnostics                                              | validated  | 01                   | Timestamp retention, coverage, bounded export and R085 are qualified in the [profiling closeout](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/profiling-closeout-manifest.json). Instrumentation overhead is accepted and frozen.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| EX051-03   | Recipe, event and memory inventory / existing workload owners                          | validated  | 01                   | Documentation-only source inventory complete: eight entry-point/settings mappings and prior workload evidence above; public event operations and fixed script in PostProcessService; live/cache/lease/scratch expectations and existing lifecycle evidence in SceneTextures. Verified against current source and existing reports; no build or native run required/performed. Event-script wiring/execution and final repeated timings belong to 13.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| EX051-04   | FP32-only baseline / PostProcess + Environment                                         | validated  | 02, 10A              | Runtime FP32/P1, admission/certificate bypass, omitted unused reports and precision-only mode invalidation implemented; normal exposure/range/events/sharing/history remain active. Debug: 109 latest unique checks pass; Release: four catalog plus nine native controls pass. Both 234-module shader archives and Release-only benchmark paths compile. Existing `fp32` remains format-only. [Commands, frozen hashes and preserved test-observation correction](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/fp32-only/checkpoint-manifest.json). D01-D04 measurements belong to 05.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| EX051-05   | Format-benefit decision / rendering owner                                              | validated  | 03, 04               | D01-D04 complete in one Release binary: eight timed runs/60,000 frames, ten passing endpoint pairs, zero steady creation churn, 648 unchanged frozen identities. No repeats needed. Every dynamic-production pair is slower; C01 alone saves memory. Reject a frame-time benefit, record the C01 memory tradeoff, and propose FP32 production for the required 09 agreement. [Decision table](validation.md#ex051-05-results-and-decision), [raw evidence/checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/checkpoint-manifest.json). Final budgets remain 13.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| EX051-06   | Bound-publication reduction experiment / shader owners                                 | validated  | 05 checkpoint        | H3 was tested, rejected and reverted. [Closeout](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h3-closeout-manifest.json) preserves the native comparison, DXIL, numerical checks and restored scalar regression. No active follow-up experiment.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| EX051-07   | Maximum reuse and scan fusion / ExposurePass + producers                               | validated  | 05 checkpoint        | H1 (`73ced156`) and H2 (`efecd6185`) are retained with owning Debug, Release comparisons, DXIL and image checks: [H1](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h1-closeout-manifest.json), [H2](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h2-closeout-manifest.json). A new scan hypothesis requires a new 05 decision; final matrix coverage belongs to 13.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| EX051-08   | Standalone fog optimization                                                            | superseded | —                    | Removed from active delivery. Exposure-related temporal precision and error propagation belong to 09. General fog optimization is outside this slice.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| EX051-09   | Precision operating policy / PostProcess + Environment                                 | validated  | 05                   | Review approved the [state/decision and validity table](../../../lld/post-process-service.md#agreed-ex051-09-precision-policy) and C01 memory tradeoff on 2026-09-20. FP32 production, explicit `qualified` diagnostics and control-revision acknowledgement rejection implemented. Numerical fixtures explicitly select certified behavior without renaming their identities. 122/122 focused Debug checks pass, including both control-switch acknowledgement directions, qualification failures, source/layout/stale-status cases and queued readers. [Checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/production-policy/checkpoint-manifest.json). No automatic production admission/retry or tolerance change; integrated owning gates and final acceptance remain 12/13.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| EX051-10   | Copy/reuse/retirement correction / SceneTextures + Graphics                            | validated  | Measured checkpoints | H4 (`20e8968f`), H5 (`28e0f1ee`) and R091/accounting (`8903305`) are committed. H5 removes measured steady churn; R091 passes seven focused Debug cases. The two Release lifecycle matrices pass eight cases each with identical same-control peaks, P/state trajectories and retired populations. Evidence: [H4](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h4-closeout-manifest.json), [H5](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h5-closeout-manifest.json), [lifecycle protocol](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/protocol-shared-idle-fix.json), [matched 1080 production](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-1920-production-Release.json), [1080 FP32](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-1920-fp32-Release.json), [4K production](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-3840-production-Release.json), [4K FP32](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-3840-fp32-Release.json). |
| EX051-10A  | Independent SceneColor fallback ownership / SceneTextures                              | validated  | 10                   | Independent color allocation/consumer leases and attachment-frame retirement implemented. Framebuffer bindings retain resources separately from immutable reader leases; both pool reuse paths preserve readers/fences. Final Debug batch: 56/56, including existing queued color/depth cases and 4K temporal-off lifecycle. All three retained-resize cycles preserve warmed attachment identities/populations; retired leases are zero. Engine peak 3747.207 MiB (322.5 MiB below the superseded draft); no matched-Release benefit claim. [Checkpoint, exact commands, hashes and preserved diagnosis](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/color-ownership/checkpoint-manifest.json). Item exit passes; Release format-benefit decisions belong to 05 and final acceptance to 13.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| EX051-11   | CPU attribution and bounded corrections / PostProcess + Graphics callers               | validated  | 05, 09               | Existing profiling separates exposure preparation/submission intervals from explicit frame-start waits. 11A is accepted for measured CPU benefit; 11B is accepted for source-proven resource work and maintainability under the agreed criterion, with no demonstrated total CPU gain. Owning Debug and matched instrumented Release evidence are recorded below. No general submission rewrite. Active-only CPU budget proof and normal production performance acceptance remain 13/GATE; this closes the bounded delivery/decision scope. [Owner decisions](validation.md#cpu-attribution-and-integrated-qualification), [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/checkpoint-manifest.json).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| EX051-11A  | Adjacent exposure recorder ownership / PostProcess + ExposurePass                      | validated  | 11 attribution       | Final range and histogram/solve share one successful submission; reused same-frame solves retain an independent current range check. Owning Debug: 228 native + 88 CPU pass (latest unique outcomes); two diagnostic/lifecycle fixture failures diagnosed, corrected and preserved. Matched Release I02: all 6,000 frames use six submissions instead of eight; elapsed CPU p95/p99 0.762/0.902 -> 0.646/0.760 ms, all five cycle p95 ranges disjoint. GPU phase counts, memory and zero steady churn preserved; four endpoint pairs pass. All 1,011 frozen inputs unchanged. Accept candidate; active-only CPU/global budgets remain open. [Decision](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/recorder/decision-table.json), [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/checkpoint-manifest.json).                                                                                                                                                                                                                                                                                                                                                                                           |
| EX051-11B  | Exposure constant and descriptor publication / ExposurePass + existing upload owner    | validated  | 11A decision         | Accepted for clarity and reduced resource work under the agreed criterion: publish one immutable histogram record per view, explicitly rebind for clear/accumulation; four publications become two in every sampled frame, removing two staging allocations and descriptor lookup/create operations plus 128 payload-write bytes per two-view frame. No persistent storage or lifetime change; GPU placement unchanged. Total elapsed CPU p95/p99 0.640/0.804 -> 0.664/0.775 ms, overlapping cycle ranges: no total CPU gain demonstrated. All 228 Debug cases and four endpoint pairs pass; GPU phase counts and six submissions preserved. Valid baseline/candidate freezes pass; invalid initial baseline retained. [Decision](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/publication/decision-table.json), [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/checkpoint-manifest.json).                                                                                                                                                                                                                                                                                                             |
| EX051-12   | Integrated correctness / existing owners                                               | validated  | 04, 09, 10A, 11      | Existing numerical/mask/range/history, transitions/sharing/stale status, independent color/fences and combined recording/constant reuse coverage mapped in the owner. Reused accepted Debug and unchanged Scene/settings/shader-catalog evidence; no new test or repeated old campaign. Missing normal Release (Tracy off) gate passes 371/371 enabled checks: 143 CPU + 228 native, no failures/errors/skips; 979 frozen inputs unchanged. Eight disabled benchmarks unrun. [Coverage](validation.md#ex051-12-integration-coverage), [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/integration12/checkpoint-manifest.json). Final performance remains13/GATE.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| EX051-13   | Final performance acceptance / rendering owner                                         | validated  | 12                   | Final GPU matrix: 48 runs/296,100 steady frames, 981 unchanged inputs; all 1080p whole-frame and attributed exposure/scaling GPU targets pass. Three I02 event scripts, 64 output pairs, zero steady churn, presentation and copy attribution pass. Joint 13A/B adds one matched 7,200-frame Tracy capture: active CPU p95/p99 0.705693/0.888333->0.514935/0.625473 ms, 27.0%/29.6% better; 403 Release owning checks pass. The 2026-09-21 operating point retains this CPU cost; VX-CPU-01 tracks further optimization and broader CPU qualification. Original CPU targets remain unmet. [Accepted disposition](validation.md#shared-recording-and-binding-correction), [checkpoint](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/acceptance13/checkpoint-manifest.json).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| EX051-14   | Owner and evidence closeout                                                            | validated  | 13                   | Existing owner, tracker, plan, performance report and checkpoint reconcile final measurements, implementation commit 22cea346b, raw paths/hashes and the explicit CPU acceptance. Further optimization is recorded under the later-milestone follow-up below. Slice 5.2 subsequently closed under its separate agreement and evidence below.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| EX051-13A  | Shared recording ownership / SceneRenderer + PostProcess + participating Vortex stages | validated  | 13 CPU diagnosis     | Approved move-only recording owner and borrowed stage API migration complete; successful submission commits history/status/cache publication, discard/failure preserves prior history and permits retry. Superseded deferred-queue API removed; default scope-exit submission remains ergonomic. C4456 and shared depth/HZB handoff corrected. Owning checks: 589 Debug and 403 Release pass; changed-code clang-tidy clean across 89 TUs. Matched 7,200-frame I02 Tracy comparison: exposure recordings 6->2, all render-thread recordings 40->10, elapsed CPU p95/p99 0.708597/0.901528->0.515917/0.630422 ms; six cycle ranges disjoint. Four output pairs, identical GPU work/placement, zero churn and 1,030 unchanged capture inputs. Accept joint A/B correction; active CPU disposition is closed by the acceptance in 13. [Decision and checkpoint](validation.md#shared-recording-and-binding-correction).                                                                                                                                                                                                                                                                                                                                                                                          |
| EX051-13B  | Root-signature and binding reuse / Graphics D3D12                                      | validated  | 13 CPU diagnosis     | Compatible complete layouts/flags share root-signature ownership; descriptor heaps/root tables reuse valid bindings and invalidate on real changes or recorder reset. Required PSO/root arguments and shader ABI preserved. Layout/flags/lifetime and five invalidation/reset checks pass within owning gates; 89-TU changed-code clang-tidy clean. In the joint matched candidate, nested compute-binding p95/p99 falls 0.070302/0.096123->0.061307/0.076787 ms with 12 calls per frame preserved. This attribution is nested, not an isolated B-only result. Accept with 13A; numerical/GPU/resource checks pass. [Decision and checkpoint](validation.md#shared-recording-and-binding-correction).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| EX051-GATE | Slice acceptance                                                                       | validated  | 12, 13, 14           | Closed 2026-09-21. Numerical/integration, native GPU/frame/scaling targets, transitions, resource stability, presentation and copy attribution are qualified. 13A/B has measured benefit and passes owning Debug/Release checks. Acceptance of measured CPU cost explicitly disposes the remaining CPU gate; future CPU optimization is deferred without claiming the original CPU targets passed.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |

## Deferred CPU optimization — later milestone — task evidence

| Follow-up                                                       | Status               | Retained scope and evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| --------------------------------------------------------------- | -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Active exposure CPU reduction and broader scaling qualification | deferred, 2026-09-21 | Start from implementation `22cea346b` and its accepted I02 1080p active mean/p95/p99 of 0.371156/0.514935/0.625473 ms. Preserve original one-view 0.10/0.20-ms and two-view 0.15/0.30-ms targets plus the 4K scaling criterion as future optimization goals. Scope/design and any API migration require review before coding. This is not part of Slice 5.2's behavior-preserving quality work, and does not reopen Slice 5.1. Reuse the saved Tracy capture, CPU comparison, GPU matrix and owning checks; schedule additional qualification only with that milestone. |
