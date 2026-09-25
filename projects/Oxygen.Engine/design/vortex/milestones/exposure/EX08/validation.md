# EX08 candidate validation

Status: validated and closed (2026-09-25); final native/widget qualification and
acceptance are recorded in [EX10 closeout](../EX10/validation.md). The initial
candidate measurements below are retained. Structured commit delivery was authorized after user review.

## Delivered candidate

- One canonical LightBench scene builder shared by the demo and native test:
  0.18/0.9/0.02 rough dielectric cards, a white 1000-lux directional key,
  orthographic reference camera and independently derived EV100 8.380766.
- Explicit normal-rendering configuration, with the default ground-grid overlay
  disabled so it cannot contaminate calibration output. No renderer/shader changes.
- Complete frame-boundary reset and explicit schema-validated local save/load.
  Invalid documents/coupled values do not mutate current state. Saved files load
  only explicitly; user preferences are separate. The shipped indoor file is
  migrated and its camera faces the scene.
- Reference/Modified configuration status, reference-value labels and preserved
  panel/window preferences. Camera controls were restored after user feedback;
  directional/point/spot controls are immediately visible in the Lights section.
  Manual projection edits survive resize and save/load; reset restores auto framing.
- Final UI retains the original translucent overlay contract: the scene uses
  the full window and panel switches do not affect its viewport/camera. The
  initial reserved-width implementation and its accessor were rejected and removed.

## Automated evidence

Both existing non-Tracy Ninja Debug and Release builds succeed. Five tests pass
in each configuration, with zero failures/skips. A final shipped-preset camera
assertion was added afterward; that affected case passes again in both configurations.
No EX07 benchmark or capture was repeated. No runtime measurement service,
instrumented shader or validation build flag was introduced.

The native case exercises forward and deferred production rendering. It checks
all 256 pixels of each inset region for valid foreground and expected plane
depth, independently expected HDR response, the uploaded exposure state and
final gamma/dither output. Native readback belongs exclusively to the test.
The generic readback API excludes stencil formats; the fixture uses the existing
EX05 native depth-plane copy approach, without changing the production backend.

Arithmetic tolerance remains 2e-5 relative plus 2^-120 absolute for HDR, with
existing deferred packing and encoded-side half-code sRGB sampling uncertainty
propagated separately. Output comparison tolerance is 2e-5 for the float target.
No tolerance was fitted to the observed image. Expected response uses the existing
independent model-2 GGX integrator, not the production lookup table.

Observed Release mean scene luminance (cd/m²):

| Card  |            Forward |           Deferred |
| ----- | -----------------: | -----------------: |
| Gray  | 59.997676849365234 | 60.501419067382812 |
| White | 286.10552978515625 | 284.38946533203125 |
| Black | 9.7514839172363281 | 9.8199167251586914 |

The difference between rendering families includes their established material
packing/sampling behavior. These are focused correctness results, not a new
performance baseline or live application measurements.

Settings tests cover modified-state round-trip and full reset, unknown/missing
fields, obsolete versions, invalid modes, EV/percentile/curve coupling, invalid
geometry/camera/cones, nonfinite values and the shipped indoor settings.

Durable records: [summary and hashes](evidence/validation/ex08-20260925/summary.json),
[Debug results](evidence/validation/ex08-20260925/lightbench-ex08-debug.json.gz),
[Release results](evidence/validation/ex08-20260925/lightbench-ex08-release.json.gz),
[final indoor Debug](evidence/validation/ex08-20260925/lightbench-ex08-indoor-debug.json.gz),
[final indoor Release](evidence/validation/ex08-20260925/lightbench-ex08-indoor-release.json.gz).
Raw logs are retained alongside these results. Run commands and user checklist
are in the [LightBench README](../../../../../Examples/LightBench/README.md).

## User-reported directional toggle defect

The user replied OK to the handoff but reported that the directional checkbox had
no visible effect. LightBench was missing `SceneObserverSyncModule`, so a changed
`affects_world` value did not invalidate the directional resolver's cached list.
The native rendering fixture explicitly synchronized observers and therefore
masked this missing application integration during initial qualification.

LightBench now registers the same existing module and reserved priority used by
RenderScene. No light-culling rule or shader was changed. The control is now
**Enabled** under **Directional Light**, avoiding the ambiguous “Directional key”.

The new on/off/on regression executes the real sync module and checks the resolver
snapshot before the renderer fixture can synchronize it. It verifies dark output
when disabled and restored illumination when enabled in both deferred and forward
paths. This one affected case passes in Debug and Release; prior unrelated checks
are credited. Six distinct tests are now qualified in each configuration.

[Toggle Debug](evidence/validation/ex08-20260925/lightbench-ex08-toggle-debug.json.gz),
[Toggle Release](evidence/validation/ex08-20260925/lightbench-ex08-toggle-release.json.gz).
The Release app was rebuilt and reopened for a focused manual retest.

## Final acceptance

Manual checks confirmed the corrected directional checkbox (off darkens the cards;
on restores them), smooth Dim → Bright → Dim adaptation, resizing and normal
close in the final ordinary Release app. Complete reset/save/load, numeric
editing, panel return, mask toggles and typed console operations also pass the
real-app Test Engine suite in Debug/Release. EX08.1 and EX08.2 are delivered.
See [the final result and migration records](../EX08.2/validation.md).

Relevant new-code findings were corrected and scoped checks recorded. A
repository-wide clean clang-tidy result is not claimed; existing owner warnings
and explicit-alignment C4324 warnings remain documented. The user subsequently authorized structured commit delivery after review.

## Auto exposure observation for EX09B/C

Manual checks confirmed responsive UI and supplied a modified Full-scene image with
clipped highlights. They confirmed Auto exposure, tone mapper None and compensation 0. The None shader path saturates exposed RGB to [0,1]; global histogram metering
may favor the large dim ground over the bright cards. This is a plausible expected
clipping configuration, not a measurement of the live Auto solve's correctness.
The concrete preset/operating-guidance case is recorded under EX09B/C. No exposure
algorithm, tolerance or renderer policy was changed in response to the image.

### Saved-scene Auto diagnosis (native Release)

A focused test loads the user's exact authored settings from
`Examples/LightBench/Test/Data/auto-full-scene.json`, including the moved camera,
Full geometry, 1000-lux directional, point/spot lights, Auto/Average, None and
zero compensation. It uses a 320x256 (5:4) scene viewport approximating the
supplied image's scene area; this is not a pixel-identical window reproduction.
It waits for all six draw records before sampling and enables the production
shadowing capability. No capture or runtime instrument was added.

The independent CPU histogram predicts luminance **0.009247482 cd/m²** and gain
**19.464758**. GPU metering reports **0.009247496**, with a constant target gain
**19.464727**. Starting from reference gain **0.003000116**, applied gain rises
through **0.00614** at about 1 s, **0.786** at 8 s, **18.693** at 16 s and
**19.464586** at 30 s. It converges within the retained 5e-4 EV budget; target
and meter match the independent histogram within 2e-4 EV throughout the ready
scene. P remains 1. The case passes in optimized Release.

This is a **12.66-stop** adaptation to the dim ground-dominated histogram, not
an unbounded feedback loop. None hard-clips exposed RGB above 1. The saved scene
has 21,749 clipped pixels among 76,829 positive pixels at convergence; the bright
cards/sphere surfaces lose detail while the dim ground is exposed upward.
The horizontal reference light illuminates the vertical cards but does not give
the horizontal ground comparable illumination. Zero compensation adds no offset;
it does not preserve reference gain or prevent highlight clipping.

EX09B/C owns a useful explicit Auto viewing preset and operating guidance:
appropriate scene lighting/metering plus a tone curve, while Manual/None remains
the neutral numerical reference. A tone curve alone does not correct a meter
that strongly favors dim background. No production exposure algorithm or
numerical tolerance was changed. [Result](evidence/validation/ex08-20260925/lightbench-ex08-auto-release.json.gz)
and the adjacent log retain the numerical evidence.
