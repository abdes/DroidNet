# EX09 — Useful controls and renderer qualification

Status: `validated`

| Field     | Summary                                                                 |
| --------- | ----------------------------------------------------------------------- |
| Outcome   | Useful light/exposure presets, renderer fixes and MultiView acceptance. |
| Remaining | None in the recorded scope.                                             |
| Evidence  | [Validation record](validation.md)                                      |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

Keep the five existing step IDs. Each closes with applicable evidence, focused
new checks for gaps/changes, relevant user interaction and working instructions.
Do not require a separate experiment implementation for each renderer contract.

#### EX09A - Point and spot calibration presets

**Dependency:** EX08.1. **Tracked by:** EX09-07–08, with EX09-10/12/14.

Provide two simple presets: white point on the receiver normal and white spot
aimed at its receiver. Keep camera, material and exposure controlled; show useful
distance/cone/normal information. Reset restores all preset inputs. Credit EX07
flux, inverse-square/range-fade, cone integration, singularity and invalid-input
proofs when applicable. New/changed presets receive focused rendering checks
and manual visual acceptance. No angular-sweep UI or general batch recipe engine.

#### EX09B - Fixed exposure controls

**Dependency:** EX09A. **Tracked by:** EX09-04, with EX09-10/12/14.

Use Neutral Reference and existing Manual/ManualCamera/key/compensation/disabled
controls. User checks understandable visual response. Native existing/focused
fixtures retain the exact 4096-input, None/gamma-1 EV14/15/16 expectations
0.25/0.125/0.0625 before encoding, camera equation, supported bounds and atomic
invalid-input rejection. These are test values, not claimed live measurements.
No separate Fixed Exposure experiment UI or consumed-gain probe.

#### EX09C - Adaptation and lifecycle

**Dependency:** EX09B. **Tracked by:** EX09-05–06 and EX10-03.

Provide a simple reproducible bright/dark transition and reset, using existing
exposure owners. User checks both directions without unintended flashes or black
frames. Preserve native timing and lifecycle requirements: equal-time schedules,
linear/exponential crossing, pause/zero speed/long frames, masks/profiles/curves,
startup, event-frame seed/cut, mode changes, zero target/restoration, sharing,
source loss, stateless views and recovery. Credit applicable existing proof;
add only focused regressions for changes/gaps. Retain 5e-4 EV equal-time tolerance
and event-generation semantics. No timeline editor, response plot, sequence
language or UI intended solely to force device/resource failures.

#### EX09D - HDR correctness coverage

**Dependency:** EX09C. **Tracked by:** EX09-09.

Audit existing EX05 mixed opaque/emissive/forward/translucent/sky/AP/fog/bloom,
endpoint and history evidence for applicability. Preserve accepted FP32/P=1
production behavior; varying P/half eligibility remain explicit diagnostic tests.
Keep product P-invariance budgets (0.5% relative + 2e-5 absolute), one-code-value
UNorm8 budget, range/recovery and delayed-status/history correctness. Only missing
or invalidated requirements need new focused tests/inspection. No LightBench HDR
experiment UI or new capture campaign. Record applicability instead of silently
assuming prior results cover changed code.

#### EX09E - MultiView operational acceptance

**Dependency:** EX09D. **Tracked by:** EX09-13/15 and EX10-04–05.

Use the existing demo, exposure controls, proof scripts and tests. Preserve
section 7.5's standalone/family equivalence, independent/shared exposure,
previous-owner-frame delay, view lifetime and composition requirements. Credit
applicable ordinary/PiP, auxiliary/offscreen and feature-layout evidence; fix
remaining control/interaction gaps and obtain the user's outstanding UI checks.
Retain intentional BLACK expected cells and inspect lit panes correctly.
Update actual README commands. No new instrument integration or report schema.

**EX09 group gate:** every retained renderer requirement has applicable evidence
or a focused passing new check; retained controls/presets have acceptance;
discovered bugs are fixed. Seven experiment implementations are not required.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 9A-E — Useful controls and renderer gaps | validated | Seven presets, focused renderer repair, numerical coverage and accepted MultiView workflows. | [EX09 results](validation.md) |

## Tasks and outcome

**Validated.** Useful demo controls plus retained renderer guarantees. Applicable
EX01–EX07 evidence counts; only changed or uncovered behavior needs new checks.
Original IDs remain, with narrowed delivery obligations stated explicitly.

| ID        | Required result / disposition                                                                                 | Delivery step | Status    |
| --------- | ------------------------------------------------------------------------------------------------------------- | ------------- | --------- |
| EX09-01   | Universal UI/batch experiment schema/controller removed.                                                      | —             | removed   |
| EX09-02   | Local complete frame-boundary settings application/reset, including temporal state.                           | EX08          | validated |
| EX09-03   | Calibrated readable three-card Neutral Reference.                                                             | EX08          | validated |
| EX09-04   | EV/camera/key/compensation/disabled controls and existing/focused numerical proof; no separate UI experiment. | EX09B         | validated |
| EX09-05   | Simple bright/dark transition; native schedule/mask/profile/curve evidence.                                   | EX09C         | validated |
| EX09-06   | Native startup/seed/cut/mode/pause/zero/sharing/stateless/recovery coverage; no lifecycle UI framework.       | EX09C         | validated |
| EX09-07   | Useful point preset, distance relationship and applicable independent reference proof.                        | EX09A         | validated |
| EX09-08   | Useful spot preset, aimed receiver/cone controls and applicable flux proof.                                   | EX09A         | validated |
| EX09-09   | Applicable HDR endpoint/mixed-path/history evidence and focused gap repair; no HDR demo experiment.           | EX09D         | validated |
| EX09-10   | Relevant controls/units and truthful Reference/Modified/settings status; live numerical verdict removed.      | EX08–EX09C    | validated |
| EX09-11   | Explicit local save/load, supported indoor settings, personal preferences preserved.                          | EX08          | validated |
| EX09-12   | Manual visual acceptance for retained presets/transitions and layouts; seven-experiment capture gate removed. | EX08–EX09C    | validated |
| EX09-13   | Existing MultiView controls/proofs and remaining operational checks; no new measurement integration.          | EX09E         | validated |
| EX09-14   | Actual LightBench launch/reset/load/save and focused native test instructions.                                | EX08–EX09C    | validated |
| EX09-15   | Existing MultiView README: actual controls, sharing delay and supported commands.                             | EX09E         | validated |
| EX09-GATE | Retained renderer requirements covered; useful workflows accepted; discovered bugs fixed.                     | After EX09E   | validated |

**Concrete EX09B/C usability case (2026-09-25):** Auto/Average on the user's
modified Full scene with tone mapper None and compensation 0 clips the bright
cards. A native Release reproduction with saved authored settings and a 5:4
viewport passes the independent histogram/stability check: meter 0.0092475 cd/m²,
constant target gain 19.4647, converged gain 19.4646 after 30 simulated seconds.
The rise from reference gain is 12.66 stops, not runaway feedback. The dim ground
dominates metering while the horizontal directional light illuminates vertical
cards. Provide an explicit Auto viewing setup with useful lighting/metering and
a tone curve; preserve Manual/None for the neutral reference. No solver/tolerance
change, runtime instrumentation or capture campaign. See [diagnosis and proof](../EX08/validation.md#saved-scene-auto-diagnosis-native-release).

EX09-13 has final user OK for ordinary and offscreen MultiView. Both 144-frame
operational sequences pass and EX09-15 operating instructions are updated. EX09-06/09 native
coverage and the fog repair are qualified in the [checkpoint](validation.md).

## Supporting records

- [validation](validation.md)
- [evidence](evidence/README.md)
- [Captured evidence](evidence/README.md)
