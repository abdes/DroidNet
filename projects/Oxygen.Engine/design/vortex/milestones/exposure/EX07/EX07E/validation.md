# EX07E — Implementation and comparison record

**E06 implementation and S9 automated qualification are complete.**
S1–S8 implementation and native integration tests are complete. S9 captures and
comparisons are in the [shadow-sharing results report](validation.md);
manual visual and numeric acceptance were approved on 2026-09-25. The final
evidence is committed in `b8f1376e1`. The current checkpoint
is in the [E ledger](README.md#optimization-tasks-and-outcome).
The records below remain the accepted controls or explicitly provisional candidates;
new baseline acceptance still requires manual visual validation.

**EX07E is closed. The following sections preserve the earlier checkpoint history;
the [final report](validation.md) owns current acceptance.**
The user manually approved Sponza and Instancing on 2026-09-24 before committing
these application comparison records. Later changed baselines still require
manual visual validation. The [E01–E08 ledger](README.md#optimization-tasks-and-outcome)
owns remaining work; this result does not close the whole E stage.

Initial evidence: [EX07D register](../EX07D/validation.md), renderer
`137b681b2`, evidence `894a25e57`. New durable evidence:
[matrix-access register](evidence/baselines/ex07e-20260924/matrix-access/register.json),
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

The first Instancing native attempt is excluded because an `oxyrun` rebuild
changed the backend DLL hash during capture. `oxyrun` builds the full
target; the initial E build had updated only ShaderBake. Full RenderScene builds
in both existing Ninja Release trees and native benchmark/test targets now pass.
The replacement Instancing runs pass before/after identity checks and close with
exit code 0. Original capture identities remain recorded, not retroactively
rewritten to match the final build.

## Correctness and manual visual approval

- The review approved Sponza: "visual confirmation is good."
- The review approved Instancing: "visual confirmation for instancing scene is also good".
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
  These display-image checks supplement numeric tests and scope review.

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
checks do not replace manual visual acceptance.

The [UE5.7 comparison](../EX07C/sponza-analysis.md#comparison-with-the-local-ue574-implementation)
remains applicable: this changes data access within Oxygen's current conventional
path, and does not claim to establish UE-equivalent filtering or bias.

## Selected synthetic regression comparisons

These are automated controls against the existing D baselines, not newly
promoted synthetic reference baselines. Native offscreen intervals are not
application FPS. The initial B20/B22/B27 timing records lack CPU preflight;
small timing changes against them are not accepted performance claims. Current
records include preflight, complete percentiles, memory and image proof in
[benchmark comparisons](evidence/baselines/ex07e-20260924/matrix-access/benchmark-comparisons.json).

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

Retain candidates supported by the recorded comparisons. Compare
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
[the cooperative-grid register](evidence/baselines/ex07e-20260924/cooperative-grid/register.json).

## E04/E07 discovered defect: caster normal under nonuniform transforms

**E07.1, repaired and native-test validated.**
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

### E07.1 reproduction and repair

The native regression renders identical world-space triangles, once with
nonuniform runtime scale and once with that scale baked into positions/normals.
It reads the actual cube-shadow depth through its R32 shader view using the
existing test GPU probe (the generic D32 copy-readback path is unsupported).
Before repair the depths are **0.26634109 versus 0.41634110**, a **0.15** mismatch.
With the published inverse-transpose normal matrix and resolved instance index,
the values match within **1e-6**. All **19 native image tests pass in each Ninja Release mode** after repair
(Tracy OFF and ON); the final native targeted case also passes after formatting
and constant-name correction.

The private shadow-pass constants remain 128 bytes; offset 124 now carries the
normal-matrix descriptor. Zero slope coefficient skips the normal fetch/math.
No bias value, depth encoding, filter kernel, map resolution or authored range is
retuned. Point/spot setup constants are renamed to remove a false UE attribution;
the [shadow-service contract](../../../../lld/shadow-service.md#23-published-shadow-contract)
now spells out the retained local bias equation and its metric effect. The native
reproduction, both suite results and final targeted result are versioned in the
[caster-normal evidence register](evidence/baselines/ex07e-20260924/caster-normal/register.json).
Both complete RenderScene targets are rebuilt so their C++ pass payloads match
the updated shared shader archive. This is correctness evidence, not a newly
established performance baseline.

### Next bounded E04 investigation

A quality-preserving PCF candidate can use four raw `GatherRed` footprints to
fetch the same nine clamped texels, retaining every comparison and the 1/9 box
weight. This is not hardware comparison filtering or a changed kernel. Verify
texel/component ordering, edge clamping, nonuniform descriptors and supported
map resolutions with native tests before timing one candidate/control pair.
Microsoft documents the raw gather component order and unfiltered semantics in
[gather4](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/gather4--sm5---asm-).
No PCF change is implemented or accepted yet. Keep larger projected-depth/bias
recalibration separate because it changes authored behavior and needs a concrete
quality decision; do not copy UE's constants into the linear-depth profile.

## E05 source-confirmed redundant CPU work

The accepted post-grid 4,096-light deferred capture records 4.708 ms CPU lighting
union, including 2.620 ms in `RecordDeferred`; forward records 1.772 ms union.
The mixed recipe alternates point and spot lights. The current draw loop preserves
that order, so its cached pipeline binding still changes on every alternating
source. Investigate stable grouping by existing pipeline kind/volume mode while
preserving selection/shadow identity and the established accumulation tolerance.
No grouping change has been implemented or timed yet.

`GpuEventScope`'s string-view constructor creates `std::string(label)` before
`BeginProfileScope` checks whether collection is enabled. The fixed per-light
labels exceed the usual small-string storage, so this is avoidable CPU allocation
work even without Tracy. Full per-light Tracy timestamps remain a separate cost.
Reuse the existing descriptor-taking constructor with static owned descriptors;
measure the combined submission change against matched native controls. This
finding corrects any broader implication that all scope-wrapper cost vanishes
when Tracy is off. No runtime saving is claimed before measurement.

A separate metadata defect, E07.2, was found while checking build identities:
`Core/Version.cpp::Patch()` returns the major constant. Current major and patch
are both zero, so current baseline labels are not changed by that latent API
bug. Repair it after the PCF paired captures to keep their DLL identities fixed.

### E04 gather result: direction requested

Both native and Tracy pairs differ only in `shaders.bin`. All 20 native tests
pass, including **3,888** original-nine-load versus gather cases, **47** with
fractional PCF visibility, varying descriptors and face/edge coordinates.
Sponza's scene-only display region differs by at most one RGB byte, mean
0.00000524 byte/channel.

| Sponza measurement       | Original nine-load control | Gather candidate |
| ------------------------ | -------------------------: | ---------------: |
| Native mean frame ms     |                     23.835 |           23.122 |
| Native block-mean spread |                    11.910% |           0.666% |
| Tracy mean frame ms      |                     24.125 |           24.360 |
| Tracy point-light ms     |                     11.561 |           11.711 |
| Tracy translucency ms    |                      1.928 |            1.841 |
| Tracy block-mean spread  |                     1.050% |           0.624% |

This is a mixed result, not an accepted overall performance improvement. The
native control's noisier final block prevents treating its mean reduction as a
clear win. On 2026-09-24, the user was asked to choose one bounded forward/
translucency-only gather follow-up (retaining original deferred filtering), or
to drop the gather candidate and continue CPU/resource work. PCF experiments
are paused pending that direction; the all-path candidate is uncommitted and
has no manual baseline approval. Raw pairs are under `out/analysis/ex07e/e04-pcf-{control,candidate}`.

### UE5.7 PCF choice and corrected decision

The user's subsequent question prompted a direct check of the installed
UE5.7.4 source (CL 51494982). The earlier two-option question omitted the
UE-aligned point-light option and is superseded by this decision record.

| Conventional shadow projection | Verified UE5.7 implementation                                                                                                                                                                                                                                                                             |
| ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Point-light cube               | `CubemapHardwarePCF`, `ShadowProjectionCommon.ush:153`: `TextureCube.SampleCmpLevelZero` with a bilinear comparison sampler. Quality levels use 1, 5, 12 or 29 sampling operations; higher levels use disc patterns. Receiver comparison is based on projected z/w with the corresponding bias treatment. |
| Projected spot/directional     | `ManualPCF`, `ShadowFilteringCommon.ush:352`, called by `ShadowProjectionPixelShader.usf:260`: raw gathers feed shader-side occlusion and fractional-texel weighting. The named 3x3 filter uses four gathers over 4x4 texels; 5x5 uses nine gathers over 6x6 texels.                                      |

The cube comparison sampler is configured in `ShadowRendering.cpp:290`.
These are conventional-shadow paths; this record does not describe VSM/SMRT.
UE's split is by shadow projection and filtering contract, not a general rule to
use gather only in forward rendering. Oxygen's candidate rearranges accesses to
its existing nine-comparison box kernel; it does not reproduce either UE kernel.

### Point-shadow filtering decision — 2026-09-24

The selected path uses cube hardware comparisons with a coherent producer/receiver
depth and bias contract. Low/Medium/High/Ultra use **1/5/29/29 comparisons**;
UE's High/Epic conventional point-shadow settings both use 29. Projected spots
and CSM retain their separate filtering paths.

| Alternative                          | Disposition and reason                                                                                                                                                             |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Cube hardware PCF                    | Selected for the conventional point-shadow path. Depth/bias encoding, contact, grazing, seams, range, resolution and shading-family checks qualify the change.                     |
| Forward/translucency-only raw gather | Rejected as the production direction. It rearranged the existing nine-comparison box kernel and offered a small path-specific opportunity, without reproducing the UE cube kernel. |
| Retain nine-load filtering           | Retained as the comparison control, superseded for production by the cube path.                                                                                                    |

The cube path changes filter response and encoding. Its comparisons therefore
report quality and timing together rather than assuming pixel identity with the
nine-load control. The [filtering contract](../../../../lld/point-shadow-filtering.md)
defines the producer/consumer migration; the measurements and visual checks below
record the candidate decisions. Raw-gather changes were removed.

#### Hardware-PCF candidate qualification (2026-09-24)

At this initial hardware-PCF checkpoint both existing Ninja Release RenderScene
targets were rebuilt against the 222-module candidate. The subsequent punctual-
point variant adds one module, producing the current 223-module archive. No new
baseline is accepted or committed. The user has authorized committing the
validated implementation and its automated test evidence separately.

| Check                                  | Result                                  | Coverage                                                                                                                                                                                                                                                                                         |
| -------------------------------------- | --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Shadow service/setup, non-Tracy        | 31/31 pass                              | Independent native cube-coordinate equations on six asymmetric face samples, reversed-Z near/intermediate/far depths, ranges 0.05/3/4096 m, metric bias normalization, existing selection/quality/reference tests.                                                                               |
| Native lighting image suite, non-Tracy | 21/21 pass                              | Existing forward/deferred reference, admission, contact, off-screen caster and long-range checks plus the new cube cases below.                                                                                                                                                                  |
| Native lighting image suite, Tracy     | 21/21 pass                              | Same suite in the separate Tracy-enabled Release tree.                                                                                                                                                                                                                                           |
| Shadow ABI subset, each tree           | 3/3 pass                                | CPU/HLSL cube record remains 448 bytes; distinct 5/29 sample-count words and selected face-matrix access survive GPU decoding.                                                                                                                                                                   |
| Bilinear comparison oracle             | 108 cases pass per tree                 | Two uploaded cubes, six faces, asymmetric coordinates and positive/negative comparison bias. A 0.5 visibility result proves filtering of comparisons rather than comparison of interpolated depth.                                                                                               |
| Rendered cube oracle                   | 594 samples per material state per tree | Three lights, two backings, a nonzero cube index, all faces, edges/corners and poles; 1/5/29 comparisons. Analytic box intersections verify stored projected depth and visibility. Opaque, rejected masked, and accepted masked states also exercise cache invalidation and both lighting paths. |

The caster-normal regression now uses a projected spot, preserving its slope-bias
coverage after cube caster bias was removed. The contact-occluder test now records
separate map-shadow and unshadowed references: its former assumption that authored
bias 1 completely removes map shadows is invalid for the new filter. Contact
attenuation and caster/receiver gates retain their original numerical tolerances.

Point shader inspection: 25,484 DXIL bytes versus 24,440 for the accepted
nine-load path; no 96-float local array and no local float stores. Static
comparison-call counts include quality branches and are not runtime tap counts.
Quality/performance acceptance remains open pending scene measurements and the
manual visual validation. Raw logs are under `out/analysis/ex07e/e04-pcf`, with
the hardware-PCF inspection and scene captures under
`out/analysis/ex07e/e04-hardware-pcf`; the results above are retained here so their
meaning does not depend on that transient directory.

The first Sponza hardware-PCF Tracy attempt is **excluded** because CPU
contention invalidated the capture. Its preflight (CPU mean 3.2%, GPU mean
38.4%) cannot establish in-run headroom. No timing from that attempt is accepted.
The rerun adds total-CPU sampling throughout the capture and records a separate
20–50-second CPU-load disposition; quiet startup alone is no longer sufficient.

#### Confirmed Sponza GPU regression — candidate not accepted

The rerun passed preflight (CPU mean/peak
5.4/12%, GPU 29/34%). During its measured 20–50-second window, 23 CPU samples
averaged **3.435%**, peaked at **12%**, and none exceeded 25%. The application
closed successfully and its binaries/content remained unchanged during capture.
The retry script initially misclassified coverage because PowerShell converted
UTC JSON timestamps to DateTime values before reparsing their culture-formatted
strings as local time. Reprocessing the preserved ISO8601 samples with explicit
offsets proves full coverage; the script now preserves date strings explicitly.
This parser correction does not change measured performance or load thresholds.

| Sponza, Tracy Release, 2560×1400 | Prior nine-load profile | Hardware PCF, High = 29 |                 Change |
| -------------------------------- | ----------------------: | ----------------------: | ---------------------: |
| Mean GPU frame interval          |               24.125 ms |               31.606 ms |                +31.01% |
| Derived FPS                      |                  41.451 |                  31.639 |                −23.67% |
| Point-light GPU work             |               11.561 ms |               17.430 ms |              +5.869 ms |
| Deferred lighting, inclusive     |               12.510 ms |               18.410 ms |              +5.900 ms |
| Translucency                     |                1.928 ms |                3.380 ms |              +1.452 ms |
| Shadow depth work                |                3.673 ms |                3.724 ms |              +0.051 ms |
| Deferred recording CPU, per call |               0.1173 ms |               0.1207 ms |             +0.0034 ms |
| Ten-second block variation       |                  1.050% |                  0.955% | Both stable within run |

Both captures contain 23 point-light draws per frame and have identical scene,
container, demo-settings and UI-settings hashes. End-of-run graphics clocks were
1920 versus 1890 MHz; this small difference does not account for the observed
GPU increase. These are the prior nine-load profile and the new encoding/filter
candidate, not a shader-only matched pair. The new 29-comparison filter changes
quality, but it is still a **confirmed performance regression**, not an accepted
optimization. CPU load does not explain it: point evaluation and translucency
account for almost all of the extra GPU frame time. Scope times are inclusive.

The High profile retains 29 comparisons. The subsequent specialization separates
avoidable point/finite-emitter shader cost from filter-kernel cost without lowering
quality. The depth/bias migration is required for correct hardware comparison;
29 comparisons are the selected quality policy, not a shadow-correctness minimum.
Further PCF-parameter tuning and a shadow-mask-pass redesign were not selected.

The [candidate register](evidence/baselines/ex07e-20260924/point-hardware-pcf/register.json)
retains timings, in-run CPU samples and the screenshot. Final adopted baselines
and their visual checks are recorded in the S9 results and commit sequence.

#### Bounded point/punctual specialization

The bounded point/punctual specialization preserves all PCF parameters and
selects its smaller variant only for exactly zero source radius. Cooked files
contain 23/23 zero-radius Sponza points and 49/49 zero-radius Instancing points.
The compiled punctual shader is 20,664 bytes versus 25,484. Both Release trees
pass the 22-image-test suite, including mixed zero, tiny-positive and finite
radii within one frame. The implementation is test-validated; its performance acceptance remains subject to the limitations below.

| Same-quality Sponza experiment |   Control | Specialized |
| ------------------------------ | --------: | ----------: |
| Point-light GPU work           | 17.317 ms |   16.223 ms |
| Inclusive deferred lighting    | 18.907 ms |   20.992 ms |
| Translucency                   |  3.357 ms |    3.335 ms |
| Whole GPU frame interval       | 32.517 ms |   36.519 ms |
| Derived FPS                    |    30.753 |      27.383 |
| Ten-second block variation     |    9.191% |      0.213% |
| Measured-window CPU mean/peak  | 4.391/17% |   6.826/19% |

The pair uses identical runtime binaries with only the shader archive changed.
Point-draw work is lower by 1.093 ms, but **there is no established whole-frame
benefit**. The inclusive deferred interval grows outside those point-draw scopes;
the control is also unstable. Do not label the result an FPS optimization or
attribute the unexplained interval to CPU overload. The earlier exploratory
specialized capture had 17.807% block variation and is not a small-delta baseline.
No additional PCF-side capture is planned; preserve these limits for integrated
E08 qualification while proceeding with E05.

E02 credits the already accepted shared face-matrix fix: Sponza translucency
fell from the frozen D 7.726 ms to 1.940 ms in that matched capture. The current
29-comparison filter adds the explicitly chosen quality cost. Source inspection
also confirms that forward direct lighting already prepares its BRDF context
once outside the local-light loop and rejects zero influence/back-facing sources
before shadow evaluation. Do not schedule those existing optimizations again or
discard contributing off-screen lights to lower the workload. Final integrated
transparent-material/scene qualification remains E08 work.

### E05 — CPU allocation and draw submission

Implemented at this checkpoint: static owning GPU scope descriptors, reused constant
and draw-order scratch, borrowed immutable CBV-index spans, and stable counting
sort by local-light PSO. This removes per-light label allocations even in
non-Tracy Release and avoids switching PSO for every interleaved light. It retains
directional-first/sky-last ordering, original order within a local bucket, and
the original indices for constants and shadow selection. Only additive local
accumulation order changes; the existing image error budget still applies.

The borrowed index view is CPU-only and consumed during recording. Same-frame
publications retain its backing; callers must stop using it at `OnFrameStart` or
publisher destruction. Existing fenced staging and descriptor retirement remain
unchanged. This is independent of the deferred E06 ownership work.

Both Ninja Release trees pass 34 lighting-service tests (including cached CBVs,
growth, same-frame publications and retirement) and all 22 native image tests
across the suite and a focused counter-check rerun. The 1,024-source native preview
renders **704 local draws with 2 pipeline binds**, and still lights 2,232 of 2,304
preview pixels in each tree. The first added counter assertion read state after
frame cleanup; the correction captures it inside the existing publication probe.
Endpoint measurements, broader interaction qualification and visual baseline
acceptance remain open. Bind-count reduction alone is not an FPS claim.

The next gather change reuses the scene's already-updated world positions and
retains selection-vector capacity. It removes the extra ancestor-matrix traversal
for every light and the unused quaternion traversal for isotropic point emitters.
Spot directions retain their authored quaternion-chain semantics: decomposing a
scaled/sheared world matrix would change that contract. A new native hierarchy
test covers parent rotation/nonuniform scale, parent movement and
`IgnoreParentTransform`; all **23 native image tests pass in each Release tree**.

| Non-Tracy Release, 4,096-light deferred workload | Accepted post-grid | Submission changes | Plus gather changes |
| ------------------------------------------------ | -----------------: | -----------------: | ------------------: |
| Lighting CPU interval union                      |           4.708 ms |           2.494 ms |            2.295 ms |
| Deferred recording CPU                           |           2.620 ms |           0.378 ms |            0.492 ms |
| Light gathering CPU                              |           1.163 ms |           1.154 ms |            0.894 ms |
| Whole frame mean                                 |          10.669 ms |           9.822 ms |            9.245 ms |
| Within-run block variation                       |            14.044% |             7.156% |              3.662% |
| Steady buffer/texture creations                  |                0/0 |                0/0 |                 0/0 |

The current lighting CPU union is approximately **51% lower** than the accepted
post-grid measurement. The frame-time reduction is reported, but these rows do
not isolate an FPS benefit: the historical reference is noisy and the current
candidate also contains the shader work above. These unshadowed endpoints do not
measure the added PCF quality cost. Both new 1-light/4,096-light submission rows
and the final 4,096-light gather row passed load preflight and complete-list image
comparison. Against the frozen EX07D images, all 6,220,800 channels per image
pass the `0.5% + 2e-5` budget; the final 4,096-light result uses at most
0.0000661 of that budget. Original order is not claimed to remain bit-identical.

The one-light submission endpoint records 0.0135 ms deferred CPU recording
versus the historical 0.0121 ms, and 1.896 versus 1.432 ms whole-frame time. Keep
this result visible; the small CPU difference does not explain the whole-frame
change. No small-workload FPS win or isolated regression attribution is claimed.
The retained scratch arrays trade bounded high-water CPU capacity for fewer
allocations; no GPU-memory saving is claimed from that trade. GPU staging and
retirement contracts remain unchanged.

Local candidate evidence at
`baselines/ex07e-20260924/cpu-submission/register.json` contains the summaries,
preflights, reference comparisons and a source replay recipe. These baseline
artifacts remain uncommitted pending manual visual approval. The
[automated validation records](evidence/validation/ex07e-pre-e06/README.md) preserve the
passing tests independently. These are candidate measurements, not manually accepted
new baselines. Broader dynamic/multi-view qualification and the final integrated
scene captures still belong to E08.

The additional non-Tracy interaction qualification passed **12 rows / 20 images**:
moving-1024, two-view-1024, orthographic-1024, five-point/nine-spot shadows, finite
sources and hemispherical spots, each in forward and deferred. Every image is
exactly equal to its current complete-list reference. This is automated current-
candidate qualification, not a claim that the new filter matches D images or that
it has passed manual visual checks.

For the final 4,096-light endpoint, both memory snapshots report 4,333,568 bytes
charged to lighting (6,692,864-byte peak), 786,432 compact-index bytes, and no
rejected allocations. Whole-allocator local allocation/commitment is 684,666,880
bytes with zero local block slack; non-local allocation is 33,816,576 bytes in
134,610,944 committed bytes, leaving 100,794,368 bytes of block slack. These are
whole-allocator figures, not lighting-only savings. Lighting/shared staging is
604,519/19,333 bytes per frame, retaining the existing upload contract.

At the pre-E06 checkpoint, the inventory exposed native placement and registration
state without an authoritative ownership split. E06 now measures local-shadow
unique/spare/closing bytes, aliases/versions, retained CPU payloads and actual
submission costs. The [final report](validation.md) records the
40 MiB shared allocation, 6 MiB diagnostic-retention charge and 128 -> 64 MiB
forced copy-on-write/release case. These are explicit local-shadow ownership
measurements, not an invented whole-engine queued-memory split. At the pre-E06 checkpoint, no E06 infrastructure
change has been pulled forward under E05. Final integrated scene captures occur
after E06, as agreed; they are not a prerequisite that moves E06 earlier.

The separate E07.2 metadata defect is fixed: `Version::Patch()` returns
`cVersionPatch`. Both trees rebuild; current version numbers do not expose a
runtime numerical difference because major and patch are both zero.

The user separately identified E06 sharing as critical and required a proper
proposal, including evaluation of Graphics resource management and Nexus, **for
approval before code changes**. The
[E06 design proposal](README.md) records that direction.
The user subsequently agreed with the direction, requested independent review,
and deferred E06 to the **last EX07E implementation item**. Complete all other
optimizations and non-sharing correctness repairs first; resolve the independent
review before implementing E06, including its Graphics/Nexus changes. Final
integrated E08 qualification follows E06. This changes execution order, not scope
or the manual visual approval requirement for new baselines.

## EX07E — Shadow sharing implementation and baseline comparison

**EX07E closed (2026-09-25). S1–S9 implementation and qualification are complete.
The review approved visual acceptance, reviewed the before/after numbers and
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
  closure. [F engine-side acceptance](../EX07F/validation.md) subsequently
  credits these results and final editor approval; overall EX07 is closed. The C
  caller/importer follow-up is [closed](../EX07C/validation.md).

## Acceptance and durable evidence

**Manual visual acceptance approved in review on 2026-09-25:** "Visual acceptance is approved."
The requested numeric before/after summary was delivered, and the user then
authorized structured commits. Approval applies to the final Sponza/Instancing
implementation and comparison records. The three reviewer documents remain
excluded from commits as requested. Historical candidate registers retain their
capture-time disposition; the final accepted register owns current acceptance.

Final screenshots: [Sponza](evidence/baselines/ex07e-20260925/cross-view-sharing/qualified-scenes/NewSponza_Main_glTF_003-native/scene.png)
· [Instancing](evidence/baselines/ex07e-20260925/cross-view-sharing/qualified-scenes/InstancingTestScene-native/scene.png)
· [Shared-map benchmark](evidence/baselines/ex07e-20260925/cross-view-sharing/qualified-native/shadow-share-static-deferred/phase-0-view-0.png)
· [Second benchmark view](evidence/baselines/ex07e-20260925/cross-view-sharing/qualified-native/shadow-share-static-deferred/phase-0-view-1.png).

[Numeric records, compressed traces, images, frozen source and checksums](evidence/baselines/ex07e-20260925/cross-view-sharing/register.json)
· [Initial D register](../EX07D/validation.md)
· [Earlier E comparison report](validation.md)
· [Authoritative tracker](README.md#optimization-tasks-and-outcome)

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
[F acceptance](../EX07F/validation.md) reuses this evidence without recapture.
Pre-commit also reformatted the Loader test CMake file and benchmark JSON schema;
byte/token-equivalence checks and exact diffs are preserved in
`validation/commit-hook-format-only.json`. Immutable capture fixtures are excluded
from automatic normalization/formatting; ordinary product code and these Markdown
reports continue through the standard hooks.

### Execution record — 2026-09-24

The comparison control is committed pre-E06 source `e824c97a7`; existing E04/E05
candidate records remain provisional, not newly accepted baselines. S1/S2 changed
no shaders, rendering policy, or shadow allocation partitioning.

| Owning suite (existing `out/build-ninja`)                | Release     | Debug       |
| -------------------------------------------------------- | ----------- | ----------- |
| Nexus reuse (including core and frame/timeline adapters) | 45/45       | 45/45       |
| Nexus allocation failure                                 | 4/4         | 4/4         |
| Graphics deferred reclaimer                              | 18/18       | 18/18       |
| Transform uploader                                       | 18/18       | 18/18       |
| Draw metadata emitter                                    | 15/15       | 15/15       |
| Geometry uploader                                        | 30/30       | 30/30       |
| Material binder                                          | 34/34       | 34/34       |
| Atlas buffer                                             | 10/10       | 10/10       |
| Texture binder regression control                        | 27/27       | 27/27       |
| **Total**                                                | **201/201** | **201/201** |

The allocation-failure executable intercepts allocations in its instantiated
Nexus templates; it does not intercept allocation inside the Graphics DLL.
Reclaimer mixed-order, reentrant, exception-continuation and uncommitted-action
tests pass. The tests exposed and fixed the MSVC checked-iterator temporary-vector
failure in GenerationTracker. Material atlas return capacity is now secured during
growth, and material/geometry/shadow activation restores unexposed indices on
failure. Existing multi-view publisher and repeated-frame-slot tests pass.

Local JSON/log evidence is under `out/analysis/ex07e/e06-s12/{Release,Debug}`.
This table is the durable S1/S2 result summary. Later-step checkpoint results
follow below; native integrated qualification, Tracy integration, and final
visual baseline acceptance remain open.

S3 ordinary lifetime qualification: **27/27 in Release and 27/27 in Debug**, in
the same existing Ninja tree: loader 17, Common ownership/admission 2, Common
lifecycle 6, and native loader integration 2 (Headless and D3D12 with debug layer).
Native tests retain a manually registered texture/SRV, buffer and unfinished
recording across close without starting a nursery. They verify discard on the
recording thread, descriptor cleanup, canonical-owner retirement, D3D12 budget
retention through native destruction, reload exclusion and eventual reload while
dead weak observers remain. The ownership helper preserves texture
`shared_from_this()`. Nested factory admission is tested against concurrent close.
Evidence: `out/analysis/ex07e/e06-s3/{Release,Debug}`. S5's remaining lifecycle
failure cases and final integrated qualification are not covered by these counts.

S4 qualification so far (existing non-Tracy Ninja tree):

| Owning suite                                   | Release | Debug |
| ---------------------------------------------- | ------- | ----- |
| Resource registry, including managed ownership | 73/73   | 76/76 |
| Descriptor allocator                           | 26/26   | 28/28 |
| Descriptor segments                            | 27/27   | 27/27 |
| Queue ownership/state retirement               | 19/19   | 20/20 |
| Native lifetime integration                    | 4/4     | 4/4   |

Managed coverage includes independent allocation owners/use pins, immediate
acquisition closure, monotonic/exhausted IDs, stale identity rejection, all raw
mutation routes (including descriptor source and destination), native backing
aliases, forced view-hash collisions, concurrent equal-view acquisition,
transactional view rollback and late cleanup without a facade/frame. Native
Headless/D3D12 cases create a cube SRV and six immutable DSVs and retain them
through close with only an internal use pin. These are correctness tests, not
timing baselines.

All-allocation denial passes for raw/bindless descriptor return, prepared-action
commit, managed owner/use/view retirement, and queue-state retirement. View
construction rollback also passes fault injection; its fixture uses concrete
allocator access so mock-framework allocations are not confused with product work.

### Registration-construction fault coverage

**S4 qualification limit (2026-09-24):** exhaustive Debug registration-construction
fault injection reaches MSVC's `_Hash_vec` noexcept constructor allocating a
16-byte checked-iterator proxy. The debugger records termination inside the STL
before Oxygen can handle `bad_alloc`. The construction fixture therefore excludes
allocations the size of `std::_Container_proxy`. It also excludes unrelated
allocations of the same size; this is a test-coverage limit, not a product policy
change. The exclusion is opt-in and used only by registration construction.
View rollback and all retirement tests continue rejecting every allocation.
The complete Debug registry suite now passes 76/76. Do not count the earlier
aborted run as a pass. Current proof is
`out/analysis/ex07e/e06-s4/Debug/Oxygen.Graphics.Common.ResourceRegistry.Tests.json`;
the debugger evidence remains `out/analysis/ex07e/e06-s4/managed-oom-debugger.log`.

### S5 closure — actual submission and retirement

**Complete: 93/93 Debug, 90/90 Release**, both using `out/build-ninja`.

| Owning suite                            | Release   | Debug     |
| --------------------------------------- | --------- | --------- |
| Prepared submission/use-batch contracts | 4/4       | 5/5       |
| Queue strategy and frame retirement     | 19/19     | 20/20     |
| Graphics lifecycle                      | 6/6       | 6/6       |
| Recording/publication callbacks         | 20/20     | 20/20     |
| Command-list pool and late returns      | 15/15     | 16/16     |
| Headless/D3D12 native integration       | 26/26     | 26/26     |
| **Total**                               | **90/90** | **93/93** |

Both production backends now use Common's prepared submission transaction.
Managed uses retire from private completion receipts. Ordinary frame-scoped
submissions retain frame reclamation. Cross-queue dependencies reference the
producer timeline; same-queue dependencies add no native self-wait. Resolved
recordings release their recorder/backend owner and preserve only their result.
Queue enumeration retains an immutable snapshot so completion polling allocates
no temporary queue collections.

Native qualification covers stale reserved signals, impossible same-list waits,
pre-issue discard, partial array issue, post-issue/pre-marker failure, recovery
of mixed manual/managed states, out-of-frame retirement, late submission after a
frame marker, two consuming queues, actual cross-queue copied buffer contents,
device loss, queue replacement guards, close/reload and worker-thread late release.
The Debug submission test rejects every allocation after native issue and during
completion retirement; no proxy-size exemption is used for that test.

Recovery drains all queues, reconciles prepared states, and checks cross-queue
agreement before finalizing quarantined ownership or clearing the fault. Conflicting
queue state records or failed drain/reconciliation close the backend. A device-loss
sentinel never counts as successful completion. Unresolved pre-fault recordings
are rejected through their preparation epoch. Existing publication callbacks
invalidate products for both discard and uncertain execution.

The regression suite exposed a missing null-result check during queue creation;
that defect is fixed and the full queue suite passes. Evidence logs/JSON:
`out/analysis/ex07e/e06-s5/<suite>-{Debug,Release}.{json,log}`. The counts and
behavior above are the durable summary. S6–S9 remain; no E06 timing baseline or
shared-shadow acceptance is claimed by this gate.

### Native-integration repairs discovered after the S5 core gate

- Legacy readback producers allocate their own increasing signal values. Common
  submission now accepts these without requiring a prior `Signal()` reservation;
  stale/non-monotonic values remain rejected before issue.
- Headless reserves accepted legacy signal values on the CPU before enqueuing its
  async task, preventing a following frame/drain marker from duplicating a value.
- Readback tickets require submission acceptance before fence completion can
  publish data. Unissued shutdown work is cancelled, and a device-loss sentinel
  reports failure. Staging/resolve allocations use managed owners and actual-use
  pins; pending Reset no longer depends on indefinite manual-registration retention.
- Readback facades and mapping guards use Common-owned deleters and retain their
  canonical backend until backend cleanup returns.

These are E07 correctness/lifetime repairs within S3/S5 and the S8 native-capture
path. The original S5 table remains its completed checkpoint, not a claim that
these later changes have finished validation. Added native, tracker and readback
regression suites are being run before integrated closure.

### S6 checkpoint — canonical local-shadow requests

**38/38 tests pass in Debug and Release; production integration remains open** in the existing non-Tracy Ninja
tree (`Oxygen.Vortex.ShadowService.Tests`). Exact immutable caster records retain
geometry/LOD/generation/revision, bindings/ranges, world transform, raster and
masked coverage inputs. Interning compares values after hash lookup. Unknown
sampled-texture continuity disables reuse; texture-free masked materials need no
texture revision.

`ShadowService::PrepareLocalRequests` prepares the family before placement.
Caster/request caches honor frame and CPU preparation revisions, including a
same-frame offscreen snapshot rebuild. Point/spot math is separated from physical
binding; existing projection and per-view reference tests pass. Content identity
includes the explicit depth contract and relevant caster set, while excluding
view/frame/placement identity and cube receiver-only bias/filter settings.

Seven added request tests cover collisions, unchanged-record interning, sampled
texture continuity, reordered/partial membership, entering/unknown-bound casters,
producer versus consumer bias, and geometry/LOD/raster invalidation. The existing
cache regression now uses an explicitly sampled texture with a known revision,
and also checks a same-frame preparation rebuild. S7/S8 will replace the old
physical-map cache and attach all readers; S6 alone does not enable shared maps.

The S7/S8 call-site audit found that SceneRenderer still supplies only the current
view, so the earlier S6 completion label was premature. The CPU/service gate
stands; production must pass the full preparation family while rendering only
the current view. This integration and its regression check are being completed
with S7/S8 before S6 is marked complete again.

Durable result: **38/38 per configuration**. Supporting JSON/logs are
`out/analysis/ex07e/e06-s5/shadow-preparation-{Debug,Release}.{json,log}`.

### S6–S8 integrated closure

The previously reopened SceneRenderer family-input gap is fixed: the full CPU
preparation family is separate from the current view selected for rendering.
The old per-surface local content cache and its reuse-only submissions/self-waits
are removed. Physical maps and immutable versions are shared per compatible light.
Unsubmitted backing users prevent writes to any of its layers; submitted users on
other queues contribute actual completion dependencies.

Frame publications close at EndFrame, snapshot replacement and service shutdown.
Already attached frame-ring recordings are rejected if submitted after closure.
Explicit content leases support delayed recordings with independently owned inputs.
Native tests preserve old depth across five frame advances while a new caster
version renders elsewhere. A 32 MiB budget forces exact six-layer cube fallback;
a retained diagnostic texture keeps **6,291,456 native bytes** charged until release.

| Final owning qualification                    | Result                      |
| --------------------------------------------- | --------------------------- |
| Non-Tracy Release integrated selection        | 494/494                     |
| Shadow service, non-Tracy / Tracy Release     | 50/50 in each tree          |
| Native image suite, non-Tracy / Tracy Release | 26/26 in each tree          |
| Debug shadow service                          | 50/50                       |
| Debug native shadow/capture/budget selection  | 10/10                       |
| Native backend lifetime, Debug / Release      | 36/36 in each configuration |
| Readback tracker, Debug                       | 17/17                       |
| Headless readback manager, Debug              | 27/27                       |
| D3D12 buffer / texture readback, Debug        | 29/29 and 25/25             |

The same-frame native family renders five cube and nine projected maps for two
views, including reversed publication order, deferred/forward opaque and
translucency. Resident views share the same placements; warm frames produce no
shadow writers. Different exposure settings are compared in scene-radiance space.
The 14 map-use pins deduplicate to two backing-registration pins per view.

Readback integration is also qualified: independently allocated monotonic legacy
signals work; unsubmitted tickets cannot complete from another queue marker;
shutdown cancels unissued copies; staging/resolve owners and mapping guards retire
safely from actual use. Fourteen later lifecycle cases expanded the original
native set to 36, including canonical facade/mapping lifetime across close.

Proof: `out/analysis/ex07e/e06-final/tests-Release.json`, the two owning test JSONs
per Release tree in that directory, and the named Debug JSON/logs under
`out/analysis/ex07e/e06-s5`. Counts and essential outcomes above remain durable if
those transient logs are removed. S9 now has the separate timing/ownership report
linked above; automated test success is not manual visual acceptance.

## 14. Design integration review

The three-document review resolves the following implementation conflicts:

| Review finding                                                    | Resolution in the LLDs and plan                                                                                                                           |
| ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Strict activation conflicts with dense publishers                 | S1/S2 migrate the core, adapters, and two dense callers together                                                                                          |
| Extra per-action allocation in the initial reclaimer draft        | Prepared nodes supplement the existing action vectors within the same buckets and preserve enqueue order                                                  |
| A version retaining a pool that owns versions creates a cycle     | Pool lookups are weak; versions own slots; slots own chunks and a weak return target                                                                      |
| Internal use pins retaining Graphics create a cycle               | Public leases split backend ownership from internal pins; submitted storage receives internal pins only                                                   |
| Buffer ownership does not retain frame-ring contents              | Frame read sets remain frame-scoped; retained content publishes new bindings; delayed native recordings use retained inputs                               |
| Module pinning alone does not protect restart or cleanup code     | One retiring incarnation excludes reload; Common owns outer destruction calls and native lifetime outlasts allocations                                    |
| Weak observers retaining a deleter capture delay backend release  | Outer deleters move strong lifetime captures into their deletion call and leave dead control blocks empty                                                 |
| Checked-out lists outliving the command-pool component            | The existing pool uses shared return state; closing clears its factory and late returns destroy lists                                                     |
| A failed signal skips resource-state adoption                     | Fault recovery closes affected managed registrations and reconciles every affected issued resource, including manual registrations, or closes the backend |
| Descriptor cleanup can allocate during noexcept destruction       | S4 reserves descriptor return capacity before publication and tests rollback/final release under allocation failure                                       |
| Writer migration before reader migration permits early retirement | S7/S8 are one production integration change                                                                                                               |
| Counting submission markers as automatic signal savings           | Keep per-light dirty writers initially and measure reader-marker overhead in cold/warm/incompatible controls                                              |

Section 13 maps the original proposal's ownership, descriptor, submission, Nexus,
content, hazard, budget, shutdown, and efficiency requirements to owning steps and
acceptance IDs. The two LLDs contain no milestone identifiers; this plan carries
the execution order and baseline approval requirements.

### EX07E handoff — closed

**E06 implementation resumed by the delivery decision, 2026-09-24.** The independent
review is accepted with the descriptor-cleanup and uncertain-submission recovery
amendments. The [authoritative checkpoint](README.md#optimization-tasks-and-outcome)
records completed S1–S9 implementation/qualification and manual visual acceptance
on 2026-09-25. The requested numeric review is complete and evidence is committed
in `b8f1376e1`. The [sharing comparison report](validation.md)
owns final benchmark, scene, memory and operating-limit results.

**Closed: implementation, qualification and acceptance complete (2026-09-25).** The authoritative
[E01–E08 work ledger and resume checkpoint](README.md#optimization-tasks-and-outcome)
record all eight investigation/delivery obligations, completion evidence and
next action. Update that ledger as findings and fixes are delivered. Earlier
measured repairs remain credited. A–E are closed, including the
[C caller/importer follow-up](../EX07C/validation.md). Overall EX07 is
closed with [F acceptance](../EX07F/validation.md), reusing E measurements
and C validation. RenderScene and editor interactions are manually verified.

Begin with the committed
[baseline register](../EX07D/validation.md#how-e-and-f-use-this-register) and
[source/UE5.7 analysis](../EX07C/sponza-analysis.md). Use the current
A-SPONZA and A-INSTANCING traces to isolate the dominant local-light shader work
before selecting an implementation. Explain the expected cost reduction and
correctness/quality implications against UE5.7's corresponding path. Preserve
conservative influence-volume culling, off-screen contributing lights/casters,
authored ranges and shadow requests. Broader bias/filtering parity remains an
explicit investigation, not an established equivalence.

Select matched baseline IDs and freeze candidate acceptance/noise criteria before
timing. Requalify only affected or insufficiently controlled rows; B05-D and the
30 rows without recorded CPU preflight cannot support small timing claims as-is.
Use only existing `out/build-ninja` and `out/build-tracy-ninja`, both Release,
keeping Tracy attribution separate from native throughput. Check sustained heavy
CPU/GPU contention while allowing ordinary desktop use. Do not repeat the full
baseline campaign merely to enter E. Candidate reports must preserve image and
physical-reference checks, show whole-frame/stage and relevant memory results,
and record retained limits. F owns final integrated and interactive acceptance.

### Recorded E priorities after the New Sponza regression analysis

The [source and Tracy analysis](../EX07C/sponza-analysis.md) records
performance work still required:

| Priority                                 | Evidence / problem                                                                                                                        | Recommendation and acceptance                                                                                                                                                                                               |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GPU local-light evaluation               | Registered Sponza Tracy: 45.288 ms across 23 point draws within 46.309 ms deferred lighting; Instancing: 26.433 ms across 39 point draws. | Isolate filter, material-fetch and BRDF/register costs. Measure specialization and uniform-data reuse first, preserving contributions and matching content, quality and instrumentation.                                    |
| Translucent lighting                     | Registered Sponza Tracy: 7.726 ms.                                                                                                        | Profile the shared forward evaluator and overlap before selecting changes; retain physical/material and shadow references in both families.                                                                                 |
| Shadow bias/filtering                    | Receiver-footprint repair passes point/spot, short/long-range tests; the whole point pipeline is not quantitatively UE5.7-equivalent.     | Audit units, depth producer/consumer and nonzero authored bias together. Comparison filtering needs a quality contract and contact/grazing/cube-seam coverage; do not copy constants or hide fewer samples as optimization. |
| Shared-grid scaling                      | Registered B09 grid means: 10.959 ms deferred / 10.800 ms forward, non-Tracy Release.                                                     | Compare against count, overlap, irrelevant-light, motion and view baselines; preserve complete-list fallback and every valid contributor.                                                                                   |
| Application lifetime/final qualification | Premature DemoShell composite-target release caused the close failure; the fixed debug-layer run closes cleanly.                          | F includes application close, resize/view removal, movement and scene replacement alongside offscreen checks.                                                                                                               |

Current Sponza has 4,096 m point ranges; the accepted older capture had 10 m.
Their timing difference is not an isolated code regression, and shortening ranges
is not an acceptable manufactured speedup. VSM/clustered shadow routing is not a
drop-in fix: UE5.7's shadowed clustered eligibility depends on its VSM one-pass
path. Review a larger architecture change separately after the measured shader
work, with its ownership, resource and quality implications.
