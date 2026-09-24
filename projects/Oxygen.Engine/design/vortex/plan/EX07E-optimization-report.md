# EX07E — Implementation and comparison record

**EX07E remains in progress. The matrix-access improvement is accepted.**
The user manually approved Sponza and Instancing on 2026-09-24 before committing
these application comparison records. Later changed baselines still require
manual visual validation. The [E01–E08 ledger](../IMPLEMENTATION_STATUS.md#ex07e--work-items-and-resume-checkpoint)
owns remaining work; this result does not close the whole E stage.

Initial evidence: [EX07D register](EX07D-baseline-report.md), renderer
`137b681b2`, evidence `894a25e57`. New durable evidence:
[matrix-access register](baselines/ex07e-20260924/matrix-access/register.json),
including complete numeric records, traces, screenshots, source patch and tests.
Frozen settings/layout bytes are reused from D. The initial baseline is retained.

## Application results

All runs use 2560x1400, uncapped presentation, VSync/debug layer off and the same
20–50-second steady interval. Native Release uses 1 ms scene-completion log
intervals; Tracy Release uses GPU frame intervals. Compare within the same mode.
Nested GPU stages are not additive. Both trees are the existing Ninja trees.

| Scene / mode        | Initial D mean ms | Matrix-access mean ms | Initial D FPS | Matrix-access FPS | Candidate p95 ms |
| ------------------- | ----------------: | --------------------: | ------------: | ----------------: | ---------------: |
| Sponza / native     |            62.229 |                23.735 |         16.07 |             42.13 |           29.000 |
| Sponza / Tracy      |            63.492 |                24.327 |         15.75 |             41.11 |           25.020 |
| Instancing / native |            27.416 |                13.677 |         36.48 |             73.12 |           23.000 |
| Instancing / Tracy  |            36.319 |                14.634 |         27.53 |             68.34 |           15.119 |

| Traced GPU stage  | Sponza D -> candidate ms | Instancing D -> candidate ms |
| ----------------- | -----------------------: | ---------------------------: |
| Point lights      |         45.288 -> 11.593 |              26.433 -> 9.920 |
| Deferred lighting |         46.309 -> 12.580 |             32.413 -> 10.986 |
| Translucency      |           7.726 -> 1.940 |                 Not executed |

All 23 Sponza and 39 visible Instancing point draws remain present. The shared
cube-shadow consumer also explains the translucent-path benefit; its actual
shader/filter/BRDF residual costs still belong to E02/E04.

**Matched shader-only control:** some runtime DLL hashes differ from D, so a
native Sponza control uses exactly the candidate's runtime DLLs, scene and
settings with the original shader archive. It measures **65.277 ms mean / 68 ms
p95 / 15.32 FPS**, versus **23.735 ms / 29 ms / 42.13 FPS** for the candidate:
**63.64% less mean frame time** within this pair. The records differ only in
`shaders.bin`. The control archive SHA256 equals D's original archive hash
`3764add3b75fcd4835589a3735a855b7b665dbbdd0208889df0592906fbf0480`.

**Confidence limits:** all slow frames are retained. Native Sponza has a 275 ms
maximum and 12.81% block-mean spread; native Instancing has a 39 ms maximum and
17.89% spread. These desktop captures cannot establish small-delta precision.
Traced block-mean spreads are 0.506% and 0.359%; matched original-shader native
control spread is 0.340%. Preflight allowed ordinary desktop activity. No
blanket baseline repeat or requirement for an idle machine was introduced.

The first Instancing native attempt is excluded because the user rebuilt through
`oxyrun` during capture and the backend DLL hash changed. `oxyrun` builds the full
target; the initial E build had updated only ShaderBake. Full RenderScene builds
in both existing Ninja Release trees and native benchmark/test targets now pass.
The replacement Instancing runs pass before/after identity checks and close with
exit code 0. Original capture identities remain recorded, not retroactively
rewritten to match the final build.

## Correctness and manual visual approval

- The user approved Sponza: "visual confirmation is good."
- The user approved Instancing: "visual confirmation for instancing scene is also good".
- All 17 Release native image tests pass, including point/spot, short/long-range,
  material, forward/deferred and off-screen contributor controls.
- The rebuilt GPU ABI probe passes its selected face-accessor test. The earlier
  invocation used a cached probe; `face-accessor-built-test.json` is authoritative.
- Ten selected synthetic rows pass; all 12 timed RGBA32F images are byte-identical
  to the initial D images and their current complete-list references. Every timed
  window has zero new buffers and zero new textures.
- Both application scenes close successfully. In 2584x1464 window screenshots,
  scene region `[y=160:1419, x=740:2559]` excludes UI/FPS/borders: Sponza matches D
  exactly; Instancing differs by at most one RGB byte, mean 0.000143 byte/channel.
  These display-image checks supplement numeric tests and user approval.

The production shader archive is
`c1d6ba259d909a563cef9ffe9b1a4febd3f4e72c8a0f420bd0eddd5c2a7accf2`.
The engine ShaderBake rebuild succeeds with 218 modules. No range, bias, filter,
resolution, shadow request or culling policy was changed.

## E01 candidate 1: select cube-shadow matrices in the buffer

The production point-shadow consumer copied a 448-byte `CubeLocalShadowRecord`
and then indexed its six-matrix array with a per-pixel face index. Optimized DXIL
retained a `[96 x float]` thread-local array, loading all six matrices before
selection. This is confirmed intermediate-code work, not a measured claim about
hardware register spills or the percentage of application time it consumes.

The candidate reads the selected matrix directly from the existing structured
buffer after determining the face. Other record fields still use the existing
accessor; unused matrix fields are eliminated by DXC. Record layout, projection
math, shadow filtering, bias, range, caster admission and depth encoding are
unchanged. The shared consumer benefits point and hemispherical-spot shadows in
both forward and deferred paths.

| Optimized point PS inspection               | Initial | Candidate |
| ------------------------------------------- | ------: | --------: |
| DXIL object bytes                           |  27,676 |    24,440 |
| Thread-local matrix-array floats            |      96 |         0 |
| Raw-buffer load instructions in DXIL        |     126 |       106 |
| Float stores to thread-local memory in DXIL |     192 |         0 |

Inspection uses Windows SDK 10.0.26100.0 `dxc.exe`, `ps_6_6`,
`DeferredLightPointPS`, `-Ges -enable-16bit-types -HV 2021 -O3`, and the production
shader/Oxygen include roots. DXIL counts describe static instructions, not
executed loads per frame or GPU ISA registers. Files are under
`out/analysis/ex07e/e01-shader-inspection/point-{before,face-load}.{dxil,ll}`.
The production ShaderBake archive is validated separately with the engine tool.

The existing local-shadow GPU ABI test now exercises the production face-matrix
accessor using runtime face indices, all six faces, every matrix lane and two
distinct nonzero-index records. Existing image tests supply rendered point/spot,
range, material and family controls. The rebuilt Release GPU ABI probe passes (one test), all 17 native image tests pass,
and the engine ShaderBake rebuild succeeds with 218 production modules. These
checks do not replace the user's manual visual acceptance.

The [UE5.7 comparison](EX07-NewSponza-regression-analysis.md#comparison-with-the-local-ue574-implementation)
remains applicable: this changes data access within Oxygen's current conventional
path, and does not claim to establish UE-equivalent filtering or bias.

## Selected synthetic regression comparisons

These are automated controls against the existing D baselines, not newly
promoted synthetic reference baselines. Native offscreen intervals are not
application FPS. The initial B20/B22/B27 timing records lack CPU preflight;
small timing changes against them are not accepted performance claims. Current
records include preflight, complete percentiles, memory and image proof in
[benchmark comparisons](baselines/ex07e-20260924/matrix-access/benchmark-comparisons.json).

| D ID  | Workload / family        | Initial mean ms | Candidate mean ms | Candidate p95 ms |
| ----- | ------------------------ | --------------: | ----------------: | ---------------: |
| B22-D | shadows-1-1 / deferred   |          10.622 |             9.035 |           10.241 |
| B22-F | shadows-1-1 / forward    |           7.435 |             6.643 |            8.086 |
| B27-D | shadows-wide / deferred  |          11.980 |            11.125 |           14.148 |
| B27-F | shadows-wide / forward   |           8.197 |             6.925 |            8.408 |
| B01-D | sparse-0 / deferred      |           1.706 |             1.670 |            2.639 |
| B01-F | sparse-0 / forward       |           1.598 |             1.513 |            2.485 |
| B08-D | sparse-1024 / deferred   |          10.043 |             8.774 |           10.119 |
| B08-F | sparse-1024 / forward    |           7.518 |             6.511 |            8.118 |
| B20-D | two-view-1024 / deferred |          16.503 |            16.377 |           20.078 |
| B20-F | two-view-1024 / forward  |          11.691 |            10.587 |           13.937 |

## Continuation and run discipline

Retain demonstrated improvements unless the user directs otherwise. Compare
matched collection modes, quality, content and settings. Freeze acceptance/noise
criteria before timing. An effect within observed variation is inconclusive;
use one targeted matched repeat if needed, then seek direction if acceptance
remains unclear. Do not enter an unbounded experiment loop. Every later changed
baseline requires new manual visual approval before commit.

Use full target builds consistent with `oxyrun`, then freeze identities before
launch. Point-kind specialization is compiled only as a separate diagnostic,
not present in the accepted production shader. No performance credit is assigned
to it. E01–E08 retain their recorded remaining obligations.

## E03 candidate design: share light-bound preparation across cells

Source inspection shows that count and fill each loop over every local light
for each cell, loading the 80-byte record and transforming its influence sphere
repeatedly. The count pass at 1080p has 16,320 cells; at 4,096 lights that is
66,846,720 light/cell pairs before the fill pass. This is an algorithmic work
count, not a measured time attribution.

Evaluate 64-light batches in the existing 64-thread compute group. Each thread
prepares one view-space sphere into group-shared memory; all cell threads reuse
that batch. Retain the exact sphere inflation/intersection arithmetic, ascending
selection order, count/scan/fill contract, overflow fallback and resource ABI.
This needs no additional persistent allocation, publication or dispatch.
Invalid/padded/empty/overflow cells must still participate in synchronization;
an entire inactive group may skip its uniform batch loop. Zero-light and small
counts are required regression controls because synchronization has a cost.

UE5.7's `LightGridInjection.usf` also uses cooperative group processing and
explicit synchronization, but its thread-to-cell assignment and list algorithm
differ. This candidate applies cooperative preparation to Oxygen's existing
complete-list contract; it does not claim algorithmic parity or adopt a new
lighting architecture. Compile and inspect before timing; keep it separate from
the already measured matrix-access candidate. Acceptance requires unchanged
qualified images/lists, measured grid and whole-frame improvement, no hidden
small-count regression and unchanged memory/admission semantics.

### E03 implementation checkpoint

The cooperative shader is implemented without a resource/ABI change. Full
native RenderScene, benchmark and image-test builds pass. All 18 native image
tests pass, including the new 65-light test: a partial 32-cell thread group,
one-light tail batch, complete ordered lists, and moving the tail light out of
influence. Existing capacity/fallback and boundary tests also pass. The test's
initial compile error used the strong offset type directly; it was corrected to
use its explicit numeric accessor before execution. Endpoint timing is running;
no grid speedup or E03 baseline acceptance is yet claimed.

### E03 accepted endpoint comparisons

| Workload / family       | Initial D grid ms | Candidate grid ms | Initial D frame ms | Candidate frame ms |
| ----------------------- | ----------------: | ----------------: | -----------------: | -----------------: |
| 1 light / deferred      |           0.09710 |           0.09695 |              1.935 |              1.432 |
| 1 light / forward       |           0.09731 |           0.10420 |              1.904 |              1.378 |
| 4,096 lights / deferred |          10.95920 |           0.71686 |             22.077 |             10.669 |
| 4,096 lights / forward  |          10.80007 |           0.67628 |             20.341 |              5.591 |

All four output images match D's recorded hashes exactly. The user visually
validated Sponza and Instancing and approved the grid comparison records for
commit on 2026-09-24. These comparisons include the accepted matrix-access change;
the matched control below isolates the grid change and its small-count cost.
Results are under `out/analysis/ex07e/e03-grid-endpoints`; the control is under
`out/analysis/ex07e/e03-grid-control`. This is the bounded matched follow-up,
not permission for repeated tuning until a favorable number appears.

### E03 matched control

The endpoint control differs only in `shaders.bin`; it retains the accepted
matrix-access fix and the same runtime binaries. Grid mean times are:

| Workload / family       | Matrix-access-only control ms | Cooperative grid ms |
| ----------------------- | ----------------------------: | ------------------: |
| 1 light / deferred      |                      0.095855 |            0.096947 |
| 1 light / forward       |                      0.097220 |            0.104198 |
| 4,096 lights / deferred |                     10.822310 |            0.716857 |
| 4,096 lights / forward  |                     11.445794 |            0.676279 |

This isolates a roughly 15–17x grid speedup at 4,096 lights. Small-count
synchronization adds about 1 microsecond deferred / 7 microseconds forward in
these captures; report that cost explicitly. Whole-frame means improved in both
small-count runs, but desktop timing variability prevents assigning those
whole-frame differences to this tiny shader change. There is no sustained
whole-frame small-count regression demonstrated by these measurements.
Boundary, dense, irrelevant, moving, 4K, multi-view, orthographic and shadow
interaction qualification passes: **26 rows / 40 images, all byte-identical to D**.
The four endpoint images also match D exactly. Both endpoint/control sets have
zero new buffers/textures in their measured windows. The user's 2026-09-24
response was "Visually validated; accept and commit". Durable records are in
[the cooperative-grid register](baselines/ex07e-20260924/cooperative-grid/register.json).

## E04/E07 discovered defect: caster normal under nonuniform transforms

**E07.1, open; source-confirmed, native reproduction pending.**
`DirectionalShadowDepth.hlsl` currently computes the caster normal with the
world matrix. `BasePassGBuffer.hlsl` uses the published inverse-transpose normal
matrix, indexed through the instance transform index. Shadow pass constants omit
that normal-matrix descriptor even though `PreparedSceneFrame` publishes it.
Thus nonuniform scale/shear can give the wrong slope-bias normal when authored
depth bias is nonzero. For `A=diag(2,1,1)` and local normal `(1,1,0)`, `A*n` is
not perpendicular to the transformed tangent `A*(1,-1,0)`; `inverse(A)^T*n` is.

Repair direction: use the existing normal-matrix publication and instance index
in the shadow-depth path, with a native regression for transform-equivalent
caster geometry and nonzero bias. Preserve depth encoding, bias constants and
filter policy. This is a correctness repair, not permission to retune bias or
claim complete UE5.7 filter parity. Investigate/reproduce and fix after E03's
bounded qualification; do not let the finding disappear into the performance
work. It is separate from the already fixed receiver-depth footprint defect.
