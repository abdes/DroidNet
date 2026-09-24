# EX07F — Final acceptance

**Engine-side acceptance and documentation complete, 2026-09-25.** The user
confirmed RenderScene camera movement, resizing, scene switching and shutdown.
The user explicitly retained the remaining interactive editor check. EX07F and
the overall EX07 gate therefore await only that user-owned sign-off. The
subsequently reported test build failure is repaired and qualified in `4fd55cd8d`;
no engine implementation, benchmark capture or automated validation task remains.

## Evidence reuse and final-code applicability

No benchmarks or captures were repeated for F. The final E
implementation and its performance inputs are unchanged. A Git comparison from
E closure `0c6f60e34` to `cf1a103cc` finds only the import event-loop repair and
its regression test in production/test source. That change affects asynchronous
import, not rendering of the frozen cooked scenes; C already qualified it in
Debug and Release. Editor World, WorldEditor and Interop source is unchanged
since the C authoring implementation `2b61d102c`. The later F repair below changes
only three test files and does not invalidate rendering performance evidence.

The [reuse audit](validation/ex07f-20260925/reuse-audit.json) checks 85 retained
record hashes against E's accepted register, including 22 final benchmark rows,
58 passing reference images, four complete application captures with normal
exit, and 32 test-result records. This checks existing evidence; it is not a new
test execution or a claim that old binaries have newly built identities.
Overlapping test checkpoints are not summed as distinct tests.

| Requirement                            | Credited proof and final disposition                                                                                                                                                                                                                                                       |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Physical model, materials and ABI      | [A audit](EX07A-completion-audit.md), [B audit](EX07B-completion-audit.md), C transport/receiver checks and E's final native image/ABI qualification. Independent model-2 approximation limits remain explicit.                                                                            |
| Count, overlap, boundaries and culling | D's 27 presets in both families; E03's 26 interaction rows/40 unchanged images and four endpoints; later E interaction and final image tests cover changed shaders and sharing. Historical timings remain labelled by checkpoint.                                                          |
| Final Debug/Release correctness        | E's 494-case integrated Release checkpoint; final diagnostics delta of 81 cases in each Release tree and 82 in Debug; two retained-memory cases in all three configurations. [E report](EX07E-shadow-sharing-results.md) preserves the earlier owning-suite evidence.                      |
| Import, content and authoring          | [C closure](EX07C-completion-report.md): 759 fresh cases, 542 credited native Debug cases, 123 PakGen cases and 88 distinct managed/editor/native-bridge cases. Correct BC7/full-mip Sponza content matches E's accepted scene identities.                                                 |
| Lifecycle, failure and recovery        | E native debug-layer coverage of retained/unsubmitted readers, frame-slot rollover, budget failure/recovery, incompatible/reordered views, all surface consumers and actual release. C's I02 run covers all 16 lifecycle/exposure operations. RenderScene interactions are user-confirmed. |
| Performance and memory                 | Reuse all final E measurements below, their distributions, load checks and limitations. No new baseline or renewed timing campaign.                                                                                                                                                        |
| Visual acceptance                      | Final E Sponza/Instancing approval and numeric review remain valid. User additionally confirmed RenderScene interaction checks for F.                                                                                                                                                      |
| Interactive editor                     | **User-owned, pending.** Change directional intensity/color, observe viewport update, undo/redo, save/reopen. Automated command transport and round-trip proof remain credited; they do not claim this manual check was performed.                                                         |
| Operating documentation                | Current lighting/shadow/PCF contracts, benchmark commands and stage/deliverable tracker reconciled. Three reviewer documents remain uncommitted as instructed.                                                                                                                             |

## Build defect discovered during closeout

The user's broader Debug build found `ShadowAllocationRequirements_test.cpp`
still calling removed `AcquirePointSurface`/`AcquireSpotSurface` methods. The
initial engine-complete statement was premature and was withdrawn while this
missing E06 test migration was repaired.

The test now requests real per-light maps through `AcquireLocalMap`, exercises
physical chunk reuse/append and checks the same native D32 dimensions, layer
counts, allocation bytes and budget accounting. It polls managed retirements
before checking final release. The first migrated run correctly failed that
last assertion until this test lifecycle was updated; the rejected result is
retained. No allocation assertion was weakened, no legacy API was restored and
no production code changed. The two nodiscard warnings in the geometry catalog
test and two unused descriptor-size locals in PakPlanBuilder tests are also fixed.

All three owning targets build in **Debug and Release** in the existing
`out/build-ninja` tree. Focused execution passes **5/5 in each configuration**:
one native allocation test, two catalog cases and two affected PakPlanBuilder
cases. These are allocation/schema/plan tests, with no image capture or timing
campaign. Build logs, six passing JSON results, the rejected attempt and source/
executable hashes are preserved beside the [reuse audit](validation/ex07f-20260925/reuse-audit.json).
This qualifies the affected targets, not a claim that the user's entire `all`
build was rerun. Repair commit: `4fd55cd8d`.

## Accepted operating points

RTX 3080; existing Ninja Release trees. Native throughput and Tracy attribution
remain separate. The following native values are the accepted E records, not F
recaptures. Application scenes use 2560x1400; benchmark endpoints use 1920x1080.

| Native workload                                | Initial D mean ms | Final E mean / p95 / p99 ms | Change in mean             |
| ---------------------------------------------- | ----------------- | --------------------------- | -------------------------- |
| Sponza application                             | 62.229            | 29.451 / 31.000 / 32.000    | -52.67%                    |
| Instancing application                         | 27.416            | 14.606 / 16.000 / 17.000    | -46.72%                    |
| Sparse 4,096, deferred                         | 22.077            | 9.558 / 11.446 / 12.755     | -56.71%                    |
| Sparse 4,096, forward                          | 20.341            | 6.959 / 8.667 / 10.177      | -65.79%                    |
| 1,024 lights, 4 point/8 spot shadows, deferred | 10.341            | 5.056 / 5.423 / 5.877       | -51.11%                    |
| One light, deferred                            | 1.935             | 1.993 / 2.984 / 3.383       | +3.01%; within variability |

Application throughput is **33.95 FPS Sponza / 68.46 FPS Instancing**. Final
Tracy means are 29.975 / 15.346 ms respectively. Earlier cheaper PCF checkpoints
remain visible in the [complete comparison](EX07E-shadow-sharing-results.md):
the approved coherent cube hardware filter uses **1/5/29/29** comparisons and
has a real quality/correctness cost. No quality, range or contribution reduction
was used to manufacture the improvements.

Compatible two-view local shadows retain **40 MiB**, the same physical chunk
storage as the matching one-view shadow set: 24 aliases use 12 versions.
Moving compatible views need 12 writers for those 24 uses. Incompatible
half-resolution requests retain 50 MiB. The forced retained-reader case peaks
at 128 MiB and returns to 64 MiB after release; diagnostic retention keeps
exactly 6,291,456 closing native bytes until its final reference disappears.
All 22 final timed rows report zero steady buffer/texture creation.

Supported limits and retained costs:

- 1,024 mixed lights is the primary qualification workload; 4,096 lights,
  dense overlap, 4K and multiple views are scaling cases, not a universal 60 FPS
  promise. Earlier matrix rows retain their original source identity and timing;
  no unmeasured final timing is extrapolated for them.
- The configurable 4 GiB renderer allocation ceiling includes the 128 MiB
  aggregate index sublimit. Actual driver/resource availability also limits
  admission. These are ceilings, not allocations or throughput guarantees.
- Point/spot shadow counts are budget-driven; four/eight are benchmark subsets,
  not hard product limits. Explicit budget failure cannot silently drop shadows.
- Sharing requires identical local depth content and compatible allocation
  properties. Directional/contact products remain view-specific. Unsubmitted
  readers can require copy-on-write; backing-wide hazards can serialize work.
- Whole-process/device usage, native allocations, spare capacity and CPU payload
  bytes are distinct quantities. Debug CRT allocation counts are not Release
  heap totals. No indefinite application soak or whole-engine memory split is
  claimed from the bounded release tests.
- Noisy historical rows and differences below observed variability remain
  inconclusive. Full per-light Tracy overhead is not non-Tracy Release cost.

## Remaining handoff

Only the user's editor interaction sign-off remains. Record its outcome here and
in the [tracker](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items); if it passes,
close EX07F/EX07-GATE without rerunning unchanged benchmarks. If it exposes a
defect, repair and qualify that affected path. EX08 has not started.
