# EX07E — Implementation and comparison record

**E06 implementation and S9 automated qualification are complete.**
S1–S8 implementation and native integration tests are complete. S9 captures and
comparisons are in the [shadow-sharing results report](EX07E-shadow-sharing-results.md);
manual visual and numeric acceptance were approved on 2026-09-25. The final
evidence is committed in `b8f1376e1`. The current checkpoint
is in the [E ledger](../IMPLEMENTATION_STATUS.md#ex07e--work-items-and-resume-checkpoint).
The records below remain the accepted controls or explicitly provisional candidates;
new baseline acceptance still requires manual visual validation.

**EX07E is closed. The following sections preserve the earlier checkpoint history;
the [final report](EX07E-shadow-sharing-results.md) owns current acceptance.**
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
the [shadow-service contract](../lld/shadow-service.md#23-published-shadow-contract)
now spells out the retained local bias equation and its metric effect. The native
reproduction, both suite results and final targeted result are versioned in the
[caster-normal evidence register](baselines/ex07e-20260924/caster-normal/register.json).
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

The user must choose the E04 direction before further PCF implementation/capture:

1. **UE-aligned point-light PCF (recommended):** implement cube comparison
   sampling together with a coherent producer/receiver depth and bias contract.
   This changes the filtering/quality response and requires focused contact,
   grazing, seam, range, resolution and family checks plus manual visual approval
   before committing a newly established baseline. Projected filtering remains
   explicitly assessed against its separate UE path.
2. **Preserve the current filter response:** perform one bounded forward/
   translucency-only gather follow-up, retaining original deferred sampling.
   This pursues the small measured translucency opportunity without claiming UE
   PCF parity; mixed results still require a disposition rather than endless tuning.
3. **Keep the accepted nine-load filter:** discard the gather experiment and
   proceed with CPU/resource/sharing work. Record filtering as a retained quality
   choice and obtain an explicit E04 disposition; do not silently call parity done.

At that review checkpoint no selection was inferred from the factual question.
The subsequent explicit decisions below supersede that pending choice.

### User decisions after the PCF review

On 2026-09-24 the user explicitly approved **UE-aligned point-light hardware PCF
with coherent depth/bias handling**, superseding the earlier two-/three-option
questions. The raw-gather candidate remains an unaccepted experiment. Implement
and qualify the approved point-light path; do not treat this approval as manual
acceptance of a newly captured baseline.

The user additionally chose **1/5/29/29 hardware comparisons** for
Low/Medium/High/Ultra. UE's High and Epic conventional-shadow settings both use
29 comparisons. The [cube PCF implementation contract](EX07E-point-pcf-contract.md)
records the producer/consumer migration and its qualification gates. The
unaccepted raw-gather changes have been removed; projected spots and CSM retain
their accepted filtering. The new cube candidate changes quality and encoding,
so its future performance report must not imply identical output to the original
nine-comparison filter.

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
user's visual validation. Raw logs are under `out/analysis/ex07e/e04-pcf`, with
the hardware-PCF inspection and scene captures under
`out/analysis/ex07e/e04-hardware-pcf`; the results above are retained here so their
meaning does not depend on that transient directory.

The first Sponza hardware-PCF Tracy attempt is **excluded** at the user's request
because of CPU contention during capture. Its preflight (CPU mean 3.2%, GPU mean
38.4%) cannot establish in-run headroom. No timing from that attempt is accepted.
The rerun adds total-CPU sampling throughout the capture and records a separate
20–50-second CPU-load disposition; quiet startup alone is no longer sufficient.

#### Confirmed Sponza GPU regression — candidate not accepted

After the user cleared the machine, the rerun passed preflight (CPU mean/peak
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

Keep the approved 29-comparison High quality while investigating avoidable shader
cost. Do not assume all of the increase is inevitable, silently lower quality,
or accept/commit this as a new baseline. The next bounded investigation should
separate kernel cost from remaining point/finite-emitter specialization costs;
seek user direction if the remaining quality/performance tradeoff cannot be
resolved without changing the approved contract.

Local candidate evidence at
`baselines/ex07e-20260924/point-hardware-pcf/register.json` retains both timing
summaries, in-run CPU samples/disposition and the screenshot outside the transient
analysis directory. It remains uncommitted pending manual baseline approval;
the numerical findings above remain part of this report.

#### User disposition and bounded specialization result

The user directed stopping PCF tuning and moving to other optimization
opportunities once its work is justified. The depth/bias migration is necessary
for correct hardware comparison. The 29 comparisons implement the approved UE
High filter quality, rather than a minimum requirement for shadow correctness.
Retain the approved 1/5/29/29 mapping. Do not pursue a shadow-mask pass redesign
or another PCF parameter campaign on this instruction. New baselines still need
manual visual acceptance.

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
[automated validation records](validation/ex07e-pre-e06/README.md) preserve the
passing tests independently. These are candidate measurements, not manually accepted
new baselines. Broader dynamic/multi-view qualification and the final integrated
scene captures still belong to E08.

The additional non-Tracy interaction qualification passed **12 rows / 20 images**:
moving-1024, two-view-1024, orthographic-1024, five-point/nine-spot shadows, finite
sources and hemispherical spots, each in forward and deferred. Every image is
exactly equal to its current complete-list reference. This is automated current-
candidate qualification, not a claim that the new filter matches D images or that
the user has visually accepted it.

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
submission costs. The [final report](EX07E-shadow-sharing-results.md) records the
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
[E06 design proposal](EX07E-cross-view-shadow-sharing.md) records that direction.
The user subsequently agreed with the direction, requested independent review,
and deferred E06 to the **last EX07E implementation item**. Complete all other
optimizations and non-sharing correctness repairs first; resolve the independent
review before implementing E06, including its Graphics/Nexus changes. Final
integrated E08 qualification follows E06. This changes execution order, not scope
or the manual visual approval requirement for new baselines.
