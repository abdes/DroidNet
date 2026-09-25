# Exposure management and LightBench implementation plan

Status: `in_progress` — current slice state and evidence are maintained in the
[implementation tracker](../IMPLEMENTATION_STATUS.md#3-exposure-delivery-status).
[Current work](../IMPLEMENTATION_STATUS.md#31-current-work) and
[item-level Slice 5–10 completion](../IMPLEMENTATION_STATUS.md#32-slice-5-work-items)
are updated there; this plan owns the requirements and gates.

Date: 2026-09-16
Updated: 2026-09-25 — EX01–EX10 are validated and closed under the approved reduced scope. [EX10 closeout](EX10-completion.md) records final qualification and user acceptance. Structured commit delivery was authorized after user review.

Paths are relative to `projects/Oxygen.Engine` unless identified as
repository-root paths.

## 1. Delivery scope and sequence

Deliver predictable production exposure, a useful calibrated LightBench, and
correct MultiView operation. The user approved the following reduced scope on
2026-09-25. Renderer correctness requirements remain; a general experiment and
measurement platform is no longer a package deliverable.

1. **Production rendering:** physically defined lights and exposure produce the
   specified values through forward and deferred rendering. Credit applicable
   EX01–EX07 evidence and fix demonstrated defects.
2. **LightBench:** launch a calibrated reference, inspect and edit useful controls,
   reset completely, and explicitly save/load local settings. Focused native
   tests verify the same canonical scene definition against independent values.
   The running demo does not certify arbitrary edited frames numerically.
3. **MultiView:** ordinary lit main/PiP and existing proof layouts remain correct,
   with independent exposure unless sharing is explicit, including camera,
   layout and view-lifetime changes.

### Completed delivery at a glance

EX07 remains closed. EX08, EX09A–E, retained EX08.1, reactivated EX08.2 and
EX10 are complete. The initial instrumentation draft was discarded; stable IDs
preserve removed requirements explicitly. [Final evidence and acceptance](EX10-completion.md)
cover the reduced package. Structured commit delivery was authorized after user review.

| Step   | User value                                                                          | Completion evidence                                                                                                       |
| ------ | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| EX08   | Readable calibrated Neutral Reference; complete reset and explicit local save/load. | Shared canonical scene definition, focused native numerical check, user UI acceptance and README.                         |
| EX08.1 | Inspect/edit post-processing and request transitions through the existing console.  | Same validated owners as UI; atomic rejection, honest asynchronous status and user console checks.                        |
| EX08.2 | ImGui interaction regression automation, reactivated after EX09.                    | Named widget workflows and build isolation qualified before EX10.                                                         |
| EX09A  | Useful point and spot presets with distance/cone controls.                          | Applicable EX07 references plus focused checks for changed inputs and user visual acceptance.                             |
| EX09B  | Predictable EV, compensation and physical-camera controls on the reference scene.   | Existing/focused native fixed-exposure tests and user interaction checks.                                                 |
| EX09C  | Simple reproducible bright/dark transitions and reset.                              | Native timing/lifecycle evidence and user checks for transitions without unintended flashes.                              |
| EX09D  | Preserve mixed HDR rendering correctness.                                           | Applicable existing HDR evidence; focused regressions for uncovered or changed behavior. No new experiment UI.            |
| EX09E  | Usable independent/shared exposure in existing MultiView.                           | Existing controls, scripts and tests; targeted repairs and remaining user interaction checks.                             |
| EX10   | Clear package closeout.                                                             | Applicable evidence for every retained requirement, affected final-build tests, user acceptance and working instructions. |

### Reuse the delivered foundation

EX01–EX07 remain closed, including the accepted FP32/P=1 production policy,
EX051 CPU disposition, EX06 authoring/isolation and EX07 lighting/performance.
Reuse their evidence when inputs and relevant implementation are unchanged.
Do not repeat their captures or performance campaigns for these demo changes.
Varying P and qualified half storage remain explicit diagnostic test coverage.

EX08 reuses DemoShell settings and experiment-owned activation. A small local
settings structure and complete apply/reset operations replace the proposed
universal controller. Reuse the canonical scene construction/parameters in the
native reference check so the demo and test cannot drift into separate scenes.
EX08.1 uses existing console policies and production settings/transition owners;
ordinary commands remain available in normal Release builds.

### Explicit scope disposition

The following are **removed from EX08–EX10**, not implemented or completed:
reusable region-measurement service, live measurement scheduling/readbacks,
instrumented tonemap variants/refactoring, runtime numerical verdicts for edited
frames, universal experiment schema/controller, matching interactive/batch recipe
engine, new LightBench runner/report schema, and seven complete experiment UIs.
EX08.2 was deferred in the earlier scope decision; the latest user goal reactivates it after EX09.

Earlier discussions of a validation build flag and on-demand/live measurement
are superseded as implementation tasks by this reduction. If a concrete future
need justifies instrumentation, enable it explicitly through a build capability
independent of NDEBUG, usable in the existing Debug or Release Ninja tree. No
such capability or shader change is required now. Test-only readbacks remain in
native fixtures; ordinary Release rendering acquires no new measurement machinery.

The [scope decision](EX08-execution.md) records acceptance and removal mapping.
The [LightBench specification](../../renderer-core/lightbench.md) owns demo
behavior; the [tracker](../IMPLEMENTATION_STATUS.md#31-current-work) owns progress.

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
| LightBench exposure benchmark | Calibrated reference and useful presets, complete reset/save/load, focused independent native checks and user UI acceptance                                                 |
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

Begin with a runnable scene and its independent expectation. EX08 delivers the
canonical reference and a focused native check using existing fixtures. Later
steps add only useful presets/controls or close a demonstrated coverage gap.
No generic controller, runtime instrument or new report framework is required.

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

EX07 also closed the physical-lighting defects in both forward and deferred
consumers; calibration, many-light qualification and final editor acceptance are
recorded in the [F report](EX07F-acceptance-report.md). The remaining delivery
gaps belong to EX08 onward:

- `LightScene::ApplyScenePreset` changes object visibility; a calibrated
  directional reference and complete reset still need implementation.
- Existing settings/status APIs are sufficient for controls. They must not be
  labelled same-frame measured luminance or consumed GPU gain.
- MultiView already has exposure scenarios and assertion tools. Audit applicable
  evidence and repair remaining operational gaps without a second framework.
- LightBench still needs local reset/save/load, focused reference qualification,
  clear controls and an operating README.

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

The user owns UI and visual checks; the agent owns implementation and automated
unit/native correctness tests. When ready, launch the demo and supply numbered
actions with expected results. Record OK/NOK and explanations, fix failures and
retest affected checks only.

Check clean launch/reset, entire-card framing, readable controls and backgrounds,
relevant edits and explicit save/load at 1080p, 1440p and a resized layout. Check
new point/spot and transition behavior as delivered, reusing unchanged layout
acceptance. No mandatory repeated captures or ImGui Test Engine integration.
Unexpected black frames, flashes, stale status or preference reapplication remain
bugs. Test convergence against elapsed time in native fixtures, not an arbitrary
warmup-frame count.

### 7.5 MultiView visual acceptance

The following is retained coverage, not an instruction to rerun accepted scenes.
Credit applicable EX05 proof, run focused checks for gaps/changes, and ask the
user to perform outstanding interactions. No new capture campaign is required.

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
Use existing diagnostics and native numerical evidence to support user visual acceptance; no new per-view measurement service is required.
Physical sky and fog remain exposed scene radiance; only display-space backgrounds,
bars, labels and UI are exposure-independent.

## 8. Ordered implementation slices

### File entry points

The shader root below is
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Keep unit/oracle tests next to
the owning module; use the existing native fixture and capture tools for GPU tests.

| Slice    | Start in these files                                                                                                                                                                                 | First observable check                                                                        |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| 2        | `Core/Types/PostProcess.h`, `Vortex/PostProcess/Passes/ExposurePass.cpp`, `Vortex/Test/PostProcessService_test.cpp` under `src/Oxygen/`                                                              | EV14 reaches the tonemap constant unchanged                                                   |
| 3        | `Vortex/PostProcess/Types/PostProcessConfig.h`, `PostProcess/Passes/ExposurePass.*`; shader `Services/PostProcess/Exposure.hlsl`                                                                     | Known two-bin distribution and hybrid trajectory                                              |
| 4        | `Vortex/CompositionView.h`, `Internal/ViewLifecycleService.*`, `PostProcess/PostProcessService.*`, `ExposurePass.*`                                                                                  | One source update and idempotent event generation                                             |
| 5        | `Vortex/Types/ExposureStateData.h`, `Types/ViewFrameBindings.h`, `SceneRenderer/SceneTextures.*`, `Stages/InitViews/InitViewsModule.cpp`; shader families in section 4.4                             | Opaque/emissive output invariant under a change of P                                          |
| 6        | `Scene/Environment/PostProcessVolume.h`, `Data/PakFormat_world.h`, `Data/PakFormatSerioLoaders.h`, cooker schemas, `Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.cpp`, DemoShell services | Current records round-trip; obsolete records are rejected                                     |
| 7        | `Vortex/Lighting/Internal/DeferredLightPacketBuilder.cpp`; shader `Services/Lighting/DeferredLightingCommon.hlsli` and `ForwardDirectLighting.hlsli`                                                 | Point flux and spot angular integral match independent values                                 |
| 8        | `Examples/LightBench/LightScene.*`, `MainModule.*`, `LightBenchPanel.*`; existing native reference fixtures                                                                                          | Calibrated reference resets correctly and passes a focused independent check                  |
| 9A-E, 10 | Existing LightBench/MultiView controls and native tests                                                                                                                                              | Useful presets/controls and retained renderer requirements covered without new infrastructure |

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
work is scheduled in EX08.1; ImGui Test Engine integration (EX08.2) is now
explicitly deferred outside this package.

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
[EX07E handoff](EX07-lighting-correctness-and-scalability.md#ex07e-handoff--closed)
is closed with accepted measurements and visual approval. The
[F acceptance report](EX07F-acceptance-report.md) credits that unchanged evidence
and closes overall EX07, including engine qualification, documentation and
final user editor acceptance.

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
depend on future demo or automation work. EX08 runtime instruments are removed
from package scope; EX08.2 now follows EX09 under the latest user goal.

### Slice 8 - Deliver a calibrated Neutral Reference

**Dependency:** closed EX07. **Tracked by:** EX08-07–11/GATE and
EX09-02–03/10–11/14. EX08-01–06 are explicitly removed instrument work.

- Correct the default scene: linear albedos 0.18/0.9/0.02, metalness 0,
  roughness 1, neutral white 1000-lux directional source and fixed camera.
  Use the production BRDF, including dielectric specular and packing.
- Derive fixed exposure independently from the expected gray-card response and
  chosen output transform. Frame all cards and label units/reference values.
- Add complete Reset Reference and explicit local settings save/load through
  existing owners. Preserve user window/panel preferences and other demos.
- Share canonical scene construction/parameters with one focused native GPU
  reference check. Reuse existing fixture/readback infrastructure and EX07 oracle;
  do not import test-only measurement machinery into the running demo.
- Provide readable controls, Reference/Modified configuration status and actual
  launch/test instructions in `Examples/LightBench/README.md`.

**Exit gate:** independently expected card response meets frozen production
budgets in the focused test, with valid inset regions/coverage; reset and load
restore complete settings coherently; user UI acceptance covers launch, edits,
reset/save/load and layouts. Debug/Release affected checks pass. No runtime
measurement, general controller, batch engine or new report schema is required.

### Slice 8.1 - Post-processing console controls

**Dependency:** EX08. **Tracked by:** EX081-01–04/GATE.

Use the existing console namespace/help/completion, access policies and
`CommandSource::kAutomation`. Commands inspect/edit exposure, camera inputs,
mask/curve and output settings, and request seed/remeter transitions through the
same validated services as UI. Keep explicit view/owner targeting and truthful
queued/applied/rejected revision/token/error feedback. Invalid complete requests
must leave accepted state unchanged. A queued mask is not immediate success.

LightBench preset/reset commands call its local operations directly; no general
experiment adapter is required. Ordinary post-process commands remain available
in normal Release builds under existing policies. There are no new measurement
commands. Test valid/invalid inputs, stale targets, lifecycle unregistration and
transition ordering. The user tests the integrated console UI from a checklist.

**Exit gate:** documented commands and UI converge on the same accepted settings
and transitions; native tests cover validation/lifetime; user confirms visible
behavior. No measured-output service or new persistence path is needed.

### Slice 8.2 - ImGui interaction regression automation

**Disposition:** reactivated by the latest user goal after EX09 and before EX10.
The prior 2026-09-25 deferral is superseded. EX082-01–04/GATE cover Test Engine
dependency/configuration/context integration; numeric/focus/drag/mode/curve/mask/
reset/save/load workflows; TexturedCube assignment and panel-return workflows;
and the native UI runner with failure evidence and build isolation. Reuse the
existing application controls and native owners. Do not restore the removed
experiment controller, measurement system or universal recipe/report engine.

The UI test configuration must be explicitly enabled in an existing Ninja tree
and excluded from ordinary Release builds. Native rendering tests remain the
numerical authority; widget tests verify actual input, focus and visible state.

### Slice 9 - Close renderer gaps and deliver useful controls

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
and user visual acceptance. No angular-sweep UI or general batch recipe engine.

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
or a focused passing new check; retained controls/presets have user acceptance;
discovered bugs are fixed. Seven experiment implementations are not required.

### Slice 10 - Concise package closeout

**Dependency:** EX09. **Tracked by:** EX10-03–08/GATE; EX10-01–02 are removed.

Map retained requirements to applicable existing proof or focused new results.
Record relevant source/build/shader/scene identities and numerical tolerances in
a durable Markdown summary using existing test outputs. Run affected final
Debug/Release checks; do not repeat unchanged captures, benchmarks or whole suites
without a reason. Include user UI acceptance and working application/test commands.
Reconcile owner docs and tracker. No new acceptance platform, runner or schema.

**Exit gate:** no unresolved retained product requirement or discovered defect;
automated evidence, user acceptance and operating instructions are recorded.
Removed infrastructure stays removed. Reactivated EX08.2 must qualify its named
workflows before EX10 can close; it is not covered by earlier native-only checks.

## 9. Acceptance matrix and execution

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

## 10. Documentation and source references

Update the owning documents in each implementation slice. Keep a single
mathematical specification in the PBR document, one exposure-runtime design
in the PostProcessService LLD, and one calibration-demo specification for LightBench.

- [PLAN.md](../PLAN.md), [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md),
  and relevant indexes: execution order, slice results and remaining failures.
- [Exposure reference companion](../lld/exposure-improvement-plan.md): source
  pointers and deliberate Oxygen choices; keep algorithms in this plan/their LLDs.
- [Diagnostics LLD](../lld/diagnostics-service.md): retain existing diagnostics; optional future measurement infrastructure is outside this package.
- `ARCHITECTURE.md`, `init-views.md`, shader/environment/lighting LLDs: update
  affected ownership, domains and publications.
- [Existing LightBench specification](../../renderer-core/lightbench.md):
  reference presets, reset/save/load and user acceptance. Create and maintain
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

Checked foundations are validated by EX02-07 in the tracker. EX07's engine gates
and final interactive editor acceptance are complete. Remaining
unchecked items belong to the later steps and do not reopen those foundations.
Package closure remains dependent on final integrated evidence.

- [x] Fixed/manual-camera/Auto/disabled exposure use one consistent state contract.
- [x] Hybrid EV/s adaptation, masks, curves, black handling and zero target work.
- [x] All lifecycle and shared/stateless policies are implemented and tested.
- [x] Every active HDR path uses frame-pinned P and final S/P consistently.
- [x] Bootstrap and numerical recovery preserve valid bright/dark metering signals.
- [x] Authored fields round-trip through all active persistence surfaces (EX06).
- [x] Directional, point and spot reference units and material expectations pass.
- [x] EX07 many-light correctness, supported capacities and measured performance/improvement gates pass.
- [x] EX07 final editor interaction sign-off; user confirmed creation, live light edits, undo/redo and save/reopen persistence.
- [x] LightBench is a properly repaired, visually useful exposure benchmark;
      retained reference/presets/transition workflows pass focused numerical and user UI acceptance.
- [x] MultiView succeeds visually in ordinary and proof layouts; multiple views
      do not break exposure, and exposure changes do not break rendering/composition.
- [x] Retained renderer requirements have applicable existing proof or focused passing new checks.
- [x] Post-processing console tests and user UI checks pass (EX08.1); EX08.2 widget automation and build isolation pass in Debug/Release.
- [x] Owning documents and operational instructions describe the implemented behavior.
