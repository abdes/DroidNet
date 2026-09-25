# Integration qualification record

- [A completion audit](../EX07A/validation.md): contracts/interfaces and native proof.
- [B completion audit](../EX07B/README.md): closed exit checklist and final affected test results.
- [B detailed validation record](../EX07B/validation.md): historical numerical, image, instrument and benchmark evidence; not an additional task list.
- [EX07 implementation plan](../README.md): required workloads, contracts and gates for the remaining stages.

B's instrument-overhead runs qualify the measurement tools. The accepted
application baselines are credited to D above. The 1,024-light preview is fixture
correctness evidence; D supplies the runnable scene and its full-resolution
baseline using the repaired production path.

The glTF adapter preserves peak candela under Oxygen's squared cone profile,
retains explicit range and resolves omission through the approved finite fallback.
Invalid source values, collapsed GPU cone profiles and unrepresentable photometry
are rejected. The current 32-case import suite passes Debug and Release. Scene-v7
transport and the corrected-content result are included in the C validation above.

C's cone precision repair closes the 23 punctual-photometry residuals: the
2,160-input probe now reports zero failures, with maximum budget fraction
0.010959. Two formerly reserved words retain relative cone corrections in the
unchanged 80-byte record; both direct families use compensated FP32 evaluation.
An additional 294-case native matrix covers rotated axes, hard/soft/hemispherical
cones and outer angles down to 1e-18 radians (maximum budget fraction 0.196322);
omitting the corrections fails 22 negative controls. The admission verifier now
requires this matrix too. Debug/Release each pass 29 native ABI/instrument,
30 lighting-service and five image/material/workload tests. Both 233-module
shader archives rebuild. RenderDoc checks all 16 corrected spot records in the
33-light forward fixture and finite scene output. Oxytidy covers all eight
changed C++ files/headers with no changed-line findings or new suppressions;
seven existing whole-file findings remain. Seven admission-tool tests pass.
Evidence under `out/build-ninja/analysis/vortex/exposure-lightbench/ex07c`:
`cone-{native,cpu,images}-{debug,release}.json`, `cone-forward-report.txt`,
`cone-tidy-changed-lines.json` and `cone-physical-admission.log`.
This is punctual factor/ABI/image-consistency evidence, not full source calibration, finite-emitter
or shadow qualification, an official-resolution workload baseline, or a
performance improvement. Those C–F obligations remain open.

C's BRDF moment-data work now has a runnable offline generator under
`Test/Lighting/Tools/GgxMomentTable.cpp`; production does not depend on the CPU
test oracle. It stores float32 loss/B pairs, integrates their piecewise-linear
table for matching means, preserves exact grazing energy and records model and
payload identities. The 513x513 candidate fails seven of 787,456 cell-center/edge
checks (maximum difference 2.07628e-4); it is rejected. Increasing roughness
resolution to 513x1025 passes all 1,574,400 corresponding checks against a
1025x2049 CPU reference (maximum difference 1.33442e-4, with the separate 1e-5
reference allowance retained). The 4,214,800-byte candidate also passes the 55
existing directional and six mean certificates. Reusing B's independent Arb
oracle at the eight largest stencil residuals bounds the maximum sampled error
by 1.33443e-4. These are sampled numerical checks, **not a continuous-domain
certificate**. Runtime integration, native sampling, energy/reciprocity and
orthographic image evidence are recorded below.

The generator is oxytidy-clean. One/six-worker smoke outputs are byte-identical;
11 malformed-data/CLI controls and an underresolved-table control are rejected.
Build `Oxygen.Vortex.GgxMomentTable` in Release under `out/build-ninja`, then run
`Oxygen.Vortex.GgxMomentTable.exe <candidate.json> 513 1025 6`.
`InspectGgxMomentTable.py` checks anchors and optional `--refined-reference` data;
`CertifyGgxMomentTableSamples.py` reuses `python-flint==0.9.0` / FLINT 3.6.0 for
the ranked sample enclosures. Evidence in the same `ex07c` directory:
`moments-513x1025{,-anchors,-stencil,-sample-certificates}.json`,
`moments-513-stencil.json`, `moments-tool-validation.json` and
`moments-tidy-verified/`. `BuildGgxMomentData.py` packages the sampled evidence and
binary payload, then generates a private build header; the renderer links no
CPU reference implementation.

C's direct-BRDF cutover now uses one compensated correlated-GGX implementation
for forward/deferred shading and their direct-BRDF diagnostics. It preserves the
perceptual roughness floor, stable GGX peak, reciprocal compensation and approved
diffuse coupling; receiver cosine is evaluated once inside the common response.
The visibility denominator sorts and scales the two cosines before evaluation,
keeping valid extreme-grazing responses finite and reciprocal; `precise` prevents
underflow-producing reassociation. Symmetric half-vector Fresnel evaluation
preserves the same response when light and view are exchanged. Forward's extra
directional normalization and deferred's direct-light AO multiplier are removed.
Existing skylight consumers now use the matching integrated specular/diffuse
response. The old environment BRDF lookup fields and shader permutation are
removed from CPU/HLSL contracts, publication, processors and tests. The ambient
bridge still does not introduce an absent specular-IBL feature.

The LightingService-owned immutable RG32Float pair is shared across views and
initialized by one graphics-queue submission. Required data/allocation failures
prevent complete publication. Upload buffers and texture descriptors use deferred
retirement. RenderDoc matches all 4,214,800 GPU payload bytes to the packaged hash
`b3224a671f44b196302e83d0421d2cad1a5ae14ba457f776e914c656900c45d6`.
The 69 native moment queries pass their independent enclosures, and all original
565 BRDF residuals are eliminated. **Physical-probe admission passes all 2,649
required inputs in Debug and Release.** This includes the punctual/cone matrices,
108 direct-BRDF inputs, 69 moment queries and 18 analytic grazing cases down to
the smallest normal float. These selected-case gates do not close the full BRDF,
finite-emitter, ingress, shadow, lifetime or performance requirements.

The initial direct cutover passed 31 native ABI/instrument, 30 lighting-service
and five image/material/workload tests per configuration, with 233-module
shader archives. All 12
changed C++ files/headers are covered by a clean oxytidy run. Seven admission-tool
tests pass, and five damaged-package controls fail before emitting a build header.
Evidence in `ex07c`: `brdf-{native,cpu,images}-{debug,release}.json`,
`brdf-weighted-report.txt`, `brdf-tidy-clean/` and `package-controls/results.json`.
The combined shading repair adds these passing Debug/Release gates:

- **Energy and indirect agreement:** independent native hemisphere integration
  of the production lobes for six roughnesses, five view cosines and three
  material channels (dielectric, mixture, unit conductor), with 256/512-order
  refinement. All 90 cases pass; maximum reflected-energy/indirect discrepancy
  is 2.98420e-4 against 2e-3, and maximum refinement change is 4.63436e-4 against
  5e-4. Single scattering also matches the independent CPU moment reference;
  each lobe remains finite/nonnegative and the conductor diffuse lobe is zero.
- **Reciprocity:** 1,800 material/angle queries compare every RGB lobe separately
  with exchanged light/view directions. The expanded test exposed the prior
  asymmetric arithmetic; the fixed maximum error uses 0.01222 of the unchanged
  2e-5-relative plus 2e-7-absolute budget.
- **Orthographic rendering:** forward, deferred and diagnostic consumers share
  the projection-aware view direction. Twenty-four actual raster images cover
  opaque, masked and blended materials, camera translation/rotation and a
  perspective control. Orthographic highlights are spatially constant within
  the existing image budget; perspective highlights retain their variation.
- **Radiance and migration:** the existing direct/skylight endpoint, source
  rejection and masked-depth tests now use the compensated-model oracle with
  the existing packed-specular input interval. All 136 affected tests pass in
  each configuration (33 native, six image/material/workload, 30 lighting-service,
  63 environment-service and four radiance tests), for 272 test executions.
  The shader archive now has 217 modules after removing the obsolete option.
  Oxytidy covers all 12 changed C++ files/headers, with no new diagnostics;
  pre-existing diagnostics remain in unchanged source. The admission tool now
  requires the energy and reciprocity matrices; ten damaged-evidence controls
  are rejected.

Evidence in `ex07c`: `shading-{LightingGpuAbi,LightingImageReference,
LightingService,EnvironmentLightingService,radiance}-{debug,release}.json`,
`shading-admission-controls.json`, `shading-final-tidy/`,
`shading-radiance-tidy/` and `shading-existing-diagnostics.json`. The moment
payload is unchanged, so the existing RenderDoc byte-identity evidence remains
applicable. These are the stated sampled numerical/image gates, not a
continuous-domain certificate. At this historical checkpoint the 1,024-light
preview was 64x36, with remaining C work and D–F qualification still ahead.
The current stage table supersedes that status: D now supplies the closed
full-resolution baseline register; E/F retain candidate and final qualification.

C's finite-emitter implementation evaluates flux-conserving point spheres and
angularly shaped spot disks through one production helper shared by forward and
deferred shading. Radius zero uses the same punctual response. Positive-radius
integration cancels source area analytically, evaluates the independent 1 mm
guard/range window per source contribution, clips support at the receiver horizon
and spot/range boundary, and preserves sphere-interior and disk-backside zeros.
The initial adaptive Gauss implementation partitioned at the specular peak and
required two successive per-lobe refinements. **It was rejected for production
performance**: the exact reported MultiView proof configuration took 52.707 s
for 12 frames including startup. The bounded replacement and its native/runtime
evidence are recorded below. The following 72-case evidence describes the earlier
correctness checkpoint, not the current production integration algorithm.

Center-only distance, cone and normal rejection is removed from forward local
lighting. Finite spots and exact 90-degree soft spots route through the existing
six-face conventional shadow product. Cube far coverage includes `range+radius`;
zero-range sources request no shadow surface. Narrow projected spots retain their
authored angle without the old cosine clamp. Local visibility is selected by the
shadow-reference projection kind and applied once after emitter integration in
both paths; this also repairs missing forward local-shadow consumption. No
radius-dependent shadow penumbra is introduced.

Debug and Release each pass 94 affected tests: 34 native ABI/physical, eight
image/material/workload, 30 lighting-service, 18 shadow-service and four radiance
tests, for 188 test executions. The added evidence includes:

- 72 native sphere/disk cases compared per lobe with B's independent geometry
  integrators and BRDF oracle, using the already-qualified moment payload to
  isolate integration error. These cover three roughnesses, punctual/tiny-radius
  limits, the sub-millimetre guard, interior/backside zeros, horizon overlap,
  clipped range, hard cones and soft hemispheres. Maximum error consumes
  0.001157 of the fixed 1%-relative plus 5e-6-absolute per-lobe budget.
- 36 production raster cases prove nonzero finite-source contribution when the
  source center is outside the punctual range, below the normal horizon, or
  outside the spot cone. Zero-radius controls are black; forward/deferred
  opaque, masked and blended receivers agree within the existing material budget.
- 24 native occluder images prove point/hemispherical-spot cube-shadow consumption
  in both rendering paths and all three material domains. A separate CPU record
  test verifies selection identity, all six projected support endpoints and
  zero-range exclusion.

Both shader archives have 217 modules. Oxytidy covers all eight changed C++
files/headers with no new diagnostics; unchanged-source warnings are retained.
Physical admission now requires the 72 finite-emitter cases, and five missing,
skipped, incomplete, excessive-error and nonfinite-evidence controls are rejected.
Evidence in `ex07c`: `finite-{LightingGpuAbi,LightingImageReference,LightingService,
ShadowService,radiance}-{debug,release}.json`, `finite-admission-controls.json`,
`finite-final-tidy/`, `finite-clean-tidy/` and `finite-existing-diagnostics.json`.
At that checkpoint, the inherited four-cube/eight-projected allocator limits and
renderer-wide allocation admission remained open. The subsequent repair removes
those fixed limits and isolates shadow backing by view. Replacement surfaces are
published transactionally; old backing remains charged through deferred retirement
and outstanding owners. Failed growth rejects the affected view and permits a
smaller request to recover using its retained backing.

Lighting now owns a shared 4 GiB allocation ceiling, the 128 MiB compact-index
sublimit and the manually verified 256 MiB driver headroom. Tagged native allocations
charge actual backing requirements; uploads, moment textures, deferred geometry
and shadows share the owner. The ceiling is not preallocation. Dedicated lighting
staging starts at 64 KiB per partition and grows transactionally. Failure reports
include requested/available bytes and suppress duplicate reports.

Debug and Release allocation, headless, upload, lighting-service and shadow-service
checks pass. Native tests validate backing size, budget rejection, deferred release
and recovery. Two native image tests prove five cube plus nine projected shadows
across isolated views, and 64 MiB rejection/recovery without partial publication.
Release evidence is in `ex07c/performance-{Oxygen.Graphics.Common.AllocationBudget,
Oxygen.Graphics.Headless.All,Oxygen.Vortex.RingBufferStaging,
Oxygen.Vortex.TransientStructuredBuffer,Oxygen.Vortex.LightingService,
Oxygen.Vortex.ShadowService}-release.json`, `performance-native-release.json`
(allocation cases) and `performance-shadow-release.json`.

**2026-09-24 stable performance-repair checkpoint.** The performance work prioritized
work during C and explicitly prohibited further linting. Shipping finite-source
work now uses seven-point projected-area cubature on smooth support, fixed four-
or eight-point rules per dimension on clipped pieces, and importance coordinates
for narrow specular peaks. There is no runtime refinement loop. View/material
BRDF terms are shared; moment texture filtering retains FP32 interpolation.
A hardware-filtered candidate was rejected after it failed to improve measured
GPU time. The physical model and error budgets are unchanged.

Deferred lighting rejects reverse-Z clear depth before material/shadow work,
loads each local-light surface once, rejects unsupported finite-spot receivers
early, and batches all directionals into one draw per view. The three-source
orthographic image matrix checks the complete batch with separate RGB sources.
Shadow recording reuses immutable root bindings and raster state across slices,
while still updating each slice's pass constants. Queue retirement now waits only
for the reused frame slot on each queue, preserving a bootstrap drain and explicit
resize/shutdown flushes. The native swap-chain index replaces guessed rotation;
uncapped HWND presentation requests tearing when supported. Factory outputs use
`dx::ISwapChainFactoryOutput` and COM RAII before querying `dx::ISwapChain`, retaining
the engine's centralized interface aliases.

The shared `--resolution WIDTHxHEIGHT` option accepts `x` or `X` in all eight demos.
It resolves to physical pixels through the platform window API, verifies actual
size and fullscreen state, and rejects unavailable modes rather than silently
changing the workload. The selected non-4K profiling resolution is 1920x1080.

Release and Debug each pass 93 focused checks: 36 native lighting ABI/physical,
10 native image, 19 queue, five graphics lifecycle, 17 headless and six shared CLI.
All eight demos build in both configurations. The finite-source matrix expands
to 90 cases, including on-axis/reflected highlights and an inner-cone boundary;
maximum error consumes 0.334511 of the unchanged per-lobe budget. Physical
admission passes 2,649 inputs, 1,800 reciprocity queries, 90 integrated material
cases and all 90 emitter cases. Eight Python admission-tool tests pass. No linting
was run for this repair. The RenderDoc directional analyzer was migrated for
batching and syntax-checked; no new RenderDoc capture was run.

Native Tracy captured 600 Release frames at verified fullscreen 1920x1080 with
`--directional-array-proof true --offscreen-proof-layout true --pip-wireframe false
-v=-1 --fps 0 --vsync false --debug-layer false --aftermath false`. All 600 presents
used the tearing path. After 64 warmup frames, 535 frame intervals have mean
13.635 ms, median 12.741 ms and p95 15.027 ms (73.34 profiled FPS). The user's
75 FPS observation without Tracy is accepted and was not rerun. Earlier windowed
captures have different resolution and are not a matched comparison with this run.

Exact GPU frame ordinals 64–598 contain three deferred views: main 1920x1080,
PiP 864x486 and offscreen preview 512x288. Their mean deferred totals are
5.058/1.476/0.952 ms. Across those views, the single spot costs 3.581 ms, the point
1.762 ms and the three-directional batch 1.116 ms. An additional 1.027 ms lies
between child scopes; the intervals include native Tracy query resolves and may
include scheduling stalls, so they are not assigned to shader arithmetic.
Deferred CPU recording is 0.128 ms/frame. The fourth, 256x256 offscreen forward
view is outside the deferred totals. The dominant measured target is finite local
lighting, especially the main-view spot; further optimization is directed there.
Tracy does not provide per-expression ALU/texture or integration-branch attribution.

Evidence is under `out/build-tracy-ninja/analysis/vortex/exposure-lightbench/ex07c`:
`final-Oxygen.Vortex.{LightingGpuAbi,LightingImageReference}-{debug,release}.json`,
`final-Oxygen.{Examples.DemoShell.GraphicsToolingCli,Graphics.Common.Queues,
Graphics.Common.GraphicsLifecycle,Graphics.Headless.All}-debug.json`,
`resolution-cli-release.json`, `holistic-Oxygen.Graphics.{Common.Queues,
Common.GraphicsLifecycle,Headless.All}-release.json`, and
`multiview-1080p-{fullscreen.tracy,summary.json,detailed.json,analysis.md}`.
The capture SHA256 is
`e9944e412c1bb952925faa157fd7e9be961588e1901433164e7f6d84b052d6c1`.

This historical regression-repair checkpoint and controlled application profile
did not itself close C or D–F. D has since closed with the official many-light
and application baseline register. The current stage table owns the remaining
closed A–F acceptance, including the manually verified editor workflow.

**Reviewed finite-light optimization, 2026-09-24.** The follow-up specializes the
deferred spot source family, defers highlight/partition setup until the fast path
is rejected, and uses scalar disk-boundary access with source-appropriate loop
expansion. Explicit point-family forcing was rejected after a measured regression;
the retained point path preserves its runtime record kind. Review centralized the
boundary-access policy, documented its capacity, removed an unused deferred BRDF
wrapper and fixed the sphere-rim 0/0 intermediate. Deferred batching and shadow
slice bindings were reviewed with no further pass change required.

The optimized 1080p capture measured 9.280 ms mean / 9.941 ms p95 (107.75 profiled
FPS); main spot/point were 1.807/1.066 ms. After review, the requested fullscreen
2560x1440 capture measured 13.098 ms mean / 13.757 ms p95 (76.35 profiled FPS).
The uppercase `--resolution 2560X1440` path passed mandatory framebuffer checks;
all 600 presents used tearing with FPS target zero. Excluding 64 warmup frames,
535 complete GPU frames have deferred mean/p95 8.427/9.022 ms. Main/PiP/preview
means are 6.037/1.742/0.648 ms; spot/point/three-directional totals are
4.394/2.475/1.500 ms. CPU deferred recording is 0.116 ms. Main-view cost scales
approximately with pixel count while the fixed preview stays stable, supporting
finite local pixel shading as the dominant measured bottleneck. Per-expression
ALU/texture/occupancy attribution is not available from these Tracy scopes.

The reviewed final code passes 36 native lighting and 10 image checks in each of
Debug and Release; physical admission and the unchanged 90-case emitter budget
pass, with maximum fraction 0.334511. Both 217-module shader archives rebuild.
No linting or new test suite was introduced. Evidence in the same Tracy `ex07c`
directory: `reviewed-{LightingGpuAbi,LightingImageReference}-{debug,release}.json`,
`multiview-1080p-spot-final-{comparison.json,tracy}` and
`multiview-1440p-reviewed-{comparison.json,analysis.md,tracy}`. The 1440p capture
SHA256 is `dd42eb5025737639e566ce3695442e53b4c0e058b41eb6685ff98ce61b10bd7c`.
These captures preserve the earlier model's performance history. The accepted
model-2 measurements below supply the current MultiView operating point credited
to D. The many-light operating point is now recorded in the D report above.

**Model-2 implementation and measurement checkpoint.** Contract update
`0a35e790d` and implementation `0e0741619` replace source quadrature with analytic
finite-source shading, center-cone attenuation, view-dependent compensation and
one 32x32 hardware-filtered RG32Float energy texture. The old numerical evaluator
and dense data are retained under Test. Ordinary spots use cone proxies and one
projected shadow; hemispheres retain cube coverage. CPU/HLSL records and all
consumers/tools migrate together, without a shipping legacy path.

Debug and Release each pass 94 owning checks: 36 native lighting/ABI, 10 image,
30 lighting-service and 18 shadow-service. The 96-case finite-source comparison
includes an antipodal fully rough source; the zero-half-vector case is finite.
Release's added-case CPU reference axis was normalized and that case alone was
rerun; consolidated evidence preserves the initial run and focused rerun. Eight
measurement-validator tests pass. No linting was run.

At fullscreen 2560x1440 with FPS target zero and tearing, the final 600-frame
Tracy run (64 warmup frames excluded) records mean/median/p95 frame intervals
7.804/7.380/10.760 ms, or 128.14 profiled FPS. Previous same-recipe measurements
were 13.098 ms / 76.35 FPS. Across the three deferred views, spot time falls
4.394->0.661 ms, point 2.475->0.865 ms and deferred total 8.427->2.901 ms.
The native BRDF allocation falls from 4,718,592 to 65,536 bytes; payload falls
from 4,214,800 to 8,192 bytes. These are measured runs, not an uninstrumented FPS
claim or final EX07-wide acceptance.

Native integrated-material energy/indirect discrepancy is at most 0.703% in the
recorded matrix. The full offline LUT grid reports worst/p99 unit-conductor
energy error 2.888%/0.354%; square-root view mapping improves the same-size linear
grid's 10.839%/4.257%. Per-case finite-source and cone differences are preserved
in the report rather than rejected against superseded model-1 budgets.

RenderDoc verifies all three deferred views share the hash-matched energy texture
and each ordinary spot uses a 144-vertex cone plus one projected shadow. The
frame-120 image was overexposed and is superseded by frame 2000; all four captured
exposure states are within 0.000162 EV of target. Source transport, shadow counts,
actual 2560x1440 output and replay shutdown are verified.

Evidence in `out/build-tracy-ninja/analysis/vortex/exposure-lightbench/ex07c`:
`model2-results.{md,json}`, `model2-LightingGpuAbi-release-validated.json`,
`model2-finite-release-rerun.json`, `model2-*-debug.json`,
`model2-{LightingImageReference,LightingService,ShadowService}-release.json`,
`multiview-1440p-model2-final.tracy`, and `model2-settled-report.{txt,png}`.
The measured quality/performance result was accepted. It is credited to D
within its measured MultiView scope; no new capture is required to transfer that
credit. The existing stage table records closed C–E work and closed F acceptance.

# EX07C — Final validation and closure

**Closed on 2026-09-25.** The omitted caller builds and executions are qualified,
the importer follow-up is reconciled, and the defect discovered during validation
is fixed in `3fb0a8b17`. EX07F remains the final combined acceptance stage.

## Fresh qualification

Only the existing `out/build-ninja` tree was used, with Release and Debug
configurations. No build tree was created. The exposure workload ran in Release
with Tracy off; its implementation deliberately rejects Debug timing runs.

| Owning target                              | Release                              | Debug                                |
| ------------------------------------------ | ------------------------------------ | ------------------------------------ |
| `Oxygen.Scene.EnvironmentComponents.Tests` | Built; **5/5 passed**                | Built; **5/5 passed**                |
| `Oxygen.Vortex.Exposure.Benchmarks`        | Built; **I02 event scenario passed** | Built; Release-only scenario not run |
| `Oxygen.Cooker.AsyncImportCore.Tests`      | **190/190 passed**                   | **190/190 passed**                   |
| `Oxygen.Cooker.AsyncImportTexture.Tests`   | **149/149 passed**                   | **149/149 passed**                   |
| `Oxygen.Cooker.AsyncImportGltf.Tests`      | **32/32 passed**                     | **32/32 passed**                     |
| `Oxygen.Cooker.AsyncImportFbx.Tests`       | **3/3 passed**                       | **3/3 passed**                       |

This is **759 freshly executed cases**, with no failures or skips in the accepted
runs. The caller migration in `ff1746220` is now exercised: RGB atmosphere disk
scale is checked by EnvironmentComponents; the I02 scenario changes and restores
attached directional-light intensity through `EditLight` at event frames 300/360.

The unchanged I02 acceptance path executed all 16 operations, including exposure
mode changes, sharing, view removal/recreation, layout changes and delayed GPU
status delivery. It retained 1,200 CPU status observations and 62 GPU checkpoints.
The accepted run used automatic calibration: **6,000 measured frames / 43.989 s**,
with complete, valid GPU timing. CPU/GPU preflight passed before launch.
These are correctness/execution proofs; incidental timings are **not a newly
established performance baseline** or a replacement for D/E results.

The initial 2,400-frame attempt passed the transition checks but failed the
existing 30-second sampling minimum at 17.844 s. That rejected attempt is
preserved. The successful rerun used the benchmark's existing automatic frame
count; no assertion or threshold was weakened.

## Defect found and repaired

`TexturePipelineEdgeTest.MaterialPresetsKeepCompressionAndMipChains` aborted on
its second import. `ImportEventLoop::Stop()` released the constructor-owned ASIO
work guard; `Run()` restarted the context without restoring that guard. The next
run could exit while a ThreadPool result was still pending.

Each Run now owns a fresh guard and releases it before restarting the context.
Stop only operates on thread-safe ASIO state. The new regression executes three
successive worker-backed runs on the same loop; the existing multi-policy texture
checks and all four importer suites also pass in both configurations. The actual
glTF and FBX Sponza imports ran successfully; none were skipped for missing assets.

## Importer and cooked-content reconciliation

The existing policy checks verify material BC7 presets and full mip chains,
explicit/source-format choices, and preservation of HDR radiance. Both model
adapters select the material preset unless an explicit override is supplied.
The D3D12 allocation admission guard still applies the hard driver-budget flag
only when a scoped budget owner exists; unowned material resources retain their
ordinary allocation policy. The previously accepted D/E native scene loads and
shutdown checks remain credited for that path.

The active Sponza content is already corrected:

- **72 textures:** 25 BC7 sRGB and 47 BC7 linear; every texture has **13 mips**.
- Every OTEX sidecar exactly matches its resource-table entry and full mip count.
- Total texture payload is **1,610,901,504 bytes**, rather than the rejected
  reimport's 121 RGBA8 textures / 7,539,563,776 bytes.
- The scene and container hashes still match the visually accepted final E
  captures, before and after the importer tests. No accepted content was replaced.

Consequently the old instruction to recook the uncompressed Sponza generation
no longer describes the active application content. Real-source import tests,
policy regressions and current-content inspection close that follow-up.

## Credited earlier C evidence

The archived results were inspected, not rerun or silently expanded:

- **542 native Debug cases**, with zero recorded failures across the final C
  suites and focused image/failure/ABI records.
- **123 PakGen cases**, with no failures/errors/skips.
- **88 distinct named managed/editor/native-bridge cases** across the three TRX
  reports. Their execution totals overlap; duplicate cases are not counted twice.
- The original C scene-v7, atomic-edit, light-property transport, photometry,
  receiver/contact-shadow and per-view failure work remains credited, together
  with the previously recorded 16-scene / 124-asset / 29-resource package and
  manual visual acceptance of the conventional-shadow repair.

[F engine-side acceptance](../EX07F/validation.md) subsequently credits these
results, with final editor acceptance now confirmed. C has no remaining
implementation or validation item.

## Durable evidence

[Qualification summary and artifact checksums](evidence/validation/ex07c-20260925/summary.json)
contains all fresh test counts, build/source/binary identity, exposure operations,
content inspection and credited historical counts. Original test/build logs,
JSON/TRX/XML reports, rejected attempts and exposure event records are stored
losslessly as gzip files beside it. The historical report retains its original
withdrawn-completion notice as provenance; this report supersedes that notice.

[Authoritative tracker](../README.md#stages-and-ownership).
