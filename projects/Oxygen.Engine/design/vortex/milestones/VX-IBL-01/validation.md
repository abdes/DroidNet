# VX-IBL-01 validation

**S1–S8 validated.** [S7 integrated qualification](#s7-integrated-qualification)
closes the six extractions, performance, captures, installed SDK and applications.
[S8 correctness and performance](#s8-correctness) closes seven atmosphere/fog
regressions at the existing sample/resource budgets. The [acceptance table](README.md#acceptance) links every
required result. The S1–S6 audit records
baseline provenance; earlier measurements retain their original source/build scope.

Read: [S8 correctness](#s8-correctness), [S7 qualification](#s7-integrated-qualification), [S7 checkpoints](#s7-checkpoints), [integrated results](#integrated-results), [checkpoint evidence](#checkpoint-evidence),
[reproduce](#reproduce).

## S8 correctness

Validated on **2026-09-28**, implementation **`82e99f11b`**.

**S8.1 passes:** the production volumetric shader now uses incident propagation
for the HG cosine. The native regression covers positive/zero/negative anisotropy
and both atmosphere-light slots. It reproduced reversed scattering before the
fix; **18 fog tests pass in Debug and Release**, including shadow-source routing
and unchanged extinction. The shared phase helper and local-fog caller are unchanged.

**S8.2–S8.4 pass:** seven native sky/fog checks pass in each configuration.
They exercise both explicit slots, none/both/disable/re-enable transitions,
zero/scalar/RGB factors, unchanged-input reuse and actual volumetric fog ambient.
The 272-byte CPU/HLSL constants preserve layout while replacing count-based
participation with two enable flags. Shared sky/AP and sky-only factors apply once.

The 64 native sample directions integrate constant/linear radiance without the
former 14.18% linear bias. A 72-azimuth synthetic positive-cosine⁸ radiance lobe at 0.3 rad elevation
has **1.98% maximum relative error**, versus **48.30%** for UE5.7’s fixed seed, against a
32,768-direction reference. This qualifies angular quadrature, not atmospheric
transport accuracy. Ray count, integration steps, LUT formats and dispatch count
are unchanged. The probe stays outside the production shader archive.
Reproduce with Exposure filters `ExposureGpuTest.DistantSky*`,
`ExposureGpuTest.CapturedAtmosphere*`, `ExposureGpuTest.CaptureSkyLut*` and
`IblSurfaceGpuTest.CapturedAtmosphereAndFogHalfMatchesCanonicalAcrossPaths`, plus
`ExposureGpuTest.VolumetricPhaseScattersTowardEachAtmosphereLight`.

**S8.5–S8.7 pass:** six native fixtures validate per-pixel orthographic ray
origins (including translated/rotated views and both depth conventions), unchanged
perspective distance, continuous scattering strength and preserved extinction.
Real deferred/forward opaque, masked and translucent surfaces retain extinction;
illuminated AP holdout removes added RGB. Sky/disks and independent fog preserve
premultiplied coverage over a colored background at two exposure scales. Captured
IBL matches independently regenerated non-held products and reuses products for
holdout-only edits.

All selected cases pass in Debug/Release: **42 atmosphere/fog/sky tests**,
**56/54 IBL GPU tests** and **63 environment-service tests** per configuration.
Debug totals combine the suite with two focused reruns after fixture corrections.
All modified C++ passes oxytidy; the extra-high review is clear. A native framebuffer
regression also verifies inferred attachment formats, which backend depth clears
require. Production shaders contain no new testing diagnostics.
Reproduce the new cases with `AtmosphereCompositionGpuTest.*`,
`ExposureLightingGpuTest.AerialExtinctionSurvivesZeroStrengthAcrossPaths` and
`ExposureLightingGpuTest.CapturedIblIgnoresAtmosphereAndFogHoldout`. The integrated
Exposure filter is `*Fog*:*Sky*:*Aerial*:*Atmosphere*`; LightingGpuAbi uses
`*Ibl*:*HeightFog*`, and EnvironmentLightingService runs in full.

**Application and SDK:** Async's isolated `lighting` UI test passes with the
D3D12 debug layer. RenderDoc inspection confirms readable daylight materials,
spotlight shadow toggling (18,357 changed pixels) and zero lights-off RGB. Replay
handles close cleanly. Matching Debug/Release SDK dev/runtime/data components are
installed; installed shader archives match their corresponding builds.
Reproduce with the [Async lighting procedure](../../../../Examples/Async/README.md#validation)
and its existing replay analyzer. User settings are not used by the test.

The pre-S8 native Release baseline uses the S7 production code (`5d54e438f`),
RTX 3080, 1920×1080, default power and unchanged benchmark settings. M01 records
12,000 warmed frames; the IBL authoring scene records 1,800 at 60 Hz.

| Workload / affected GPU scope    | Before mean / p95 (ms) | S8 mean / p95 (ms) |
| -------------------------------- | ---------------------: | -----------------: |
| M01 / volumetric fog             |          0.133 / 0.131 |      0.130 / 0.130 |
| M01 / AP volume                  |          0.105 / 0.101 |      0.099 / 0.101 |
| M01 / AP composition             |          0.200 / 0.201 |      0.200 / 0.201 |
| M01 / fog composition            |          0.212 / 0.218 |      0.201 / 0.201 |
| M01 / sky                        |          0.007 / 0.011 |      0.007 / 0.008 |
| Authoring / distant sky          |          0.014 / 0.014 |      0.016 / 0.017 |
| Authoring / captured distant sky |          0.012 / 0.014 |      0.015 / 0.017 |
| Authoring / AP volume            |          0.190 / 0.202 |      0.191 / 0.208 |
| Authoring / AP composition       |          0.214 / 0.243 |      0.230 / 0.253 |
| Authoring / fog composition      |          0.232 / 0.373 |      0.233 / 0.252 |
| Authoring / sky                  |          0.007 / 0.011 |      0.006 / 0.008 |

All frames have valid timestamps; no samples are trimmed. The largest mean
increase is **0.0165 ms** in authoring AP composition. The two distant-sky
passes each add about **0.002–0.003 ms**; these costs do not warrant additional
complexity or reduced quality. The authoring workload passes its existing
IBL gates: update GPU union **p95 0.459 / p99 0.770 ms**, 1,800 same-frame
publications, source age zero, two unchanged product slots and stable product
allocation bytes. M01 completes its 12,000-frame measurement window.

Reproduce correctness with Exposure filter
`*Fog*:ExposureGpuTest.VolumetricPhaseScattersTowardEachAtmosphereLight`.
For timing, run `ExposureProfilingOverheadTest.DISABLED_ReleaseMixedBaseline`
with `OXYGEN_EXPOSURE_BASELINE_CASE=M01`, `OXYGEN_EXPOSURE_BASELINE_FRAMES=12000`
and a fresh `OXYGEN_EXPOSURE_BASELINE_RUN`; then run
`IblSceneBenchmark.DISABLED_Authoring` with a fresh `OXYGEN_IBL_TIMING_OUTPUT`.
Both use the Release Exposure.Benchmarks executable and
`--gtest_also_run_disabled_tests`. Keep generated files under ignored `out/`.

## S7 checkpoints

### S7 integrated qualification

Validated on **2026-09-28**, implementation `5d54e438f`. All six owner migrations
and their named production adopters are complete; S1–S6 tolerances are unchanged.

| Gate                                 | Result                                                                                                                                                                                                                                                                   |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Debug/Release integration            | Engine, RenderScene, Async, Interop and editor builds pass. All modified C++ passes oxytidy; owning fixtures and exported C++20 APIs compile.                                                                                                                            |
| Native regression                    | 84/81 registry tests, 29 upload tests and both backend-lifetime tests pass in Debug/Release. IBL passes 43/41 tests; 13 exposure/image tests per configuration include the 144-case material matrix.                                                                     |
| Captured atmosphere                  | RenderDoc verifies 319,504 frozen LUT bytes, 2,097,120 product values and complete-generation publication.                                                                                                                                                               |
| Incremental products and orientation | Five recorded batches contain 694 dispatches. Generation 980 stays complete while 981 remains pending until its final producer. All 24,576 labelled face/pixel values match exactly.                                                                                     |
| Performance                          | The accepted [isolated/full-scene baseline](#s7-performance-qualification) passes cost, latency and resource-population gates.                                                                                                                                           |
| Installed SDK                        | Matching dev/runtime/data components installed for Debug/Release. Actual Interop and native command consumers compile with `stdcpp20`; nine environment-command tests pass per configuration. Installed shader archives match the normal builds byte-for-byte.           |
| Editor smoke                         | Real Inspector edits, Undo/Redo, view recreation, Save/reopen and scene replacement retain current, usable IBL products at source age zero. Paused publication followed by viewport destruction also passes. The native log loads the selected Debug SDK shader archive. |
| DemoShell smoke                      | RenderScene's `ibl_controls` and `ibl_source_and_fog` widget cases pass using isolated settings; user settings remain byte-identical.                                                                                                                                    |

The Release catalog contains 237 production modules, with no test-source entries
or qualification defines. Earlier [LUT producer/consumer](#s71-immutable-lut-uploads)
and [retained-reader captures](#s73-retirement-accounting) retain their stated
scope; current native and image checks exercise their integrated consumers.

Reproduce the added capture checks with Release tests
`ExposureGpuTest.CapturedAtmosphereUsesGlobalAnchorAndIgnoresViewExposure`
(`OXYGEN_EXPOSURE_CAPTURE`) and
`IblConvolutionGpuTest.MultiFrameJobPublishesOnlyCompleteProducts`
(`OXYGEN_IBL_CAPTURE`). Set each variable to an absolute prefix under ignored
`out/`, then replay with `tools/shadows/Invoke-RenderDocUiAnalysis.ps1` and
`AnalyzeRenderDocCapturedSky.py` / `AnalyzeRenderDocIblJobs.py` respectively.
For the editor, build the app, Interop native tests and WorldEditor UI tests with
`MSBuild.exe /m`; run `EnvironmentCommandCliTests` and the UI cases
`CapturedSkyInspectorHistoryReopenAndViewRecreationUseCurrentProducts` and
`PublicationPauseThenViewportDestructionKeepsNativeFrameAlive` using VS VSTest.
DemoShell uses `OXYGEN_UI_TEST_FILTER` and `OXYGEN_UI_TEST_SETTINGS` to select the
widget case and isolated settings. Captures, logs and test results stay in `out/`.

### S7.5 group reductions

All seven 64-lane specializations pass correctness qualification over `76cd94a`
with S7.5 changes. Normal Debug/Release builds publish all 237 production modules.
IBL convolution/lifetime/admission passes **43 Debug / 41 Release tests**; **13
exposure/image tests** pass in each, including the **144-case material image
matrix**. These exercise SH sum, max/validity, precision minimum, distant sky,
and all three exposure tuples. Existing tolerances are unchanged.

**Shader cost:** 233 Release DXIL payloads are byte-identical. Distant sky and
exposure only reorder declarations; both SH shaders hoist an integer index add
out of the inner term loop. Instruction counts, groupshared allocations and
barriers are unchanged:

| Entry                        | DXIL instructions before → after | Shared bytes |  Barriers per reduction |
| ---------------------------- | -------------------------------: | -----------: | ----------------------: |
| `IblShCS`                    |                        587 → 587 |       10,240 |                       7 |
| `IblShReduceCS`              |                        556 → 556 |       10,240 |                       7 |
| `VortexDistantSkyLightLutCS` |                        738 → 738 |          768 |                       6 |
| `GatherSuitabilityMaximum`   |                    8,673 → 8,673 |        4,352 | 7 in each selected tree |

Counts describe DXIL instructions, not device ISA or GPU cycles. IBL keeps its
looped tree; exposure keeps three alternative unrolled trees. ShaderBake records
the new include in 25 modules, and the native tone probe names it as a dependency.

The [S7 performance qualification](#s7-performance-qualification) passes under
the approved isolated/full-scene protocol. No sample counts, quality limits or
production shader diagnostics changed.

Reproduce with LightingGpuAbi filter `IblConvolutionGpuTest.*`. In Exposure, use
`ExposureGpuTest.Filter*`, `HardwareFilter*`, `ProducerStoreBounds*`,
`PreEnvironmentRange*`, `ProspectiveProductBounds*`, `ComposedSceneAdmission*`,
`DistantSkyRadiance*`, `CapturedAtmosphere*` and `CaptureSkyLut*` (each prefixed with
`ExposureGpuTest.`), plus
`IblSurfaceGpuTest.ImagesAgreeAcrossPublishedAndOffscreenPaths`; join filters with
`:`. For timing, use `IblUpdateBenchmark.DISABLED_AuthoringSunUpdates` and the
[existing benchmark procedure](#earlier-slices). Compare ShaderBake module DXIL
with `dxc -dumpbin`; keep disassemblies, timings and logs in ignored `out/`.

### Frame-start upload retirement

Upload retirement polls the completed fence instead of draining the queue at
every frame start. Graphics still waits for recycled frame slots across all
queues before staging reuse. Device loss remains an error, and shutdown still
waits for the last submitted upload fence.

**29 upload tests and two frame-slot tests pass in Debug and Release.** New
cases cover unrelated pending work, pending-upload results and device-loss
rejection. All changed C++ is oxytidy-clean with no added suppressions.
Tracy's 1,924-frame comparison removes **1,924 frame-start fence waits**; CPU
frame-start p95 falls from **3.259 ms to 0.257 ms**. This is a CPU synchronization
fix; the GPU power-policy qualification measures a separate effect.

Reproduce with `Oxygen.Vortex.UploadCoordinator.Tests` and Graphics Common Queues
filter `QueuesStrategy.Frame*`. Capture the isolated authoring workload in the
Tracy build to inspect `Vortex.OnFrameStart` and `D3D12.FenceWait`.

### S7 performance qualification

RTX 3080 native Release, 2026-09-27, `76cd94a` plus S7.5 and poll-only upload
retirement; 120 warmup + 1,800 frames at 60 Hz. All cost, latency and population
gates pass. Isolated runs use stable base core clocks (1,440 MHz; observed memory
9,501 MHz); 1920×1080 scene runs use default power. Formats, samples and limits
follow [the performance contract](../../lld/captured-sky-ibl.md#43-performance-and-latency-gates).

| Workload           | IBL GPU p95 / p99 (ms) | Whole-frame GPU p95 / p99 (ms) | First-use wall / IBL GPU (ms) |
| ------------------ | ---------------------: | -----------------------------: | ----------------------------: |
| Isolated authoring |          0.430 / 0.656 |                              — |                39.813 / 0.444 |
| Isolated runtime   |          0.172 / 0.175 |                              — |                30.258 / 0.454 |
| Static scene       |                  0 / 0 |                  8.581 / 9.726 |                88.156 / 0.376 |
| Runtime scene      |          0.228 / 0.242 |                  8.739 / 9.652 |                88.871 / 0.330 |
| Authoring scene    |          0.419 / 0.778 |                  8.858 / 9.994 |                89.606 / 0.377 |

CPU recording p95 is 0.966/0.951 ms for isolated authoring/runtime. Whole-scene
CPU p95 is 5.477/5.963/6.134 ms for static/runtime/authoring. First-use wall time
includes initialization; scene geometry uploads may still be pending afterward.
Runtime completes within three submitted frames with source age at most four;
all 1,800 authoring updates publish in their submitted frame. Static work remains
zero. Product memory/registrations remain constant: 9,125,888 bytes / 140 for
static, and 18,235,392 bytes / 152 for either changing scene, with at most two
product slots.

The same-binary authoring control measures 4.653/11.876 ms p95/p99 under dynamic
power versus 0.430/0.656 ms at stable base clocks. This establishes the isolated
workload's power-state sensitivity; it does not change production power policy.
The benchmark restores normal power at exit and records `stable_power` in its
result. Its summary command returns failure when a cost or latency gate fails.

Reproduce isolated authoring/runtime with `OXYGEN_IBL_STABLE_POWER=1` and the
[existing benchmark commands](#earlier-slices). For default-power scenes run
`IblSceneBenchmark.DISABLED_Static`, `DISABLED_Runtime` and `DISABLED_Authoring`
separately, then `tools/vortex/SummarizeIblScene.py <run-directory>`.
Use a fresh ignored output directory per run. The remaining integrated checks
are recorded in [S7 qualification](#s7-integrated-qualification).

### S7.6 cubemap geometry

Shared cube geometry passes Debug/Release qualification over `82e20534c` with
S7.6 changes. Two native tests cover **179 direction/face cases** and **1,093
texel areas**, using independent signed face bases and spherical-triangle areas.
Five existing IBL cases cover cube faces/mips, Oxygen axes, source rotation and
hemisphere policy; the **144-case image matrix** passes in both configurations.
Existing product/image tolerances are unchanged.

All **237 Release production DXIL payloads are byte-identical** to the pre-change
baseline, including forward debug-face variants. ShaderBake fingerprints the new
include in **127 modules**; the production catalog still contains 237 modules.
The new probe is test-only. No shader instruction or resource-cost retuning was
needed. See [integrated S7 qualification](#s7-integrated-qualification).

Reproduce geometry with LightingGpuAbi filter `LightingGpuAbiTest.CubemapGeometry*`;
product filters are `IblConvolutionGpuTest.ConstantCubePreservesEveryFaceAndMip`,
`IblConvolutionGpuTest.DirectionalCubeMatchesAnalyticDiffuseInOxygenAxes`,
`*Rotation*` and `*Hemisphere*`. Run Exposure filter
`IblSurfaceGpuTest.ImagesAgreeAcrossPublishedAndOffscreenPaths` for images.
For shader identity, compare the `DXIL` chunks in ShaderBake's per-request module
artifacts from normal Release builds of the baseline and this extraction.
Keep generated artifacts in ignored `out/`.

### S7.2 bounded GPU feedback

The shared pool and IBL, exposure and light-grid adopters pass focused qualification
on 2026-09-27 over `a037697d3` with S7.2 changes. Normal builds include the exposure
benchmark harness; its event-frame constants and JSON schema are unchanged.

| Check                                                    |        Debug |      Release |
| -------------------------------------------------------- | -----------: | -----------: |
| PostProcess / Lighting / Environment CPU suites          | 27 / 34 / 63 | 27 / 34 / 63 |
| New native feedback regressions                          |            8 |            7 |
| IBL diagnostics, exposure status and precision consumers |           17 |           16 |
| Spatial-grid membership, growth and fallback             |            5 |            5 |

The native cases cover moves, pool exhaustion, recorded/accepted abandonment,
GPU-fence delay, failed enqueue, payload lengths, reverse polling and facade
teardown. Debug additionally denies allocation during mapping. A failed new grid
submission preserves an older pending copy. Consumer tests retain FIFO/epoch
validation, retry, acknowledgements and stable readback creation. All modified C++
passes oxytidy; the approved native-fixture downcast is the only new exception.

Reproduce with the three owning CPU executables. In `Oxygen.Vortex.Exposure.Tests`,
run `ExposureGpuTest.Feedback*`, `IblDiagnosticsGpuTest.*` and
`ExposureLightingGpuTest.*Status*`. The remaining `ExposureGpuTest` cases are
`PrecisionStatusRetriesTransportWithoutEarlyAdmission`,
`CompletedPrecisionAdmissionTracksViewSettingsLayoutAndDiagnostics`,
`BackloggedStatusAcknowledgesLatestSubmissionWithoutRenderingOwnerAgain`,
`ServiceDiagnosticFramesDoNotAcknowledgePendingTransition`,
`InactiveModeValidationCannotRejectAnObservedSubmission`,
`InactiveDiagnosticOwnerPreservesPendingRequest` and
`DiagnosticFramesDeferCameraCutUntilNormalExposureResumes`; join filters with `:`.
Run `SpatialLightGridGpuTest.*:SpatialLightGridFallbackGpuTest.*` in
`Oxygen.Vortex.LightingImageReference.Tests`. No shader diagnostics, copy submission
or readback frequency was added. See
[integrated S7 qualification](#s7-integrated-qualification).

### S7.3 retirement accounting

The shared Nexus state and both production adopters pass focused qualification
on 2026-09-27 using normal Debug/Release builds; implementation commit `ea6f64004`:

| Check                                               |  Debug | Release |
| --------------------------------------------------- | -----: | ------: |
| Nexus reuse / allocation-failure                    | 53 / 5 |  53 / 5 |
| Shadow service, including owner/use retirement      |     51 |      51 |
| Native IBL convolution, lifetimes and admission     |     43 |      41 |
| Native shadow sharing, retained reads and budgets   |      4 |       4 |
| Queued preemption / capture after renderer shutdown |      2 |       2 |

Debug denies allocations during actual IBL/shadow owner and use retirement.
The shadow test retains its fake command-list object outside that scope; slot
expiration and generation reuse still occur through the production callbacks.
RenderDoc checks 1,024 retained cube scalars and 32 metadata bytes after renderer
shutdown. Oxytidy is clean across the changed files with the explicitly approved
allocator-interceptor and CRT declaration-name exceptions. Captures/logs stay local.

Reproduce with `Oxygen.Nexus.Reuse.Tests`, `Oxygen.Nexus.AllocationFailure.Tests`,
`Oxygen.Vortex.ShadowService.Tests`, LightingGpuAbi filter `IblConvolutionGpuTest.*`,
and the retained/queued cases named above in `Oxygen.Vortex.Exposure.Tests`.
The native shadow filters are `BoundedShadowAdmissionGpuTest.*`,
`ShadowAdmissionGpuTest.CompatibleViewsShareFiveCubeAndNineProjectedMapsInOneFrame`,
`ShadowAdmissionGpuTest.RetainedShadowReadbackSurvivesUnsubmittedFrameSlotRollovers`
and `ShadowBudgetGpuTest.*` in `Oxygen.Vortex.LightingImageReference.Tests`.
For capture, run Exposure filter
`IblSurfaceGpuTest.RendererCaptureAdmissionPreservesLiveUpdatesAndRetainedReads`
with `OXYGEN_EXPOSURE_CAPTURE` under `out/`; replay with the existing wrapper and
`AnalyzeRenderDocIblCaptureLease.py`, pass `IblCaptureLease`. Queued pressure uses
`ExposureGpuTest.QueuedIblPreemptionKeepsPinnedCapturesAndBoundedStorage`.
Integrated performance, SDK and application results are
[above](#s7-integrated-qualification).

### S7.1 immutable LUT uploads

S7.1 shares 2D packing and
managed recording between IBL and BRDF-energy initialization. Both Debug and
Release pass **27 upload**, **25 planner**, **34 lighting-service** and **six
native LUT tests**, plus **four IBL consumer/reference tests** per configuration.
The native checks compare all **4,096 IBL texels** and
**1,024 energy texels** after CPU owner release, then exercise discard, rejected/
uncertain submission, recovery/retry and staging-budget rejection. Async tests
cover cancellation and partial-batch failure through private GPU completion.
**50 shadow** and **18 GPU-timeline** Release regressions pass.

RenderDoc separately checks exact texel readback and production shader reads.
The analyzed captures cover deferred direct/indirect, opaque-forward and
translucent LUT consumers in shader-resource state. The **144-case image matrix**
and Async lighting/VortexBasic forward smoke runs pass. Oxytidy covers every
changed C++ file; no check is disabled. Integrated performance, SDK and editor
results are [above](#s7-integrated-qualification).

Validated on 2026-09-27 using the normal Ninja Debug/Release builds;
the S7.1 implementation is committed as `2e6bfb09f`.
MSVC reports existing C4702 in `OxCo/Detail/SanitizedAwaiter.h:101` through the
cancellation test. [Reproduction](#reproduce-s71) uses the committed tests and
analyzer; generated captures and logs stay in ignored `out/`.

### Earlier S7 checkpoints

S7.4 final cleanup: the three requested
source files pass oxytidy with **zero warnings/errors**, with all configured
checks enabled. Oxyformat covers all pending C++ files. After cleanup,
**80 Release / 83 Debug registry tests**, **50 shadow-service tests in each
configuration**, **four native integration tests in each configuration** and
**four Release native shadow tests** pass. Explicit default initializers in the
registry receive a final Debug/Release build and registry-test pass. The preceding
product/fault and retained-capture proof below preserves its exact tested sources.

Managed registration and views: S7.4 now uses
Graphics `ResourceRegistry` methods, with no Vortex registration wrapper.
**80 Release / 83 Debug registry tests**, **52 / 54 native product tests** and
**four integration tests in each configuration** pass, including the 144-case
image matrix. **50 Debug shadow-service tests** and **four native Release shadow
cases** cover shared maps, retained reads and tight-budget fallback. Initial-view
failure/retry and concurrent initialization are checked in the registry's own
suite. Bundles use Oxygen's special-member macros and are move-only.
RenderDoc verifies 1,024 retained
cube values and 32 metadata bytes after renderer shutdown. Current integrated S7
qualification is tracked in the [milestone](README.md#s7-execution-and-exit).

Distant-sky synchronization: **six native sky/LUT
tests** pass after adding the group barrier between offset-2 writes and the final
lane-zero pair. ShaderBake publishes all **237** production modules. The isolated
1,800-update authoring run measures **1.269 / 2.536 ms p95/p99**, inside the
2/4-ms gates. The arithmetic tree is unchanged. Current S7.5 extraction qualification is
[above](#s75-group-reductions); the [integrated S7 exit](#s7-integrated-qualification) passes.

## Integrated results

**S1–S6 baseline.** All seven acceptance areas pass. Source audit confirms **12 performance-critical
files** and **nine migration-core files** unchanged from their qualified snapshots.
Native/editor ordinary runs and separate captured runs use the same production
owners. Production shaders contain no added qualification probes; optional
metadata readback is disabled by default in Release.

Specified-cubemap scaling uses four serial native Release runs on the reference
RTX 3080. Each measures **1,800 immediate updates at 60 Hz after 120 warmup frames**.
No new timing threshold applies to these sizes; the 128-face captured-sky gates
remain the matched S4 measurements below.

| Face size | Producer GPU mean / p95 / p99 | Retained product placement |
| --------- | ----------------------------- | -------------------------- |
| 64        | 0.232 / 0.338 / 0.352 ms      | 5.00 MB                    |
| 128       | 0.605 / 1.266 / 1.568 ms      | 17.19 MB                   |
| 256       | 1.902 / 5.545 / 6.501 ms      | 65.95 MB                   |
| 512       | 3.823 / 4.988 / 5.699 ms      | 260.59 MB                  |

Storage includes two processing slots, scratch and the shared BRDF; source and
upload buffers are excluded. Each run holds **two allocations and 21 registrations**
through the warm window and validates complete finite GPU metadata afterward.
These are direct-producer scaling runs, not the budgeted scene scheduler.
Raw timings, snapshots and commands.

Owner LLDs now describe the actual Stage 13/shared evaluator, complete GPU
products, source modes and automatic updates. They retain source conventions,
SH mathematics and the separately tracked future families. The
document map records that reconciliation.

## Checkpoint evidence

Render-path images: **144 cases** cover
specified-cube, fog-only and atmosphere/fog lighting, dielectric/metal receivers,
three roughness values, opaque/translucent materials and runtime/offscreen paths.
All **72 runtime/offscreen display pairs** match exactly; forward/translucent
contributions match after opacity normalization. Unclipped deferred/forward
images stay below **0.36 RMS / 1.82 peak 8-bit code equivalents**, within the
per-case visual budget of 1 RMS / 4 peak. Comparison.
G-buffer quantization and small HDR residuals remain recorded diagnostics;
production formats, shaders and sampling counts are unchanged. **18 FP32 controls**
pass the existing FP16 filtering gates, and **14 surface regressions** pass.
RenderDoc verifies the actual deferred, forward and translucent shader paths.

Editor/standalone images: two editor views
match across five complete frames. The cooked scene in RenderScene has identical
SH/specular products, material buffers, direct lighting and **3,404 displayed
geometry pixels**. Three sky pixels differ by one display code after camera
quaternion round-tripping; the raw comparison
records the separate host-comparison bounds and HDR/depth differences. FP16 gates
are unchanged. Cooking now preserves captured skylight and the engine's current
atmosphere coefficients; DemoShell hydrates authored Mie absorption. **29 descriptor
and 53 environment-service tests** pass, along with captured and ordinary editor
runs and the standalone run. The oracle rejects the stale-coefficient and
presentation-only captures.

Native town4new appearance: fixed EV12
off/on runs have byte-identical direct lighting, depth and material buffers.
IBL lights **613,412 previously black geometry pixels** while all **607,743 sky
pixels** remain identical. The shadowed façade gains visible texture detail;
the sunlit façade has **8.57×** the shadowed façade's mean scene-linear luminance.
Both HDR and displayed-image checks pass. Compare IBL off
and IBL on.
Four real app runs pass; captures, pixel exports and isolated settings are retained.

S5 DemoShell lifecycle: **four real app cases**
pass. Sidebar and Library clicks switch town4new → Lantern → town4new; the
selected profile now applies before the new scene's first frame. A second process
restores the saved scene and Custom IBL/fog controls, with valid GPU metadata and
matching scene-linear scale/brightness. The runs use isolated settings and leave
the user's file unchanged. **99 owning service tests** also pass.
Reopened controls.

S5 editor workflows: **42 real editor UI cases**
pass. Inspector radiance edits and Undo/Redo advance the rendered IBL generation
and restore the matching source identity at zero age. View recreation retains
scene-global products; Save/reopen and scene replacement publish under new native
scene lifetimes. Forty environment-field history/reopen cases also pass.

**22 SDK/configuration checks** pass, including the actual Debug cooker preflight
and native built-in catalog query. The Debug editor builds. Default shader loading
uses the compatibility-selected SDK; explicit overrides remain intact. Cooking
reads the canonical `share/oxygen/schemas` directory. Both installed shader
archives and scene headers match their current builds/source. No shader code was
added for these checks.

Release editor packaging passes the full
application build and Runtime package generation. Interop retains its declared
`net9.0` framework when the outer NuGet query supplies an empty framework; the
same query previously failed with `NETSDK1013` and `MSB4181`.

S5 GPU validity: **30 Debug / 28 Release native
tests, 13 DemoShell CPU tests and two real widget cases** pass. Frame Diagnostics
reads existing GPU metadata asynchronously; production shaders are unchanged.
Invalid generations contribute zero lighting and remain visible in status. Late
results cannot reject a newer generation. Diagnostic allocation or rejected-copy
failure preserves lighting and retries; uncertain submission closes the existing
product pool. Registered resources stay fixed across **24 authoring generations**.
The widget runner restored the user's current settings at its run boundary.
The Release SDK installs successfully and **nine C++20 editor command tests**
pass against it.

S5 native controls: **two real RenderScene
widget cases** pass on town4new. They exercise source failure/recovery, independent
intensity/diffuse/specular/reflection controls, hemisphere capture edits, sun/fog
drags, fog-only lighting with main-pass fog hidden, preset reset, panel return,
and the Diagnostics GPU readout. The panel reports scene-specific readiness and
keeps prior lighting active during updates; the obsolete unavailable warnings
are gone. Native UI.

**73 CPU tests, 23 Debug native tests and nine C++20 editor command tests** pass.
Coverage includes settings reload, poisoned-pool/source identity, unresolved
cubemaps, unchanged 16-face source allocation failure, and rejection of partial
GPU timing when collection is enabled. The public status header is installed.

S4 matched scene runs pass on the reference GPU:
**1,800 frames per workload at 1920×1080/60 Hz**, after 120 warmup frames. All use
the same fixed-camera mixed scene with two atmosphere lights, height fog,
shadows, deferred lighting and translucency.

| Workload          | IBL GPU p95 / p99    | Whole-frame GPU p95 / p99 | Publication / storage                                                                        |
| ----------------- | -------------------- | ------------------------- | -------------------------------------------------------------------------------------------- |
| Static            | **0 / 0 ms**         | **8.134 / 8.751 ms**      | Unchanged generation; one allocation, **9.13 MB**                                            |
| Runtime sun/fog   | **0.200 / 0.205 ms** | **8.386 / 9.251 ms**      | 600 publications; completion ≤**3 frames**, source age ≤**4**; two allocations, **18.24 MB** |
| Authoring sun/fog | **0.406 / 0.705 ms** | **8.710 / 9.620 ms**      | All 1,800 edits publish in-frame; source age zero; two allocations, **18.24 MB**             |

Product placement bytes and allocation counts stay fixed after warmup; registered
resources also stay at 142 for static and 154 for animated runs. Capture-specific
LUT labels keep visible-sky work out of the IBL cost union. Whole-frame GPU spans
include queue gaps; measured frames all execute the required rendering stages.
The label split passes **63 environment tests and 8 native integration tests**.

Isolated update qualification also
passes: authoring **1.415 / 2.016 ms p95/p99**, runtime **0.475 / 0.861 ms**,
completion ≤4 frames and source age ≤6. First-use producer work, including BRDF
upload, is **0.365–0.381 ms GPU / 30.46–39.55 ms wall** in those isolated runs.
The matched scene startup frame costs **0.365–0.377 ms IBL GPU / 92.28–93.97 ms
wall**; this is the first IBL-ready frame, before all geometry uploads complete.

The pressure test queues three scene frames behind a GPU fence, preempts an
incremental candidate with immediate edits, and keeps two older captures pinned.
Across **36 queued frames**, it verifies published metadata after completion,
peaks at **four normal generations**, uses **six physical allocations**, and
shows no resource/registration growth after warmup. Retired normal occupancy
returns to one; both captured generations remain readable.

**26 Release / 27 Debug integration tests** and **54 Release / 56 Debug
product/fog tests** pass, including the queued-pressure test in both configurations.

Tracy identified tiled producer costs in the
baseline. Independent tiles share
resource transitions and native timing batches; source mip dependencies retain
barriers. Parallel max/validity reduction preserves the range result. The model
learns launch/work cost and balances remaining work against the four-frame
deadline. Failed recordings invalidate timing without reusing live queries.
The updated trace
measures range reduction at **11.6 µs mean**, down from **149.4 µs** in the
baseline; GGX prefilter remains the largest producer at **35.3%** of traced
dispatch time. Native measurements above supply the acceptance results.
RenderDoc verifies complete-generation
bindings and frozen lighting values through adaptive publication.

S4 scene scheduling: **16 Release / 17 Debug
integration tests** pass. Continuous fog edits publish four frozen snapshots in
sixteen frames and converge after edits stop. Authoring preempts an unfinished
candidate; unchanged-key edits consume their authoring intent. Later views reuse
the scene's first scheduling decision that frame. Rejected submissions retain
usable prior lighting; immediate failures and poisoned pools report unavailable.
Source-lease allocation failure retains prior lighting only for runtime edits
to the same resident source; authoring and replacement failures report unavailable.
RenderDoc checks four actual Stage 13
draws: bound revisions **1, 1, 1, 2**, with the old lighting visible until the final
producer and the new lighting matching the frozen candidate.
Capture.

S4 GPU jobs: **54 Release / 56 Debug product/fog
tests** and **12 native integration tests** pass. A candidate advanced over four
frames matches immediate cube chains and SH bitwise; semantic metadata fields
match while the prior generation stays readable. Frozen atmosphere inputs,
rejected-batch retry, cancelled-work retirement and closure/fault gates pass.
RenderDoc verifies initialization plus four
work submissions, **694 dispatches**, unchanged prior metadata and candidate
completion only at the final producer. Capture.

S4 spatial tiling: **49 Release / 51 Debug
product/fog tests** and **12 native integration tests** pass. Tiled and whole
dispatches agree bitwise across **524,160 cube values** and SH for specified HDR
and captured fog inputs; semantic metadata fields match. Constant-buffer growth failure returns
its slot; retry and later smaller workloads reuse the same product storage.
RenderDoc verifies **694 actual dispatches**
in the 8×8 stress case and completion only after the final producer.
Capture.

S3 authoring and SDK: **236 CPU tests**, **nine
C++/CLI command tests**, **49 Debug / 47 Release IBL/fog tests** and **16 native
integration tests** pass. Twelve successive authoring batches each render the
latest fog radiance in one frame, with exactly one new generation per batch.
Removing a registered view closes its capture access; recreation reuses the
scene's IBL while an earlier capture remains readable.

Native/editor edits carry a transient scene revision outside the radiance key.
Applied batches, preview toggles, completed skybox loads and editor light,
transform, hierarchy and visibility commands mark authoring; direct runtime
setters do not. The scheduler consumes authoring revisions even when the radiance
key is unchanged. The public capture API uses existing Oxygen `Result`, preserving
C++20 SDK compatibility. Matching engine, example, SDK and editor builds pass.

S3 producer allocations: **49 injected
resource-factory failures** pass in both native Debug and Release. The sweep covers
new storage, resized storage, fog snapshots, frozen atmosphere LUTs and first use
without an existing generation. Each failure returns its normal slot, preserves
any retained generation, permits retry and releases all additional registrations
on pool closure. **16 Graphics managed-registration tests** also pass, including
registration/view allocation rollback and allocation-free retirement.

S3 lifetime faults: **48 Debug / 46 Release
product/fog tests**, **31 Debug / 30 Release Graphics tests** and **10 native
renderer integration tests** pass. Injected
pre-issue failure returns normal capacity; uncertain submission closes its
generation even after backend recovery. Forced D3D12 device removal releases
capture pins and rejects further admission. Four injected capture-allocation
failures leave admission unchanged, followed by a successful retained read.

CPU release, recording discard and completed GPU-reader retirement make **zero
allocation attempts**, including reentrant submission callbacks. The completion
test checks that hardware has finished while the capture is still pinned, then
releases it under allocation denial. Discard closes
the native list without preparing submission-only state snapshots; callback
resolution consumes its existing storage. The incremental scheduler's
preemption/lifetime cases remain in S4.

S3 capture admission: **43 product/fog
tests**, **63 environment tests** and **10 native integration tests** pass.
Two captured generations leave all five normal update slots available once
ordinary readers drain. Duplicate leases share admission; a third generation
returns busy while lighting continues updating. Discard, rejected submission,
delayed GPU completion and pool closure preserve the expected reservation and
retirement. Pending GPU captures do not retain the external Graphics owner.
Denied capture after scene expiry preserves next-frame invalidation.

The renderer API retains the requested view's generation across renderer
shutdown. RenderDoc replay
checks **1,024 scalar values** and **32 metadata bytes** in the retained readback,
matching revision 2 after later lighting updates.
Capture.

S3 cache identity: **63 environment tests**
and **three native regressions** pass. Recycled light-node generations invalidate
capture identity. A failed first update in another scene exposes no previous
scene product; recovery shares the BRDF. A → B → A rendering reuses each live
scene's products. Expired-scene cache removal preserves retained product reads.
Camera-only aerial controls, view-ID
changes and an inactive specified-cubemap selection reuse captured IBL. Native
atmosphere output is unchanged by the aerial controls.

S2 half admission: **38 product/fog tests**
and **1,083 raster cases** pass. The GPU certificate checks every actual stored
texel in both chains, preserving the **1/1024-stop storage guard** and gain-driven
FP32 promotion without recapture. Native filtering meets the approved
**2/1024-stop** limit in the specular domain reached by the production roughness
mapping: **1,063,548 scalar pairs**, maximum **0.001332 stop**. Qualification ran
on RTX 3080, driver 610.62. Broader characterization remains in the same result;
processed-half and unreachable specular LODs are not qualified consumer domains.

The **48** captured atmosphere/fog raster comparisons cover deferred, forward,
masked and alpha-blended surfaces, roughness and channel-isolating tint. Their
maximum FP16/FP32 difference is **0.000524 stop**.
RenderDoc replay verifies the same
generation actually samples RGBA32Float and RGBA16Float in the paired draws,
with coverage preserved and no ambient bridge.
Capture.

The S2 isolated run measured **1.096 ms mean / 2.475 ms p95 / 2.937 ms p99**;
the current S4 measurements are above. The texel certificate reuses SH scratch
and needs **245,776 logical scratch bytes** per 128-face slot.

S2 precision batching: all **36**
IBL/fog checks plus the new **1/4/512-face** boundary test pass. Constant-cube
and atmosphere RenderDoc replays each check **2,097,120** product values, with
metadata complete only after all **40** producer dispatches. Both stored chains
are scanned in one batch and reduced once; the certificate and filtering are
unchanged. Scratch grows by **18,368 bytes per 128-face slot**.

In the S2 matched 1,800-update workloads, precision batching reduced Tracy
scanning/reduction from **0.451 to 0.148 ms mean** and native GPU update mean
from **1.447 to 1.173 ms**. The baseline and
candidate trace retain
those measurements. S4 supplies the current cost and first-use qualification.

S2 surface run: **6 native tests / 1,035 cases**
pass across deferred, forward, opaque, masked and alpha-blended surfaces. Checks
cover diffuse/specular/tint/intensity/reflection controls, dielectric and colored
metal split-sum composition, material-normal maps and backfaces, glossy/rough
reflections, captured height-fog lighting, HDR scale and tiny-channel retention.
Deferred references use the actual typed GBuffer inputs to separate material
packing from lighting error. The diffuse normal reference is analytic; BRDF and
prefilter integration retain their S1 numerical qualification.

Gain-only edits reach the next frame without changing product generation. A
captured fog-color edit, fog capture visibility and SkyLight disable also affect
the next frame. Capture remains active with main-view fog disabled. The volumetric scattering
multiplier leaves surface IBL unchanged.
RenderDoc replay verifies one Stage 13
contribution, no ambient bridge, complete metadata, preserved coverage and the
independent one-pixel diffuse-plus-specular reference.
Capture.

S2 migration run: scene source and packed
version **8** use an **88-byte** SkyLight record. Source readers reject the retired field; packed loaders reject prior scene
versions and the old record size. Both former boolean values
migrate to automatic updates with other lighting values preserved; fog's capture
visibility remains independent. DemoShell v5 settings persist the v6 migration
even when custom controls are inactive. The editor generator emits v8.

Checks pass: **336** PakGen, **8** source migration, **33** importer/schema,
**9** native loader, **28** physics import, **50** environment settings,
**2** settings persistence, **5** Scene, **61** environment service,
**27** editor descriptor and **9** C++/CLI environment-command tests.
The 15 maintained source descriptors preserve all effective values. All **16**
shared manifests recooked and the rebuilt PAK validates; RenderScene's four
local models were reimported from original sources. All **26** resulting scene
descriptors are v8. town4new ran 180 frames and exited successfully, with its
settings restored. The installed SDK
showcase was rebuilt; its **12** scene descriptors are also v8. Previous cooked roots/PAK are retained at the run's recorded
backup paths. This is migration/load qualification; the appearance proof remains
the earlier GPU capture below.

S2 precision run: **36** IBL/fog product tests
and **30** native sky/fog/offscreen tests pass. Both complete cube chains retain
FP32 and nearest-even FP16 representations under the same Nexus generation.
Native tests check every half texel against the canonical result, the 32-byte
metadata ABI, finite/generation guards, the descriptor actually selected and
surface output. Intensity, tint, lobe and material-gain edits switch back to FP32
without rebuilding; amplified tiny radiance survives.

RenderDoc replay checks **2,097,120**
scalar values across all four constant-cube chains, with zero error. Canonical
and precision flags become complete only after all **71** producer dispatches.
Capture. The
atmosphere replay also checks
all four chains, exact half narrowing, and **319,504** bytes of frozen LUT copies.
Atmosphere capture.

S2 production run: 30 native sky/fog/offscreen
tests, 31 IBL product tests, 64 environment tests and 22 scene-publication tests
pass. Specified cubemaps use TextureBinder residency/revisions and the common GPU
pipeline; wide and amplified tiny radiance survive. Deferred/forward unlit
surfaces remain unchanged by sky lighting. Immediate source failures publish
unavailable-zero, and repeated offscreen setup preserves pending upload tickets.

Native town4new with sky lighting

Town replay records one Stage 13 draw and
no Stage 12 sky-light draw. In that same frame, IBL illuminates **577,727**
previously black geometry pixels and leaves **210,159** sky pixels unchanged,
with coverage preserved. This isolates the contribution at identical camera and
exposure. The screenshot is the native output. RenderDoc timings are not performance acceptance.

S2 atmosphere source: 22 sky/fog tests,
30 IBL/fog tests and 64 environment tests pass. The native capture check verifies
both-light additivity, camera/exposure/disk independence, combined atmosphere
and fog, failed-recording retry and source release before readback. A separate
all-texel comparison matches captured and visible sky-view LUTs at the capture
anchor for pre-exposure 1/8 and 8. Coordinate checks cover all three planet modes.
Tests.

RenderDoc replay verifies the
unit-exposure shader has no camera constant block, 319,504 bytes of frozen LUT
copies match their sources, and 1,048,560 product values satisfy finite/HDR/mip
checks. Metadata becomes complete after all 22 IBL dispatches.
Capture.

S2 capture adapter reuses the S1 pool and
convolution path. A frozen fog snapshot produces all six 128-face processed and
specular mip chains plus SH and complete metadata. The 75-million peak fixture
checks HDR normalization and two-ULP binary16 product bounds; SH retains its
0.1% relative bound. Main-view visibility does not gate capture. With no captured
source, even a colored lower hemisphere publishes ready-zero. Concurrent
generations retain separate CPU snapshots and reuse storage after retirement.
Tests ·
RenderDoc products ·
Capture.

This earlier adapter checkpoint covers fog-only input; the atmosphere-source
checkpoint above adds scene-global LUT production and frozen copies.

S2 shared fog run: 26 IBL/fog tests,
64 environment tests and 19 fog/sky regression tests pass. The independent
height-fog reference covers 120 rays across axes, edges, corners, altitudes and
distance limits. Capture and main-view visibility are independent; both fog
lights remain active with atmosphere and analytic disks disabled.

The native fog-only scene test checks
linear SceneColor, coverage and final display at EV0/EV2 with background and
main-pass fog independently enabled. RenderDoc replay
verifies the eight frames: four sky-fog draws, the 304-byte view ABI, and eight
depth-fog draws that leave the far background unchanged. Sky fog is applied once;
the display background remains independent of exposure.
Capture ·
IBL/fog tests ·
Environment tests ·
Fog/sky regression.

The earlier source checkpoints below precede the production integration above.

Latest S1 run: 23 IBL tests pass. The preceding
publication checkpoint passed 75 resource-registry and 63 environment tests. The shared evaluator covers
independent diffuse/specular gains, occlusion, HDR scale, grazing views,
near-black metals and stale generations. Recorded readers retain BRDF and cube
products after their CPU owners are released.

Native GPU material fixture

The fixture evaluates analytic sphere normals through the production IBL helper
on D3D12. Linear pixels precede the preview's
Reinhard/sRGB display transform. Production scene-pass integration is S2.

| Check                                             | Result                                                                                                       |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Release build and shader archive                  | Passed.                                                                                                      |
| Native IBL products, reuse and surface evaluation | 23/23 passed.                                                                                                |
| Managed raw/bindless views and cache identity     | 75/75 passed.                                                                                                |
| Environment regression                            | 63/63 passed.                                                                                                |
| Surface RenderDoc replay                          | 16 producer dispatches, 16,368 product values and 216 surface values passed.                                 |
| 128-face cube checkpoint                          | 22 dispatches, 1,048,560 product values passed; metadata transitions invalid → finite/incomplete → complete. |

The earlier cube checkpoint covers full mip
chains, four-child reduction, hemisphere orientation, invalid-source rejection,
shader catalog and standalone-header checks.

Nexus retirement tests reuse one physical resource bundle across twelve
successive generations. Live owners and unsubmitted readers prevent reuse;
discard releases the pending reader, and retained products survive pool closure.
Dimension changes replace a free slot's storage. No retirement tickets remain
pending or abandoned after the repeated-use test.

The independent GGX check covers 306 scalar values across faces, mip levels,
centers, edges and corners. CPU code computes double-precision sample directions,
PDFs, footprint LODs and weights; the GPU supplies seamless cube sampling only.
Matching 32/64-sample results pass `0.001 * max(1, reference)` including FP16
storage. Against 1,024 samples, the smooth-plus-directional 5.2-peak fixture has
**0.6274% peak error and 0.0878% RMS**, within its 5%/1% quality bounds.
Reference results retain the measured values.

The lookup check exercises every texel and all four domain corners through the
production HLSL sampler. Quantized texel reads must agree within one UNorm16 unit.
The separate double-precision 256×256 halfway-vector quadrature checks twenty
view/roughness points against the prescribed 128-sample generator, with absolute
A/B error at most 0.035. This estimator check does not replace material-image
qualification.

## Reproduce

### Reproduce S7.1

From the engine directory in a Visual Studio developer shell, build and run
`Oxygen.Vortex.UploadCoordinator.Tests`, `Oxygen.Vortex.UploadPlanner.Tests`,
`Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.Exposure.Tests` and
`Oxygen.Vortex.LightingGpuAbi.Tests` in Debug and Release with the normal
`cmake --build out/build-ninja --config <configuration> --target <targets>`.
Run the first three suites in full; Exposure filter `ExposureGpuTest.Immutable*Lut*`
covers the six native checks. LightingGpuAbi filters `IblBrdfLookupTest.*`,
`IblConvolutionGpuTest.BrdfLookupIsSharedAndRequiredByGenerations`,
`IblConvolutionGpuTest.SurfaceRecordingRetainsBrdfAfterCacheAndProductRelease`
and `LightingGpuAbiTest.IblBrdfNativeUnormSamplingHasNoSecondRemap` cover the
four consumer checks (join with `:`).

For exact-texel captures, put RenderDoc on `PATH`, set
`OXYGEN_EXPOSURE_CAPTURE` to an absolute prefix under `out/`, and run Exposure
filter `ExposureGpuTest.ImmutableIblLutPreservesEveryTexelAfterOwnerRelease`
or `ExposureGpuTest.ImmutableEnergyLutPreservesEveryBitAfterServiceRelease`.
The same capture hook with `IblSurfaceGpuTest.ImagesAgreeAcrossPublishedAndOffscreenPaths`
runs the 144-case image matrix. Replay captures serially with
`tools/shadows/Invoke-RenderDocUiAnalysis.ps1`,
`-UiScriptPath tools/vortex/AnalyzeRenderDocImmutableLut.py` and the matching
`-PassName` documented in that analyzer. Supply absolute capture/report paths;
keep generated output under ignored `out/`. Native readback checks texel contents;
use production deferred, forward and translucent modes to inspect consumer reads.

### Earlier slices

For specified-size scaling, use `IblUpdateBenchmark.DISABLED_SpecifiedCubeUpdates`
with `OXYGEN_IBL_FACE_SIZE` set to 64, 128, 256 or 512 and a fresh
`OXYGEN_IBL_TIMING_OUTPUT` directory per process. Use the same summary tool below;
the retained runner executes all four serially.

For the isolated timing workload, build `Oxygen.Vortex.Exposure.Benchmarks`
in Release. Set `OXYGEN_IBL_STABLE_POWER=1` (Windows Developer Mode required)
and `OXYGEN_IBL_TIMING_OUTPUT` to a new directory, then run the binary
with `--gtest_also_run_disabled_tests`
`--gtest_filter=IblUpdateBenchmark.DISABLED_CapturedSunUpdates -v=-1`.
Run `tools/vortex/SummarizeIblUpdate.py <run-directory>` for CPU/GPU window
checks and update interval unions. For Tracy attribution use the same target
in `out/build-tracy-ninja`, activate its generated Release Conan runtime
environment, and start `tracy-capture -a 127.0.0.1 -o <trace>` before the binary.
`tracy-csvexport -g <trace>` exports the per-dispatch GPU events.

Use `--gtest_filter=IblUpdateBenchmark.DISABLED_ScheduledSunUpdates` for the
automatic runtime workload. Its CSV also records source age, candidate completion
and feedback sample count; the summary reports the unchanged cost/latency gates.
Use a fresh output directory for each measurement.

From the repository root in a Visual Studio developer shell:

```powershell
cmake --build projects/Oxygen.Engine/out/build-ninja --config Release --target Oxygen.Vortex.LightingGpuAbi.Tests Oxygen.Vortex.EnvironmentLightingService.Tests --parallel 8
& projects/Oxygen.Engine/out/build-ninja/bin/Release/Oxygen.Vortex.LightingGpuAbi.Tests.exe '--gtest_filter=*Ibl*'
& projects/Oxygen.Engine/out/build-ninja/bin/Release/Oxygen.Vortex.EnvironmentLightingService.Tests.exe
```

For the S2 checkpoint, also build `Oxygen.Vortex.Exposure.Tests` and run its
`--gtest_filter=*Fog*:*Sky*` subset. Run LightingGpuAbi with
`--gtest_filter=*Ibl*:*HeightFog*`. To reproduce the visible-fog capture, set
`OXYGEN_EXPOSURE_CAPTURE` to an absolute capture prefix and run
`--gtest_filter=*FogOnlySky*`; replay with the existing wrapper and
`tools/vortex/AnalyzeRenderDocSkyHeightFog.py`, pass name `SkyHeightFog`.

For the capture adapter, run LightingGpuAbi test filter `*CapturedFogProduces*`
with `OXYGEN_IBL_CAPTURE` set. Replay that capture with
`tools/vortex/AnalyzeRenderDocIblProducts.py`, pass name `IblFogCapture`.

For the atmosphere source, run Exposure test filter `*CapturedAtmosphereUses*`
with `OXYGEN_EXPOSURE_CAPTURE` set. Replay with
`tools/vortex/AnalyzeRenderDocCapturedSky.py`, pass name `CapturedSky`.

The [milestone plan](README.md)
owns slice state and the complete acceptance list.

To capture and replay the constant-cube producer test:

```powershell
$env:OXYGEN_IBL_CAPTURE = 'H:/projects/DroidNet/projects/Oxygen.Engine/out/build-ninja/ibl-capture/constant'
& projects/Oxygen.Engine/out/build-ninja/bin/Release/Oxygen.Vortex.LightingGpuAbi.Tests.exe '--gtest_filter=IblConvolutionGpuTest.ConstantCubePreservesEveryFaceAndMip'
Remove-Item Env:OXYGEN_IBL_CAPTURE
& projects/Oxygen.Engine/tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath out/build-ninja/ibl-capture/constant_capture.rdc -UiScriptPath tools/vortex/AnalyzeRenderDocIblProducts.py -PassName IblProducts -ReportPath out/build-ninja/ibl-capture/products.txt
```

For the surface capture, use the same commands with test filter
`IblConvolutionGpuTest.SurfaceEvaluationMatchesDiffuseAndSplitSumReference` and
analyzer `-PassName IblSurface`. Set `OXYGEN_IBL_IMAGE` to an absolute `.pfm` path
when running `IblConvolutionGpuTest.OffscreenDielectricAndMetalRoughnessImage` to
export the linear samples. The retained
plot script takes that PFM path and
writes the PNG preview beside it.
