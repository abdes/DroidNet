# Exposure management and LightBench implementation plan

Status: `in_progress` — current slice state and evidence are maintained in the
[implementation tracker](../IMPLEMENTATION_STATUS.md#3-exposure-delivery-status).
[Current work](../IMPLEMENTATION_STATUS.md#31-current-work) and
[item-level Slice 5–10 completion](../IMPLEMENTATION_STATUS.md#32-slice-5-work-items)
are updated there; this plan owns the requirements and gates.

Date: 2026-09-16
Updated: 2026-09-24 — EX06, EX07A/B and EX07D are closed; EX07E is prepared and awaits the user's explicit start signal. Remaining C validation and E/F gates keep overall EX07 open.

Paths are relative to `projects/Oxygen.Engine` unless identified as
repository-root paths.

## 1. Delivery scope and sequence

Deliver a desktop global-exposure system whose behavior developers can predict,
author, inspect and reproduce. The finished package has three observable results:

1. **Production rendering:** physically defined lights and the implemented exposure
   system produce the specified values through forward and deferred rendering.
2. **LightBench:** launch a readable calibrated reference, select one of seven
   experiments, see expected and measured results, edit it, and reset to a known
   state. Run the same definition unattended and obtain a trustworthy report.
3. **MultiView:** ordinary lit main/PiP and proof layouts remain visually correct;
   exposure is independent unless sharing is explicit, including during changes
   to cameras, layouts and view lifetimes.

The exposure engine, HDR/lifecycle contracts, performance disposition and authoring
are already validated in Slices 1-6, including 5.1 and 5.2. Remaining work completes
physical calibration and the maintained applications/instruments that demonstrate
those contracts. EX07 now also owns many-light correctness/performance and targeted
lighting improvements. It does not reopen the completed exposure optimization cycle.

### Remaining delivery at a glance

Execute **EX07 -> EX08 -> EX08.1 -> EX08.2 -> EX09A -> EX09B -> EX09C -> EX09D -> EX09E -> EX10**.
EX09A-E divide the original large Slice 9; existing requirement IDs remain stable.
All are planned. Each step ships its focused tests, usable controls, batch case
and documentation together; EX10 integrates already working delivery.

| Step                                               | What the user can do when it closes                                                                                                       | Concrete completion evidence                                                                                                                                                |
| -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| EX07 — Correct and scalable lighting               | Physically correct lights and large supported light sets in both rendering families, preserving Vortex's deferred-first desktop contract. | Independent calibration, conservative culling/overflow tests and measured CPU/GPU/memory qualification; [detailed EX07 plan](EX07-lighting-correctness-and-scalability.md). |
| EX08 — Use one trustworthy reference               | Launch Neutral Reference; inspect three cards, expected/measured values, reset and explicitly save/load an experiment.                    | Qualified instruments; readable native image; identical interactive/batch recipe; one schema-valid report.                                                                  |
| EX08.1 — Drive post-processing through the console | Inspect/edit settings and trigger transitions through Oxygen's integrated console.                                                        | Commands use the same validated services, report requested/accepted state and reject invalid/stale targets atomically.                                                      |
| EX08.2 — Automate the actual ImGui controls        | Run repeatable UI regression tests without coordinate scripts.                                                                            | Native tests cover edits, focus, curves, masks, reset and panel navigation, with failure artifacts.                                                                         |
| EX09A — Inspect point and spot lighting            | Change prescribed distance or cone angle and understand the visible falloff.                                                              | Point Falloff and Spot Distribution pass their numerical sweeps and native visual checks.                                                                                   |
| EX09B — Verify fixed exposure                      | See how EV, camera settings, key and compensation change known HDR input.                                                                 | Fixed Exposure produces 0.25/0.125/0.0625 for EV14/15/16 before output encoding.                                                                                            |
| EX09C — Understand transitions                     | Play/restart bright/dark steps and lifecycle events and inspect the response over time.                                                   | Adaptation and Lifecycle pass timestamped trajectories, event-frame checks and interactive reset.                                                                           |
| EX09D — Verify mixed HDR content                   | Change numerical pre-exposure while the scene's intended appearance remains stable.                                                       | HDR Domain passes product comparisons, bright/dark recovery and inspected native output.                                                                                    |
| EX09E — Trust multiple views                       | Use lit PiP and existing proof layouts with independent or explicitly shared exposure.                                                    | Existing proofs integrated with qualified measurements; every lit pane and required interaction passes.                                                                     |
| EX10 — Reproduce package acceptance                | Run documented acceptance commands and identify any failed case from the report.                                                          | All requirements mapped to valid evidence, final integration passing, operating docs complete.                                                                              |

The detailed scope and exit gate for each step are in section 8. The
[tracker](../IMPLEMENTATION_STATUS.md#31-current-work) owns current execution
state; historical manifest `remaining` lists do not reopen closed slices.

### Reuse the delivered foundation

| Delivered and retained                                                                                             | Actual remaining gap                                                                            |
| ------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------- |
| Canonical settings, fixed/camera/Auto/disabled state, masks, curves and hybrid response (EX02-04)                  | Present those behaviors as reproducible LightBench experiments.                                 |
| Frame-pinned HDR domains, recovery and native MultiView proofs (EX05)                                              | Integrate instruments and final demo operation; repair only demonstrated regressions.           |
| Accepted production precision/performance and focused quality work (EX051/052)                                     | Preserve their decisions; perform affected regression checks, not a new optimization campaign.  |
| Strict current-format persistence, scripting/editor transport, public C++20 boundary and DemoShell controls (EX06) | Versioned complete experiment recipes and independent verdicts.                                 |
| Experiment-owned activation, camera framing and settings isolation (EX06)                                          | Whole-recipe Reset/Load/Save and a calibrated directional Neutral Reference.                    |
| MultiView exposure CLI, scenario fixtures and analyzers (EX05)                                                     | One report/runner integration and any remaining user-facing controls; reuse existing sequences. |

Production remains the accepted FP32/P=1 policy. Varying P and qualified half
storage are explicit diagnostic coverage, not a plan to restore automatic FP16
admission. The accepted CPU cost remains closed; further optimization is deferred.
The EX06 console and ImGui Test Engine deferrals now have explicit planned
slices EX08.1 and EX08.2. They follow the first working benchmark so they can use
its controller and reports, and precede the remaining experiments so new scenarios
can gain UI regression coverage as they land. EX06 stays closed; native numerical
fixtures remain independently runnable.

The [exposure reference companion](../lld/exposure-improvement-plan.md) contains
UE source pointers and Oxygen's implementation choices. This document owns
delivery scope and gates; the [LightBench specification](../../renderer-core/lightbench.md)
owns experiment definitions and presentation.

### Included features

| Area                          | Deliverable                                                                                                                                                                 |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Mathematical contract         | Preserve Oxygen's EV100 equation; correct documentation and numeric propagation                                                                                             |
| Exposure state                | One per-view GPU state consumed by Manual, ManualCamera, Auto, and disabled modes                                                                                           |
| Metering                      | Bounded deterministic sampling, interpolated histogram bins, percentiles, black handling, masks, compensation curves                                                        |
| Adaptation                    | Predictable EV/s far from the target and smooth exponential convergence near it                                                                                             |
| Lifecycle                     | Concrete startup, cut, seed, mode transition, pause, stateless, sharing, and recovery policies                                                                              |
| HDR integration               | One frame-pinned pre-exposure domain across every active HDR producer and consumer                                                                                          |
| Authoring                     | Native components, source schemas, cooker, packed records, loader, scripting, DemoShell and existing editor adapters round-trip active fields                               |
| LightBench exposure benchmark | Properly repaired demo, readable reference scenes, complete exposure experiments, independent numerical expectations, repeatable interaction/reset and visual qualification |
| MultiView visual correctness  | Main/PiP, multi-camera, auxiliary and offscreen layouts render correctly with independent or explicitly shared exposure; exposure and multiview changes preserve each other |
| Qualification                 | GPU measurements, native gameplay scenarios, multiview tests, numerical and visual results                                                                                  |

EX07 owns review, correction, optimization and qualification of many-light
rendering: eligibility/culling, active shaders, deferred submission/overdraw,
shadow integration, resource scaling and necessary cross-module dependencies. Its
[bounded workload and gate](EX07-lighting-correctness-and-scalability.md) are part
of EX07 completion, not a later optional benchmark.

### Design limits

This package implements global exposure. Spatially varying local exposure,
mobile rendering, stereo-specific sharing, UE legacy migration modes, and a
second Basic auto-exposure algorithm are outside the product defined here.
Keep Oxygen's existing tone curves and output conventions. GI, new area-light
models, and general editor redesign are not required.

Use UE5.7 for reference behavior and source comparisons. Retain Oxygen's
runtime ownership and public types; implement only the data and passes needed
by the features above.

### Implementation boundaries

- Extend the existing PostProcessService, ExposurePass, SceneTextures, view
  lifecycle, and diagnostics owners. Add no separate exposure framework.
- Use one settings resolver, one exposure-state family, one metering algorithm,
  and one frame-exposure binding shared by rendering families.
- Use FP32 accumulation with optional qualified FP16 storage. Keep FP32 for
  bootstrap, recovery and FP32-only operation. Implement
  them through existing texture descriptors, PSO keys and resource leases;
  add no general dynamic-precision service.
- Reuse one bounded status/readback path for initialization acknowledgment,
  numerical range errors and diagnostics. Add no full-resolution telemetry target.
- Calibrate the existing directional, point and spot models. Add no light-type
  selector, alternative BRDF, generic curve editor or general benchmark framework.

### Working through the remaining plan

Begin with a runnable case and its independent expectation, then implement only
what that case needs. EX07 uses existing native fixtures/readbacks. EX08 qualifies
the reusable instrument before enabling Neutral Reference verdicts. Subsequent
experiments reuse that instrument, controller, schema and report.

Close each step with code, focused tests, native inspection where applicable and
its documented command. Check affected code with Oxygen formatting/oxytidy; do
not suppress clang-tidy diagnostics merely to close a gate. Preserve public C++20
boundaries, enum printing, complete-candidate validation and concise diagnostics.
Each demo launch must state the scenario and expected visible result in advance.
Record unexpected warnings/errors as failures; identify intentional negative-test
diagnostics in the case definition. Section 9 defines how to reuse evidence and
avoid repeated validation.

## 2. Current gaps and implementation owners

The original fixed-gain floor, histogram/adaptation, sharing, pre-exposure,
serialization and settings-isolation defects are closed by EX02-06. Their
requirements remain in sections 3-5 and the completed slice records.

Source review after EX06 identifies these remaining delivery gaps:

- `DeferredLightPacketBuilder` still forwards local-light intensity directly;
  `DeferredLightingCommon.hlsli` still uses `1/(d*d + 1)`. EX07 must complete
  the physical contract across both consumers, not calibrate a demo around it.
- `LightScene::ApplyScenePreset` changes object visibility. It is not a complete
  experiment controller, and there is no calibrated directional Neutral Reference.
  EX06's camera/isolation changes are retained as prerequisites.
- `Renderer::InspectExposureSettings` exposes accepted settings and bounded
  asynchronous status. It is not a same-frame GPU luminance/consumed-gain instrument.
- MultiView already has scripted exposure cases and capture/assertion tools.
  The reusable measurement/report integration is missing; those cases need no
  second implementation.
- The LightBench experiment specification exists, but its controller, operating
  README and package validation runner are not delivered.

Primary owners are `Vortex/Lighting`, the forward/deferred lighting shaders,
`Vortex/Diagnostics` and existing extraction/readback services,
`Examples/LightBench`, `Examples/MultiView` and `tools/vortex`. Use the file map
in section 8. Keep experiment semantics in LightBench and rendering measurements
in Vortex; do not duplicate runtime exposure or physical-light logic in the UI.

## 3. Exposure equations and state

### 3.1 Preserve the accepted calibration

For manual modes:

```text
bias = 2^compensation_ev * (exposure_key / 12.5)
manual_scale = bias * 2^-ev100
camera_ev100 = log2(aperture_f^2 * shutter_rate * 100 / iso)
```

The normalization constant 12.5 is not an EV setting. At key 12.5 and zero
compensation, EV14 gives `0.00006103515625`. The f/11, 1/125 s, ISO100
example gives EV100 approximately 13.885.

For automatic exposure, let `L_meter` be the positive scene-referred,
percentile-trimmed geometric-mean luminance:

```text
metered_ev = log2(L_meter / 0.18)
bounded_ev = clamp(metered_ev, min_ev, max_ev)
curve_ev = compensation_curve(metered_ev)
auto_bias = 2^(compensation_ev + curve_ev) * (exposure_key / 12.5)
target_scale = target_luminance * auto_bias / (0.18 * 2^bounded_ev)
```

Evaluate the curve from raw metered EV before the authoring range clamp.
When min_ev equals max_ev, use that bound for both the denominator and curve
evaluation, and apply the fixed target immediately. This locked-range mode can
resolve without a histogram. It overrides Manual-to-Auto continuity; an explicit
Seed owns its event frame, and the locked target applies afterward. A remeter
request completes when the locked fixed solve is applied. Manual modes ignore
the automatic compensation curve.

Validate complete settings atomically. Preserve valid fixed-exposure multipliers
through configuration, upload and shader consumption. Evaluate products in log
space when needed to avoid intermediate overflow. Invalid settings report an
error and leave the previous valid settings revision active.

Define the supported domain from CPU conversion, shader normal-float behavior,
and intermediate arithmetic headroom. Audit actual ShaderBake/DXC flags;
include subnormal boundaries in tests. Output quantization is a separate error
budget, not a reason to floor exposure. The compiler reference is
[DXC denormal mode](https://github.com/microsoft/DirectXShaderCompiler/wiki/Denorm-Mode).

### 3.2 One GPU state, with distinct measurements and controls

PostProcessService owns persistent state keyed by `ViewStateHandle`.
`ViewId` remains the frame publication/composition identity.

The state contains only fields used by this implementation:

- Current and target linear exposure scales, plus positive latent gain for
  zero-target operation.
- Raw metered luminance and metered EV, each with validity.
- Settings revision, requested/applied transition generation, and frame sequence.
- Flags for history validity, initialization, metering validity, numerical
  saturation, and fallback reason.

Adaptation operates on `log2(scale)`. Do not label an adapted gain as
"measured luminance". Manual and disabled modes also publish this state,
including their effective gain, so mode switches share one history contract.

Keep prior and current state generations separate. Frame bindings pin the
generation they consume until the GPU finishes. Reuse existing allocators,
publication services, and fence retirement. CPU readback serves diagnostics and
nonblocking resource-format acknowledgments; numerical exposure remains GPU-owned.

### 3.3 Hybrid adaptation in stops

Use a finite positive transition distance D, default 1.5 stops. Existing
SpeedUp/SpeedDown fields become actual maximum EV/s rates. Increasing illumination normally
requires a lower exposure gain: use SpeedUp when target log gain is below
current log gain, and SpeedDown otherwise.

For a constant target over a frame, integrate the following response exactly:

```text
q = log2(current_positive_scale)
q_target = log2(target_positive_scale)
r = abs(q_target - q)
v = selected_speed_ev_per_second

if v == 0 or dt == 0: retain q
otherwise:
    if r > D:
        t_linear = min(dt, (r - D) / v)
        r -= v * t_linear
        dt -= t_linear
    r *= exp(-(v / D) * dt)
    q_next = q_target - sign(q_target - q) * r
```

Handle zero error directly. The slope is continuous at `D`; splitting a
constant-target interval into different frame schedules gives the same result
within floating-point tolerance. Initialization, explicit seeds, manual updates,
and remeter cuts bypass adaptation even when speed or delta time is zero.

Use finite nonnegative game delta time. Pause freezes ordinary adaptation.
Do not discard elapsed time through an undocumented delta clamp; the analytic
update handles a long frame without overshoot. Clamp the metered target to its
authored EV range before adaptation, rather than clipping history every frame.

Preserve existing authored speed numbers; document that they now mean EV/s.
Update defaults only where the authoritative default contract requires it.

### 3.4 Mode changes, seeds, and zero target

- Manual/ManualCamera entry immediately applies the authored value.
- Manual-to-Auto entry preserves the previous effective positive gain for the
  transition frame, then adapts toward the metered target on subsequent frames.
  Carry the gain directly; do not fabricate a metered luminance from it.
  Locked-range and zero-target rules take precedence.
- Explicit `SeedFromEv100(ev)` computes
  `L_seed = 0.18 * 2^ev` and derives gain from the current auto target, bias
  and curve at that EV. Publish the seed for the transition frame and begin
  adaptation on the next frame. A seed can be applied without valid metering.
  Permit numerically valid seeds outside the metering EV range; the range bounds
  subsequent measured targets, not the requested seed. A locked-range target
  applies immediately after the seed's event frame.
- Remeter initializes gain from the first valid current measurement. Its
  generation is consumed only when that solve is applied. Locked-range Auto
  resolves its fixed target and consumes the request without histogram input.
- Auto target zero produces displayed gain zero. Continue metering and maintain
  positive latent gain using the nominal 0.18 target. Pre-exposure remains
  positive. Zero target overrides positive-gain continuity and immediately
  displays black, including entry from Manual. Never take the reciprocal or
  logarithm of displayed gain zero.
- Restoring a positive target remeters immediately from valid current input,
  or the last valid metered EV while awaiting current input. Without either,
  follow the normal initialization path. Do not invert the previous zero gain.
- Exposure disabled publishes displayed gain one. Its HDR input still needs
  the inverse pre-exposure factor during tonemapping.

## 4. Runtime lifecycle and HDR domains

### 4.1 Public view-transition contract

Resolve exposure settings from scene-authored values plus explicit per-view
overrides through the same normalization function. A view override does not mutate
the shared scene or another view's settings. Add the narrow exposure override
surface to the public composition/view contract; reuse the canonical exposure
types rather than a second post-process configuration model. This allows the
MultiView demo to exercise different modes and biases in one view family.

Add a typed transient request to the public view lifecycle: target
`ViewStateHandle`, request generation, policy, and optional EV100 seed.
Requests are runtime events, not scene-asset properties. Game code and DemoShell
use the same interface.

Renderer allocates 64-bit transition generations. `QueueExposureTransition(handle,
policy, seed)` returns a request token; `RetryExposureTransition(token)` reuses
that identity. Implicit resets share the counter. Submission and GPU application
remain separate, with settings/mode/ownership validation at the frame boundary.

| Event                                            | Required behavior                                                                            |
| ------------------------------------------------ | -------------------------------------------------------------------------------------------- |
| New unseeded Auto view                           | Meter current frame and initialize immediately                                               |
| Camera cut / replaced world / device recovery    | Remeter by default; explicit Preserve or Seed overrides are supported                        |
| Walking between illumination levels              | Continue adaptation                                                                          |
| Streaming, spawning, ordinary light changes      | Continue adaptation                                                                          |
| Compatible viewport/format/resolution change     | Preserve exposure; recreate only size/format-dependent resources                             |
| Explicit seed                                    | Publish the seed for the event frame; adapt next frame                                       |
| Manual/ManualCamera entry                        | Apply authored exposure immediately                                                          |
| Exposure disabled                                | Displayed scale one                                                                          |
| Temporary diagnostic/wireframe exposure override | Use transient unit-exposure output; preserve authored mode and persistent adaptation history |
| Stateless Auto view                              | Meter current frame without temporal adaptation; transient GPU state only                    |
| Destroyed view                                   | Retire resources after all GPU readers complete                                              |

At a frame boundary, resolve the settings revision, mode change, and transition
request together. The highest request generation wins; an explicit request
overrides an implicit cut/first-use policy. A remeter/seed request in Manual or
disabled mode is rejected with a diagnostic rather than saved for a later mode.
A sharing consumer cannot reset the source. Conflicting requests with the same
generation are invalid.

Separate request submission from GPU application. Repeated submission of the
same generation is idempotent. Failed recording/submission cannot consume it.
A metered reset remains pending through invalid inputs; an explicit seed or
locked-range fixed solve does not depend on histogram validity.

Temporary diagnostic overrides do not consume pending exposure transitions or
overwrite persistent gain. Leaving the override resumes the same history unless
a cut/reset request also occurred. This differs from authored ExposureEnabled
being false, which follows the disabled-mode state contract.

Detaching a view from a shared source remeters by default. Preserve or Seed is
an explicit transition; do not revive an old dormant independent history.

### 4.2 Shared exposure: one writer and deterministic readers

Retain Oxygen's previous-frame source-sharing contract:

- Only the source owner meters and updates the source state, at most once per
  logical frame. Consumers use its published prior generation for final exposure
  and their frame's numerical pre-exposure.
- Resolve a chain of references to one root owner; reject cycles and references
  to removed/unknown handles atomically.
- With no published source history, use its resolved initial gain: one for
  disabled exposure, manual gain, explicit auto seed, or the standard Auto
  initialization gain at EV0. Apply
  source EV bounds to the default EV0 fallback; explicit seeds retain their
  requested EV. Use the source target and bias, tag the fallback, and never
  initialize from a consumer's image.
- If a source remains registered but is not rendered, consumers retain its last
  published result. No history means the same initialization fallback.
- A source reset takes effect for consumers when that generation becomes their
  published prior result. Consumers never access an in-progress update.
- Local consumer exposure settings do not modify borrowed gain. A consumer
  reset request is rejected with a clear ownership diagnostic.

If an active source is destroyed, detach its consumers at the next frame
boundary and report the broken relationship. Copy the last borrowed displayed
gain and positive latent/history gain into each consumer's own transient/persistent
state before retiring source resources.
An Auto consumer preserves that gain for the transition frame, then meters
independently. Manual applies its authored value immediately; disabled uses one.
Zero-target rules take precedence. With no published gain, use the captured
source initialization fallback. Resolve
chains to the disappearing root so every affected consumer follows the same rule.

If the borrowed displayed gain was zero but the consumer's Auto target is
positive, retain zero only for the continuity frame. Then use valid independent
metering with the positive latent gain as its starting state. While metering is
unavailable, retain that positive latent gain and mark the measurement invalid;
never initialize log-gain adaptation from the displayed zero.

A camera cut on a borrowing view preserves borrowed exposure and resets only
that view's affected camera/color histories and numerical bootstrap. It does not
create an implicit reset request for the source owner.

This gives order-independent results with one frame of shared-result latency.
Test both source/consumer render orders with different images and source reset.
UE's borrower-bootstrap exception is deliberately not used.

### 4.3 Frame-pinned pre-exposure

Publish explicit `pre_exposure P` and `one_over_pre_exposure 1/P`, not the
overloaded `ViewColorData.exposure` scalar.

Use one bindless per-frame GPU record referenced from the existing view ABI:

```text
FrameExposureData (16 bytes, matching C++/HLSL layout)
    float pre_exposure
    float one_over_pre_exposure
    uint  global_exposure_state_slot
    uint  flags
```

Allocate the record with existing frame resources. The early resolve writes it
as a UAV; insert its UAV-to-SRV transition before the first dependent HDR pass.
Keep the record and descriptor immutable until the frame fence retires. Every
rendering family reads this record through the common view helper. The referenced
current exposure state is separately ordered between Stage 22 and tonemapping;
shared consumers reference the pinned prior source state instead.

1. Before HDR passes, a small GPU resolve selects a positive numerical P from
   the prior exposure generation. Manual modes can use the resolved manual gain;
   shared views use the pinned source generation.
2. Freeze this binding for the frame. Record P with every product/history whose
   color is stored pre-exposed.
3. HDR scene accumulation stores `C_pre = P * C_scene`.
4. Histogram samples recover scene-referred radiance using `1/P`.
5. Stage 22 writes the current global gain S for all modes.
6. Tonemapping applies `S/P` once to pre-exposed foreground and bloom.

Numerical bounds on P protect resource precision; they do not clamp displayed S.
Resolve P entirely from GPU-visible state and current settings. No synchronous
CPU readback or separate CPU authority for automatic exposure.

### 4.4 Migrate every active producer and consumer together

Create and execute a source-to-consumer domain checklist covering:

- Deferred base-pass emissive and deferred direct/indirect lighting.
- Forward opaque, masked and translucent shading; lit and unlit materials.
- Sky sphere/background radiance, atmosphere sky-view LUTs and aerial perspective.
- Height/volumetric fog and their temporal histories.
- Bloom extraction, thresholds and upsampling; thresholds remain scene-referred.
- Scene resolves, blending, auxiliary/offscreen views and capture products.
- Diagnostic wireframe/debug colors and display-space overlays.
- Existing temporal color consumers and static/dynamic sky capture products.

Apply P exactly once at the appropriate write boundary. Atmosphere LUT producers
already apply exposure and sky/AP currently undo it; replace this paired contract
coherently. Canonical reusable environment products remain scene-referred;
view-dependent pre-exposed products carry their P and generation.

Rescale a reused pre-exposed RGB history by `P_current/P_stored`; do not scale
alpha, coverage, depth or transmittance. Include P or its explicit conversion in
cache validity, and invalidate only histories whose domain cannot be converted.
UI/display backgrounds remain outside scene exposure and metering.

Remove old exposure compensation/cancellation paths after their consumers use
the new bindings. Implement shader ABI, reflection/catalog, PSO and format-key
updates with the corresponding C++ producers.

### 4.5 First-frame and discontinuity headroom

An unseeded first frame cannot choose a reliable FP16 scale from exposure
history. A tiny fixed P can erase dark half-float values; P=1 can overflow
bright half-float values.

Keep SceneColor accumulation FP32 in both modes. Convert into the existing
resolved-color texture with suitability checks: FP16 when qualified and selected
by the precision policy, FP32 for bootstrap, recovery or FP32-only operation.
Required high-range view-dependent intermediates also use
FP32 for first unseeded metering, remeter cuts and device recovery.
Use P=1, meter and tonemap that frame. Exposure validity and FP16 suitability
are separate conditions. The CPU starts this format on the known event and
permits FP16 only after nonblocking completed status for the matching view, settings
revision and requested/applied transition generation confirms both valid GPU
history and FP16 eligibility for that view's required HDR products. Older valid
history with a pending reset cannot authorize the switch. Delayed acknowledgment
means additional FP32 frames, not a wait. The acknowledgment selects resource
format only; numerical P and S remain GPU-owned. Keep exposure history through
format changes and add both formats to affected PSOs/resolves.

Define required signals by the metering and image-error budgets in the
[PBR specification](../../renderer-core/physically-based-rendering.md) and the
per-product narrowing checks in [SceneTextures](../lld/scene-textures.md).
Arbitrarily tiny RGB components do not force FP32 when their loss fits those
budgets and does not change required metering classification. A uniform P cannot
reduce the bright/dark ratio; valid metering does not prove FP16 representability.

If required products cannot meet the FP16 budget, retain the same FP32 recovery
mode and continue ordinary metering/adaptation. Retention itself is not a cut,
remeter event or generation increment. Return requires two consecutive eligible
completed frames with half the allowed error and two stops of overflow headroom.
Pin the GPU-owned candidate-P record qualified by that status for the first
FP16 frame; an acknowledgment for another candidate, superseded settings/event,
destroyed/recreated view or incompatible product layout cannot authorize return.
Subsequent unexpected range failures follow the recovery rule below. The
status path remains bounded; there is no third format or precision service.

EX051-09 specifies when a persistent FP32 view attempts admission again. It must
separate mandatory current-frame protection from prospective FP16 work and name
the retry triggers and certificate-validity keys. The EX051-04 performance
baseline stays FP32 with P=1 and omits FP16-admission work while preserving
metering, adaptation, transitions, sharing and temporal rendering.

Suitability belongs to each view, including borrowers: shared gain validity does
not certify the consumer's image. Qualify stable excessive-range retention,
reduced-range return, insignificant below-budget values, delayed/stale status
and contrasting shared consumers. Account for actual products, concurrent views,
leases and histories: RGBA32F costs eight additional bytes per texel over
RGBA16F (15.82 MiB at 1920x1080; 63.28 MiB at 3840x2160 per allocation).

Stateless Auto uses this FP32 metering route on every invocation. A shared view
using a source's initial fallback also uses FP32 until that source has published
valid state. These paths allocate transient products without inventing history.

Freeze a supported scene-radiance/dynamic-range envelope during slice 1 and
verify its bright and dark endpoints through all upstream producers. A format
upgrade after an upstream overflow does not recover the signal.

Normal frames detect nonfinite/saturated HDR signals separately from genuine
black samples. On range failure, retain valid exposure history, flag the meter
invalid, and request FP32 remetering at the next schedulable frame after its
nonblocking completed status is available. Ignore acknowledgments for superseded
generations. Do not repeatedly adapt from corrupted pixels. If FP32 recovery
also exceeds the supported input domain, report a content/range error and
preserve valid state.

Keep range detection small: one per-view status record with flags, first-failure
producer/product ID, frame and settings generation. At the audited high-range
write boundaries, check values before narrowing and atomically record the first
failure. Reuse the completed-status readback ring from initialization. Test the
checks with injected overflow/underflow; do not infer a pre-store failure from
an already saturated texture. Depth, normals and coverage formats remain unchanged. The shared canonical
transmittance and unit-illuminance multiple-scattering tables use RGBA32F in both
view modes (approved 2026-09-18); see the owning scene-texture format inventory.
Per-view radiance and its transmittance alpha retain their conditional FP16/FP32
contract.

## 5. Robust metering and authoring

### 5.1 Bounded histogram with conserved weights

Keep 256 logarithmic bins. Use a deterministic normalized-view stratified grid
with at most 512 by 512 samples, limited by the actual content dimensions.
Sample positions are stable across frames; the grid scales with the content
rectangle, not the window or output bars.

Each sample uses the chosen analytic metering weight multiplied by the optional
scalar mask sampled in normalized content UV. Quantize the combined weight to
Q in [0,4095] using half-up rounding (`floor(weight*4095+0.5)`). Split
between neighboring bins with the same tie rule:

```text
q_upper = floor(Q * fractional_bin_position + 0.5)
q_lower = Q - q_upper
```

Total mass is conserved. Maximum total integer weight is
`512 * 512 * 4095 = 1,073,479,680`, below the uint32 limit. Use the same bound
for bins, total and percentile accumulation. Support 8K output without allocating
an 8K histogram workload. Test small bright features and moving edges to establish
sampling error; do not area-average HDR before binning and silently change the
percentile distribution.

Use the renderer's working RGB luminance coefficients. Apply the tonemapper's
coverage convention: background-enabled composition uses saturated scene alpha;
otherwise coverage is one. Recover unpremultiplied scene-referred RGB where
coverage is positive and include coverage in sample weight. Exclude UI, display
background and letterbox bars. Meter scene sky when it is part of the scene
signal. Reject nonfinite
samples and count them separately.

Percentile trimming requires positive retained weight. Validate
`0 <= low < high <= 1`, finite ordered EV bounds, positive histogram span,
nonnegative speeds/target, and finite positive D. Equal EV min/max uses the
locked-range rule in section 3.1, including fixed curve input.

### 5.2 Black input, invalid input and masks

Default black-bucket influence is zero. Classify finite luminance at or below
the histogram's lower luminance bound as a dark-bin sample. Exclude it from
the ordinary weighted solve at zero influence; keep separate exact-black,
positive-below-window, finite, weighted and rejected counts. Allow a [0,1]
black influence setting.

- All finite, positively weighted samples in the dark bin: valid dark solve;
  use min_ev for target and curve evaluation. Tag the synthetic EV as a dark
  solve and report measured luminance as below the histogram bound. Do not take
  log2(0) or describe that synthetic EV as an exact luminance measurement.
- Zero mask/coverage weight, unavailable source, or no finite accepted samples:
  invalid measurement; retain history and pending metered remeter request.
  Locked-range solves and explicit seeds remain independent of that validity.
- Mixed dark-bin and brighter samples: apply configured black influence, then
  percentile trimming. Empty retained weight is invalid.
- Missing initial history plus invalid measurement: use Auto EV0 initial gain
  independent of the inactive manual EV, flag fallback, and keep initialization
  pending.

Average, CenterWeighted and Spot remain available. An optional texture mask
uses linear R-channel values, clamped to [0,1], with bilinear clamp-to-edge
sampling in normalized content UV. It multiplies these profiles; absent mask
means unit weight. A pending mask load retains the prior valid settings/mask
revision. An explicitly authored mask that
fails to load reports an error; it does not silently turn into average metering.

### 5.3 Compensation curve and authored settings

Add an optional sorted piecewise-linear curve mapping metered EV100 to compensation
EV. Empty means zero; clamp to endpoint values outside the authored interval.
Support at most 64 keys; require finite values and strictly increasing EV
coordinates. Upload compact key/value data and evaluate it once per view in the
exposure solve, using the existing resource/upload infrastructure. A small key
buffer avoids a texture-LUT
approximation and unnecessary sampling machinery.

Add mask reference, curve data, black influence and hybrid transition distance
to the native authored/configuration model. D defaults to 1.5 stops; keep it
advanced in the UI. Zero target remains legal. Keep scene-system and DemoShell
defaults explicitly named while using one normalization/validation function.

Round-trip through source schemas, cooker, versioned packed environment records,
loader hydration, scripting, relevant managed/editor adapters, and DemoShell.
The user requires a strict scene-v6 cutover: all producers and consumers migrate
together. Reject older sources/assets with an actionable migration/re-cook error;
retain no compatibility decoder, fallback hydration, legacy layout or migration
shim. Preserve enum ordinals and authored values; reject malformed coupled
settings atomically. Validate the one current packed layout and its version. Runtime transition generations and GPU history are
not serialized.

Masks use an authored texture path, cooked source-local texture indices, PAK
remapping and runtime ResourceKey. The 144-byte exposure prefix stores the uint32
texture index at offset 116; bytes 120..131 remain zero-reserved. No UUID lookup
layer or new texture asset type is introduced.

## 6. Calibrated light and material reference

### Directional-light authority

The established Oxygen contract is scene-owned directional lights with explicit
assignment to up to two atmospheric slots. Primary/Secondary identify slots,
not mandatory Sun/Moon types: the pair may represent two suns or a sun and moon.
An unassigned directional light still provides direct surface illumination.
Atmospheric assignment does not replace the light or create a second source.
Each assigned source retains its identity; a lone Secondary is not promoted to
Primary. The [editor rendering contract](editor-v01-rendering-contract.md#3-independent-directional-array-and-atmosphere-assignments)
owns the canonical scene/rendering representation and assignment validation.

Demo convenience is explicit and separate: when requested, a demo may infer a
sun from an existing scene directional light, or inject a preview directional
light into the scene. These actions must respect explicit scene assignments.
The production renderer neither silently elects a sun nor creates a fallback
light. A preview source follows ordinary scene-light lifetime and contribution
rules; it is not a hidden rendering path.

EX07 qualification must cover an unassigned directional light, each atmospheric
slot independently, and both assigned sources together, with predictable direct
contributions in both rendering families. Isolate atmospheric transport for the
photometric reference; this does not remove or reinterpret authored assignments.
Current primary-only publication is an implementation gap against this agreed
contract that EX07 must repair and validate. Follow the existing directional-array
design and update its owners together; a primary-sun-only fixture cannot qualify
the complete directional case.
Full scattering and shadow-quality acceptance remain with their owning work.

### Physical reference

Finish the physical unit chain needed by the shipped neutral, point and spot
experiments in this package:

- World distance is metres; record working RGB primaries/white point and the
  luminance coefficients used by both the renderer and independent oracle.
- Directional authored lux denotes illumination on a perpendicular receiver.
  Apply the cosine and full production BRDF once.
- Isotropic point intensity is `I_cd = flux_lm/(4*pi)`.
- For the existing spot profile, let `ci = cos(inner_angle)` and
  `co = cos(outer_angle)`. With
  `w = saturate((cos(theta)-co)/(ci-co))^2`, use
  `omega = 2*pi*((1-ci) + (ci-co)/3)` and `I_peak = flux_lm/omega`.
  Equal inner/outer angles use the hard-cone limit `omega = 2*pi*(1-co)`.
  Reject a zero-solid-angle cone through authored validation.
- Use `distance_factor = range_fade / max(d*d, epsilon_m*epsilon_m)` with
  `epsilon_m = 0.001` metres. Retain the existing smooth range fade:
  `range_fade = saturate(1-(d/range)^4)^2`, zero at or beyond range.
  At exactly zero separation return zero direct contribution before normalizing
  the light vector. The floor is a numerical point-source guard, not an area-light
  approximation or a reinterpretation of authored source radius.
- The BRDF evaluation supplies the receiver cosine once. Place calibration
  samples far beyond the numerical guard and well inside the range-fade boundary;
  include the known fade in expected values rather than assuming it is exactly one.
- Apply the same conversion helpers/contracts to active deferred and forward
  paths. Keep neutral white calibration sources; document color tint's energy
  effect separately.
- Use the existing production BRDF, including dielectric specular and material
  packing. The current G-buffer fixed specular term is part of that reference;
  high roughness alone is not a Lambertian material.

Independent integration verifies spot flux normalization; distance and cone-edge
tests verify the shared attenuation helper. Keep source-radius BRDF behavior
separate from the numerical point-source guard.

The bench oracle is a separate CPU/reference calculation with frozen tolerances
for BRDF evaluation, quantization, reconstruction, sample integration and output
precision. Derive exposure from expected luminance, not measured GPU brightness.

## 7. LightBench benchmark and MultiView visual qualification

LightBench is Oxygen's maintained exposure benchmark for visual validation,
not only a scene generator for automated tests. Its scene design, controls,
initial image and behavior under interaction must be repaired as part of delivery.
Numerical results and visible behavior are both acceptance requirements.

### 7.1 Complete experiments

Ship these experiments through one versioned schema and one execution path:

| Experiment        | Controlled variables and expected result                                                                      |
| ----------------- | ------------------------------------------------------------------------------------------------------------- |
| Neutral Reference | Three framed cards, actual white directional light, fixed camera/exposure, known production material response |
| Fixed Exposure    | Known HDR input; EV/key/compensation sweeps through uploaded and consumed gain                                |
| Adaptation        | Scripted luminance steps, both directions, controlled dt, mask and curve variants                             |
| Lifecycle         | Startup, seeds, cuts, mode changes, pause, shared views, stateless views and recovery                         |
| Point Falloff     | White point on receiver normal, prescribed distances, expected inverse-square response                        |
| Spot Distribution | Aimed receiver, cone overlay, angular falloff and integrated flux normalization                               |
| HDR Domain        | Bright/dark endpoints and mixed opaque/forward/translucent/sky/fog content under varying numerical P          |

Neutral Reference is the default. Put camera and key on the visible side of the
cards; keep presentation labels/background separate from physical illumination.
Select exposure from the reference calculation and an explicitly authored output
transform. Provide clear light position, receiver normal, distance and cone
overlays only where useful.

### 7.2 Settings and reset ownership

A staged experiment application owns geometry, materials, lights, environment,
camera, rendering features, exposure/output settings and measurement regions.
Publish them coherently at a frame boundary and issue the relevant transition.

EX06 supplies `SceneActivationPolicy::kExperimentOwned` across the relevant
DemoShell services. Reuse that policy and its tests. Whole-recipe application,
revision changes and reset are the remaining controller work. Other demos retain
their existing persistence behavior.

Persist window/panel preferences separately. Saved modified experiments load
explicitly. Reset restores all reference inputs. Migrate the shipped indoor
settings file into a versioned experiment. Preserve user-owned settings files.
Automatic batch runs use isolated experiment state.

The UI shows experiment, relevant controls, expected/measured values, effective
exposure, and Pass/Fail/Modified/Invalid states. Advanced controls are collapsed.
Debug overrides are visible. Every required acceptance experiment must run and
pass; Unsupported is a diagnostic for optional platform capability, not a
completion state for this package.

### 7.3 Reusable GPU measurements

Use renderer diagnostics/extraction infrastructure for region statistics and
exposure inspection. Results carry frame sequence, view-state identity, source
product/domain, region, validity and sample counts. LightBench associates the
experiment revision and calculates its verdict.

Measure scene-referred luminance before display transforms and inspect the gain
actually consumed by the same frame. Recover scene-referred values with the
product's P. The tonemapper has no separate post-exposure texture: use an opt-in
GPU probe at the production operation or a genuine extracted product; label
CPU-derived quantities as derived. No always-on extra full-resolution target.

Use bounded asynchronous readback and fence-safe lifetime. Disabled measurement
adds no dispatches or readback allocations. Reject late, occluded, insufficient
or edge-contaminated regions instead of reporting zero. Independently qualify
the instrument against known GPU inputs before using it to judge lighting.

### 7.4 LightBench visual acceptance

- A clean launch frames the entire Neutral Reference, with readable cards and
  useful material response, sensible exposure, and labels/measurement overlays
  that do not obscure the subject. Reset restores this exact view and experiment.
- Each of the seven experiments presents the phenomenon it tests clearly.
  Point/spot receivers show the illuminated region and distance/cone relationship;
  adaptation scenes visibly exercise bright-to-dark and dark-to-bright transitions.
- EV, compensation, mask, curve and mode changes produce the documented visual
  response and matching measurements. No unintended black frames, white flashes,
  stale overlays, abrupt history resets, or settings reapplication occur.
- Verify interaction and layout at 1920x1080 and 2560x1440, plus a resized window.
  Measurement regions remain aligned with their intended surfaces.
- Capture the startup reference, each experiment, and representative transition
  sequences. Inspect native presented output as well as intermediate textures.
  A nonzero SceneColor test alone does not satisfy visual acceptance.
- Fix demo lighting, camera placement, framing and control issues found by these
  checks in this package. Keep the benchmark scenes and captures reproducible.

### 7.5 MultiView visual acceptance

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
forward/deferred products. Reuse producer-owned view-state handles and the
renderer measurement facility.

Compare the same view's pre-composition output rendered alone and in a family
with identical input, settings, history and timestep. Its measured gain and
image must agree within
the established numerical/output tolerances. Explicit sharing is the exception
and is tested against its source policy. Capture presented windows and inspect
every lit pane, not only the main view or a final surface-average statistic.
Validate the final composite separately: overlap and z-order legitimately change
visible pixels. Resize can change the camera's framing and therefore its own
metering; it must not contaminate another view's meter or reset unrelated history.

Use a native scripted interaction sequence covering reordering, resizing,
mode changes, camera movement and lifecycle events while all panes are visible.
GPU diagnostics and per-view measurements accompany the visual captures.
Physical sky and fog remain exposed scene radiance; only display-space backgrounds,
bars, labels and UI are exposure-independent.

## 8. Ordered implementation slices

### File entry points

The shader root below is
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Keep unit/oracle tests next to
the owning module; use the existing native fixture and capture tools for GPU tests.

| Slice    | Start in these files                                                                                                                                                                                 | First observable check                                                                       |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| 2        | `Core/Types/PostProcess.h`, `Vortex/PostProcess/Passes/ExposurePass.cpp`, `Vortex/Test/PostProcessService_test.cpp` under `src/Oxygen/`                                                              | EV14 reaches the tonemap constant unchanged                                                  |
| 3        | `Vortex/PostProcess/Types/PostProcessConfig.h`, `PostProcess/Passes/ExposurePass.*`; shader `Services/PostProcess/Exposure.hlsl`                                                                     | Known two-bin distribution and hybrid trajectory                                             |
| 4        | `Vortex/CompositionView.h`, `Internal/ViewLifecycleService.*`, `PostProcess/PostProcessService.*`, `ExposurePass.*`                                                                                  | One source update and idempotent event generation                                            |
| 5        | `Vortex/Types/ExposureStateData.h`, `Types/ViewFrameBindings.h`, `SceneRenderer/SceneTextures.*`, `Stages/InitViews/InitViewsModule.cpp`; shader families in section 4.4                             | Opaque/emissive output invariant under a change of P                                         |
| 6        | `Scene/Environment/PostProcessVolume.h`, `Data/PakFormat_world.h`, `Data/PakFormatSerioLoaders.h`, cooker schemas, `Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.cpp`, DemoShell services | Current records round-trip; obsolete records are rejected                                    |
| 7        | `Vortex/Lighting/Internal/DeferredLightPacketBuilder.cpp`; shader `Services/Lighting/DeferredLightingCommon.hlsli` and `ForwardDirectLighting.hlsli`                                                 | Point flux and spot angular integral match independent values                                |
| 8        | `Vortex/Diagnostics/DiagnosticsService.*`, existing extraction/readback owners; `Examples/LightBench/` and `tools/vortex/`                                                                           | Qualified known-float probe, then a measured Neutral Reference that resets and runs in batch |
| 9A-E, 10 | `Examples/LightBench/LightScene.*`, `MainModule.*`, `LightBenchPanel.*`; `Examples/MultiView/MainModule.*`, `SceneBootstrapper.*`, `main_impl.cpp`; `tools/vortex/`                                  | One additional experiment family per step; reuse MultiView proofs and aggregate reports      |

Unqualified paths in the table's engine rows are under `src/Oxygen/`; shortened
paths after a `Vortex/` entry stay within that module. Avoid creating duplicate
helpers under LightBench for renderer-owned behavior.

### Slice 1 - Freeze contracts and update owning designs

- Update [PBR specification](../../renderer-core/physically-based-rendering.md)
  with equations, units, examples, numeric domain and the complete target behavior.
- Reconcile [physical-lighting roadmap](../../renderer-core/physical-lighting-roadmap.md),
  [panel design](../../renderer-core/post-process-panel-design.md), and repository-root
  [environment authoring](../../../../../design/editor/lld/environment-authoring.md).
- Expand [PostProcessService LLD](../lld/post-process-service.md) with the state
  layout, hybrid solve, lifecycle policies, masks/curve, numerical bootstrap and
  failure behavior in this plan. Include state and frame-sequence diagrams.
- Update [multiview](../lld/multi-view-composition.md),
  [InitViews](../lld/init-views.md), [shader contracts](../lld/shader-contracts.md),
  [scene textures](../lld/scene-textures.md), and environment/lighting LLDs for
  frame-pinned P, sharing and bootstrap format support.
- Fix the supported radiance envelope, operational FP32 bounds and validation
  error budgets against the compiler/format audit. Approve the GPU/asset layouts
  and exact list of dual-format HDR products. Document section 6's regularization
  and unit equations in the owning lighting LLD.
- Register this delivery order in PLAN/status. Create `design/renderer-core/lightbench.md`
  for experiments and instrument requirements.

**Gate:** public behavior is specified by sections 3-7, GPU/asset layouts have
owners, and every active HDR path appears in the domain migration checklist.

### Slice 2 - Implement normalized settings and fixed exposure

- Implement shared validation/resolution for native settings and view overrides,
  including zero target, coupled ranges and representable gain.
- Remove the fixed-exposure floor and propagate the exact resolved multiplier
  through constants. Add scalar, upload and native GPU regressions.
- Introduce explicit frame exposure bindings and common state definitions,
  with compile-time CPU/HLSL size/layout checks and ShaderBake validation.
- Define native mask/curve/new-setting types for the following runtime slices.

**Gate:** EV14/15/16, boundaries, compensation, keys and disabled exposure reach
the GPU correctly. Invalid settings retain the previous valid revision.

### Slice 3 - Implement robust metering and hybrid adaptation

- Implement bounded stratified sampling, conserved two-bin weights, percentiles,
  finite/black/coverage rules and overflow-safe counts.
- Implement mask sampling and exact piecewise-linear compensation curves.
- Implement the analytic hybrid log-gain update, clamped targets, zero speeds,
  equal EV bounds, zero target and positive-target restoration.
- Add independent histogram and adaptation reference tests plus native GPU
  captures using existing fixtures; vary frame schedules at equal elapsed time.

**Gate:** weighted distributions, targets and adaptation trajectories match
independent expectations; 8K output cannot overflow histogram accumulation.

### Slice 4 - Implement one exposure history and complete lifecycle

- Update the same GPU state in Manual, ManualCamera, Auto and disabled modes.
- Add public per-view transitions and request generation handling, including
  recording/submission failure, invalid metering and idempotent retries.
- Implement the policies in section 4.1, including exact seed event-frame
  behavior and manual-to-auto continuity by retaining gain.
  Exercise controlled-input discontinuity notification, source destruction,
  default detach, lifetime-safe selection, inactive-owner validation and
  acknowledgement backpressure. Include captured controls, DemoShell reset,
  and both offscreen execution paths.
- Implement source-owned updates, pinned prior generations, root-source
  resolution, cycle rejection, inactive-source retention and bootstrap fallback.
  Cover registered composition views and offscreen facade routing.
- Implement stateless transient state, recovery events, frame-safe uploads,
  synchronization and fence retirement using controlled float-input fixtures.
- Exercise native game-facing producers without DemoShell. Wire DemoShell's
  reset to the same public API.

**Gate:** state-machine, request, ownership and publication tests pass against
controlled float inputs, including both shared-view execution orders. GPU state
identifies the applied generation and settings revision. Slice 5 supplies and
validates the scene-integrated high-range route for startup, cuts, stateless
views and recovery.

### Slice 5 - Complete pre-exposure migration and numerical recovery

- Add the early GPU P resolve and bind a frame-invariant P/1P to every HDR pass.
- Migrate the entire section 4.4 checklist, including atmosphere producer/
  consumer pairs, fog/color histories, bloom and offscreen/capture domains.
- Meter with 1/P and tonemap with S/P in every mode, including disabled and
  zero-target cases. Remove obsolete manual-exposure cancellation paths.
- Add transient FP32 bootstrap/remeter products and matching PSO/resolve
  format keys. Keep exposure history through FP32-to-FP16 transition.
- Add saturation detection and the FP32 recovery event. Verify the supported
  scene-radiance envelope at upstream intermediates as well as SceneColor.
- Run the complete lifecycle matrix through real scene rendering, including
  startup, cuts, stateless views, source loss, delayed acknowledgment and recovery.
- Run the MultiView main/lit-PiP, independent and shared-exposure cases from
  section 7.5 before integrating further bench work.

**Gate:** varying numerical P leaves scene-referred measurements and final
output invariant within precision budgets. Startup/cuts preserve bright and
dark meter signals; all scene-integrated lifecycle cases pass. No active HDR
path consumes the old overloaded scalar.

### Slice 5.1 - Qualify and correct exposure performance

The authoritative task breakdown, measurement protocol, budgets, dependencies
and commit boundaries are in
[IMPLEMENTATION_STATUS.md, Slice 5.1](../IMPLEMENTATION_STATUS.md#321-slice-51-performance-qualification-and-correction).

- Close the delivered profiling, H1/H2 scan, rejected H3, H4/H5 reuse and R091
  lifecycle-accounting checkpoints. Their final matrix coverage belongs to 13.
- Implement EX051-10A: lease FP32 SceneColor independently so a checked-half
  fallback does not retain depth, GBuffer, velocity or custom-depth attachments.
  Both color reuse and attachment-family reuse must honor readers and GPU fences.
- Replace the performance reference in EX051-04 with `fp32-only`: FP32 storage,
  P=1, normal exposure/events/history and current-frame protection, with no FP16
  candidate scans, FP16-only certificates or eligibility-only status jobs.
  Preserve the existing `fp32` format-only control for numerical diagnostics.
- EX051-05 runs four production/FP32-only pairs in one Release binary: C01 at
  1080p and 4K, C02 at 1080p, and I02 at 1080p. Eight timed runs decide whether
  FP16 helps accepted-half, temporal and retained-FP32 operation. Repeat a pair
  once only if timing variation prevents the decision.
- EX051-09 owns the explicit FP32/admission/FP16/recovery state machine, retry
  triggers and certificate validity. Preserve two actual consecutive eligible
  frames, GPU-owned P/S, current-frame protection and immutable histories.
  Fold exposure-related temporal precision work from 08 into this item.
- EX051-13A/13B are a user-approved joint follow-up to the measured CPU budget
  failure: shared recording ownership at the participating Vortex stages, plus
  D3D12 root-signature/binding reuse. The tracker and PostProcess owner carry the
  concrete scope and gates. Migrate affected APIs cleanly, remove superseded
  entry points, and preserve actual-submission publication and GPU-fence safety.
  Do not migrate unrelated Oxygen callers merely to make everything batch.
  Keep logging OFF, GPU timing unchanged and use Tracy; no further custom timing
  mechanism, map rewrite or shader/root-constant ABI change is authorized.
- EX051-11 uses existing profiling to isolate active exposure CPU work from
  queue waits. Correct only the identified preparation/submission bottleneck.
  The user-approved bounded candidates are EX051-11A (one recorder/submission
  for adjacent final range plus histogram/solve, preserving early boundaries)
  and EX051-11B (attribute publication and evaluate one immutable histogram
  constant record for clear and accumulation). Their concrete contracts,
  checks and accept/reject conditions are tracked in
  [PostProcessService](../lld/post-process-service.md#ex051-11-cpu-attribution-checkpoint)
  and the existing item table. They authorize no general submission framework,
  unsafe descriptor reuse or budget relaxation.
  For 11B specifically, the user directed that clarity, maintainability and
  resource-operation reduction also inform the decision when measured timing
  benefit is small or absent; keep those claims separate from frame-time gains.
- EX051-12 integrates correctness. EX051-13 alone runs final production
  acceptance: eight recipes at two resolutions, three runs per cell, with the
  fixed transition schedule attached to the 1080p I02 runs and one presentation
  check. EX051-14 reconciles the results and owner documents.

An item closes with its named delivery and accept/reject evidence. Rejected
experiments close without a production change. Final performance acceptance
does not keep completed implementation items open. Store raw results once;
analyses and the checkpoint manifest link them by path/hash.

**Gate:** EX051-GATE passes with native timing distributions, controlled cost
attribution, bounded resources, independently verified correctness and current
owner evidence. Existing replay cost measurements alone do not close it.

**Closed 2026-09-21:** the full native GPU matrix, correctness, resource and
presentation gates pass. Joint 13A/B reduces measured active I02 1080p CPU
p95/p99 by 27.0%/29.6%, to 0.514935/0.625473 ms. The user explicitly accepts
that CPU cost for current delivery and defers further optimization to a later
milestone. This accepted disposition closes 13/14/GATE; the original CPU limits
remain future goals, not passed results. Broader CPU/scaling qualification
belongs to that deferred work. No further benchmark or production correction
is required in Slice 5.1.

### Slice 5.2 - Close remaining exposure quality issues

**Validated 2026-09-21.** The approved residual batch and Release include repair
are committed as `9ff39edcc` and `dc9ef824e`. Scoped changed code is tidy-clean;
65 selected Debug and 65 Release cases pass. A single matched I02 run confirms
performance/resource preservation and four byte-identical endpoint images.
The [checkpoint](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice52/checkpoint-manifest.json)
records raw references, retained diagnostics and reused evidence. No new warning
suppression was added. The following requirements governed the completed pass.

**Scope revised 2026-09-21 at the user's request.** The authoritative residual
scope, reused evidence and change-specific checks are in
[IMPLEMENTATION_STATUS.md, Slice 5.2](../IMPLEMENTATION_STATUS.md#322-slice-52-code-quality-and-test-structure).

1. **EX052-02 — Agree residual fixes.** Reuse the existing diagnostics and
   accepted source/test/performance checkpoints. Start with ExposurePass and
   PostProcessService; add another file only for a named exposure finding.
   Agree a finite list with concrete benefit, proposed change and affected checks.
   Refresh only stale/missing analysis; no new general inventory or baseline run.
2. **EX052-04 — Fix that list.** Keep changes coherent, warning-free and within
   existing contracts. A necessary API/ownership change requires interface and
   migration review before coding. Add tests only for real uncovered defects.
3. **EX052-10 — Validate affected code.** Use focused Debug checks while editing.
   At closure, compile the affected Release paths and run the union of required
   affected cases, reusing results still valid for final inputs. A full owning
   executable runs only when the shared change affects all its cases, once per
   required configuration. No automatic engine-wide or repeated per-item gates.
4. **EX052-12/GATE — Close.** Record the result and reused evidence, update owners
   and commit. An empty justified fix list closes through applicable existing
   proof; do not invent work to fill the slice.

Native fixture extraction/scenario splitting (`2c696c522`), the complete Vortex
test-quality review (`4f359ce2d`), and recording/binding migration with 589 Debug
and 403 Release checks (`22cea346b`) are completed inputs. EX052-05/06 are already
validated; the other redundant setup/restructuring/check tasks are merged into
the four steps above. Do not repeat those deliveries, sweep every test again,
change targets for cosmetic reasons, or experiment with `/bigobj` without a
real build problem. Preserve independent numerical oracles and existing filters.

No benchmark or visual campaign is mandatory for 5.2. Test-only and non-semantic
quality edits reuse accepted performance/output evidence. Changes that can alter
hot-path work, lifetime, synchronization, shader data or output need an explicit
impact decision and only the relevant matched check. The accepted CPU cost is
preserved; its original tighter targets and further optimization stay in the
later milestone. The 48-run GPU matrix and previous overhead/lifecycle campaigns
remain closed.

**Gate:** the agreed residual fixes are resolved, changed code is tidy-clean,
necessary affected checks pass, and existing contracts and accepted performance
are preserved.

### Slice 6 - Finish authoring, serialization and configuration isolation

**Closed 2026-09-21.** Source/cook/package/load/script/editor integration, strict
current-format cutover, C++20 editor boundary and rendered DemoShell acceptance
are qualified. The [item tracker](../IMPLEMENTATION_STATUS.md#33-slice-6-work-items)
and [local evidence](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice6/slice6-progress.json)
record validation. The TexturedCube assignment/panel-refresh regression is
unit-tested and confirmed fixed by the user's rebuilt-app test. Its deferred console
and ImGui Test Engine work is now scheduled separately in EX08.1 and EX08.2.

- Include native aperture/shutter/ISO source/cook/load persistence, as
  approved on 2026-09-16. Scene-v6 perspective/orthographic records are 32/40
  bytes. Older source/asset versions are rejected; new cameras default to
  11/125/100. Existing editor
  adapters preserve the fields; physical-camera editor controls stay deferred.
- Update source JSON schemas, scene component/config types, versioned packed
  records, cooker, loader, scripting and existing editor/native adapters.
- Round-trip mask resource references, curve keys, black influence, D and
  every existing exposure field; preserve enum ordinals and current authoring defaults.
- Add async mask residency/error behavior and atomic settings revision changes.
- Update DemoShell controls and labels; expose requested/effective exposure
  separately and show resource/metering failures.
- Add the LightBench experiment-owned activation policy so saved settings
  cannot override its camera, scene or post-process recipe.

#### Repository-wide strict format cutover

**User direction, 2026-09-21: no backward compatibility or retained legacy code.**
This supersedes the original v5-hydration contract. Scene descriptor sources and
cooked scene assets use v6 only. Do not retain v5 readers, default-hydration
branches, alternate record types or compatibility shims. Obsolete inputs fail
with concise, actionable migration/re-cook diagnostics.

Synchronize all affected producers and consumers before closure: native packed
records and Serio code, cooker/PAK dependency remapping, Content and SceneAsset
loaders, PakGen/PakDump/Inspector and their schemas, scripts/fixture generators,
managed/editor/scripting adapters, every example/demo and shipped scene source.
Regenerate affected current-format generated/golden assets through their owning
tools. Retain old-version byte samples only as rejection tests, never acceptance
fixtures or a supported read path. Verify each affected surface against the new
layout and record the checks; a successful engine build alone cannot close this
repository-wide requirement.

PakGen source specifications use v7 only; obsolete v6 acceptance and older
version aliases are removed. Scene payloads use v6 independently of the PAK
container/specification version. The editor dependency snapshot and manifest
flow must include authored metering-mask texture descriptors and their image
sources, so masks cannot disappear between scene save and native cooking.

PAK inputs use the same resource/dependency planning path as loose sources.
Directory-only projection is not a supported repacking path. Use bounded source-file slices for current resource tables and
payloads, retain script parameter ownership, and remap source-local references
after final placement. Reserve index zero when patch filtering removes an
input's null record. Qualify loose-to-PAK-to-PAK mask preservation and multiple
input sources; do not emit a successful package with missing resource regions.
Script parameter payloads must follow the same owner/slot order as patch slot
records, even when source offsets use a different physical order. The public
PakGen file APIs must validate the supplied specification version without
replacing it before validation. Current descriptor versions are material 2,
geometry 1 and scene 6, matching their native definitions.

#### Approved editor SDK boundary repair

The user approved repairing engine public headers on 2026-09-21 after the
installed compiler confirmed that C++/CLI cannot consume C++23 declarations.
Use the existing typed `oxygen::Result` for public exposure validation results.
Expose submission callbacks through a C++20-compatible, move-only owning callback
with inline storage; preserve exactly-once submission/discard behavior and
exception isolation. Lift published environment diagnostics out of the private
SceneRenderer definition so Renderer clients do not include retained-pool internals.
No C++/CLI language-mode workaround, shared-ownership conversion, legacy overload
or warning suppression is permitted. Qualify lifetime behavior with the existing
submission tests and the installed editor build.

#### Scripting exposure transport

Post-process `get`/`set` round-trip every authored exposure scalar and ordered
curve key. `set` validates one complete candidate and rejects it without partial
mutation. It returns `false, reason` and logs one warning for an invalid edit.
ManualCamera defers camera-dependent gain validation to the active view.
The runtime `auto_exposure_metering_mask` field carries the existing uint64
ResourceKey as a decimal string (`"0"` clears it), preserving all identity bits
across Luau's numeric boundary. This is a runtime handle representation, not a
persisted GPU descriptor or asset UUID. Source descriptors continue to author a
texture virtual path, and cooked scenes continue to store a source-local index.

LightBench startup must author its camera pose, post-process state and a usable
reference light explicitly. Experiment-owned activation cannot depend on saved
camera/environment state to make the reference cards visible. Its scene-control
panel follows the same transient ownership policy. This establishes a stable
initial scene; it does not implement or qualify the later experiment registry.
LightBench must publish a persistent main-view state, using the existing
RenderScene lifetime pattern: retire the old publication when the scene/camera
owner changes and at shutdown. A stateless view cannot support the authoring
panel's accepted-state reporting or temporal automatic adaptation.

#### DemoShell user scenarios and implementation quality

The acceptance run also qualifies ordinary demo startup at warning verbosity.
Demo applications resolve hot-reload scripts from their existing example content
root. Missing optional profiling CVars and valid scenes without a directional
sun are verbose diagnostics, not warnings. Explicit source failures remain
warnings/errors; no console feature or Test Engine integration is added here.

The user requires a scenario-driven logic and UI review for EX06-08/09, not
mechanical exposure of persistence fields. Review the existing native panel and
activation/settings owners together, using fresh rendered evidence for visual
claims. Keep the existing DemoShell design language and reduce cognitive load.

- Open an authored scene: make the current mode, effective behavior and ownership
  understandable without repeating the same values in several places.
- Change mode or exposure: show relevant controls and units, give predictable
  feedback, and preserve valid settings while a coupled edit is invalid.
- Request a mask: distinguish pending, accepted and failed resources; explain
  which settings remain active and provide a clear recovery action.
- Switch scenes or return to an ordinary demo: apply the correct scene/user
  ownership policy without stale overrides or unexpected resets.
- Activate or reset an experiment: apply the complete recipe coherently, keep
  unrelated personal preferences, and make reset scope clear.
- Assign textures, import another texture and switch panels: keep source-qualified
  assignments reloadable, and avoid cache invalidation for an unchanged browser
  refresh. Same-path external-file extent updates refresh metadata; conflicting
  file mappings retain their collision policy.
- Run a batch: preserve personal settings and avoid requiring UI intervention.

Use progressive disclosure for advanced controls such as transition distance,
consistent alignment/spacing, and precise labels. Verify keyboard interaction,
focus, narrow panel layouts, disabled controls and error/status presentation in
the actual UI; do not infer visual usability solely from source code.

Use named constants for meaningful limits, shared defaults and wire contracts,
not incidental literals. Apply schema validation where expressible and canonical
validation for coupled/runtime constraints. Reject nonfinite/out-of-range values,
invalid enum values and malformed packed boundaries before partial application.
Any new enum follows repository naming, underlying-type and ADL `to_string`
pretty-printing conventions, including unknown-value behavior.

Log meaningful actions/state changes at INFO, recoverable rejections at WARNING,
and failures at ERROR. Include concise asset/view/revision, field and reason
context, and whether prior accepted state was retained. Reuse existing diagnostic
owners; avoid per-frame repetition, logs for each drag sample, duplicate logging
across layers, and serialized settings dumps.

**Gate:** source -> cook -> load -> runtime and save/reload retain identical
resolved settings. Every affected tool and demo uses the new format; obsolete
versions/layouts are rejected without compatibility code. Batch runs do not mutate
personal settings.

### Slice 7 - Complete the reference lighting unit chain

**Outcome:** physically normalized directional/point/spot illumination using
[production model 2](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2),
with qualified quality/performance tradeoffs and scalable supported light sets.
**Dependency:** closed EX06. **Tracked by:** EX07-01–14/GATE.

EX07 owns correctness and performance of the complete agreed lighting path,
including pre-existing defects and necessary dependency repairs. Review the path,
correct discrepancies, optimize measured costs and validate the final result.
An existing implementation limitation is repair work, not a completion exemption.

The [EX07 correctness/scalability plan](EX07-lighting-correctness-and-scalability.md)
owns workload definitions, cost attribution, capacity/overflow behavior and
performance acceptance. Execute six steps: **A contracts -> B references and
instruments -> C correctness repair -> D many-light scene and baseline -> E scalable
culling and optimization -> F final validation**. Its directional cases retain the explicit dual-source
contract in section 6. These checkpoints all belong to EX07 before EX08 begins.

**EX07D is closed (2026-09-24)** with implementation in `137b681b2` and durable
baseline evidence in `894a25e57`. The
[EX07E handoff](EX07-lighting-correctness-and-scalability.md#ex07e-handoff--awaiting-user-signal)
is prepared; no E execution begins until the user's explicit signal.

D credits the accepted MultiView and conventional-shadow baselines and B's
reference/instrument qualification. Its new delivery is the runnable many-light
test scene, built from the existing workload generator/manifest, and that scene's
full-resolution baseline in both deferred and forward rendering. Deferred
many-light scalability remains an explicit qualification and optimization
obligation. E/F reuse the scene and unaffected evidence; accepted
cases are repeated only for a specific invalidating change or diagnosed noise.
The [D delivery scope](EX07-lighting-correctness-and-scalability.md#ex07d-delivery-many-light-scene-and-baseline)
and [baseline register](EX07D-baseline-report.md) record the benchmark and
Instancing/New Sponza operating points, precise collection modes, confidence
limits and accepted historical evidence required for E/F. The register includes
54 timed benchmark rows and four application runs; preflight coverage gaps and
noisy measurements remain explicit, not silently accepted as controlled timing.

- Implement section 6's shared point/spot conversion, distance/range falloff,
  cone normalization, hard-cone limit and invalid-cone rejection. Directional
  lux and receiver cosine must pass the same complete unit-chain review.
- Cover active forward and deferred consumers; freeze working color space and
  the actual packed material/production BRDF interpretation in the independent
  oracle. Include white-source reference values and separate tint behavior.
- Use existing native scene fixtures and readbacks for a directional card,
  point distances and spot angles. Freeze the numeric case inputs and tolerances
  before accepting captures. The production helper cannot serve as its own oracle.
- Review affected authored examples/fixtures when corrected intensity changes
  their appearance. Record intentional physical recalibration; never compensate
  with a hidden exposure offset or preserve old behavior through a compatibility path.

**Calibration gate (EX07C):** both families pass photometric normalization,
material decoding, ABI and shared-model consistency checks. Report independent
BRDF/finite-source approximation differences with the current model-2 quality
measurements. Independent spot-profile integration recovers authored flux; point
ratios include known range fade.
Zero/near separation, at/beyond range, inner/outer and equal-angle cones have
explicit finite/zero/invalid expectations. Inspect the lit calibration fixtures
and run affected light/shader tests. EX08 consumes these qualified references.

**Full EX07 gate:** also pass the many-light culling/reference-image, overflow,
shader/shadow association, multi-view, resource-lifetime and editor-input cases.
Measure native quality/time/memory operating points; deliver accepted improvements
and explicit supported limits. Primary workload: 1,024 mixed local lights at
1080p; 4,096 lights, dense overlap and 4K qualify scaling. Shadowed subsets are
measured separately. Benchmarks remain opt-in executables under `Benchmarks`,
separate from correctness tests. Use existing fixtures/profiling; EX07 does not
depend on EX08's future reusable measurement API or EX08.1/8.2 automation.

### Slice 8 - Deliver a measured Neutral Reference

**Outcome:** one complete LightBench experiment works interactively and in batch.
**Dependency:** EX07. **Tracked by:** EX08-01–06/GATE plus existing
EX09-01–03/10–11/14 and EX10-01–02/06 for the initial experiment.

Build this as one narrow end-to-end path, with two internal checkpoints:

1. **Qualify the instrument.** Implement section 7.3 through existing diagnostics,
   extraction and readback owners. Known float input and consumed-gain probes
   must meet the PBR 2e-5 relative + 2^-120 absolute budget before lighting verdicts
   are enabled. Test P recovery, true black versus absent samples, coverage,
   nonfinite data, stale view/revision results and delayed readback lifetime.
   Verify zero added dispatches/readback allocations when disabled and bounded
   in-flight resources when enabled. Reuse EX06 settings/status diagnostics;
   do not present those as measured GPU gain.
2. **Ship Neutral Reference.** Use the existing LightBench specification's three
   cards (0.18/0.9/0.02), 1000-lux white directional key and production materials.
   Resolve exposure independently from EX07's oracle. Supply one versioned,
   schema-validated recipe/controller with complete staged apply and reset.

The delivered workflow is Launch -> inspect -> edit -> Reset; explicit Save/Load
round-trips a modified recipe. Reuse EX06's activation/persistence isolation.
Convert the shipped indoor preset to the current experiment format without a
legacy reader or overwriting personal files. Show experiment, expected/measured
values, units and verdict; collapse unrelated controls. Pending results have no
current verdict; Modified and Invalid cannot masquerade as a reference Pass.

Add experiment selection and bounded batch execution now, using the same
controller and asset-readiness barrier as the UI. Implement the first case of
`Run-LightBenchValidation.ps1` and its versioned result schema. Record recipe,
build/shader/device identity, timestep, frame/view/revision, validity, tolerances,
measurements and captures. Add actual commands to `Examples/LightBench/README.md`.
Console commands and ImGui Test Engine integration follow in their own slices;
this first batch case uses the native controller/CLI directly.

**Exit gate:** all three inset card regions meet the production BRDF budget and
the specification's >=95% foreground coverage rule; wrong/occluded/late samples
cannot yield Pass. Clean launch and reset are readable at 1080p, 1440p and a
resized window. Reset reproduces resolved inputs and declared temporal policy;
saved reload preserves authored values. Interactive and batch resolved recipe
hashes match, and measurements match the same oracle within declared budgets.
One native batch run emits a schema-valid report and a failing comparison yields
a failing result/exit status. Other experiments remain explicitly unimplemented.

### Slice 8.1 - Post-processing console controls

**Outcome:** developers can exercise post-processing from Oxygen's existing ImGui
console and automation execution source, with reliable feedback.
**Dependency:** EX08. **Tracked by:** EX081-01–04/GATE. This is the concrete
follow-up deferred during EX06, not a new console subsystem.

- Add a documented command namespace to the existing Console registry with help,
  completion and existing development/source policies. Cover settings/status
  inspection; complete validated exposure edits (including camera inputs, mask,
  curve, tone mapper and gamma); seed/remeter transitions; and experiment
  selection/reset through the appropriate owners. Settle command spelling at
  implementation without creating duplicate CVars for canonical settings.
- Keep general post-process commands on the engine/settings boundary and
  experiment selection/reset on LightBench's controller. Reuse DemoShell's
  accepted-candidate path; apply changes on the owning thread at its normal
  boundary. Do not write renderer internals or GPU state from command handlers.
- Make the target view/owner explicit. Report queued/applied/rejected state,
  settings revision and transition token where applicable. Distinguish requested,
  accepted and measured values; a queued asynchronous mask is not immediate success.
- Test valid edits, unknown enums/fields, nonfinite/out-of-range values, missing
  masks, invalid curves, lost/recreated views, seed/cut ordering and shutdown
  unregistration. Use the existing `CommandSource::kAutomation` execution path
  for scripts; add no remote service, general console framework or persistence bypass.

**Exit gate:** one documented console sequence selects/resets Neutral Reference,
changes mode/EV, issues a seed/remeter request and inspects its eventual result.
Equivalent UI and console requests produce the same accepted revision/settings
and measured output; invalid requests make no partial changes. Pending/failure
diagnostics are concise and attributable. Both native execution and the integrated
console UI are exercised; numerical tests remain independently runnable.

### Slice 8.2 - ImGui interaction regression automation

**Outcome:** a repeatable native test run drives the real DemoShell/LightBench
widgets and produces actionable failure evidence.
**Dependency:** EX08 (scheduled after EX08.1). **Tracked by:** EX082-01–04/GATE.
This closes the ImGui automation deferral from EX06.

- Pin a Test Engine revision compatible with the repository's ImGui dependency
  (currently 1.92.5); verify its license applicability before adding it. Follow
  upstream [setup](https://github.com/ocornut/imgui_test_engine/wiki/Setting-Up)
  and [license](https://github.com/ocornut/imgui_test_engine#licenses), without
  assuming the test engine has the same license as Dear ImGui.
- Provide an opt-in UI-test build target/configuration. Its ImGui library and
  consumers must use consistent test-hook configuration. Integrate context/frame/
  presentation/shutdown hooks with the existing ImGui owner. Production SDK and
  normal demo builds remain free of the test-engine dependency and test payload.
- Address real widgets by stable IDs/paths and inject UI input. Cover numeric
  typing/Enter/Tab/focus commit/Escape/drag, mode selection, curve Apply/Discard,
  mask pending/failure/recovery, experiment Reset/Load/Save and switching panels.
  Add the TexturedCube Wood/cube, Marble/sphere, panel-return regression with
  isolated assets/settings. Console calls may establish setup but cannot replace
  clicking or typing for an interaction assertion.
- Use bounded state/readiness waits, not arbitrary sleeps or fixed screen
  coordinates. Integrate pass/fail/timeout and failure captures into the existing
  reports; prove a deliberate assertion failure returns a failing process result.
  See upstream [result export](https://github.com/ocornut/imgui_test_engine/wiki/Exporting-Results).

**Exit gate:** the named EX06 workflows pass in the actual rendered applications,
including 1080p, 1440p and resized layouts; one intentional failure demonstrates
usable diagnostics. A normal build still runs without test-engine components.
Tests run without altering personal settings. Subsequent experiment slices add
only their new interactions to this suite. Human inspection still judges framing,
readability and rendered lighting; automated widget assertions do not establish
visual or photometric correctness by themselves.

### Slice 9 - Complete the experiment set and MultiView delivery

EX09 is a delivery group, closed only after the five steps below. Each adds its
case definitions, oracle comparisons, relevant UI, batch execution and README
instructions to EX08's working path. Extend EX08.1 commands only for new operations
and EX08.2 tests for new interactions. No second controller or separate automated
scene implementation is permitted. Shared schema/controller/UI/report work moved
to EX08 retains its original tracking IDs.

#### EX09A - Point Falloff and Spot Distribution

**Outcome:** the user can see and measure distance/cone behavior.
**Dependency:** EX08.2. **Tracked by:** EX09-07–08, with EX09-10/12/14 coverage.

Place the point on the receiver normal and aim the spot at its receiver. Use
prescribed distances and angular sweeps from EX07, with helpful distance/normal/
cone overlays. Keep materials, camera and exposure fixed during comparisons.

**Exit gate:** both experiments pass independent point intensity/range-fade and
spot center-cone/flux expectations, with model-2 approximation differences reported
separately and clearly visible
illuminated receivers. Reset restores all inputs; UI edits mark the recipe
Modified. Both cases pass the same batch controller and native layout checks.
Reuse EX07's singularity/invalid-input tests; rerun them only for affected code.

#### EX09B - Fixed Exposure

**Outcome:** EV and camera controls have an immediately understandable response.
**Dependency:** EX09A. **Tracked by:** EX09-04, with EX09-10/12/14 coverage.

Use known HDR input 4096 with tone mapper None and gamma 1. Include Manual,
ManualCamera, key/compensation and disabled cases. Show authored EV/camera inputs,
actual consumed gain and expected linear output with explicit units/domains.

**Exit gate:** EV14/15/16 produce 0.25/0.125/0.0625 before dither/encoding; +1 EV
halves gain, +1 compensation doubles it, and the physical camera agrees with
the canonical EV equation. Disabled has unit displayed gain. GPU probes meet
the fixed-gain budget; displayed pixels meet the output-encoding budget. Invalid
edits preserve the last accepted state, supported-limit probes remain valid,
and Reset/Load/Save/batch preserve the experiment's inputs.

#### EX09C - Adaptation and Lifecycle

**Outcome:** the user can play and restart reproducible bright/dark transitions
and understand how seeds, cuts, modes and pause affect them.
**Dependency:** EX09B. **Tracked by:** EX09-05–06 and EX10-03, with EX09-10/12/14.

Add a single scripted-time sequence path and a focused response plot with target,
actual gain and event markers; keep detailed lifecycle variants in advanced
controls or documented batch cases. Use existing public transitions and native
fixtures for game-facing callers, source loss, stateless views and recovery.
No artificial UI is needed solely to force a device/resource failure.

**Exit gate:** both light-step directions, transition-distance crossing and
30/60/120 Hz plus irregular schedules agree at equal elapsed time within 5e-4 EV,
without overshoot. Pause, zero speed and long frames follow the contract.
Mask/profile/curve variants match the metering oracle. Startup, event-frame seed
(including out-of-meter-range), cuts, mode changes, zero target/restoration,
sharing/source loss, stateless and recovery cases have exact expected event
generations and timing. Delayed/stale results cannot overwrite a newer verdict.
Restart/reset reproduces inputs and time zero. Native inspection shows both
transition directions without unintended flashes or black frames; intentional
zero-target black is declared. Batch and non-DemoShell fixture evidence cover
the same contracts without duplicating the implementation.

#### EX09D - HDR Domain

**Outcome:** mixed rendering content keeps its intended appearance across
numerical exposure-domain changes, startup and recovery.
**Dependency:** EX09C. **Tracked by:** EX09-09, with EX09-10/12/14 coverage.

Reuse EX05's qualified mixed opaque/emissive/forward/translucent/sky/AP/fog
fixtures, endpoints and history checks as the experiment's basis. Expose a focused
diagnostic comparison of P with fixed scene and intended output. Production
continues to use the accepted FP32/P=1 policy; half eligibility and return are
tested only under the explicit qualified diagnostic control.

**Exit gate:** required float products satisfy P invariance (0.5% relative +
2e-5 absolute), displayed UNorm8 comparisons respect the one-code-value budget,
and no producer loses required bright/dark signals before metering. First frame,
range/recovery, delayed acknowledgment and reused fog/radiance histories match
their established policies. Native capture shows the intended mixed content,
stable presentation and truthful diagnostics. Reuse prior internal matrices
where their inputs and relevant implementation identities remain valid.

#### EX09E - MultiView operational acceptance

**Outcome:** ordinary main/PiP and existing proof layouts work as a coherent
application with trustworthy per-view exposure diagnostics.
**Dependency:** EX09D. **Tracked by:** EX09-13/15 and EX10-04–05.

Integrate EX08's instruments into existing MultiView exposure scenarios, CLI,
`Run-VortexMultiViewValidation.ps1`, analyzers/assertions and result schema.
Preserve structural checks and reuse the existing interaction sequences.
Provide/document the controls needed to select independent/shared exposure and
understand the owner and one-frame sharing delay; avoid a second bench UI.

**Exit gate:** section 7.5's complete scenario/layout matrix is accounted for.
Ordinary lit PiP, standard, auxiliary, offscreen and feature layouts show every
required lit pane; intentional BLACK expected cells remain labelled. Standalone
and family outputs agree for equivalent inputs within established budgets;
independent edits stay isolated and sharing follows the previous-owner-frame
contract. Reorder/resize/camera/mode/hide/recreate/cut/source-loss interactions
retain correct images, histories and UI. Inspect the native composite separately.
The existing runner emits integrated measurements/captures and pass/fail results;
the README contains reproducible commands. Rerun only invalidated EX05 proofs
plus the new integrated flow, not the entire historical capture campaign.

**EX09 group gate:** all seven experiments and MultiView satisfy their per-step
numerical, interaction and visual gates. EX09-12 records the seven-experiment
visual coverage at 1080p, 1440p and resized dimensions as each experiment lands;
it is not an instruction to recapture every unchanged scene again.

### Slice 10 - Close integrated package acceptance

**Outcome:** one documented acceptance entry point produces a complete, auditable
result for this package. **Dependency:** EX09 group gate.
**Tracked by:** EX10-07–08/GATE, aggregating EX10-01–06 delivered with EX08/09.

- Complete `Run-LightBenchValidation.ps1 -Suite acceptance` as the orchestrator
  of the existing seven experiment cases, native game-facing fixtures and the
  extended MultiView runner. Use their shared report/provenance contract.
- Map every section 9 row to a passing current result or explicitly reusable
  prior evidence. A required failed, unsupported, missing or invalid case blocks
  completion. Verify a deliberately corrupted result fails the checker.
- Run the final integrated commands on the final Debug and Release build/shader
  identities; carry forward unchanged per-slice visual/mathematical evidence.
  Correctness captures use the debug layer. Any timing measurement uses native
  optimized Release without capture/debug validation overhead.
- Reconcile PBR/runtime/diagnostics/LightBench contracts, both application
  READMEs, this plan and the concise tracker. Preserve the accepted EX051 CPU
  disposition and include the delivered EX08.1/EX08.2 operating instructions.

**Exit gate:** the acceptance report covers every required matrix row and all
seven experiments plus MultiView, with zero unexplained failures or warning/error
logs. Every advertised command works. All implementation/test/doc changes are
accounted for; there is no remaining product feature hidden in the closeout step.

## 9. Acceptance matrix and execution

### Validate the change once; retain valid evidence

The matrix is a coverage requirement, not an instruction to repeat every earlier
test/capture in every slice. Each result records its case/recipe, relevant source,
shader and asset identities, configuration, backend, oracle and tolerance revision.
Declare the affected rows before running validation. Reuse prior results only
when their inputs and relevant implementations are unchanged; rerun affected
cases after a change or failure, including shared shader/helper consumers.

New per-slice tests and native visual checks close with that slice. EX10 runs the
integrated final-build workflows once per required configuration and references
valid detailed evidence. It does not repeat the EX051 performance matrix, EX052
quality review, EX06 migration campaign or every historical RenderDoc capture.
If a change invalidates one of those contracts, identify and test that affected
scope explicitly. No blind full-repository test loop or repeated successful run.

Freeze tolerances before results, using the
[PBR budgets](../../renderer-core/physically-based-rendering.md#acceptance-budgets)
and [LightBench coverage rules](../../renderer-core/lightbench.md#independent-measurement).
GPU values, derived CPU values and displayed pixels have distinct comparisons.
Keep exposure, encoding and same-model consistency tolerances fixed. Assess
production shading approximations using the PBR model-2 quality/time/memory
comparisons; the old integrated-source and reciprocal-model errors are not
implementation-failure thresholds for this model.

### Required coverage

EX07's [many-light matrix](EX07-lighting-correctness-and-scalability.md#deterministic-workload-envelope)
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
| Console and UI automation          | Shared command/UI validation, explicit targets and async outcomes; actual widget edit/focus/mask/reset/panel workflows; isolated state; failing exit/report/capture on error                                      |
| Instruments                        | Known float signals; actual consumed gain; stale results; invalid regions; zero disabled cost                                                                                                                     |
| LightBench visual benchmark        | All seven experiments, clean startup, full reset, saved experiments, identical interactive/batch inputs, readable native output and correct interactive exposure behavior                                         |
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

EX08 implements the runner for Neutral Reference; EX09A-E add their cases.
EX10 closes its complete acceptance interface (planned, not yet available):

```powershell
./tools/vortex/Run-LightBenchValidation.ps1 -Suite acceptance -Output out/build-ninja/analysis/vortex/exposure-lightbench/acceptance
```

Retain underlying launch arguments and use the existing `--capture-provider`,
`--capture-load`, `--capture-output`, `--capture-from-frame`, and
`--capture-frame-count` options. Capture initialization, transition and steady
frames, including shader inputs, exposure states and final consumption.
Control actual simulation delta, camera motion, seeds and asset readiness;
an FPS cap alone is insufficient.

## 10. Documentation and source references

Update the owning documents in each implementation slice. Keep a single
mathematical specification in the PBR document, one exposure-runtime design
in the PostProcessService LLD, and one experiment specification for LightBench.

- [PLAN.md](../PLAN.md), [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md),
  and relevant indexes: execution order, slice results and remaining failures.
- [Exposure reference companion](../lld/exposure-improvement-plan.md): source
  pointers and deliberate Oxygen choices; keep algorithms in this plan/their LLDs.
- [Diagnostics LLD](../lld/diagnostics-service.md): reusable measurement interface.
- `ARCHITECTURE.md`, `init-views.md`, shader/environment/lighting LLDs: update
  affected ownership, domains and publications.
- [Existing LightBench specification](../../renderer-core/lightbench.md):
  experiment/controller/measurement semantics. Create and maintain
  `Examples/LightBench/README.md` from EX08 onward.
- `Examples/MultiView/README.md` and multiview validation tools: visual exposure
  scenarios, per-view comparisons and reproducible interaction sequences.
- Repository-root editor authoring/API documents: new authored fields and
  versioned round-trip behavior; no general UI redesign.
- Reports/captures: `out/build-ninja/analysis/vortex/exposure-lightbench/`.

UE5.7 source reference root: `F:/Epic Games/UE_5.7/Engine`.

| Reference                                                                             | Use                                                                              |
| ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp`            | Target/current exposure, force-target conditions, normalization and pre-exposure |
| `Source/Runtime/Renderer/Private/SceneRendering.cpp::ShouldUpdateEyeAdaptationBuffer` | Owner/borrower update boundary                                                   |
| `Shaders/Private/PostProcessHistogramCommon.ush`                                      | Log histogram reduction and hybrid EV response                                   |
| `Shaders/Private/Histogram.usf`                                                       | Interpolated bins and black weighting                                            |
| `Shaders/Private/PostProcessTonemap.usf`                                              | Pre-exposure removal and final gain application                                  |
| `Source/Runtime/Engine/Private/Components/LocalLightComponent.cpp`                    | Explicit distance and lumen/candela conventions                                  |

Oxygen retains its calibration key, analytic metering profiles and
previous-frame sharing. It uses a compact curve-key buffer, deterministic bounded
sampling, exact hybrid integration, and a targeted FP32 bootstrap. These choices
serve the specified behavior without adopting UE's legacy compatibility paths.

## 11. Completion criteria

Checked foundations are validated by EX02-06 in the tracker. Remaining unchecked
items are delivered by the steps above; they do not reopen those foundations.
Package closure remains dependent on final integrated evidence.

- [x] Fixed/manual-camera/Auto/disabled exposure use one consistent state contract.
- [x] Hybrid EV/s adaptation, masks, curves, black handling and zero target work.
- [x] All lifecycle and shared/stateless policies are implemented and tested.
- [x] Every active HDR path uses frame-pinned P and final S/P consistently.
- [x] Bootstrap and numerical recovery preserve valid bright/dark metering signals.
- [x] Authored fields round-trip through all active persistence surfaces (EX06).
- [ ] Directional, point and spot reference units and material expectations pass.
- [ ] EX07 many-light correctness, supported capacities and measured performance/improvement gates pass.
- [ ] LightBench is a properly repaired, visually useful exposure benchmark;
      all seven experiments pass numerical, interactive and visual acceptance.
- [ ] MultiView succeeds visually in ordinary and proof layouts; multiple views
      do not break exposure, and exposure changes do not break rendering/composition.
- [ ] Independent measurements and the complete acceptance matrix pass.
- [ ] Post-processing console controls and native ImGui interaction automation pass (EX08.1/EX08.2).
- [ ] Owning documents and operational instructions describe the implemented behavior.
