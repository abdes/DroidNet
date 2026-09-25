# Exposure model and integration

This document owns exposure equations, metering, HDR domains and calibrated-light integration. [PostProcessService](post-process-service.md) owns runtime state, APIs and lifetime; [SceneTextures](scene-textures.md) owns allocation and formats.

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
per-product narrowing checks in [SceneTextures](scene-textures.md).
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
Scene version 7 is the current packed format. Producers and consumers use
one layout; the exposure prefix introduced by the earlier v6 migration remains
part of that format. Reject older sources/assets with an actionable migration/re-cook error;
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
Primary. The [editor rendering contract](editor-rendering.md#3-independent-directional-array-and-atmosphere-assignments)
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

## Exposure architecture references

Status: `reference`

Updated: 2026-09-20

The [exposure and LightBench implementation plan](../milestones/exposure/README.md)
owns delivery order and acceptance. This design owns the exposure algorithms. LightBench is the
visual exposure benchmark; native MultiView visual/exposure correctness is a
mandatory delivery gate.

## 1. Canonical contracts

| Subject                                             | Owner                                                                                                                                                                                                    |
| --------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Exposure equations and hybrid response              | [PBR specification](../../renderer-core/physically-based-rendering.md); runtime state/layouts in [PostProcessService LLD](post-process-service.md)                                                       |
| View identity, transitions, sharing and HDR domains | [Exposure design section 4](#4-runtime-lifecycle-and-hdr-domains); [multiview LLD](multi-view-composition.md) owns the public view boundary                                                              |
| Histogram, masks, curves and authoring              | [Exposure design section 5](#5-robust-metering-and-authoring)                                                                                                                                            |
| Physical-light reference                            | [Exposure design section 6](#6-calibrated-light-and-material-reference); [PBR specification](../../renderer-core/physically-based-rendering.md) owns engine units                                        |
| LightBench and MultiView behavior                   | [Exposure design section 7](../milestones/exposure/validation.md#lightbench-and-multiview-acceptance)                                                                                                    |
| Numerical qualification                             | Existing native fixtures/readbacks and independent references; [LightBench](../../renderer-core/lightbench.md#independent-measurement) defines the focused calibration check. No new runtime instrument. |
| Compiler/format checkpoint                          | [Audit report](../milestones/exposure/EX01/validation.md), [HDR inventory](scene-textures.md#exposure-hdr-domain-and-format-inventory), per-view eligibility independent of gain validity                |
| Performance controls and execution                  | [EX051 scopes and benchmark matrix](../milestones/exposure/EX05.1/README.md#tasks-and-outcome); [FP32-only control](post-process-service.md#slice-51-fp32-baseline-and-precision-policy)                 |
| Fallback allocation ownership                       | [EX051-10A SceneColor lease contract](scene-textures.md#ex051-10a-independent-scenecolor-fallback-ownership)                                                                                             |
| Sequence and tests                                  | [Implementation slices](../milestones/exposure/README.md#source-entry-points) and [acceptance matrix](../milestones/exposure/validation.md#qualification-protocol)                                       |

## 2. UE5.7 source map

Reference root: `F:/Epic Games/UE_5.7/Engine`.

| Source / symbol                                                                                        | Relevant behavior                                       |
| ------------------------------------------------------------------------------------------------------ | ------------------------------------------------------- |
| `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp::GetEyeAdaptationParameters` | Ranges, compensation, force-target and speed parameters |
| `PostProcessEyeAdaptation.cpp::LuminanceMaxFromLensAttenuation`                                        | Scene-luminance normalization                           |
| `PostProcessEyeAdaptation.cpp::AddHistogramEyeAdaptationPass`                                          | Owner/borrower handling and buffer publication          |
| `PostProcessEyeAdaptation.cpp::FViewInfo::UpdatePreExposure`                                           | Per-view pre-exposure and final-composition consistency |
| `Source/Runtime/Renderer/Private/SceneRendering.cpp::FViewInfo::ShouldUpdateEyeAdaptationBuffer`       | Owner updates and borrower bootstrap exception          |
| `Shaders/Private/PostProcessEyeAdaptation.usf::EyeAdaptationCommon`                                    | Target/current exposure and metered-luminance outputs   |
| `Shaders/Private/PostProcessHistogramCommon.ush::ComputeEyeAdaptation`                                 | Linear-to-exponential response in log space             |
| `Shaders/Private/Histogram.usf::CalculateBucketsAndWeights`                                            | Interpolated bins and black-bucket influence            |
| `Shaders/Private/PostProcessTonemap.usf`                                                               | Pre-exposure removal and global gain                    |

## 3. Deliberate Oxygen choices

| Area                      | Choice and reason                                                                                                                                                                                                                       |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Calibration               | Preserve the accepted key/EV equation and explicit client defaults; compare equivalent effective bias with UE                                                                                                                           |
| Ownership                 | Producers own persistent view identity; Renderer Core validates/routes; PostProcessService owns GPU exposure state                                                                                                                      |
| Authority                 | GPU gain and frame-pinned exposure binding; nonblocking CPU status controls resource formats, not the numerical gain                                                                                                                    |
| Camera cuts               | Remeter by default; explicit Preserve and Seed support gameplay/cinematic intent                                                                                                                                                        |
| Shared views              | One owner and prior-generation readers give scheduling independence, at the documented one-frame latency                                                                                                                                |
| Adaptation                | Exact linear/exponential crossing integration gives consistent trajectories across frame schedules                                                                                                                                      |
| Histogram                 | 256 bins and a bounded normalized-view grid retain current bin resolution while bounding cost and accumulation                                                                                                                          |
| Mask/curve                | Scalar texture multiplied by analytic weights; at most 64 curve keys evaluated once per view                                                                                                                                            |
| HDR precision             | FP32 SceneColor accumulation in both modes; checked conversion into existing resolved color, FP16 when suitable and FP32 during recovery; other qualified radiance products remain dual-format; no generic precision-management service |
| FP32 performance baseline | EX051-04 adds FP32 storage with P=1 and normal exposure/history/events, omitting FP16-admission work. The existing format-only `fp32` control still executes certification and remains a numerical diagnostic.                          |
| Precision economics       | EX051-05 uses four production/FP32-only pairs. EX051-09 selects the operating policy from those results and specifies admission/retry transitions.                                                                                      |
| Fallback retention        | EX051-10A replaces whole-family retention with an independent SceneColor lease; unrelated attachments remain reusable while delayed color consumers retain their input.                                                                 |
| Validation                | Production renderer paths, independent expected values, readable LightBench scenes and native MultiView visual isolation                                                                                                                |

UE's borrower-bootstrap exception, generic curve infrastructure, legacy exposure
modes, local exposure, mobile permutations and a second Basic metering algorithm
are not part of this global-exposure product.

## 4. Integration reminders

Use the [slice file map](../milestones/exposure/README.md#file-entry-points)
for concrete implementation entry points.

- Migrate atmosphere LUT producers and sky/AP consumers together. Convert temporal
  RGB by its stored P; leave transmittance, coverage, depth and normals unscaled.
- Keep transient view events and GPU history out of packed scene records.
  Version authored fields and retain deterministic old-record defaults.
- Treat DemoShell's saved-settings reapplication as a separate activation policy
  from environment override. LightBench owns its experiment inputs.
- MultiView uses the same public view contracts and measurement service. Compare
  isolated per-view outputs before checking final composition.
- Extend existing capture/assertion tools; do not introduce another validation
  framework for the demos.
