# Implementation owners

The original fixed-gain floor, histogram/adaptation, sharing, pre-exposure,
serialization and settings-isolation defects are closed by EX02-06. Their
requirements remain in sections 3-5 and the completed slice records.

EX07 also closed the physical-lighting defects in both forward and deferred
consumers; calibration, many-light qualification and final editor acceptance are
recorded in the [F report](EX07/EX07F/validation.md). The remaining delivery
gaps belong to EX08 onward:

- `LightScene::ApplyScenePreset` changes object visibility; a calibrated
  directional reference and complete reset still need implementation.
- Existing settings/status APIs are sufficient for controls. They must not be
  labelled same-frame measured luminance or consumed GPU gain.
- MultiView already has exposure scenarios and assertion tools. Audit applicable
  evidence and repair remaining operational gaps without a second framework.
- LightBench still needs local reset/save/load, focused reference qualification,
  clear controls and an operating README.

## LightBench and MultiView acceptance

LightBench is Oxygen's maintained exposure benchmark for visual validation,
not only a scene generator for automated tests. Its scene design, controls,
initial image and behavior under interaction must be repaired as part of delivery.
Numerical results and visible behavior are both acceptance requirements.

### 7.1 Useful calibration workflows

Deliver Neutral Reference, point/spot presets and a simple bright/dark transition.
Fixed exposure uses the reference scene and existing post-process controls.
Lifecycle, metering and HDR numerical matrices remain native tests, not separate
LightBench experiment UIs. The LightBench specification owns canonical geometry,
materials, light units, framing and independently resolved exposure.

### 7.2 Settings and reset ownership

Reuse EX06's `SceneActivationPolicy::kExperimentOwned`. A local settings structure
and coherent apply/reset operations own geometry, materials, lights, environment,
camera, rendering and exposure/output settings. Reset restores every reference
input and relevant temporal state through public transitions at the normal frame
boundary. Keep window/panel preferences separate; other demos retain their policy.
Validate loaded settings completely before application. Explicit save/load
round-trips modified settings without serializing GPU state/handles or overwriting
personal files. Bring the shipped indoor settings into the local supported format;
no universal experiment schema, transition scripting language or legacy reader.

### 7.3 Focused native numerical verification

Use existing native GPU/readback fixtures and independent EX07 lighting oracles.
The test and app share the canonical scene definition, while expected results
remain independently calculated. Test source/product identity, recover stored P,
and account for production BRDF, packed values and output encoding. For reference
regions, exclude edges, reject unexpected geometry/depth and require at least 95%
expected foreground coverage. Zero/invalid samples cannot pass as true black.
These are fixture requirements, not a new runtime diagnostic service.

Tests can run in optimized Release in the existing Ninja tree. Retain frozen
numerical tolerances; add only missing or affected cases. Display expected values
as reference/derived values and existing settings/status as such. The live demo
shows reference configuration versus modified settings, not a numerical Pass/Fail
verdict for arbitrary current frames. No new tonemap probe or shader variant.

### 7.4 LightBench visual acceptance

Validation combines automated unit/native correctness tests with explicit
interaction and visual checks. Record the action, expected result and observed
result; correct failures and rerun the affected checks.

Check clean launch/reset, entire-card framing, readable controls and backgrounds,
relevant edits and explicit save/load at 1080p, 1440p and a resized layout. Check
new point/spot and transition behavior as delivered, reusing unchanged layout
acceptance. EX08.2 supplies the delivered widget-regression automation; manual
visual checks cover framing and image quality.
Unexpected black frames, flashes, stale status or preference reapplication remain
bugs. Test convergence against elapsed time in native fixtures, not an arbitrary
warmup-frame count.

### 7.5 MultiView visual acceptance

Reuse applicable EX05 evidence. Run focused checks for changed behavior and
record the remaining interaction/visual results against the scenarios below.

Use the existing `Examples/MultiView` application and its production renderer
paths. Extend its test controls for exposure cases rather than creating another
multiview demo.

| Scenario                                 | Required visible result and exposure behavior                                                                                               |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Main + lit PiP (`--pip-wireframe false`) | Both cameras show correctly framed, stable lit images; each view's Auto meter uses its own content rectangle                                |
| Different view brightness/modes          | A bright view and dark view adapt independently; changing one view's mode, compensation or camera does not change the other's gain or image |
| Explicit shared exposure                 | The consumer follows the owner's published prior gain with the specified one-frame latency; its scene content never drives the owner        |
| Submission/layout reordering             | Identical view inputs yield the same per-view exposure and image, regardless of submission order or screen placement                        |
| Resize and scissor                       | Correct aspect ratio, viewport/scissor and UI placement; bars and neighboring views do not enter a view's histogram                         |
| Hide/reopen/recreate                     | Retained handles follow the inactivity policy; recreated handles initialize independently and never inherit another view's history          |
| Cut/reset/source destruction             | Only the target history changes; source loss follows the documented detach/continuity rule without blank panes or stale bindings            |
| Auxiliary and offscreen products         | Producer/consumer images retain the correct HDR/exposure domain; no double exposure, unintended second tonemap or stale product             |
| Wireframe/debug/feature variants         | Diagnostic presentation remains correct and does not corrupt neighboring or retained exposure histories                                     |

Run ordinary main/PiP, the standard proof layout, auxiliary, offscreen, and
feature-variant layouts. Preserve the existing intentional `BLACK expected`
cells for depth-only, shadow-only and diagnostics-only profiles; unexpected
black lit views fail. ImGui, display backgrounds and fixed bars remain independent
of scene exposure.

Reuse the bounded exposure-proof modes already implemented in MultiView's
configuration and CLI. Use `MainModule::UpdateComposition` for view settings/transitions,
`UpdateCameras` for framing/resize, and `RenderOffscreenProofProducts` for visible
forward/deferred products. Reuse producer-owned view-state handles, existing
status and native comparison fixtures; no new renderer measurement facility.

Compare the same view's pre-composition output rendered alone and in a family
with identical input, settings, history and timestep. Its measured gain and
image must agree within
the established numerical/output tolerances. Explicit sharing is the exception
and is tested against its source policy. Inspect the presented output and
every lit pane, not only the main view or a final surface-average statistic.
Validate the final composite separately: overlap and z-order legitimately change
visible pixels. Resize can change the camera's framing and therefore its own
metering; it must not contaminate another view's meter or reset unrelated history.

Use a native scripted interaction sequence covering reordering, resizing,
mode changes, camera movement and lifecycle events while all panes are visible.
Use existing diagnostics and native numerical evidence to support manual visual acceptance; no new per-view measurement service is required.
Physical sky and fog remain exposed scene radiance; only display-space backgrounds,
bars, labels and UI are exposure-independent.

## Qualification protocol

### Validate the change once; retain valid evidence

The matrix is a coverage requirement, not an instruction to repeat every earlier
test/capture in every slice. Each result records its case/recipe, relevant source,
shader and asset identities, configuration, backend, oracle and tolerance revision.
Declare the affected rows before running validation. Reuse prior results only
when their inputs and relevant implementations are unchanged; rerun affected
cases after a change or failure, including shared shader/helper consumers.

New per-slice tests and native visual checks close with that slice. EX10 runs affected final-build checks and references applicable existing evidence. It does not repeat the EX051 performance matrix, EX052
quality review, EX06 migration campaign or every historical RenderDoc capture.
If a change invalidates one of those contracts, identify and test that affected
scope explicitly. No blind full-repository test loop or repeated successful run.

Freeze tolerances before results, using the
[PBR budgets](../../../renderer-core/physically-based-rendering.md#acceptance-budgets)
and [LightBench coverage rules](../../../renderer-core/lightbench.md#independent-measurement).
GPU values, derived CPU values and displayed pixels have distinct comparisons.
Keep exposure, encoding and same-model consistency tolerances fixed. Assess
production shading approximations using the PBR model-2 quality/time/memory
comparisons; the old integrated-source and reciprocal-model errors are not
implementation-failure thresholds for this model.

### Required coverage

EX07's [many-light matrix](EX07/validation.md#deterministic-workload-envelope)
extends the physical calibration rows below. EX10 carries its qualified results
forward, rerunning affected cases only when relevant changes invalidate them.

| Area                               | Required cases                                                                                                                                                                                                    |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fixed exposure                     | EV14/15/16 and supported limits; keys/compensation; invalid input; disabled; CPU/HLSL propagation                                                                                                                 |
| Metering                           | Known distributions; two-bin weight conservation; percentiles; masks/profiles; partial coverage; mixed/all black; zero mask; nonfinite samples; tiny and 8K outputs                                               |
| Curves                             | Empty, single key, endpoints, interpolation, out-of-range clamp, malformed keys, raw-EV input independent of adapted gain                                                                                         |
| Adaptation                         | Both directions; linear/exponential crossing; 30/60/120 Hz and irregular dt at equal elapsed time; pause; zero speed; long dt; no overshoot                                                                       |
| Lifecycle                          | First valid frame; seed frame and out-of-meter-range seeds; cuts; changed settings; manual/auto; zero target precedence/restoration; invalid meter; retries; view destruction; device recovery; stateless Auto    |
| Sharing                            | Both render orders; contrasting views; source startup/inactivity/reset; source destruction and consumer-owned fallback transition; cycle rejection; multiple frames in flight                                     |
| HDR domains                        | P invariance; FP32 bootstrap and delayed/stale status acknowledgment; bright/dark endpoints; opaque, emissive, forward, translucency, sky/AP/fog, bloom, capture and reused histories                             |
| Authoring                          | Schema boundaries; current packed records and obsolete-format rejection; cook/load/save/reload; mask pending/failure; curve round-trip; experiment-owned activation                                               |
| Calibration                        | Directional lux; point inverse square; spot flux normalization; near-field finite behavior; range/cone edges; production BRDF                                                                                     |
| Many-light correctness/performance | Conservative culling/reference equivalence; dense/overflow/capacity behavior; shared shader response; shadow identity; mutation/lifetime/multi-view; CPU/GPU/memory scaling and measured improvements under EX07. |
| Console and user UI acceptance     | Shared command/UI validation, explicit targets and async outcomes; native unit tests plus user edit/reset/save/load/panel checks. EX08.2 automation follows EX09.                                                 |
| Native reference verification      | Existing fixtures/readbacks; independent expected values and valid reference regions. Reusable runtime instruments are removed from scope.                                                                        |
| LightBench calibration             | Neutral/point/spot presets and bright/dark transition; clean startup, complete reset/save/load, shared canonical scene definition, readable UI and correct exposure interaction.                                  |
| MultiView visual integration       | Ordinary lit main/PiP plus standard, auxiliary, offscreen and feature layouts; standalone/family equivalence, per-view isolation, intentional sharing, resize/reorder/lifecycle, stable UI/backgrounds            |

### Numerical comparisons

Use independent calculations, not a second call to the production helper.
For fixed EV14/15/16 at key 12.5/compensation zero, a scene-linear input 4096
produces 0.25, 0.125 and 0.0625 before dithering with no tone curve and gamma one.
Include Bayer dithering, target encoding and rounding in stored-pixel expectations.
Use float probes for values smaller than display quantization can resolve.

Record absolute and relative tolerances, including G-buffer packing, sampling,
histogram quantization and readback. Freeze them before acceptance captures.
Measure convergence in elapsed time and a specified stop error; a fixed count
of warmup frames is not a settling criterion.

### Build and test commands

Run from `projects/Oxygen.Engine`:

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.PostProcessService Oxygen.Vortex.SceneRendererDeferredCore --parallel 4
ctest --preset test-debug -R 'Oxygen\.Vortex\.(PostProcessService|SceneRendererDeferredCore)' --output-on-failure
cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-lightbench oxygen-examples-multiview --parallel 4
cmake --build out/build-ninja --config Debug --target Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4
ctest --preset test-debug -R 'Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog' --output-on-failure
```

Register focused schema/cooker/loader, public lifecycle, light-unit and bench
tests in their owning suites as the slices are implemented. Record exact new
targets/filters here and rebuild them before execution.

Run native D3D12 with the debug layer for correctness and GPU-output inspection.
Timing uses optimized native Release with capture/debug validation disabled.
Existing multiview regression:

```powershell
./tools/vortex/Run-VortexMultiViewValidation.ps1 -Output out/build-ninja/analysis/vortex/exposure-lightbench/multiview -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

Slice 1 arithmetic/format audit (does not execute GPU acceptance):

```powershell
python tools/vortex/audit_exposure_contract.py --output out/build-ninja/analysis/vortex/exposure-lightbench/contract-audit
```

Slice 3 production-pass numerical qualification (the offscreen fixture uses
RGBA32 inputs, existing upload/readback infrastructure and the real exposure
pass; it does not qualify full scene/HDR migration):

```powershell
python tools/vortex/exposure_reference.py
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.Exposure.Tests oxygen-graphics-direct3d12 --parallel 4
./out/build-ninja/bin/Debug/Oxygen.Vortex.Exposure.Tests.exe
cmake --build out/build-ninja --config Release --target Oxygen.Vortex.Exposure.Tests oxygen-graphics-direct3d12 --parallel 4
./out/build-ninja/bin/Release/Oxygen.Vortex.Exposure.Tests.exe
ctest --preset test-debug -R 'ExposureSettings|PostProcessService|SceneRendererDeferredCore|RuntimeViewPublication|ShaderBakeCatalog' --output-on-failure
./out/build-ninja/bin/Debug/Oxygen.Examples.VortexBasic.exe --validation-scene exposure-locked --validation-exposure-ev=160 --frames 20 --fps 10 --vsync false --debug-layer true --capture-provider renderdoc --capture-load search --capture-from-frame 10 --capture-frame-count 1 --capture-output out/build-ninja/analysis/vortex/exposure-lightbench/metering/locked-meter -v=-1
./tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath out/build-ninja/analysis/vortex/exposure-lightbench/metering/locked-meter_capture.rdc -UiScriptPath tools/vortex/AnalyzeRenderDocExposureMeter.py -PassName Auto160 -ReportPath out/build-ninja/analysis/vortex/exposure-lightbench/metering/locked-meter-analysis.txt -AnalysisTimeoutSeconds 60
```

EX08–EX09 document exact focused test filters and supported application commands
as implementation lands. There is no planned `Run-LightBenchValidation.ps1`
runner or versioned aggregate report. Commands above are existing reference
entry points, not a requirement to rerun their historical campaigns.
Use controlled simulation time in numerical tests; an FPS cap alone is not
sufficient. Reuse accepted evidence and run only affected checks. Do not repeat
EX07 captures or benchmarks. Store durable summaries in tracked Markdown;
transient analysis paths alone are not acceptance records.

evidence and remaining gate. A validated component does not close its slice. 7. Batch coherent implementation work and use focused Debug tests between
checkpoints. Run the broader owning Debug gate for item closure; reserve
Release validation for slice closure, as requested on 2026-09-18. For the
revised Slice 5.2, its impact-based validation table below replaces automatic
broad gates at each item; reuse applicable passing evidence. 8. Keep a TODO at each deferred exposure code boundary, naming the owning item
or plan and the concrete work required before activating that path. 9. For subsequent broad native gates, freeze the checkpoint and binary/shader/
fixture hashes, then delegate the specified test run to one validation
subagent. Its mandate is test execution and reporting only: no edits, builds
or scope expansion. Continue source implementation, review and documentation
while it runs; defer only builds, runtime-input changes and competing GPU work.
Reconcile its result before committing.
Keep short focused checks local and do not restart an already-running gate. 10. Performance measurements and conclusions use Release only. Debug is for
correctness. Slice 5.1 uses Release measurements at each performance
checkpoint; the general rule reserving Release for slice closure does not
postpone those measurements. Reuse Oxygen's existing profiling facilities. 11. Execute the inserted slices in order: 5 -> 5.1 performance -> 5.2 code
quality -> 6. Resume from **Current work**, which owns the active checkpoint
and execution state. Follow the remaining order in section 3.2.1 rather than
restarting at EX051-01. Slice 5.2 requires agreement on its concrete residual
fix list and validation selection before code changes. 12. Slice 5.2 reuses completed diagnostics, restructuring and validation. Agree
only the remaining fixes and any necessary API/ownership change; do not
repeat a general restructuring-design exercise. Fix warnings at their cause,
justify narrow exceptions, and keep each coherent increment buildable.
Further performance optimization belongs to the deferred later milestone.
