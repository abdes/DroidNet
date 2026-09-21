# LightBench exposure benchmark

Status: experiment delivery remains planned. EX06 delivered settings isolation
and startup prerequisites; the calibrated controller/instruments are still open.
The [delivery plan](../vortex/plan/exposure-and-lightbench-correction.md) owns
slice order and acceptance. [PBR](physically-based-rendering.md) owns equations
and frozen precision budgets. This document owns experiments and presentation.

## Delivery outcomes

The engine's exposure foundation is validated. Remaining LightBench work makes
it a usable reference with independent expected/measured values, reproducible
reset and the same experiment in interactive and batch execution.

| Delivery step | Experiments or workflow delivered                                                                                              |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| EX07          | Qualified physical references and many-light correctness/performance; existing native fixtures and separate opt-in benchmarks. |
| EX08          | Neutral Reference, qualified instrument, complete recipe/reset/save/load, initial batch report and operating README.           |
| EX08.1        | Console inspection/edits/transitions through existing validated owners.                                                        |
| EX08.2        | Actual ImGui interaction regression tests with isolated state and failure artifacts.                                           |
| EX09A         | Point Falloff and Spot Distribution.                                                                                           |
| EX09B         | Fixed Exposure.                                                                                                                |
| EX09C         | Adaptation and Lifecycle.                                                                                                      |
| EX09D         | HDR Domain.                                                                                                                    |
| EX09E         | Existing MultiView scenario/measurement/runner integration.                                                                    |
| EX10          | Final integrated package evidence and documentation.                                                                           |

Every experiment arrives with its controls, batch case, numerical gate and native
visual check. Reuse the same controller/report and valid previous proof. The
[delivery plan](../vortex/plan/exposure-and-lightbench-correction.md#8-ordered-implementation-slices)
owns exact gates; this document remains the experiment/presentation contract.

EX07's [many-light workload suite](../vortex/plan/EX07-lighting-correctness-and-scalability.md)
qualifies production scalability before this controller is available. It retains
its own correctness fixtures and benchmark executable; it does not add an eighth
LightBench experiment or delay its baseline until EX08 instruments exist.

## One experiment controller

Interactive selection, reset, saved-experiment loading and batch execution use
one controller and the same schema-validated version-1 definitions. An experiment
owns geometry, materials, lights, environment, camera, feature settings,
exposure/output transform, measurement regions and scripted transitions.
Stage a complete recipe, wait for required asset readiness, then publish at one
frame boundary with a new experiment revision and appropriate public transition.
Do not expose a half-applied recipe or start measurement before readiness.

Persist window/panel preferences separately. DemoShell's experiment-owned
activation policy skips saved camera/post-process/environment reapplication;
other demos retain their existing policy. Batch runs never mutate personal
settings. Saved modified experiments load explicitly. Reset restores every
reference input, scripted time, measurement validity and temporal state.
Convert the shipped indoor preset to the same versioned experiment format.

Each definition contains: schema version, stable experiment ID, recipe revision,
scene/material/light/camera configuration, view/layout settings, canonical
exposure settings, output transform, fixed game timestep, readiness requirements,
measurement regions, transition sequence and independently derived expectations.
No runtime resource slots, GPU state or view handles are serialized.

## Seven experiments

| ID                | Initial configuration                                                                                                                                         | Sequence and evidence                                                                                                                                                                                                                                                 |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| neutral-reference | Three nonmetallic gray/white/black cards; neutral white directional key on visible side; fixed camera, controlled environment, explicit tone/output transform | Default launch/reset; full production BRDF oracle for decoded packed normals, albedo, roughness and fixed specular=0.5; readable full-card framing                                                                                                                    |
| fixed-exposure    | Known constant HDR foreground 4096; key 12.5, compensation 0, None, gamma 1                                                                                   | EV14/15/16 -> 0.25/0.125/0.0625 before dither; sweep supported gain bounds, keys and compensation; disabled -> unit gain                                                                                                                                              |
| adaptation        | Known uniform and structured luminance distributions, Auto and controlled game dt                                                                             | Both step directions; 30/60/120 Hz and irregular schedules at equal time; pause/zero speed/long frame; mask/profile/curve variants and linear/exponential crossing                                                                                                    |
| lifecycle         | Public game-facing view handles/events and controlled scene signal                                                                                            | Startup, seed event frame including out-of-meter-range EV, cuts, Manual/Auto, zero target/restoration, pause, sharing/source loss, stateless and recovery; delayed/stale status                                                                                       |
| point-falloff     | White point on receiver normal, camera on visible side; fixed material/exposure; distances far above 1 mm and inside range                                    | Prescribed distances and known range fade; independent flux-to-candela and production BRDF; zero separation, near-field and range-edge fixtures                                                                                                                       |
| spot-distribution | White spot aimed at receiver centre; receiver shows inner/outer cones clearly                                                                                 | Angular sweep, hard-cone limit, finite cone boundaries; independent numerical solid-angle integration verifies total flux                                                                                                                                             |
| hdr-domain        | Bright/dark required signals plus opaque, forward, translucent, sky and fog content                                                                           | Vary numerical P without changing scene/output; startup and recovery; sustained excessive-range FP32; reduced-range eligibility/return under the explicit qualified diagnostic control; insignificant-signal case; inspect upstream writes and fog history conversion |

Neutral Reference uses known linear card albedos (0.18, 0.9, 0.02), metalness 0,
roughness 1, no texture or normal-map ambiguity, and a neutral 1000-lux
directional source. Include the production dielectric specular term and packed
values in its independent oracle. Derive fixed exposure from expected gray
luminance and the chosen output transform, never from a measured GPU brightness.
Record the resulting resolved EV in the recipe/report when the slice-7 material
oracle is qualified. This does not authorize substituting Lambertian-only math.

Frame cards with consistent margins; labels/background are display-space.
Point/spot overlays show receiver normal, light position, measured distance and
cone only when useful. Adaptation scenes must make both illumination transitions
visibly clear. Preserve background and UI readability as exposure changes.

## Controls and verdicts

Show experiment selector, Reset, explicit Load/Save experiment, relevant controls,
expected/measured values and effective exposure. Advanced settings (D, black
influence, histogram window, precision status) are collapsed. Show requested and
effective settings separately when residency/validation delays application.
Do not label adapted gain as measured luminance. Debug overrides are visible.

Before a current measurement is available, show progress/pending without a verdict.
States: Pass (all valid comparisons passed), Fail (valid comparison outside
budget), Modified (recipe differs from reference), Invalid (measurement or
required resource unavailable). Optional unsupported capabilities may have a
diagnostic; required experiments cannot complete as Unsupported. Do not reuse
late values after experiment reset/change as a current verdict.

## Console and UI automation ownership

EX08.1 commands reuse the existing Oxygen console and validated settings,
transition and experiment owners. Console edits and UI edits must converge on
the same accepted state. Explicit view targets and asynchronous outcome/revision
reporting are required; commands never write GPU state directly.

EX08.2 adds an opt-in ImGui Test Engine configuration and tests the actual widgets.
Use stable item paths, bounded readiness waits and isolated assets/preferences.
Exercise keyboard commit/cancel/focus, dragging, curves, mask failure/recovery,
reset/save/load and panel navigation; console setup cannot substitute for those
interactions. New experiments extend this suite as they land. Keep human visual
inspection for readability/composition and independent GPU probes for numerical
correctness. Dependency/configuration/license prerequisites belong to that slice.

## Independent measurement

DiagnosticsService owns bounded region-statistics and same-frame consumed-gain
probes; LightBench owns region definitions, independent expectations and verdicts.
Request no GPU dispatch/readback allocation when measurements are disabled.
No permanent full-resolution post-exposure target is allowed. Probe the actual
production multiplication for consumed gain/linear pixel evidence, or extract a
real product. Explicitly label any CPU-derived quantity.

Results carry logical frame, ViewStateHandle lifetime, experiment revision,
source product/domain, stored-P generation, region, settings/event generations,
validity and sample counts. Recover scene-referred values using the product P.
Readbacks remain leased through completion. Reject stale, nonfinite, occluded,
edge-contaminated and insufficient-coverage samples; zero samples is Invalid,
not a black measurement. Use inset card regions with at least 95% expected
foreground coverage and reject any unexpected geometry/depth coverage.

Before lighting verdicts, qualify instruments against known float textures,
coverage, zero/invalid regions, varying P, exact consumed gain and delayed
readbacks. Use exact bin-grid references separately from full-resolution sampling
references. For small-feature and edge sweeps report retained weighted mass and
meter EV error; a reference feature must occupy at least four bounded-grid cells
for a stable exposure verdict. Smaller features explicitly test sampling limits,
not exact full-image metering equivalence.

## Native visual and batch acceptance

Inspect startup, all seven initialized experiments, transitions, steady state and
full reset at 1920x1080, 2560x1440 and a resized window. Measure convergence by
elapsed time and stop error, not a fixed warmup-frame count. Verify mask, curve,
mode, EV and compensation interaction against numerical data. No stale overlays,
unintended black frame, white flash or saved-settings reapplication is acceptable.

Batch uses identical recipes/controller and actual controlled game dt. Reports
include resolved values, experiment/version/revision, build/shader identity,
backend/device, dt/frame/view IDs, sample validity, predeclared tolerances,
expected/measured values, captures and verdicts. Extend existing capture tools
and MultiView analysis/schema, not a separate benchmark infrastructure.

MultiView runs ordinary lit PiP and standard/auxiliary/offscreen/feature layouts.
Compare per-view pre-composition output to an equivalent standalone view before
checking final composite. Exercise independent/shared gain, submission reorder,
resize, hide/recreate, source loss and different per-view FP16 suitability.
Inspect every lit pane and retain intentional BLACK expected diagnostic cells.

Operational commands and actual supported CLI belong in the application READMEs
as implementation lands. Store reports and inspected native captures under
`out/build-ninja/analysis/vortex/exposure-lightbench/`.
