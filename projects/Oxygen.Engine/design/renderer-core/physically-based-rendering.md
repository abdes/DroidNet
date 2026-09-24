# Physically based rendering in Oxygen

Updated: 2026-09-22. Mathematical specification; implementation and acceptance
are tracked in the [exposure delivery plan](../vortex/plan/exposure-and-lightbench-correction.md).

## Units and working color

World distance is metres. Linear working RGB uses Rec.709/sRGB primaries and
D65 white. Luminance is `dot(rgb, (0.2126, 0.7152, 0.0722))`. Decode color
textures before lighting; normals, metalness, roughness and mask samples are
data. Output retains the existing tone curves, DisplayGamma and target encoding.
Exposure is not a color-space conversion.

The cooked texture format owns its transfer encoding. An sRGB SRV performs the
decode during sampling; UNORM and floating-point SRVs return linear values.
Material evaluation consumes those samples directly. Applying another sRGB
decode would darken tagged sRGB textures and clamp HDR emissive texels before
pre-exposure. This rule applies to both base-color and emissive texture samples;
alpha remains a linear coverage value.

| Quantity                        | Unit / meaning                                      |
| ------------------------------- | --------------------------------------------------- |
| Directional light / Sun         | Lux, illuminance on a perpendicular receiver        |
| Point / spot light              | Lumens, total flux converted to candela             |
| Sky / emissive / calibrated IBL | Scene-referred linear radiance, calibrated to nits  |
| EV100                           | Logarithmic camera/meter quantity in stops          |
| Calibration key                 | Dimensionless bias normalization; 12.5 is not an EV |
| Global exposure S               | Linear displayed gain                               |
| Pre-exposure P                  | Positive numerical storage scale                    |

The Sun's lux value also drives atmosphere illumination. Its optional visible
disk luminance derives from its solid angle; it is not another light. Sky tint
and luminance apply consistently to visible sky and IBL.

For the uniform analytic directional disk of full diameter D>0, use
`L_disk_rgb = E_source_rgb * disk_scale_rgb / (pi*sin(D/2)^2)` before atmospheric
path transmittance. The projected-solid-angle denominator preserves the source's
perpendicular illuminance, including large valid diameters; do not substitute
the unprojected solid angle outside the small-angle approximation. D=0 has no
analytic disk. This does not add finite-source directional surface BRDF/shadows.

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
or float32 upload. GPU interpolation remains a function of raw metered EV. Compare/interpolate
coordinates after scaling by 2^64; reconstruct subnormal coordinates from their
sign and mantissa bits before that scaling. Every possible coordinate spacing
then remains a normal float within the supported EV domain, including keys
straddling exact EV zero. This changes no curve values or gain calibration.
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
clamp-sampled linear mask R, and coverage. Absent mask means one. Compute
analytic profile distance from `(2*cell+1-grid)/grid`; integer centre offsets
preserve exact zero for tiny positive Spot radii without UV-rounding drift.

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

Use finite nonnegative game delta through both the runtime frame entry point
and every renderer facade. Preserve exact zero in the render context; pause
freezes ordinary adaptation. No hidden
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

| Comparison                               | Budget                                                             |
| ---------------------------------------- | ------------------------------------------------------------------ |
| CPU fixed gain                           | 2e-6 relative; powers-of-two EV14/15/16 exact                      |
| GPU fixed gain / known float probe       | 2e-5 relative + 2^-120 absolute; uploaded powers of two exact      |
| Histogram integer mass/counts            | Exact                                                              |
| Discretized histogram oracle             | 2e-4 EV                                                            |
| Continuous distribution versus histogram | One bin width plus 2e-4 EV                                         |
| Hybrid schedule equivalence              | 5e-4 EV at equal elapsed time; monotone, no overshoot              |
| Single normal FP16 store                 | 2^-10 relative; subnormal absolute error 2^-25 in stored domain    |
| P invariance / required float products   | 0.5% relative + 2e-5 absolute in the compared scene/output domain  |
| Packed decoding / same-model BRDF oracle | 2% relative + 2e-5 absolute, interior unoccluded regions           |
| UNorm8 image                             | One code value after independent dither, gamma and target encoding |

Sampling error is separate: compare small bright features and moving edges both
against the exact sample positions and a full-image reference. Report the two
errors separately; experiment-specific region and coverage criteria belong to
LightBench. These tolerances check implementation and encoding. Model-2
shading differences from numerical emitter integration or reciprocal compensation
are separate quality measurements, described below.

## Physical-light conversion

EX07A decision D2 (approved 2026-09-22) selects one physical point/spot
attenuation model. Remove the attenuation-model selector and custom decay
exponent through a strict API/source/packed/tooling migration; no Linear or
CustomExponent lighting modes remain in the target contract. The equations
below define punctual lighting, with the declared finite-range window and
numerical guard. Source radius modifies the analytic finite-source response
described below; it does not change the center-based attenuation profile.

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

### Production local lighting and BRDF model 2

Production model 2 uses analytic finite-source lighting and view-dependent
energy compensation. Its quality/performance tradeoff is measured through
native images, independent numerical comparisons, GPU time and actual memory
allocation. Numerical-reference differences are reported alongside those results.

Point and spot lights retain authored lumens, tint, range and source radius.
The squared range window and spot profile are evaluated from the source center.
Source radius controls the analytic finite-source diffuse horizon and specular
highlight approximation, following the production sphere/capsule strategy used
by UE5.7. Radius zero is exactly the punctual branch. There is no emitter sample
loop, angular partitioning, importance integration or convergence search in the
production shader. Independent numerical integration remains in Test.

The analytic diffuse response uses a smooth finite-sphere horizon wrap. Specular
uses the direction maximizing the normal/half-vector cosine over the apparent
source cap, one fixed refinement, and source-size energy normalization. Both
consumers use the same authored source size. The retained visibility model is
one source-center shadow lookup per receiver; this change does not introduce
area-shadow ray tracing or a new shadow-filter family.

Soft spots use the ordinary FP32 squared cosine ramp, with CPU-precomputed outer
cosine and inverse inner/outer cosine difference. Equal-angle hard spots use a
step. Unrepresentable soft intervals fail publication instead of invoking
compensated expansion arithmetic in every pixel. The existing double-precision
photometric solid-angle calculation and glTF candela-to-lumen conversion remain.
Ordinary spots use cone proxies and one projected shadow independently of source
radius. Explicit 90-degree hemispherical spots retain spherical/multi-face support.

The material model retains correlated GGX, Schlick Fresnel, metallic/specular
mapping and the existing perceptual roughness floor 0.045. A single 32x32 RG32F
texture stores unit-Fresnel directional albedo E and Schlick moment B. It is
hardware filtered. Its coordinates are `sqrt(NdotV)` and
`(r_eff-0.045)/0.955`, mapped to texel centers as `(coordinate*31+0.5)/32`. The material/view
terms are prepared once per shading invocation and shared by existing light
loops. There is no incident-direction moment lookup or separate mean texture.

For each color channel, use the real-time split-sum compensation:

```text
W = 1 + F0 * (1-E) / E
R = W * (F0*E + (1-F0)*B)
```

W scales the single-scattering specular lobe. Diffuse is normalized Lambertian
scaled by `saturate(1-luminance(R))`, preserving the base color while accounting for the
specular layer. Constant-environment consumers use the same integrated specular
R and diffuse transmission. Exact reciprocity is intentionally not a property
of this view-dependent approximation. All forward/deferred, material and
indirect consumers migrate together to model revision 2, with no shipping model
selector or compatibility evaluator.

The data generator uses the independent GGX integrator under Test; production
only embeds and uploads the compact, versioned/hash-checked payload. Numerical
reports include table approximation, energy response and finite-source deviations
from the reference. Structural contracts (ABI, complete lists, finite results,
zero contribution outside supported influence, consistent forward/deferred
images, allocation failure and lifetime behavior) retain pass/fail checks.
Report numerical differences, temporal/image quality, working-set size and
representative GPU timings with fixed scene, light, shadow and output settings.
Visual captures use settled exposure; record applied and target exposure scales.

### Design tradeoffs and rejected alternatives

| Production choice                                                                          | Benefit and compromise                                                                                                                                                                                                      | Alternative dropped and why                                                                                                                                                                                                                                                      |
| ------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Analytic source cap, horizon wrap and one fixed specular refinement                        | Bounded work per receiver with a source-size response in both lobes. It approximates an extended emitter; near/inside-source response and cone/range edges can differ substantially from numerical sphere/disk integration. | Runtime quadrature, angular partitions and convergence refinement consumed too much GPU time. Keep the independent integrator in Test to quantify differences.                                                                                                                   |
| Center-based range and spot cone; one projected shadow for ordinary spots                  | Tight influence volumes and one shadow face, independent of source radius. Radius changes shading, not the cone/range boundary or shadow penumbra.                                                                          | Expanding support by emitter radius required broader proxies and six-face shadows for finite spots. That work is unnecessary for the chosen center-support model.                                                                                                                |
| FP32 cosine ramp with double-precision CPU flux normalization                              | Cheap angular evaluation with consistent authored photometry. Cosine intervals too narrow to represent on the GPU fail publication.                                                                                         | Compensated cone expansions preserved extreme sub-FP32 intervals at recurring pixel cost. That precision is not retained in the production domain.                                                                                                                               |
| View-dependent GGX compensation and scalar diffuse transmission                            | Reuses material/view terms and preserves diffuse hue with colored specular. Gives up exact reciprocity; reflected energy and direct/indirect agreement remain measured.                                                     | Symmetric reciprocal compensation required incident-direction moments and mean data. The extra lookups and arithmetic were a poor trade for this renderer. Omitting compensation altogether loses rough-specular energy.                                                         |
| One hardware-filtered 32x32 RG32F energy table                                             | 8,192-byte payload and 65,536-byte native allocation. Introduces interpolation and hardware-filter precision error.                                                                                                         | The dense 513x1025 table plus mean texture required 4,718,592 native bytes and manual filtering. They remain reference data, not runtime resources.                                                                                                                              |
| Square-root view-cosine coordinates                                                        | Concentrates the same 32 samples near grazing at the cost of one square root. The 525,825-point unit-conductor sweep measures maximum/p99 relative energy error of 2.8882%/0.3544%.                                         | Linear view-cosine coordinates at the same size measured 10.8395%/4.257%. The small arithmetic cost buys substantially better grazing accuracy without more memory.                                                                                                              |
| PCF evaluated in the light draw; cube shadows reserved for points and 90-degree soft spots | Reuses the current nine-tap filter and supports authored hemispherical cones. Wide spots pay for six faces; radius-dependent soft shadows are outside this model.                                                           | A separate full-resolution shadow mask adds a pass, writes and reads without reducing taps when visibility has one consumer. Clamping wide cones would alter valid authored/imported lights. These are architectural cost and content reasons, not isolated timing measurements. |

These choices follow the analytic area-light and energy-compensation strategies
reviewed in UE5.7 (`CapsuleLightIntegrate.ush`, `BRDF.ush`,
`ShadingEnergyConservation.ush` and `ShadingEnergyConservationTemplate.ush`).
The source-cap construction follows the published
[Decima lighting work](https://www.guerrilla-games.com/read/decima-engine-advances-in-lighting-and-aa).
Oxygen keeps its own material mapping, explicit 90-degree support and the
measured square-root LUT parameterization.

The combined change, measured in the same uncapped 2560x1440 MultiView scene,
reduced summed spot GPU time from 4.394 to 0.661 ms and total deferred lighting
from 8.427 to 2.901 ms; frame time fell from 13.098 to 7.804 ms. These are combined
results, not isolated savings attributed to individual table rows. The native
90-material matrix measured maximum energy/indirect discrepancy of 0.7032%; this
does not bound the separate finite-emitter approximation differences.
[Measurement and image evidence](../vortex/IMPLEMENTATION_STATUS.md#34-slice-7-work-items)
records the controlled recipe, reference comparisons and settled-exposure capture.

## Qualification

[LightBench](lightbench.md) provides calibrated presets, reproducible reset and
visual controls. Focused native tests, with applicable prior evidence credited,
validate fixed gain, trajectories, reset, zero target, light units
and HDR-domain invariance with independent oracles. Native MultiView compares
each view alone and in a family before composition. FPS-dependent adaptation
and broad 10-20% brightness tolerances are not acceptance criteria.
