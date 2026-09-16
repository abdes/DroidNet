# Exposure management and LightBench implementation plan

Status: `in_progress` — slices 1-2 checkpointed; slice 3 next.

Date: 2026-09-16

Paths are relative to `projects/Oxygen.Engine` unless identified as
repository-root paths.

## 1. Delivery scope and sequence

Deliver a complete desktop global-exposure system, a serious LightBench visual
exposure benchmark, and a visually correct MultiView demo in this work package.
LightBench and MultiView are primary acceptance deliverables. Implement fixed/manual-camera exposure, histogram
metering, hybrid adaptation, masks and compensation curves, view lifecycle,
shared exposure, unified pre-exposure, authoring round-trip, diagnostics, and
automated qualification. Required light-unit corrections for the bench are
included.

Execute the slices in section 8 in order. Engine slices use native fixtures and
RenderDoc before the bench instrumentation is available. Each slice includes
its tests and owning-document updates.

The [exposure reference companion](../lld/exposure-improvement-plan.md) contains
UE source pointers and Oxygen's implementation choices. This document owns
the delivery sequence and acceptance criteria.

### Included features

| Area | Deliverable |
| --- | --- |
| Mathematical contract | Preserve Oxygen's EV100 equation; correct documentation and numeric propagation |
| Exposure state | One per-view GPU state consumed by Manual, ManualCamera, Auto, and disabled modes |
| Metering | Bounded deterministic sampling, interpolated histogram bins, percentiles, black handling, masks, compensation curves |
| Adaptation | Predictable EV/s far from the target and smooth exponential convergence near it |
| Lifecycle | Concrete startup, cut, seed, mode transition, pause, stateless, sharing, and recovery policies |
| HDR integration | One frame-pinned pre-exposure domain across every active HDR producer and consumer |
| Authoring | Native components, source schemas, cooker, packed records, loader, scripting, DemoShell and existing editor adapters round-trip active fields |
| LightBench exposure benchmark | Properly repaired demo, readable reference scenes, complete exposure experiments, independent numerical expectations, repeatable interaction/reset and visual qualification |
| MultiView visual correctness | Main/PiP, multi-camera, auxiliary and offscreen layouts render correctly with independent or explicitly shared exposure; exposure and multiview changes preserve each other |
| Qualification | GPU measurements, native gameplay scenarios, multiview tests, numerical and visual results |

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
- Use two HDR resource modes: normal FP16 and bootstrap/recovery FP32. Implement
  them through existing texture descriptors, PSO keys and resource leases;
  add no general dynamic-precision service.
- Reuse one bounded status/readback path for initialization acknowledgment,
  numerical range errors and diagnostics. Add no full-resolution telemetry target.
- Calibrate the existing directional, point and spot models. Add no light-type
  selector, alternative BRDF, generic curve editor or general benchmark framework.

### Working through the plan

Slice 1 is the rendering lead's contract checkpoint: freeze exact GPU/asset
layouts, the numerical domain and the HDR product-format inventory, distinguishing
specified limits from hardware-tested limits. The
[checkpoint report](exposure-contract-checkpoint.md) records source and compiler
evidence, decisions and the remaining gate.
The implementation slices then consume those written contracts.

| Work | First patch | Review focus |
| --- | --- | --- |
| Settings and math (2) | Independent EV14 regression and removal of fixed gain floor | Numeric bounds and compatibility |
| Metering/adaptation (3) | CPU oracle and known-distribution GPU fixture | Sampling, conserved weights and time integration |
| Lifecycle (4) | Owner-only state update and generation tests using controlled float input | Senior review of ownership and GPU lifetime |
| HDR integration (5) | Frame binding plus opaque/emissive pre-exposure invariant test | Senior review of domains, formats and barriers |
| Persistence (6) | One old-record/new-record round-trip test | Record layout and resource-reference compatibility |
| Light units (7) | Point flux conversion and inverse-square test | Physical units and shared forward/deferred behavior |
| Measurements (8) | One known-float region readback | GPU identity, lifetime and error budget |
| Bench/runner (9-10) | Neutral Reference recipe and deterministic reset | UI clarity and reproducibility |

For each first patch, add its focused test, implement that behavior, and run
the named slice gate before expanding to the remaining cases. Use the file map
in section 8 to locate the owner. Review GPU bindings, barriers and lifetime
changes before integrating another rendering family.

## 2. Baseline defects and implementation owners

- `Core/Types/PostProcess.h` has a coherent EV conversion, but
  `ExposurePass::Execute` floors fixed exposure at `1e-4`. At key 12.5 and
  zero compensation, EV14 is approximately 0.712288 stops too bright.
- Auto history starts from the inactive manual EV. DemoShell's
  `ResetAutoExposure(initial_ev)` is empty.
- A borrowed exposure handle can be updated using the borrowing view's image.
- Adaptation uses `1 - exp(-k * dt)` despite EV/s authoring labels.
- The histogram assigns each sample to one bin. Full-resolution weighted
  32-bit accumulation can overflow at 8K.
- The pre-scene exposure scalar and Stage 22 exposure result are separate.
  Atmosphere LUTs, sky/AP, forward shading and deferred shading use different
  exposure-domain conventions.
- Legal zero auto-target input is replaced by positive runtime floors.
- Saved DemoShell camera/post-process settings can overwrite scene settings.
- LightBench presets change visibility rather than a complete experiment.
  Its point light is tangent to the cards, its saved spot configuration misses
  gray/white centers, and no directional reference light is created.
- The deferred local-light path passes authored lumens through as intensity,
  with `1/(d*d + 1)` attenuation. Calibrated point/spot tests require the
  unit conversion and distance behavior specified in section 6.

Primary implementation areas:

- `src/Oxygen/Core/Types/PostProcess.h`
- `src/Oxygen/Scene/Camera/CameraExposure.h`
- `src/Oxygen/Scene/Environment/PostProcessVolume.h`
- `src/Oxygen/Vortex/CompositionView.h`, `Internal/ViewLifecycleService.*`
- `src/Oxygen/Vortex/SceneRenderer/`, `PostProcess/`, `Types/ViewColorData.h`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/`
- `Examples/DemoShell/Services/` and `Examples/LightBench/`

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

| Event | Required behavior |
| --- | --- |
| New unseeded Auto view | Meter current frame and initialize immediately |
| Camera cut / replaced world / device recovery | Remeter by default; explicit Preserve or Seed overrides are supported |
| Walking between illumination levels | Continue adaptation |
| Streaming, spawning, ordinary light changes | Continue adaptation |
| Compatible viewport/format/resolution change | Preserve exposure; recreate only size/format-dependent resources |
| Explicit seed | Publish the seed for the event frame; adapt next frame |
| Manual/ManualCamera entry | Apply authored exposure immediately |
| Exposure disabled | Displayed scale one |
| Temporary diagnostic/wireframe exposure override | Use transient unit-exposure output; preserve authored mode and persistent adaptation history |
| Stateless Auto view | Meter current frame without temporal adaptation; transient GPU state only |
| Destroyed view | Retire resources after all GPU readers complete |

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
history. A tiny fixed P can erase dark pixels; P=1 can overflow bright pixels.

Use transient FP32 scene color and required high-range view-dependent
intermediates for first unseeded metering, remeter cuts and device recovery.
Use P=1, meter and tonemap that frame. Exposure validity and FP16 suitability
are separate conditions. The CPU starts this format on the known event and
retains it until nonblocking completed status for the matching view, settings
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
an already saturated texture. Depth, normals, coverage and transmittance formats
remain unchanged.

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
Older records receive deterministic defaults for new fields. Preserve existing
enum ordinals and authored values; reject malformed coupled settings atomically.
Version packed layouts and use record-size/version handling rather than
reinterpreting older bytes. Runtime transition generations and GPU history are
not serialized.

## 6. Calibrated light and material reference

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

| Experiment | Controlled variables and expected result |
| --- | --- |
| Neutral Reference | Three framed cards, actual white directional light, fixed camera/exposure, known production material response |
| Fixed Exposure | Known HDR input; EV/key/compensation sweeps through uploaded and consumed gain |
| Adaptation | Scripted luminance steps, both directions, controlled dt, mask and curve variants |
| Lifecycle | Startup, seeds, cuts, mode changes, pause, shared views, stateless views and recovery |
| Point Falloff | White point on receiver normal, prescribed distances, expected inverse-square response |
| Spot Distribution | Aimed receiver, cone overlay, angular falloff and integrated flux normalization |
| HDR Domain | Bright/dark endpoints and mixed opaque/forward/translucent/sky/fog content under varying numerical P |

Neutral Reference is the default. Put camera and key on the visible side of the
cards; keep presentation labels/background separate from physical illumination.
Select exposure from the reference calculation and an explicitly authored output
transform. Provide clear light position, receiver normal, distance and cone
overlays only where useful.

### 7.2 Settings and reset ownership

A staged experiment application owns geometry, materials, lights, environment,
camera, rendering features, exposure/output settings and measurement regions.
Publish them coherently at a frame boundary and issue the relevant transition.

`force_environment_override = false` alone is insufficient:
`DemoShell::OnSceneActivated` also reapplies saved camera/post-process values.
Add an explicit experiment-owned activation policy across these services.
Other demos retain their existing persistence behavior.

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

| Scenario | Required visible result and exposure behavior |
| --- | --- |
| Main + lit PiP (`--pip-wireframe false`) | Both cameras show correctly framed, stable lit images; each view's Auto meter uses its own content rectangle |
| Different view brightness/modes | A bright view and dark view adapt independently; changing one view's mode, compensation or camera does not change the other's gain or image |
| Explicit shared exposure | The consumer follows the owner's published prior gain with the specified one-frame latency; its scene content never drives the owner |
| Submission/layout reordering | Identical view inputs yield the same per-view exposure and image, regardless of submission order or screen placement |
| Resize and scissor | Correct aspect ratio, viewport/scissor and UI placement; bars and neighboring views do not enter a view's histogram |
| Hide/reopen/recreate | Retained handles follow the inactivity policy; recreated handles initialize independently and never inherit another view's history |
| Cut/reset/source destruction | Only the target history changes; source loss follows the documented detach/continuity rule without blank panes or stale bindings |
| Auxiliary and offscreen products | Producer/consumer images retain the correct HDR/exposure domain; no double exposure, unintended second tonemap or stale product |
| Wireframe/debug/feature variants | Diagnostic presentation remains correct and does not corrupt neighboring or retained exposure histories |

Run ordinary main/PiP, the standard proof layout, auxiliary, offscreen, and
feature-variant layouts. Preserve the existing intentional `BLACK expected`
cells for depth-only, shadow-only and diagnostics-only profiles; unexpected
black lit views fail. ImGui, display backgrounds and fixed bars remain independent
of scene exposure.

Add a bounded scripted exposure-proof mode to MultiView's existing configuration
and CLI. Use `MainModule::UpdateComposition` for view settings/transitions,
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

| Slice | Start in these files | First observable check |
| --- | --- | --- |
| 2 | `Core/Types/PostProcess.h`, `Vortex/PostProcess/Passes/ExposurePass.cpp`, `Vortex/Test/PostProcessService_test.cpp` under `src/Oxygen/` | EV14 reaches the tonemap constant unchanged |
| 3 | `Vortex/PostProcess/Types/PostProcessConfig.h`, `PostProcess/Passes/ExposurePass.*`; shader `Services/PostProcess/Exposure.hlsl` | Known two-bin distribution and hybrid trajectory |
| 4 | `Vortex/CompositionView.h`, `Internal/ViewLifecycleService.*`, `PostProcess/PostProcessService.*`, `ExposurePass.*` | One source update and idempotent event generation |
| 5 | `Vortex/Types/ExposureStateData.h`, `Types/ViewFrameBindings.h`, `SceneRenderer/SceneTextures.*`, `Stages/InitViews/InitViewsModule.cpp`; shader families in section 4.4 | Opaque/emissive output invariant under a change of P |
| 6 | `Scene/Environment/PostProcessVolume.h`, `Data/PakFormat_world.h`, `Data/PakFormatSerioLoaders.h`, cooker schemas, `Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.cpp`, DemoShell services | One new record and one old record resolve correctly |
| 7 | `Vortex/Lighting/Internal/DeferredLightPacketBuilder.cpp`; shader `Services/Lighting/DeferredLightingCommon.hlsli` and `ForwardDirectLighting.hlsli` | Point flux and spot angular integral match independent values |
| 8 | `Vortex/Diagnostics/DiagnosticsService.*`, `SceneRenderer/SceneTextures.*` and existing extraction/readback owners | Known float region readback has the correct frame/view |
| 9-10 | `Examples/LightBench/LightScene.*`, `MainModule.*`, `LightBenchPanel.*`, `main_impl.cpp`; `Examples/MultiView/MainModule.*`, `SceneBootstrapper.*`, `main_impl.cpp`; `tools/vortex/` | LightBench reset/batch inputs match; every MultiView lit pane passes image/exposure isolation checks |

Unqualified paths in the table's engine rows are under `src/Oxygen/`; shortened
paths after a `Vortex/` entry stay within that module. Avoid creating duplicate
helpers under LightBench for renderer-owned behavior.

### Slice 1 - Freeze contracts and update owning designs

- [x] Update [PBR specification](../../renderer-core/physically-based-rendering.md)
  with equations, units, examples, numeric domain and the complete target behavior.
- [x] Reconcile [physical-lighting roadmap](../../renderer-core/physical-lighting-roadmap.md),
  [panel design](../../renderer-core/post-process-panel-design.md), and repository-root
  [environment authoring](../../../../../design/editor/lld/environment-authoring.md).
- [x] Expand [PostProcessService LLD](../lld/post-process-service.md) with the state
  layout, hybrid solve, lifecycle policies, masks/curve, numerical bootstrap and
  failure behavior in this plan. Include state and frame-sequence diagrams.
- [x] Update [multiview](../lld/multi-view-composition.md),
  [InitViews](../lld/init-views.md), [shader contracts](../lld/shader-contracts.md),
  [scene textures](../lld/scene-textures.md), and environment/lighting LLDs for
  frame-pinned P, sharing and bootstrap format support.
- [x] Fix the supported radiance envelope, operational FP32 bounds and validation
  error budgets against the compiler/format audit. Approve the GPU/asset layouts
  and exact list of dual-format HDR products. Document section 6's regularization
  and unit equations in the owning lighting LLD.
- [x] Register this delivery order in PLAN/status. Create `design/renderer-core/lightbench.md`
  for experiments and instrument requirements.

**Gate:** public behavior is specified by sections 3-7, GPU/asset layouts have
owners, and every active HDR path appears in the domain migration checklist.

Checkpoint evidence (2026-09-16): owning contracts and layouts reconciled;
source-to-consumer HDR inventory recorded; independent arithmetic audit
10/10; existing exposure shader compiled and DXIL inspected under bundled
Debug/Release profiles; owned-document file links and `git diff --check`
passed. Numerical limits are specified, not yet native-qualified. Runtime
qualification begins in slice 2 and remains required. See the
[checkpoint report](exposure-contract-checkpoint.md).

### Slice 2 - Implement normalized settings and fixed exposure

Validated foundation (2026-09-16): canonical settings and atomic per-view
revision retention; cancellation-safe log-target compilation; fixed/camera gain
math; public composition and runtime-publication overrides including update/clear;
frame-retired Exposure/Tonemap inputs; common C++/HLSL state/target layouts.
The 80-byte Auto state carries settings revision and frame sequence. The
FrameExposureData ABI is defined; live pre-exposure publication/consumption and
full mode/lifecycle ownership remain in the following integration work.

Focused build/tests pass 95 CTest entries across Scene exposure settings,
PostProcessService, deferred core, runtime publication and ShaderBakeCatalog.
The independent fixed-gain regression failed before removing the `1e-4` floor.
Twelve native cases pass: EV14/15/16, EV+/-32 supported gain limits, compensation
plus key, physical camera, disabled exposure, invalid revision retention,
locked Auto +/-160 cancellation, and locked large-curve cancellation retaining
a twofold key bias. PixelHistory checks the actual floating-point shader output
before UNorm8 storage (including EV32), alongside uploaded/consumed gain,
80-byte state identity, normalized target records and all Bayer phases.
Separate CDB audits pass all twelve with zero D3D12/DXGI errors or blocking
warnings; only the existing accepted live-factory shutdown warning remains.

Evidence: `out/build-ninja/analysis/vortex/exposure-lightbench/fixed-gain/`
(`evidence-manifest.json`, twelve captures, numeric reports, PNGs and debug
reports), with `contract-audit/slice2-foundation-tests.log` beside it.
The earlier ordinary Auto capture under `autocurve-unsettled*` is retained as
failed settling evidence, excluded from these twelve arithmetic cases. Robust
metering and temporal qualification remain slice 3 work. Reproduction is in the
[VortexBasic README](../../../Examples/VortexBasic/README.md#fixed-exposure-fixture).

The binding/publication increment completes the slice-2 interface gate:
`FrameExposureData` replaces the overloaded ViewColorData record at byte 12 of
the unchanged 64-byte view ABI. All existing readers use the explicit
pre-exposure helper. Post-scene publication retains the pre-scene descriptor.
The publication/runtime/ShaderBake checks pass 45/45; the affected shader bake
packs 196 modules. A native default-scene probe verifies the sky shader reads
the 16-byte record with P=2^-13 and 1/P=8192; its separate CDB audit passes.
Evidence is under `frame-binding/` and
`contract-audit/slice2-frame-binding-tests.log`. The fog publication assertion
was corrected to match the existing opacity output and InvSrcAlpha blend;
production fog blending was not changed.

This does not qualify GPU-owned prior-history P selection, global-state slot
association, S/P integration, unified mode history or full HDR migration. Those
remain in slices 4-5; metering/adaptation and the remaining package gates stay open.

- [x] Implement shared validation/resolution for native settings and view overrides,
  including zero target, coupled ranges and representable gain.
- [x] Remove the fixed-exposure floor and propagate the exact resolved multiplier
  through constants. Add scalar, upload and native GPU regressions.
- [x] Introduce explicit frame exposure bindings and common state definitions,
  with compile-time CPU/HLSL size/layout checks and ShaderBake validation.
- [x] Define native mask/curve/new-setting types for the following runtime slices.

**Gate:** EV14/15/16, boundaries, compensation, keys and disabled exposure reach
the GPU correctly. Invalid settings retain the previous valid revision.

### Slice 3 - Implement robust metering and hybrid adaptation

Validated metering/adaptation core (2026-09-16): bounded cell-centre sampling,
264-word histogram with conserved half-up two-bin weights, integer/fractional
percentile boundaries, profiles and bilinear R masks, content rectangle and
coverage, synthetic dark and invalid-input separation, zero-target restoration,
and exact hybrid response with overflow-safe logarithmic rate/time inputs.
Normalized targets cover the supported radiance domain so a changed histogram
window does not truncate last-valid-meter restoration.

Evidence: 20 native production-pass tests pass in both Debug and Release,
including a 7680x4320 source, maximum mass, narrow percentiles, partial coverage,
mask clamping, content bars, nonfinite samples, 30/60/120 Hz, irregular/long
steps, zero speed/delta, huge rate/distance and tiny-rate/huge-delta arithmetic.
The independent rational/Decimal reference passes 14 checks. The refreshed
foundation suites pass 96/96. The debugger-backed native suite has zero
D3D12/DXGI errors or blocking warnings and 20 accepted live-factory shutdown
warnings (one backend per case). The inspected locked scene capture qualifies
the actual 64-byte pass binding, 1056-byte histogram, 262144 samples,
1073479680 mass at bin 102 and final tonemap consumption.
Reports and capture are under
`out/build-ninja/analysis/vortex/exposure-lightbench/metering/`; build/test logs
are in the adjacent `contract-audit/` directory. `metering/evidence-manifest.json`
records artifact/source hashes and the qualification boundaries.

The subsequent curve/sampling acceptance increment passes 26/26 in Debug and
Release: ordinary Auto curve interpolation, both endpoint clamps, raw-meter EV
independent of EV clamps/adapted history, SpeedDown brightening at 30/60/120 Hz,
moving edges and single-pixel features. The controlled 1024x1 five-stop feature
cases bound sampling error by 5/1024 EV plus the frozen 2e-4 EV histogram
allowance. This measures deterministic aliasing; it is not a universal error
bound for arbitrary scenes. Logs are `contract-audit/slice3-matrix-*-tests.log`.
The expanded debugger-backed run subsequently completed and passed 26/26.
`metering/matrix-debug-layer.json` records zero errors/blocking warnings and
26 accepted live-factory shutdown warnings. The original 20-case audit is
retained separately.

The mask-residency increment implements ResourceKey requests, atomic accepted
settings/mask revisions, pending/failed diagnostics, rejection of nonlinear or
incompatible textures, and exact texture/SRV leases retained through frame-slot
retirement. Manual, disabled and locked-range Auto bypass inactive-mask
residency. Pending initial masks suppress metering; replacement failures keep
the prior accepted revision. Native tests prove cooked-mask upload and exact
quantized mass, as well as locked S=.25 through the service and tonemap at dt0
for pending/failed masks with/without previous settings.

Qualification passes 29 native cases in both Debug and Release, 134 focused
Debug CTest entries, and the Release PostProcessService (16) and TextureBinder
(27) suites. The three added native mask cases pass a separate debugger audit,
with zero errors/blocking warnings and three accepted factory shutdown warnings.
Evidence is `contract-audit/slice3-mask-final-*`, the service/binder Release logs,
and `metering/masks-debug-layer.json`; the manifest preserves earlier increments.

The percentile portability correction passes 30 native tests in Debug and
Release, including narrow fractional CDF boundaries on both sides of the
32-bit word shift. Four 16-bit partial products replace native 64-bit ALU.
Both compiled solve disassemblies now require descriptor-heap indexing without
Int64ShaderOps. A four-case debugger run passes with no errors/blocking warnings;
the refreshed `portable-meter` capture passes histogram and final-consumption
checks. Its PNG is byte-identical to the inspected locked-meter image.
Evidence: `contract-audit/slice3-portable-*`, the before/after solve disassemblies,
and `metering/portable-*`.

The exact Spot centre correction passes 34 native cases in Debug and Release.
Analytic profile distance is computed from integer cell-centre offsets, keeping
the true centre at zero for tiny positive radii as well as radius zero. The
new radius=1e-20 case failed with zero mass before correction and now retains
4095. Additional cases qualify dark subcategories, all-nonfinite EV0 fallback,
continued positive latent adaptation under displayed zero, fixed curve input
for locked Auto, and zero SpeedDown. The six-case debugger audit passes with
zero errors/blocking warnings and six accepted factory shutdown warnings.
Evidence: `contract-audit/slice3-small-spot-before.log`,
`contract-audit/slice3-spot-final-*` and `metering/edge-debug-layer.json`.

The subnormal-curve audit reproduced a one-stop error: at exact measured EV0,
keys +/-1e-40 with values +/-1 yielded gain .5 rather than 1. Interpolation now
uses coordinates scaled by 2^64, reconstructing subnormal mantissas from bits
before scaling. The regression includes the smallest positive binary32 key.
All 35 native cases pass in Debug and Release; the five-case curve debugger
audit has zero errors/blocking warnings and five accepted factory warnings.
Evidence: `contract-audit/slice3-curve-subnormal-*` and
`metering/curve-debug-layer.json`.

The public pause route is now qualified: normal frame start, standalone frame
execution, single-pass/render-graph materialization and offscreen validation
accept finite nonnegative game delta. Negative/NaN/Inf remain invalid. Both
paused facade tests failed before correction; all 36 focused facade/materializer
CTest entries now pass in Debug and Release. A separate native public-frame
session case passes in both configurations and under the debugger: target gain
changes while latent gain remains exactly unchanged at dt0. Its audit has zero
errors/blocking warnings and one accepted factory shutdown warning. Evidence:
`contract-audit/slice3-pause-*` and `metering/pause-debug-layer.json`.

This closes the slice-3 metering/adaptation gate: the 35-case numerical matrix,
the separate native public-pause case, resource/setting tests, compiler audits
and inspected scene captures have passed their specified scopes. Lifecycle,
sharing and unified mode history remain slice 4 work; this does not qualify
scene-integrated HDR migration or later package acceptance.
Full GPU P routing/upstream range qualification remain slice 5; cooked mask
persistence belongs to slice 6.

The user approved correcting the mask identity contract on 2026-09-16: reuse
cooked source-local texture indices, PAK remapping and runtime ResourceKey.
Offset 116 stores the uint32 texture index and 120..131 are zero-reserved;
the fixed record prefix remains 144 bytes. The canonical runtime mask field
now uses ResourceKey with the loading integration. No UUID lookup layer or new texture asset type is introduced.

- [x] Implement bounded stratified sampling, conserved two-bin weights, percentiles,
  finite/black/coverage rules and overflow-safe counts.
- [x] Implement mask sampling and exact piecewise-linear compensation curves.
- [x] Implement the analytic hybrid log-gain update, clamped targets, zero speeds,
  equal EV bounds, zero target and positive-target restoration.
- [x] Add independent histogram and adaptation reference tests plus native GPU
  captures using existing fixtures; vary frame schedules at equal elapsed time.

**Gate:** weighted distributions, targets and adaptation trajectories match
independent expectations; 8K output cannot overflow histogram accumulation.

### Slice 4 - Implement one exposure history and complete lifecycle

Generation allocation was decided by the user on 2026-09-16: Renderer issues
64-bit generations. `QueueExposureTransition(handle, policy, seed)` returns a
request token; `RetryExposureTransition(token)` resubmits the same identity
without allocating another generation. Implicit resets share that counter.
Submission and GPU application remain separate, and settings/mode/ownership
validation happens at the frame boundary as specified in section 4.1.

The control foundation is implemented and CPU-qualified: renderer-issued
queue/retry/status tokens with lifetime separation, syntax/conflict validation,
idempotence and shutdown rejection; explicit seed log-gain resolution outside
metering bounds; and recorder submission inspection using the existing command
list. Scene exposure tests pass 19/19 and Commander tests 37/37. The rebuilt
focused control/regression run passes 56/56. Evidence is
`contract-audit/slice4-seed-tests.log`, `slice4-submission-inspection-tests.log`
and `slice4-control-rebuilt-tests.log`. An earlier broad filter selected a stale
PostProcessService test executable after Renderer layout changed; rebuilding
that dependent target removed the teardown failure.

Unified GPU mode history and explicit transition application are implemented.
Each solve reads an immutable prior record, publishes a new pooled record only
after submission, and retains its resources through frame-slot retirement.
Manual, ManualCamera, Auto and disabled use the same GPU record at tonemap.
Stateless views and temporary diagnostics use transient records. Generation
retries, pending remeter, seed/Preserve event frames, mode changes, submission
failure and bounded asynchronous acknowledgements have controlled-input tests.
Frame-captured requests exclude later submissions until the next frame; stale
completions cannot consume newer intent or reused view lifetimes.

The expanded native suite passes 54/54 in Debug and Release, plus 54/54 under
CDB with no D3D12 errors or blocking warnings. Rebuilt CPU regressions pass
41/41 in each configuration (Debug combines 39 entries and two suite entries).
The diagnostics-ledger fixture explicitly enables its asserted feature in both
configurations. Inspected Auto160 and Manual EV32 captures prove distinct
prior/output state resources, the 112-byte solve ABI and final GPU-state
consumption; the Auto histogram mass is exactly 1,073,479,680. Evidence and
commands are in `lifecycle/unified-state-manifest.json` under the package's
analysis directory.

The runtime registry now validates source edges atomically, resolves chains to
one root, rejects duplicate persistent-handle ownership and stateless sharing,
retains inactive source chains referenced by active consumers, and detaches all
affected relationships on removal. Its 18 publication tests pass in Debug and
Release, including six new ownership cases; evidence is
`lifecycle/ownership-registry-manifest.json`. This qualifies CPU routing only.

Rendered-view frame capture now pins accepted settings, mode, mask lease and
transition semantics before scene rendering. Family priming also covers views
without resolved cameras; late scene/settings/mask changes become eligible on
the next frame. Stateless captures remain isolated by logical view and retain
no prior-frame settings. The expanded native suite passes 56/56 in Debug and
Release; service tests pass 19/19 and renderer regressions pass 40/40 entries in
each configuration. Four focused CDB cases have no blocking graphics messages.
Evidence is `lifecycle/frame-capture-manifest.json`.

The prior-frame sharing core is implemented with source-owned updates, immutable
prior snapshots, source-defined initialization fallback and inactive-source
settings capture. Borrowers copy gain without adopting another image's metered
EV, and reject consumer transitions through completed GPU status. Controlled
native tests cover both render orders, one-frame reset latency, all source modes,
seed/bounds/zero-target fallback, explicit detach/remeter, inactive-source seed
ownership and failure-safe fallback. Debug and Release each pass 62/62 native,
20/20 service and 19/19 publication tests. Six sharing CDB cases have no blocking
graphics messages. Evidence is `lifecycle/prior-sharing-manifest.json`.
Readback backpressure now retains one coalesced latest submitted status alongside
the three pending copies. Frame-start retry completes acknowledgements without
another owner render; old completions cannot consume newer intent. Submission
observation remains distinct from GPU application, and runtime removal retires
queued lifetimes even before renderer/GPU state exists. The native suite passes
64/64 in Debug and Release, service/publication suites pass 20/20 each in both,
and four focused debugger cases have no blocking graphics messages. The final
submission-observation guard also passes the four affected Debug cases. Evidence
is `lifecycle/status-retry-manifest.json`.
Registered inactive owners now validate captured settings/mode and ownership at
the pre-render boundary. Never-submitted invalid requests stay rejected across
later settings or ownership changes; already-submitted requests await GPU
completion, and diagnostics defer pending intent. Rejected dispositions are
carried into later GPU records without application. Debug and Release each pass
68/68 native tests, 20/20 service tests, 20/20 publication tests and 39/39 renderer
regressions. Four inactive-owner CDB cases have no blocking graphics messages.
Evidence is `lifecycle/inactive-control-manifest.json`.
Automatic detachment now issues a renderer generation, remeters Auto at zero
delta, applies fixed modes, honors an unsubmitted explicit policy and defers
diagnostics. Reattachment before capture cancels the pending event. Settings,
mask leases and GPU records carry view lifetime identity; handle replacement
retires the old owner after validation and outside the registry lock. The public
H1-to-H2-to-new-owner-H1 regression covers queued/applied history, mask ownership,
stateless replacement and retained GPU readers. Debug and Release each pass
75/75 native, 21/21 service, 21/21 publication and 39/39 renderer regression tests.
Nine focused debugger cases have no blocking graphics messages. Evidence is
`lifecycle/detach-lifetime-manifest.json`.
Source-destruction continuity is implemented with captured source definitions,
retained GPU publications and per-consumer delivery. Auto preserves one displayed
and positive latent-gain frame, including borrowed zero; subsequent invalid
metering uses positive latent gain. Fixed/zero-target/explicit policies retain
their precedence. Never-rendered source fallback, chain detachment, diagnostic
siblings and retired consumers have native coverage. The exact borrowed state
selected after a failed consumer copy is retained separately from successful
solve history, without importing source request identity or acknowledging failed
work. Debug and Release pass 81/81 native, 21/21 service, 21/21 publication and
39/39 renderer regressions. Six focused debugger cases have no blocking graphics
messages. Evidence is `lifecycle/source-loss-manifest.json`.
Implicit camera/world/device events and the remaining slice-4 integration/capture
gates remain open; slice 4 is not qualified.
Early GPU P selection, S/P integration and scene-integrated validation remain
slice 5 work.

- [x] Update the same GPU state in Manual, ManualCamera, Auto and disabled modes.
- [ ] Add public per-view transitions and request generation handling, including
  recording/submission failure, invalid metering and idempotent retries.
- [ ] Implement the policies in section 4.1, including exact seed event-frame
  behavior and manual-to-auto continuity by retaining gain.
  Remaining lifecycle coverage must include implicit camera/world/device events.
  Source-destruction continuity, default detach, lifetime-safe history selection,
  inactive-owner validation and acknowledgement after readback backpressure are
  covered by the checkpoints above.
- [ ] Implement source-owned updates, pinned prior generations, root-source
  resolution, cycle rejection, inactive-source retention and bootstrap fallback.
- [ ] Implement stateless transient state, recovery events, frame-safe uploads,
  synchronization and fence retirement using controlled float-input fixtures.
- [ ] Exercise native game-facing producers without DemoShell. Wire DemoShell's
  reset to the same public API.

**Gate:** state-machine, request, ownership and publication tests pass against
controlled float inputs, including both shared-view execution orders. GPU state
identifies the applied generation and settings revision. Slice 5 supplies and
validates the scene-integrated high-range route for startup, cuts, stateless
views and recovery.

### Slice 5 - Complete pre-exposure migration and numerical recovery

- [ ] Add the early GPU P resolve and bind a frame-invariant P/1P to every HDR pass.
- [ ] Migrate the entire section 4.4 checklist, including atmosphere producer/
  consumer pairs, fog/color histories, bloom and offscreen/capture domains.
- [ ] Meter with 1/P and tonemap with S/P in every mode, including disabled and
  zero-target cases. Remove obsolete manual-exposure cancellation paths.
- [ ] Add transient FP32 bootstrap/remeter products and matching PSO/resolve
  format keys. Keep exposure history through FP32-to-FP16 transition.
- [ ] Add saturation detection and the FP32 recovery event. Verify the supported
  scene-radiance envelope at upstream intermediates as well as SceneColor.
- [ ] Run the complete lifecycle matrix through real scene rendering, including
  startup, cuts, stateless views, source loss, delayed acknowledgment and recovery.
- [ ] Run the MultiView main/lit-PiP, independent and shared-exposure cases from
  section 7.5 before integrating further bench work.

**Gate:** varying numerical P leaves scene-referred measurements and final
output invariant within precision budgets. Startup/cuts preserve bright and
dark meter signals; all scene-integrated lifecycle cases pass. No active HDR
path consumes the old overloaded scalar.

### Slice 6 - Finish authoring, serialization and configuration isolation

- [ ] Include native aperture/shutter/ISO source/cook/load persistence, as
  approved on 2026-09-16. Scene-v6 perspective/orthographic records are 32/40
  bytes; v5 20/28-byte records hydrate 11/125/100 defaults. Existing editor
  adapters preserve the fields; physical-camera editor controls stay deferred.
- [ ] Update source JSON schemas, scene component/config types, versioned packed
  records, cooker, loader, scripting and existing editor/native adapters.
- [ ] Round-trip mask resource references, curve keys, black influence, D and
  every existing exposure field; preserve enum ordinals and old-record defaults.
- [ ] Add async mask residency/error behavior and atomic settings revision changes.
- [ ] Update DemoShell controls and labels; expose requested/effective exposure
  separately and show resource/metering failures.
- [ ] Add the LightBench experiment-owned activation policy so saved settings
  cannot override its camera, scene or post-process recipe.

**Gate:** source -> cook -> load -> runtime and save/reload retain identical
resolved settings. Old assets load deterministically. Batch runs do not mutate
personal settings.

### Slice 7 - Complete the reference lighting unit chain

- [ ] Correct common point/spot flux-to-intensity conversion and inverse-square
  distance behavior, including smooth-cone normalization and finite near field.
- [ ] Apply the shared physical contract to active forward/deferred consumers.
- [ ] Verify neutral directional, point and spot units against an independent
  integration/reference calculation; include range fade and cone boundaries.
- [ ] Establish the production material and color-space mapping used by every
  bench oracle, including packed specular, normal and albedo values.

**Gate:** all three required light experiments have numerical expectations
and passing unit/BRDF contracts. Required calibration is not left as a dependency
for a separate workstream.

### Slice 8 - Implement and qualify measurement instrumentation

- [ ] Implement the diagnostics contracts in section 7.3 using existing extraction
  and asynchronous readback facilities.
- [ ] Validate known GPU signals, pre-exposure conversion, actual consumed gain,
  zero samples, partial coverage and invalid/nonfinite data.
- [ ] Verify frame/view/experiment association under delayed readbacks and changes.
- [ ] Verify zero disabled-path GPU work and bounded enabled-path resources.

**Gate:** instrumentation matches independent known inputs before it reports
bench verdicts.

### Slice 9 - Finish LightBench and MultiView visual behavior

- [ ] Implement schema-validated experiments from section 7.1 through one controller.
- [ ] Stage/reset complete configurations and temporal state; supply a clear,
  consistently framed Neutral Reference at startup.
- [ ] Implement focused controls, useful overlays, measurements and explicit verdicts.
- [ ] Repair LightBench's visual scene composition and exercise every experiment
  interactively against section 7.4, including its exposure-transition sequences.
- [ ] Convert the indoor preset and provide explicit saved-experiment loading.
- [ ] Extend MultiView's existing controls/fixtures for independent and shared
  exposure, per-view overrides, and lifecycle sequences. Fix affected demo
  framing, composition and exposure behavior until section 7.5 passes visually.
- [ ] Create `Examples/LightBench/README.md` with actual launch/run/reset/save
  instructions, supported tests and interpretation.
- [ ] Update `Examples/MultiView/README.md` with the exposure scenarios, expected
  shared latency, intentional black cells and repeatable visual-check commands.

**Gate:** LightBench works as a readable, interactive exposure benchmark with
reproducible reset. MultiView's ordinary and proof layouts pass the visual and
exposure-isolation scenarios. Capture both applications' native presented output.

### Slice 10 - Automate the same experiments and close the package

- [ ] Add deterministic experiment selection and batch execution using the
  interactive controller/definitions and existing capture CLI.
- [ ] Implement `tools/vortex/Run-LightBenchValidation.ps1` and a schema-validated
  result report; include native game-facing and multiview lifecycle cases.
- [ ] Extend the existing MultiView proof tools with per-view exposure/image
  comparisons and scripted interactions from section 7.5. Include their result
  manifests in the package's acceptance report.
  Extend `Run-VortexMultiViewValidation.ps1`, its existing analyzer/assertions
  and result schema; structural stage-count checks remain alongside the new
  visual and exposure checks.
- [ ] Record resolved parameters, experiment/version/revision, build/shader identity,
  device/backend, actual dt, frames, validity, tolerances, measurements and captures.
- [ ] Run the entire acceptance matrix and update the owning docs and status.

**Gate:** every required feature and experiment passes. Failed or unsupported
required cases block package completion.

## 9. Acceptance matrix and execution

| Area | Required cases |
| --- | --- |
| Fixed exposure | EV14/15/16 and supported limits; keys/compensation; invalid input; disabled; CPU/HLSL propagation |
| Metering | Known distributions; two-bin weight conservation; percentiles; masks/profiles; partial coverage; mixed/all black; zero mask; nonfinite samples; tiny and 8K outputs |
| Curves | Empty, single key, endpoints, interpolation, out-of-range clamp, malformed keys, raw-EV input independent of adapted gain |
| Adaptation | Both directions; linear/exponential crossing; 30/60/120 Hz and irregular dt at equal elapsed time; pause; zero speed; long dt; no overshoot |
| Lifecycle | First valid frame; seed frame and out-of-meter-range seeds; cuts; changed settings; manual/auto; zero target precedence/restoration; invalid meter; retries; view destruction; device recovery; stateless Auto |
| Sharing | Both render orders; contrasting views; source startup/inactivity/reset; source destruction and consumer-owned fallback transition; cycle rejection; multiple frames in flight |
| HDR domains | P invariance; FP32 bootstrap and delayed/stale status acknowledgment; bright/dark endpoints; opaque, emissive, forward, translucency, sky/AP/fog, bloom, capture and reused histories |
| Authoring | Schema boundaries; old/new packed records; cook/load/save/reload; mask pending/failure; curve round-trip; experiment-owned activation |
| Calibration | Directional lux; point inverse square; spot flux normalization; near-field finite behavior; range/cone edges; production BRDF |
| Instruments | Known float signals; actual consumed gain; stale results; invalid regions; zero disabled cost |
| LightBench visual benchmark | All seven experiments, clean startup, full reset, saved experiments, identical interactive/batch inputs, readable native output and correct interactive exposure behavior |
| MultiView visual integration | Ordinary lit main/PiP plus standard, auxiliary, offscreen and feature layouts; standalone/family equivalence, per-view isolation, intentional sharing, resize/reorder/lifecycle, stable UI/backgrounds |

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

Run native D3D12 with the debug layer for GPU-output and timing behavior.
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
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.ExposureGpu.Tests oxygen-graphics-direct3d12 --parallel 4
./out/build-ninja/bin/Debug/Oxygen.Vortex.ExposureGpu.Tests.exe
cmake --build out/build-ninja --config Release --target Oxygen.Vortex.ExposureGpu.Tests oxygen-graphics-direct3d12 --parallel 4
./out/build-ninja/bin/Release/Oxygen.Vortex.ExposureGpu.Tests.exe
ctest --preset test-debug -R 'ExposureSettings|PostProcessService|SceneRendererDeferredCore|RuntimeViewPublication|ShaderBakeCatalog' --output-on-failure
./out/build-ninja/bin/Debug/Oxygen.Examples.VortexBasic.exe --validation-scene exposure-locked --validation-exposure-ev=160 --frames 20 --fps 10 --vsync false --debug-layer true --capture-provider renderdoc --capture-load search --capture-from-frame 10 --capture-frame-count 1 --capture-output out/build-ninja/analysis/vortex/exposure-lightbench/metering/locked-meter -v=-1
./tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath out/build-ninja/analysis/vortex/exposure-lightbench/metering/locked-meter_capture.rdc -UiScriptPath tools/vortex/AnalyzeRenderDocExposureMeter.py -PassName Auto160 -ReportPath out/build-ninja/analysis/vortex/exposure-lightbench/metering/locked-meter-analysis.txt -AnalysisTimeoutSeconds 60
```

Slice 10 implements the acceptance runner interface:

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
- New documents: `design/renderer-core/lightbench.md` and
  `Examples/LightBench/README.md`.
- `Examples/MultiView/README.md` and multiview validation tools: visual exposure
  scenarios, per-view comparisons and reproducible interaction sequences.
- Repository-root editor authoring/API documents: new authored fields and
  versioned round-trip behavior; no general UI redesign.
- Reports/captures: `out/build-ninja/analysis/vortex/exposure-lightbench/`.

UE5.7 source reference root: `F:/Epic Games/UE_5.7/Engine`.

| Reference | Use |
| --- | --- |
| `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp` | Target/current exposure, force-target conditions, normalization and pre-exposure |
| `Source/Runtime/Renderer/Private/SceneRendering.cpp::ShouldUpdateEyeAdaptationBuffer` | Owner/borrower update boundary |
| `Shaders/Private/PostProcessHistogramCommon.ush` | Log histogram reduction and hybrid EV response |
| `Shaders/Private/Histogram.usf` | Interpolated bins and black weighting |
| `Shaders/Private/PostProcessTonemap.usf` | Pre-exposure removal and final gain application |
| `Source/Runtime/Engine/Private/Components/LocalLightComponent.cpp` | Explicit distance and lumen/candela conventions |

Oxygen retains its calibration key, analytic metering profiles and
previous-frame sharing. It uses a compact curve-key buffer, deterministic bounded
sampling, exact hybrid integration, and a targeted FP32 bootstrap. These choices
serve the specified behavior without adopting UE's legacy compatibility paths.

## 11. Completion criteria

- [ ] Fixed/manual-camera/Auto/disabled exposure use one consistent state contract.
- [ ] Hybrid EV/s adaptation, masks, curves, black handling and zero target work.
- [ ] All lifecycle and shared/stateless policies are implemented and tested.
- [ ] Every active HDR path uses frame-pinned P and final S/P consistently.
- [ ] Bootstrap and numerical recovery preserve valid bright/dark metering signals.
- [ ] Authored fields round-trip through all active persistence surfaces.
- [ ] Directional, point and spot reference units and material expectations pass.
- [ ] LightBench is a properly repaired, visually useful exposure benchmark;
  all seven experiments pass numerical, interactive and visual acceptance.
- [ ] MultiView succeeds visually in ordinary and proof layouts; multiple views
  do not break exposure, and exposure changes do not break rendering/composition.
- [ ] Independent measurements and the complete acceptance matrix pass.
- [ ] Owning documents and operational instructions describe the implemented behavior.
