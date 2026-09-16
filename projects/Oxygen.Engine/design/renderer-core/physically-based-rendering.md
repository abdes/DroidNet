# Physically based rendering in Oxygen

Updated: 2026-09-16. Mathematical specification; implementation and acceptance
are tracked in the [exposure delivery plan](../vortex/plan/exposure-and-lightbench-correction.md).

## Units and working color

World distance is metres. Linear working RGB uses Rec.709/sRGB primaries and
D65 white. Luminance is `dot(rgb, (0.2126, 0.7152, 0.0722))`. Decode color
textures before lighting; normals, metalness, roughness and mask samples are
data. Output retains the existing tone curves, DisplayGamma and target encoding.
Exposure is not a color-space conversion.

| Quantity | Unit / meaning |
| --- | --- |
| Directional light / Sun | Lux, illuminance on a perpendicular receiver |
| Point / spot light | Lumens, total flux converted to candela |
| Sky / emissive / calibrated IBL | Scene-referred linear radiance, calibrated to nits |
| EV100 | Logarithmic camera/meter quantity in stops |
| Calibration key | Dimensionless bias normalization; 12.5 is not an EV |
| Global exposure S | Linear displayed gain |
| Pre-exposure P | Positive numerical storage scale |

The Sun's lux value also drives atmosphere illumination. Its optional visible
disk luminance derives from its solid angle; it is not another light. Sky tint
and luminance apply consistently to visible sky and IBL.

## Fixed and camera exposure

Preserve Oxygen's accepted calibration:

```text
log_bias = compensation_ev + log2(exposure_key) - log2(12.5)
S_manual = 2^(log_bias - ev100)
camera_ev100 = 2*log2(aperture_f) + log2(shutter_rate)
               + log2(100) - log2(iso)
```

Camera inputs and key must be finite and positive. Validate the complete
settings revision atomically. Evaluate in log space to avoid intermediate
overflow. Invalid revisions retain the previous valid revision; no arbitrary
floor may replace a valid fixed gain. Manual modes ignore the automatic curve.

At key 12.5 and compensation zero, EV14/15/16 give `2^-14`, `2^-15`, `2^-16`.
Input 4096 becomes 0.25, 0.125, 0.0625 before output mapping. The f/11,
1/125 s, ISO100 camera gives EV100 `13.884647521936682`. DemoShell's Manual
EV9.7 is a client default, not that camera's calculated EV. Scene creation
retains Auto, key 10, speeds 3/1 EV/s, bounds [-6,16] and target 0.18.

## Automatic target and compensation curve

For valid positive percentile-trimmed geometric-mean scene luminance L:

```text
metered_ev = log2(L) - log2(0.18)
bounded_ev = clamp(metered_ev, min_ev, max_ev)
curve_ev = curve(metered_ev)
log_target = log2(target_luminance) - log2(0.18) - bounded_ev
             + compensation_ev + curve_ev + log2(exposure_key) - log2(12.5)
S_target = 2^log_target
```

Curve keys are finite `(metered_ev100, compensation_ev)` pairs, strictly
increasing in EV, at most 64. Empty means zero; one key is constant; interpolate
linearly and clamp to endpoint values outside the interval. Use raw metered EV,
independent of adaptation. Equal min/max EV uses that bound for denominator and
curve and resolves immediately without a histogram. An explicit seed owns its
event frame before the locked solve.

The runtime may compile this piecewise-linear log-target function into bounded
knots at authored curve coordinates and clamp/supported-domain boundaries. Combine all
opposing compensation/EV terms with compensated arithmetic before exponentiation
or float32 upload. GPU interpolation remains a function of raw metered EV.
Do not first reconstruct a linear biased target or luminance bound: e.g. Auto
compensation=min_ev=max_ev=160 at key 12.5 has valid unit gain despite `2^160`
overflowing float32. A key of 25 must retain its additional one stop even when
authored compensation and a constant curve contain opposing `1e20` values.

Target zero immediately displays black. Continue metering and maintain positive
latent gain using nominal target 0.18. Never log or invert displayed zero.
Restoring a positive target solves immediately from valid current metering,
otherwise last valid metered EV, otherwise initialization. Disabled exposure
publishes S=1. The [runtime LLD](../vortex/lld/post-process-service.md) owns
transition precedence and invalid-meter handling.

## Metering

Use 256 logarithmic bins and a stable normalized-content stratified grid of
`min(width,512)*min(height,512)` samples at cell centres. Do not area-average HDR
before binning. Weight by Average/CenterWeighted/Spot, optional bilinear
clamp-sampled linear mask R, and coverage. Absent mask means one.

Coverage follows tonemapping: saturated alpha with display background enabled,
otherwise one. For positive coverage unpremultiply RGB and divide by P before
calculating luminance. Zero coverage contributes no mass. Physical sky is scene
signal; UI, display background and letterbox bars are excluded.

Quantize combined weight Q to [0,4095] using `floor(weight*4095+0.5)`. For continuous bin position x, scatter
`floor(Q*frac(x)+0.5)` to the upper bin and the remainder to the lower. Clamp both
indices at 255. Maximum integer mass is 1,073,479,680, safe in uint32 at 8K.
Percentile trimming retains fractional boundary-bin mass, requires positive
retained mass, and averages log-bin positions to form the geometric mean.

Finite luminance at/below the lower window bound is the dark bin. Default black
influence zero excludes it in mixed scenes. Apply influence to each classified
dark sample as `floor(Q*influence+0.5)`; never attenuate brighter samples whose
interpolated lower-bin mass lands in bin zero. Count exact black, positive below
window, finite, positively weighted and rejected samples separately. All finite,
positively weighted samples in the dark bin give a valid synthetic min-EV solve;
report luminance below-window, not an exact measurement. Zero weight, missing
input or no finite samples is invalid. NaN/Inf/range loss is never valid black.

## Exact hybrid adaptation

Adapt positive log gain q toward qt. Let r=abs(qt-q), D be finite positive
transition distance (default 1.5 stops), and v the selected maximum EV/s.
SpeedUp applies when qt<q; SpeedDown when qt>q.

```text
if r == 0 or dt == 0 or v == 0: retain q
else:
    t_linear = min(dt, max(r-D,0)/v)
    r = (r-v*t_linear) * exp(-(v/D)*(dt-t_linear))
    q_next = qt - sign(qt-q)*r
```

Use finite nonnegative game delta. Pause freezes ordinary adaptation. No hidden
delta clamp, overshoot or per-frame history clipping. Equal elapsed time under a
constant target produces equivalent trajectories across frame schedules.
Initialization, seeds, manual changes and remeter solves bypass speed and dt.

## HDR domains and numerical limits

Store `C_pre=P*C_scene`, meter `C_pre/P`, apply `S/P` once to foreground and
bloom at tonemapping. Never scale coverage, depth, transmittance or material data.
Scene-referred bloom thresholds become P-scaled thresholds at extraction.
Rescale reused pre-exposed RGB by `P_current/P_stored`. Canonical environment
products stay scene-referred; existing explicit resource normalization is
allowed, but never depends on a view's S.

Slice-1 specified operational domain: exact black plus positive scene RGB/luminance
in [2^-24,2^32], positive displayed/latent gain in [2^-32,2^32], P in
[2^-32,2^32]. Target zero and disabled one retain their separate semantics.
Validate resulting gain over curve segments and bounded EV intervals; validate
seeds separately. These are coupled gain bounds, not independent clamps on
EV/key/compensation. The
[checkpoint](../vortex/plan/exposure-contract-checkpoint.md) records the adopted
format-retention policy. This is a specified domain; native qualification is
required before it can be reported as tested support.

FP32 normal minimum is 2^-126. No dependence on subnormal arithmetic is permitted.
The upper radiance bound includes the Earth-reference 133312-lux, 0.545-degree
solar disk (approximately 1.88e9 nits); 2^24 would incorrectly exclude it.
Scene times gain reaches at most 2^64. Evaluate the existing quadratic tone
curves using reciprocal-polynomial form at large inputs to avoid squaring
overflow while preserving the same curve. Upstream BRDF, integration, coverage recovery, bloom and cumulative
writes must remain finite and respect the resulting-radiance domain. Finite
authored light values alone do not prove this. Detect loss before narrowing.

FP16 normal minimum is 2^-14, positive subnormal minimum 2^-24, maximum 65504.
One P cannot reduce a scene's dynamic-range ratio. Valid exposure history alone
cannot authorize FP32-to-FP16 return; required signals must meet their error
budget. See the checkpoint for the proposed persistent recovery policy.

### Acceptance budgets

Freeze these fixture tolerances before GPU acceptance. They are targets, not
claims of validated hardware behavior.

| Comparison | Budget |
| --- | --- |
| CPU fixed gain | 2e-6 relative; powers-of-two EV14/15/16 exact |
| GPU fixed gain / known float probe | 2e-5 relative + 2^-120 absolute; uploaded powers of two exact |
| Histogram integer mass/counts | Exact |
| Discretized histogram oracle | 2e-4 EV |
| Continuous distribution versus histogram | One bin width plus 2e-4 EV |
| Hybrid schedule equivalence | 5e-4 EV at equal elapsed time; monotone, no overshoot |
| Single normal FP16 store | 2^-10 relative; subnormal absolute error 2^-25 in stored domain |
| P invariance / required float products | 0.5% relative + 2e-5 absolute in the compared scene/output domain |
| Packed material / production BRDF oracle | 2% relative + 2e-5 absolute, interior unoccluded regions |
| UNorm8 image | One code value after independent dither, gamma and target encoding |

Sampling error is separate: compare small bright features and moving edges both
against the exact sample positions and a full-image reference. Report the two
errors separately; experiment-specific region and coverage criteria belong to
LightBench. Failed acceptance is not grounds to widen tolerances.

## Physical-light conversion

Directional lux multiplies the production BRDF and receiver cosine once.
Point candela is `flux_lm/(4*pi)`. For spots with ci=cos(inner), co=cos(outer):

```text
w(theta) = saturate((cos(theta)-co)/(ci-co))^2
omega = 2*pi*((1-ci)+(ci-co)/3)
I_peak = flux_lm/omega
range_fade = saturate(1-(distance/range)^4)^2
distance_factor = range_fade/max(distance^2, 0.001^2)
```

Equal angles use a hard cone and `omega=2*pi*(1-co)`; reject zero solid angle.
Zero separation returns zero before normalizing the light vector. At/beyond
range return zero. The 1 mm guard is numerical, not an area-light model or a
replacement for source-radius BRDF behavior. White calibration sources avoid
tint ambiguity; RGB tint changes luminance by the working weights.

Use the full production BRDF and packed material values. High roughness retains
dielectric specular and is not Lambertian. Include G-buffer quantization and
known range fade in independent references. Integrate the spot profile
independently to verify total emitted flux.

## Qualification

[LightBench](lightbench.md) owns seven reproducible experiments and visual
presentation. Validate fixed gain, trajectories, reset, zero target, light units
and HDR-domain invariance with independent oracles. Native MultiView compares
each view alone and in a family before composition. FPS-dependent adaptation
and broad 10-20% brightness tolerances are not acceptance criteria.
