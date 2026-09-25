# LightBench calibration demo

Status: EX08–EX10 validated (2026-09-25); structured commit delivery was authorized after user review.
See [closeout and acceptance](../vortex/milestones/exposure/EX10/validation.md).
The [delivery plan](../vortex/milestones/exposure/README.md) owns
sequence and gates; [PBR](physically-based-rendering.md) owns equations and frozen
budgets. This document owns the demo behavior. EX01–EX07 remain closed.

## Delivery outcomes

LightBench provides known inputs, independently qualified reference rendering,
reproducible presentation and clear controls. It does not numerically certify
arbitrary edited frames in the running application.

| Step   | Useful result                                                                                           |
| ------ | ------------------------------------------------------------------------------------------------------- |
| EX08   | Calibrated Neutral Reference; complete reset; explicit local save/load; focused native test and README. |
| EX08.1 | Existing console drives validated settings/transitions and local preset/reset operations.               |
| EX08.2 | ImGui interaction automation reactivated after EX09; user retains final acceptance.                     |
| EX09A  | Point/spot calibration presets with relevant controls and useful geometry cues.                         |
| EX09B  | Fixed exposure controls on the reference scene; exact numerical cases stay in native tests.             |
| EX09C  | Simple bright/dark transition and reset; lifecycle/timing matrices stay in native tests.                |
| EX09D  | Existing HDR correctness coverage plus focused checks for gaps; no new demo experiment.                 |
| EX09E  | Existing MultiView controls/proofs and remaining operational acceptance.                                |
| EX10   | Durable evidence summary, affected tests, user acceptance and actual instructions.                      |

The user removed the universal experiment controller/schema, reusable runtime
measurement service, live sampling/readbacks, instrumented tonemap variants,
runtime numerical verdicts, seven complete experiment UIs and new batch/report
framework from this package. These are not implemented or counted as complete.
Do not reintroduce them as prerequisites for a useful calibration demo.

## Reference presets

Neutral Reference uses linear card albedos **0.18, 0.9, 0.02**, metalness 0,
roughness 1, no texture/normal-map ambiguity and a white **1000-lux directional
light**. Place camera and light on the visible side; use a controlled environment
and explicit output transform. Include production dielectric specular and decoded
packed values in the independent oracle. High roughness is not Lambertian-only
shading. Derive fixed exposure from expected gray-card luminance and the chosen
output transform, never from measured GPU brightness. Record resolved EV and
reference expectations in the durable qualification summary.

Frame all three cards with consistent margins. Labels and background remain
readable and separate from physical illumination. Advanced controls start
collapsed. Show relevant light/exposure units and clearly distinguish authored,
accepted and independently calculated reference values.

Point preset: white point on the receiver normal; fixed material/exposure;
distances well above the 1 mm numerical guard and with known range fade. Spot
preset: white spot aimed at receiver centre with visible inner/outer-cone response.
Show position, receiver normal, distance and cone only when useful. Credit EX07
flux/inverse-square/range/cone/finite-emitter references when applicable. Do not
build sweep editors or alternate rendering models for these presets.

Fixed exposure uses the reference scene and existing controls. A simple
reproducible bright/dark change demonstrates adaptation in both directions.
Detailed lifecycle and HDR stress cases remain native fixtures.

## Local settings and complete reset

Use a small LightBench settings structure and existing scene/settings owners.
The app and focused reference test share canonical scene construction/parameters;
expected results remain independently calculated. No generic experiment engine
or matching interactive/batch execution abstraction is required.

Apply a complete validated state at the normal frame boundary; wait for required
assets through existing readiness handling. Avoid partially applied settings.
Reset Reference restores geometry, materials, lights, environment, camera,
rendering/output/exposure settings and relevant temporal state through public
transitions. Reset a selected preset to all its defined inputs.

Reuse DemoShell's experiment-owned activation policy; do not reapply personal
camera/environment/post-process preferences over the reference. Window size,
panel layout and UI preferences remain separate and unchanged. Other demos keep
their existing persistence behavior.

Explicit Save/Load round-trips modified local settings. Validate the entire load
before mutating accepted state; reject malformed or unsupported versions with an
actionable message. Update the shipped indoor settings to the supported local
format, with no universal recipe schema or legacy reader. Preserve user-owned
files. Never serialize runtime resource slots, GPU history or view handles.

Indicate defaults with the normal preset-container border and modified inputs
with an amber border. Keep popup borders neutral and omit redundant status
badges. This is not a numerical Pass/Fail verdict. Existing asynchronous settings/resource status must remain
truthful; a pending mask is not accepted, and settings status is not measured GPU
luminance or same-frame consumed gain.

## Console and UI acceptance ownership

EX08.1 reuses the existing console policies and validated settings/transition
owners. Ordinary commands are available in normal Release builds. Explicit
view/owner targeting, atomic rejection and queued/applied/rejected revision/token
feedback remain required. Local preset/reset commands call LightBench directly.
No GPU-state writes, parallel persistence path or measurement commands.

The agent owns automated unit/native correctness tests. The user owns UI checks.
When ready, launch the demo and provide numbered actions and expected outcomes.
Record OK/NOK and reasons; fix failures and retest only affected checks.
The reactivated Test Engine suite is now qualified in Debug/Release inside the
real applications. It covers widget workflows; native tests retain numerical,
schema, file-failure and lifetime contracts. The development generator enables
UI tests by default, including Release. Final-release builds explicitly use
`-UiTests:$false` and rebuild with instrumentation excluded.

## Independent measurement

Numerical measurements belong in existing native fixtures/readbacks. Reuse the
EX07 independent lighting oracle and applicable prior evidence. Render the shared
canonical scene through production rendering, including optimized Release tests
in the existing Ninja tree. No interactive measurement facility or new shader
instrumentation is required.

Fixture comparisons identify actual source products and recover scene values
with stored P. Use inset reference regions with at least 95% expected foreground
coverage; reject unexpected geometry/depth, edge contamination, nonfinite or
missing samples. Missing samples cannot pass as true black. Preserve production
BRDF, packing, sampling and output-encoding budgets. Known-input fixture checks
must establish the readback/comparison path is meaningful; no new general GPU
instrument qualification project.

Fixed-exposure fixtures retain input 4096, key 12.5, compensation 0, None/gamma 1:
EV14/15/16 produce 0.25/0.125/0.0625 before dither/encoding. Camera, disabled,
key/compensation and supported limits remain covered by existing/affected tests.
Metering/adaptation/lifecycle/HDR matrices remain in the delivery plan. For
sampling tests distinguish exact bounded-grid expectations from full-image
integration: features covering fewer than four grid cells test sampling limits.
Convergence is assessed by elapsed time and EV error, not fixed warmup frames.

## Native visual acceptance and evidence

The user checks clean launch/reset, complete framing, relevant edits and explicit
save/load at 1080p, 1440p and a resized layout. Accept new presets/transitions as
they arrive, reusing unchanged layout checks. Unexpected flashes, black frames,
stale status and personal-settings reapplication are bugs. No repeated captures
or numerical proof is demanded from the user.

MultiView retains ordinary lit PiP and existing standard/auxiliary/offscreen/
feature layouts. Preserve standalone/family equivalence, independent/shared
exposure, previous-owner-frame delay, reorder/resize/lifetime/mode/source-loss
behavior and intended composition. Credit applicable EX05 evidence; retain BLACK
expected diagnostic cells and check remaining interactions with the user.

Publish actual launch/test commands in application READMEs as implementation
lands. Record source/build/shader/scene identities, expected/measured test values,
frozen tolerances, applicable prior evidence and user acceptance in durable
Markdown. Existing test outputs are sufficient; no new report schema or runner.
Transient analysis paths alone are insufficient. Do not redo EX07 captures or
benchmarks for this work.
