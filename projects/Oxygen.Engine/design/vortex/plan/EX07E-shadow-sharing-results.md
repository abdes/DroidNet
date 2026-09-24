# EX07E — Shadow sharing implementation and baseline comparison

**EX07E closed (2026-09-25). S1–S9 implementation and qualification are complete.
The user approved visual acceptance, reviewed the before/after numbers and
authorized structured commits. Accepted evidence: `b8f1376e1`.**

The non-Tracy Release integrated checkpoint passed **494/494 tests**. The final
diagnostics addition then passed **5 submission + 50 shadow-service + 26 native
image tests in each Release tree**, and **6 + 50 + 26 in Debug**. The extra Debug
test denies allocation after native issue and during retirement. Earlier Nexus,
descriptor, backend-close, readback and failure/recovery results remain credited;
the implementation plan records their owning suites. The accepted MSVC iterator
proxy-sized construction-allocation test exclusion remains explicit and narrow.

## Native Release benchmarks

Existing `out/build-ninja`, Tracy OFF. All rows pass complete-list image
qualification, CPU/GPU preflight, frozen input checks and zero steady buffer/texture
creation. These are offscreen frame intervals, not application FPS. Every frame is
retained. The final source includes the ownership diagnostics; snapshots and image
readback run outside the timed window.

`shadows-*` retains the existing 1,024-light recipes. The new sharing controls
use 64 lights with four point/eight spot maps, except the two-light small control
with one map of each kind. Matched, offset/partial and half-resolution layouts are
recorded explicitly in each request. These are different lighting workloads:
do not attribute their whole-frame differences solely to adding a view.

| Workload / path                      | Mean / p95 / p99 ms      | Writers | Shadow-stage GPU ms | Map / backing pins | Lighting MiB | Block spread |
| ------------------------------------ | ------------------------ | ------- | ------------------- | ------------------ | ------------ | ------------ |
| shadow-share-incompatible / deferred | 12.082 / 18.973 / 23.985 | 0.000   | 0.000               | 24 / 4             | 55.070       | 34.88%       |
| shadow-share-moving / deferred       | 5.637 / 7.663 / 8.510    | 12.000  | 0.154               | 24 / 4             | 44.883       | 3.17%        |
| shadow-share-moving / forward        | 4.895 / 6.391 / 6.840    | 12.000  | 0.159               | 24 / 4             | 44.695       | 1.32%        |
| shadow-share-partial / deferred      | 10.080 / 11.382 / 12.047 | 0.000   | 0.000               | 23 / 4             | 46.008       | 1.92%        |
| shadow-share-small / deferred        | 3.601 / 4.942 / 5.316    | 0.000   | 0.000               | 4 / 4              | 44.070       | 2.87%        |
| shadow-share-static / deferred       | 5.021 / 6.780 / 7.288    | 0.000   | 0.000               | 24 / 4             | 44.883       | 1.83%        |
| shadow-share-static / forward        | 4.366 / 6.321 / 7.712    | 0.000   | 0.000               | 24 / 4             | 44.695       | 19.91%       |
| shadows-1-1 / deferred               | 6.467 / 8.153 / 9.187    | 0.000   | 0.000               | 2 / 2              | 43.258       | 11.49%       |
| shadows-4-8 / deferred               | 5.056 / 5.423 / 5.877    | 0.000   | 0.000               | 12 / 2             | 43.258       | 2.13%        |
| shadows-moving / deferred            | 6.525 / 7.803 / 8.576    | 12.000  | 0.145               | 12 / 2             | 43.258       | 2.68%        |
| sparse-1 / deferred                  | 1.993 / 2.984 / 3.383    | 0.000   | 0.000               | 0 / 0              | 2.008        | 5.51%        |
| sparse-4096 / deferred               | 9.558 / 11.446 / 12.755  | 0.000   | 0.000               | 0 / 0              | 4.133        | 3.20%        |
| sparse-4096 / forward                | 6.959 / 8.667 / 10.177   | 0.000   | 0.000               | 0 / 0              | 3.445        | 5.16%        |

Compatible warm views record zero shadow writers. The moving matched pair
records 12 writers for 24 view-local map uses; four backing pins represent two
deduplicated registrations in each of the two reader recordings. Partial overlap
has 23 uses; incompatible resolutions remain separate. No historical timed
counterpart exists for the new sharing recipes, so no isolated FPS-saving
percentage is invented for them.

## Initial D and pre-E06 controls

Only matching case/path recipes are compared with D. This measures the cumulative
E candidate, including the approved PCF contract and earlier grid/shader/CPU work.

| Workload / path           | D mean ms | Final mean ms | Change  |
| ------------------------- | --------- | ------------- | ------- |
| shadows-1-1 / deferred    | 10.622    | 6.467         | -39.12% |
| shadows-4-8 / deferred    | 10.341    | 5.056         | -51.11% |
| shadows-moving / deferred | 11.462    | 6.525         | -43.07% |
| sparse-1 / deferred       | 1.935     | 1.993         | +3.01%  |
| sparse-4096 / deferred    | 22.077    | 9.558         | -56.71% |
| sparse-4096 / forward     | 20.341    | 6.959         | -65.79% |

The closest pre-E06 native sparse controls are **1.896 ms** (one light) and
**9.245 ms** (4,096 lights), from `e824c97a7`. The first E06 4,096-light run was
10.742 ms with 9.30% block spread while unrelated build activity later tripped
the preflight gate. One bounded repeat after that build finished measured
**9.741 ms** (+5.37%) with **6.68%** spread; CPU lighting union was **2.334 ms**
versus **2.295 ms** (+1.68%). The one-light repeat was **1.934 ms** (+1.97%) with
**6.62%** spread. These residual differences do not establish an isolated code
regression at that precision. Original runs remain in the durable record.
The final counter-bearing results above are a separate source checkpoint,
not replacements selected to hide the earlier slowdown.

| Native deferred endpoint | Pre-E06 mean ms | Final mean ms | Change | Pre-E06 / final CPU lighting union ms |
| ------------------------ | --------------- | ------------- | ------ | ------------------------------------- |
| sparse-1                 | 1.896           | 1.993         | +5.09% | 0.136 / 0.155                         |
| sparse-4096              | 9.245           | 9.558         | +3.39% | 2.295 / 2.161                         |

## Tracy Release attribution

Existing `out/build-tracy-ninja`, Tracy ON. Treat these as attribution captures,
not native throughput or the expected cost of a non-Tracy release. Nested CPU/GPU
scopes are inclusive and must not be added together.

| Workload                  | Mean / p95 / p99 ms      | Preparation ms | Queue lock wait / held ms | Registry pin lock wait / held ms | Block spread |
| ------------------------- | ------------------------ | -------------- | ------------------------- | -------------------------------- | ------------ |
| shadow-share-incompatible | 32.705 / 35.875 / 36.421 | 0.120          | 0.002 / 0.101             | 0.002 / 0.003                    | 3.17%        |
| shadow-share-moving       | 8.653 / 10.417 / 10.946  | 0.038          | 0.009 / 0.291             | 0.010 / 0.009                    | 1.14%        |
| shadow-share-partial      | 30.781 / 33.324 / 35.726 | 0.115          | 0.002 / 0.098             | 0.002 / 0.002                    | 3.58%        |
| shadow-share-small        | 4.134 / 5.853 / 6.278    | 0.010          | 0.001 / 0.062             | 0.002 / 0.002                    | 4.90%        |
| shadow-share-static       | 7.935 / 10.119 / 10.966  | 0.042          | 0.002 / 0.084             | 0.002 / 0.002                    | 14.40%       |
| shadows-4-8               | 16.516 / 21.691 / 25.260 | 0.066          | 0.001 / 0.063             | 0.001 / 0.001                    | 35.57%       |
| shadows-moving            | 19.865 / 24.565 / 26.524 | 0.069          | 0.008 / 0.346             | 0.009 / 0.009                    | 14.36%       |
| sparse-1                  | 2.148 / 3.302 / 3.633    | 0.002          | 0.001 / 0.041             | 0.001 / 0.001                    | 4.66%        |
| sparse-4096               | 24.172 / 27.163 / 28.615 | 0.165          | 0.001 / 0.069             | 0.001 / 0.001                    | 1.84%        |

Lock values are accumulated per frame, including native queue submission
inside the queue critical section. They are not all mutex contention. The numeric
records retain full per-stage GPU and CPU distributions, staging traffic,
allocator committed/slack bytes and process device-memory observations.

## Ownership, memory and submission costs

Each unique native resource is counted once from D3D12 allocation information.
Pending-retirement bytes are a subset of native bytes, never an extra total.
Spare capacity excludes closing chunks and layers still held by retiring slots.
The table samples the end of each timed window; budget peaks are separately
retained in the numeric records.

| Workload / path                      | Shadow native / spare / pending MiB | Aliases / live versions | Canonical records / bytes | Version / prepared payload bytes |
| ------------------------------------ | ----------------------------------- | ----------------------- | ------------------------- | -------------------------------- |
| shadow-share-incompatible / deferred | 50.000 / 45.625 / 0.000             | 24 / 24                 | 13 / 2704                 | 16192 / 28528                    |
| shadow-share-moving / deferred       | 40.000 / 36.500 / 0.000             | 24 / 12                 | 13 / 2704                 | 8112 / 28912                     |
| shadow-share-moving / forward        | 40.000 / 36.500 / 0.000             | 24 / 12                 | 13 / 2704                 | 8112 / 28912                     |
| shadow-share-partial / deferred      | 40.000 / 36.500 / 0.000             | 23 / 12                 | 13 / 2704                 | 8096 / 28384                     |
| shadow-share-small / deferred        | 40.000 / 39.375 / 0.000             | 4 / 2                   | 3 / 624                   | 1296 / 4208                      |
| shadow-share-static / deferred       | 40.000 / 36.500 / 0.000             | 24 / 12                 | 13 / 2704                 | 8096 / 28528                     |
| shadow-share-static / forward        | 40.000 / 36.500 / 0.000             | 24 / 12                 | 13 / 2704                 | 8096 / 28528                     |
| shadows-1-1 / deferred               | 40.000 / 39.375 / 0.000             | 2 / 2                   | 3 / 624                   | 1296 / 2104                      |
| shadows-4-8 / deferred               | 40.000 / 36.500 / 0.000             | 12 / 12                 | 13 / 2704                 | 8096 / 14264                     |
| shadows-moving / deferred            | 40.000 / 36.500 / 0.000             | 12 / 12                 | 13 / 2704                 | 8144 / 14480                     |

The 4-point/8-spot one-view case and the compatible two-view sharing case use
the same **40 MiB** of physical shadow chunks. Twelve maps occupy **3.5 MiB** of
layers, leaving **36.5 MiB** spare under the unchanged chunk policy. A second view
at incompatible half resolution adds **10 MiB**, giving **50 MiB** total. This
is allocation evidence; warm cached maps do not imply a raster-time saving.

The native tight-budget test rejects spare capacity, admits exactly six cube
layers, removes the light and advances five frames. It then observes
**6,291,456 closing native bytes** retained by a diagnostic texture reference;
the charge and observed backing disappear after the last reference is released.
The retained-content test also reads the original depth after five frame-slot
rollovers and distinguishes it from updated content. Debug-layer tests cover
the cold, warm, reordered, incompatible, deferred, forward and translucent paths.

The forced copy-on-write native case holds an unsubmitted retained readback while
advancing five frame slots and changing the caster. It measures exactly **two
versions/two chunks, 128 MiB**, then returns to **one version/one chunk, 64 MiB**
after releasing the retained content and advancing five more frames. The old
depth remains readable and differs from the new depth; one additional
reader-blocked copy-on-write decision is recorded. This is a bounded peak and
release measurement, separate from the warm moving-light in-place results.

CPU payload columns include actual canonical record objects, immutable version
objects and vector capacities; they exclude allocator/control-block/node overhead.
They are not whole-heap totals. Debug allocation diagnostics include iterator
support and count shared-CRT allocations on the rendering thread only; per-mode
warm counts are recorded in the test JSON and summarized below.

| Selected native deferred row | Lighting live / peak MiB | Lighting / shared staging KiB per frame |
| ---------------------------- | ------------------------ | --------------------------------------- |
| shadow-share-moving          | 44.883 / 45.133          | 70.214 / 53.135                         |
| shadow-share-static          | 44.883 / 45.133          | 62.245 / 53.135                         |
| sparse-4096                  | 4.133 / 6.383            | 590.351 / 18.880                        |

Whole-allocator inventory for the final 4,096-light deferred row (not lighting-only storage):

| D3D12 segment | Allocation / committed / spare block MiB |
| ------------- | ---------------------------------------- |
| local         | 652.949 / 652.949 / 0.000                |
| non_local     | 32.250 / 128.375 / 96.125                |

| Debug warm two-view path | Allocations/frame range | Requested bytes/frame range |
| ------------------------ | ----------------------- | --------------------------- |
| Deferred                 | 11078–11548             | 554546–601740               |
| Forward                  | 9420–9488               | 484086–486166               |
| Translucent              | 9424–9485               | 497042–500338               |

Cumulative counters below are differenced across the timed window and divided
by its frame count. Queue counters describe successfully accepted graphics-queue
work, including non-shadow passes; private receipt markers exclude existing
legacy/frame-end signals. Uncertain submissions are tested separately and are
not reported as successful work. Cache misses are acquisition decisions and can
precede a failed producer; these qualified rows have no such failures.

| Workload / path                      | Hits / misses per frame | In-place / COW per frame | Accepted batches / private signals / dependency waits |
| ------------------------------------ | ----------------------- | ------------------------ | ----------------------------------------------------- |
| shadow-share-incompatible / deferred | 24.000 / 0.000          | 0.000 / 0.000            | 6.000 / 4.000 / 0.000                                 |
| shadow-share-moving / deferred       | 12.000 / 12.000         | 12.000 / 0.000           | 18.000 / 16.000 / 0.000                               |
| shadow-share-moving / forward        | 12.000 / 12.000         | 12.000 / 0.000           | 18.000 / 16.000 / 0.000                               |
| shadow-share-partial / deferred      | 23.000 / 0.000          | 0.000 / 0.000            | 6.000 / 4.000 / 0.000                                 |
| shadow-share-small / deferred        | 4.000 / 0.000           | 0.000 / 0.000            | 6.000 / 4.000 / 0.000                                 |
| shadow-share-static / deferred       | 24.000 / 0.000          | 0.000 / 0.000            | 6.000 / 4.000 / 0.000                                 |
| shadow-share-static / forward        | 24.000 / 0.000          | 0.000 / 0.000            | 6.000 / 4.000 / 0.000                                 |
| shadows-1-1 / deferred               | 2.000 / 0.000           | 0.000 / 0.000            | 4.000 / 2.000 / 0.000                                 |
| shadows-4-8 / deferred               | 12.000 / 0.000          | 0.000 / 0.000            | 4.000 / 2.000 / 0.000                                 |
| shadows-moving / deferred            | 0.000 / 12.000          | 12.000 / 0.000           | 16.000 / 14.000 / 0.000                               |

## Application scenes

Captured implementation in the existing Ninja Release builds; 2560x1400,
uncapped, VSync/debug layer off,
frozen D settings/camera, 20–50 s of each 55 s capture. Native intervals use the
1 ms CPU scene-completion log method; Tracy intervals use GPU frame markers.
Compare within each mode. All four final runs verify binary/input hashes before
and after, retain slow frames, and close normally with exit code 0.

| Scene / mode        | D mean ms | Final mean / p95 / p99 ms | Final FPS | Change vs D | Block spread |
| ------------------- | --------- | ------------------------- | --------- | ----------- | ------------ |
| Sponza / native     | 62.229    | 29.451 / 31.000 / 32.000  | 33.95     | -52.67%     | 0.70%        |
| Sponza / tracy      | 63.492    | 29.975 / 30.607 / 31.169  | 33.36     | -52.79%     | 0.45%        |
| Instancing / native | 27.416    | 14.606 / 16.000 / 17.000  | 68.46     | -46.72%     | 0.29%        |
| Instancing / tracy  | 36.319    | 15.346 / 15.895 / 16.297  | 65.16     | -57.74%     | 0.17%        |

| Scene      | GPU stage                       | D mean ms | Final mean ms |
| ---------- | ------------------------------- | --------- | ------------- |
| Sponza     | Vortex.Stage8.ShadowDepths      | 3.470     | 3.861         |
| Sponza     | Vortex.Stage12.DeferredLighting | 46.309    | 17.651        |
| Sponza     | Vortex.Stage18.Translucency     | 7.726     | 3.420         |
| Instancing | Vortex.Stage8.ShadowDepths      | 0.288     | 0.207         |
| Instancing | Vortex.Stage12.DeferredLighting | 32.413    | 13.481        |

The earlier accepted matrix-access checkpoint was cheaper: Sponza native
**23.735 ms**, Tracy **24.327 ms**; Instancing native **13.677 ms**, Tracy
**14.634 ms**. The later coherent hardware-PCF implementation retains the user's
approved Low/Medium/High/Ultra **1/5/29/29** comparison mapping. Its correctness
cost is not hidden by reporting only gains versus D. The closest pre-E06 Sponza
Tracy candidate was **36.519 ms**. The earlier E06 application pass, before the
diagnostics addition, was native **29.366 ms** and Tracy **29.753 ms** for Sponza,
and **16.434 / 15.347 ms** for Instancing; those captures also remain available.

## Decisions and operating limits

- Keep compatible per-light sharing, exact caster/content identity and the
  prepared submission/lifetime contract. No off-screen caster/light rejection,
  range reduction, shadow disablement, light-count truncation or PCF-quality
  reduction was introduced to obtain these results.
- Directional cascades remain view-specific. Point/spot maps share only when
  content, resolution, format, depth contract and caster dependencies agree.
  Unknown sampled-texture continuity prevents reuse conservatively.
- Retained logical readers and unsubmitted backing users can force copy-on-write.
  Submitted cross-queue users require receipt ordering. Chunk-wide hazards can
  serialize otherwise disjoint slots; this is a documented correctness cost.
- The existing lighting budget bounds native allocations, including retained
  resources. Exact-layer fallback avoids rejecting a map merely for spare
  capacity. Budget exhaustion remains an explicit preparation failure; it does
  not silently drop requested shadow maps.
- Small timing differences below observed variability remain inconclusive.
  Background desktop GPU activity is allowed; overloaded CPU windows delay
  timing. Preflight does not prove absence of interference during a capture.
- No further filter-quality or allocator-policy experiment is part of this
  closure. [F engine-side acceptance](EX07F-acceptance-report.md) subsequently
  credits these results; only the user-owned editor sign-off remains. The C
  caller/importer follow-up is [closed](EX07C-completion-report.md).

## Acceptance and durable evidence

**Manual visual acceptance approved by the user on 2026-09-25:** "Visual acceptance is approved."
The requested numeric before/after summary was delivered, and the user then
authorized structured commits. Approval applies to the final Sponza/Instancing
implementation and comparison records. The three reviewer documents remain
excluded from commits as requested. Historical candidate registers retain their
capture-time disposition; the final accepted register owns current acceptance.

Final screenshots: [Sponza](baselines/ex07e-20260925/cross-view-sharing/qualified-scenes/NewSponza_Main_glTF_003-native/scene.png)
· [Instancing](baselines/ex07e-20260925/cross-view-sharing/qualified-scenes/InstancingTestScene-native/scene.png)
· [Shared-map benchmark](baselines/ex07e-20260925/cross-view-sharing/qualified-native/shadow-share-static-deferred/phase-0-view-0.png)
· [Second benchmark view](baselines/ex07e-20260925/cross-view-sharing/qualified-native/shadow-share-static-deferred/phase-0-view-1.png).

[Numeric records, compressed traces, images, frozen source and checksums](baselines/ex07e-20260925/cross-view-sharing/register.json)
· [Initial D register](EX07D-baseline-report.md)
· [Earlier E comparison report](EX07E-optimization-report.md)
· [Authoritative tracker](../IMPLEMENTATION_STATUS.md#ex07e--work-items-and-resume-checkpoint)

The Markdown tables preserve the essential results even if `out/analysis` is
deleted. The durable register includes full distributions, recipes, source and
binary identities, images, load windows and owning test JSON. Large timing streams
and Tracy files are losslessly gzip-compressed; decompress before using the
existing analysis tools. Raw float-image arrays remain transient, while their
qualification results and display images are retained.

After capture, repository formatting changed whitespace in two C++ files, and
the existing retained-readback test gained explicit copy-on-write peak/release
assertions. The exact formatting diff and supplemental test source/results are
archived. No product behavior, shader, quality setting or scene input changed;
captured binary hashes are preserved as captured, never rewritten to match a
later build. Both Release application targets are rebuilt for manual validation.

## Commit sequence

| Commit      | Scope                                                                                   |
| ----------- | --------------------------------------------------------------------------------------- |
| `dbf226cda` | Nexus slot retirement, prepared deferred cleanup and caller migration                   |
| `cbe5ba31d` | Managed Graphics/backend lifetime, actual-completion submission and readback            |
| `d002d9ec1` | Canonical cross-view shadow sharing and all consumers                                   |
| `6cbd9e455` | Existing benchmark workload and ownership instrumentation extensions                    |
| `b8f1376e1` | Accepted capture evidence, original controls, source identities and checksum protection |

The E documentation commit records its closure. Subsequent
[F acceptance](EX07F-acceptance-report.md) reuses this evidence without recapture.
Pre-commit also reformatted the Loader test CMake file and benchmark JSON schema;
byte/token-equivalence checks and exact diffs are preserved in
`validation/commit-hook-format-only.json`. Immutable capture fixtures are excluded
from automatic normalization/formatting; ordinary product code and these Markdown
reports continue through the standard hooks.
