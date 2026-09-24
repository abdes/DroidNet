# EX07D — Many-light baseline and deferred optimization report

Date: 2026-09-24. **Evidence closeout in progress.** The initial baseline and
measured optimization are delivered, but this record is being expanded to cover
the benchmark, Instancing and New Sponza baselines required for E/F. The previous
D completion claim was premature. This report does not close EX07E/F
operating-limit, application/editor and final acceptance requirements.

**Application regression follow-up:** New Sponza's window-close resource-lifetime
repair is verified, and the general large-range shadow receiver-footprint
correction passes Debug/Release native tests. Performance remains open: the
current recooked scene uses the approved 4,096 m omitted-range fallback, while
the earlier application capture used 10 m local ranges. The many-light results
below remain scoped evidence, not application regression clearance. See the
[Tracy analysis and UE5.7 comparison](EX07-NewSponza-regression-analysis.md) for
the measured GPU costs, candidate fixes, recommendations and remaining gaps.

Vortex retains its **deferred-first desktop rendering contract**. The erroneous
Forward+-first documentation changes were reverted. Both rendering families
remain in the benchmark. Accepted MultiView, model-2 and conventional-shadow
evidence remains credited in the [implementation tracker](../IMPLEMENTATION_STATUS.md).

## Scene and established baseline

The shared native scene contains 1,024 lights (512 point / 512 spot), a neutral
XY receiver, 100 lm per light, 3 m range, a 60-degree camera and manual EV0 at
1920x1080. The primary guarantees 576 visible unshadowed contributors.
`LightingWorkloadScene` uses real scene nodes and production rendering;
the native preview and benchmark share its construction. Shadow presets include
raised occluders. Recipe revision 2 assigns stable central shadow owners that
remain visible during motion; the unshadowed recipe is unchanged.

The corrected-lifetime baseline is preserved in
[qualified-20260924](../../../out/analysis/ex07d/qualified-20260924), with
[16 completed count/family rows](../../../out/analysis/ex07d/qualified-20260924/accepted-summary.json).
The interrupted 4,096-light row is excluded. At the user's direction, work moved
from baseline collection to performance/correctness repair. Subsequent stress
rows qualify the candidate and do not retroactively expand baseline coverage.

## Matched optimization result

Both columns retain full Tracy collection, native stage timestamps and CPU
scopes for the same 1,024-light deferred scene.

| Metric                            | Established baseline | Optimized candidate |
| --------------------------------- | -------------------: | ------------------: |
| Frame mean                        |            59.530 ms |           24.327 ms |
| Frame p50                         |            58.921 ms |           24.421 ms |
| Frame p95                         |            67.179 ms |           26.621 ms |
| Frame p99                         |            70.629 ms |           27.440 ms |
| Frame maximum                     |            74.739 ms |           28.959 ms |
| CPU lighting scope union, mean    |            14.278 ms |            4.010 ms |
| GPU deferred lighting, mean       |            18.125 ms |           15.413 ms |
| Lighting allocation domain        |            3.445 MiB |           3.258 MiB |
| Lighting staging bytes/frame      |              368,999 |             287,079 |
| Steady buffer / texture creations |                0 / 0 |               0 / 0 |

Mean frame interval falls **59.1%**, with identical primary pixels. The
established forward control was 8.311 ms with full Tracy collection.
The first batch-retirement candidate measured 26.834 ms at 1,024 lights and
288.300 ms at 4,096; the final fully traced 4,096-light candidate is 39.163 ms.
The 288.300 ms result is an intermediate candidate, not a completed original
4,096-light baseline. See the [matched candidate table](../../../out/analysis/ex07d/optimization-submission/REPORT.md)
and [summary](../../../out/analysis/ex07d/optimization-submission/summary.json).

## Tracy overhead and Release operating point

Full per-light GPU tracing materially changes this workload. The current Tracy
build uses `TRACY_ENABLE` without `TRACY_ON_DEMAND`. Oxygen opens every
diagnostic scope, including a timestamp pair per local-light draw, even without
a connected Tracy client. This is profiling overhead, not an inherent
requirement of deferred rendering. Dense GPU timestamp instrumentation can
incur overhead in other profilers too; it is not unique to Tracy.

Separate Release measurements disable Tracy at build time while retaining
native stage timestamps, the bounded CPU observer and image qualification:

| Lights | Family   | Frame mean |       p95 |       p99 |   Maximum | GPU lighting/base mean | GPU grid mean |
| -----: | -------- | ---------: | --------: | --------: | --------: | ---------------------: | ------------: |
|  1,024 | Deferred |   9.999 ms | 11.291 ms | 11.830 ms | 12.119 ms |               3.057 ms |      3.336 ms |
|  1,024 | Forward  |   7.230 ms |  8.686 ms |  9.350 ms |  9.935 ms |               2.059 ms |      2.630 ms |
|  4,096 | Deferred |  22.001 ms | 24.948 ms | 25.654 ms | 26.354 ms |               3.796 ms |     10.998 ms |
|  4,096 | Forward  |  20.718 ms | 23.689 ms | 24.625 ms | 30.362 ms |               2.252 ms |     11.287 ms |

The primary images are identical to the established baseline in both families.
**Do not report 59.530-to-9.999 ms as a same-instrumentation renderer speedup.**
The matched optimization claim is the fully traced comparison above. The
submission/profiling fixes retain every diagnostic scope.
See the [Release table](../../../out/analysis/ex07d/optimization-production/REPORT.md)
and [summary](../../../out/analysis/ex07d/optimization-production/summary.json).

At 4,096 lights, the shared spatial-grid build now costs approximately 11 ms in
both families and dominates their remaining GPU cost. Its existing per-cell
scan of selected lights remains. This work does not claim UE5.7 performance
parity, a new grid algorithm or final EX07 scaling closure.

## Implemented fixes

1. **Staging growth lifetime:** old ring-buffer backing resources and published
   descriptor views now unregister together after the owning frame slot
   retires. Previously, immediate descriptor unregistration left queued GPU
   readers using recycled indices, corrupting cached shadow maps. The new
   regression failed before the repair and passes afterward.
2. **Shadow descriptor indexing:** directional, projected-local and cube-local
   shadow texture accesses declare `NonUniformResourceIndex`, as required when
   light selection varies within a wave. Both this and the staging-lifetime
   repair are retained; the original image failure is not attributed
   exclusively to either defect.
3. **Descriptor retirement:** deferred CBVs retire in one registry batch.
   Previously the descriptor/cache maps were scanned once per light, producing
   quadratic work.
4. **Descriptor reuse:** immutable CBV descriptions are reused only for matching
   backing-buffer ranges in safely retired frame slots. Current values are
   always written; queued views retain separate allocations. Overlapping,
   changed and unused ranges retire safely.
5. **Conservative deferred draw culling:** skip only draws whose complete finite
   influence sphere cannot intersect the current view, with outward numerical
   tolerance. Global light publication, shadow eligibility and off-screen
   caster selection remain unchanged.
6. **Spot volume correctness:** camera-inside classification uses the actual
   outer-cone cosine instead of an unrelated value derived from inverse
   penumbra width. Hard and soft cones share the correct geometric boundary.
7. **Submission and profiling:** reuse framebuffer/viewport and compatible
   pipeline state without reordering contributions. GPU scope storage grows
   geometrically while reserving before collectors open. Previous exact-size
   growth repeatedly copied every earlier light's scope record.

## UE5.7 culling correspondence

Local UE5.7 source at
`F:/Epic Games/UE_5.7/Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp:5630-5658`
tests point/spot/rect light bounding spheres against the view culling frustum.
`LightRendering.cpp:2856-2859` checks per-view eligibility before deferred
drawing. An off-screen light origin is not sufficient for rejection.

Oxygen follows this conservative volume-intersection principle. Its
center-based range sphere contains the entire supported point/spot influence;
for spots it is deliberately looser than a tight cone bound. UE's additional
screen-size/max-distance quality cutoffs are not imported. This is
correspondence for conservative rejection, not a claim that every UE policy
or bounding construction is identical.

The native regression places both a point light and its complete caster outside
the camera frustum, while the light's influence reaches a visible receiver.
In both deferred and forward rendering, the receiver is lit, becomes shadowed
when the off-screen caster is enabled, and returns to the same lit value when
disabled. The test verifies the light draw and shadow publication remain.
Core tests cover perspective, reverse-Z and orthographic culling while keeping
global light selection intact.

## Validation and limits

Scoped Debug and Release builds succeed, including the benchmark previously
blocked by private diagnostic API access. Test results:

| Suite                                                    | Tests per configuration |
| -------------------------------------------------------- | ----------------------: |
| Lighting service, including descriptor-cache regressions |                      34 |
| Deferred renderer core                                   |                      71 |
| Ring-buffer staging                                      |                      20 |
| Resource registry                                        |                      61 |
| GPU profiling scope lifetime                             |                       5 |
| Workload/instrumentation                                 |                      18 |
| Native lighting/material/grid/shadow images              |                      17 |

Five Python analysis-tool tests also pass. Native image tests use the D3D12
debug layer; timed benchmarks disable it.

[Candidate envelope qualification](../../../out/analysis/ex07d/optimized-envelope-qualification/qualification-summary.json)
passes **36 family/preset rows and 56 images**, all identical to their complete-list
references. Coverage includes dense 33/64/256, irrelevant 1,024/4,096, moving
1,024/4,096, 4K sparse/dense, two views, orthographic, point/spot shadow limits,
moving shadows, finite sources and wide spots. Eight historical shadow-row
comparisons are also exact. The final primary images and traced 4,096-light
candidate are exact against their preserved earlier captures.

The complete-list fallback retains spatial empty-cell rejection. These checks
supplement B/C's independent physical oracles, boundary tests and the new
off-screen-shadow regression. The unchanged general budget is 0.5% relative plus
2e-5 absolute; exact equality is observed, not a relaxed acceptance threshold.
Wider candidate rows are image-qualified, not all timed or user visually approved.

Hardware: RTX 3080, Ryzen 9 9950X, driver 610.62, optimized Release. Static timed
rows warm 120 frames plus 12 after readbacks and measure 240 frames; the primary
measures 480. Readbacks and resource inventories are untimed. Intervals are
native offscreen renderer measurements, not presented FPS. GPU frame envelopes
include queue gaps; CPU scopes are elapsed-time unions, not active CPU time.
Nested scope totals must not be added together.

Consecutive-block variability for the traced primary is 8.12% before and 3.49%
after. Release without Tracy is 3.09% deferred / 2.49% forward at 1,024 lights,
and 10.25% / 6.68% at 4,096. These are within-run ranges, not between-run
confidence intervals. Small differences within that variation are inconclusive.
Whole-renderer local allocation remains 652.95 MiB deferred / 615.45 MiB forward
in the primary; all four final timed rows have zero steady buffer/texture
creation. The lighting domain is a subset of whole-renderer memory.

## Reproduction and evidence

Run directories preserve executable/DLL/shader/recipe hashes, source snapshots
and working-tree patch, requests, launch arguments, hardware samples, raw
RGBA32F images, inspection PNGs, CPU CSVs and native GPU JSON. Fully traced runs
also preserve `.tracy` captures. Final scoped test JSON/logs are under
`out/analysis/ex07d/final-*`.

From the engine root, with `numpy`, `Pillow` and `jsonschema` available:

```powershell
# Matched full tracing; use the configured Tracy Release build.
python tools/vortex/RunManyLightBaseline.py --build-tree build-tracy-ninja --output out/analysis/ex07d/<new-traced-run> --cases sparse-1024 sparse-4096 --families deferred --tracy-capture <matching-tracy-0.13.1-capture.exe>

# Native stage timing, with Tracy disabled in this Release build.
python tools/vortex/RunManyLightBaseline.py --build-tree build-ninja --output out/analysis/ex07d/<new-release-run> --cases sparse-1024 sparse-4096 --families deferred forward
python tools/vortex/SummarizeManyLightBaseline.py out/analysis/ex07d/<new-release-run>
```

These build-tree names identify the verified local configurations; their
`OXYGEN_WITH_TRACY` values are respectively `ON` and `OFF`. Use
`--qualify-only` for image checks without timing. `--resume` accepts completed
rows only with matching frozen binary/shader/recipe identities. Use a new output
directory after a build change. Inspection PNGs use x/(1+x), gamma 2.2;
numerical comparisons use the original float images.
