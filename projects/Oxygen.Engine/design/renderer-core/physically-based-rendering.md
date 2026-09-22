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
| Packed material / production BRDF oracle | 2% relative + 2e-5 absolute, interior unoccluded regions           |
| UNorm8 image                             | One code value after independent dither, gamma and target encoding |

Sampling error is separate: compare small bright features and moving edges both
against the exact sample positions and a full-image reference. Report the two
errors separately; experiment-specific region and coverage criteria belong to
LightBench. Failed acceptance is not grounds to widen tolerances.

## Physical-light conversion

EX07A decision D2 (approved 2026-09-22) selects one physical point/spot
attenuation model. Remove the attenuation-model selector and custom decay
exponent through a strict API/source/packed/tooling migration; no Linear or
CustomExponent lighting modes remain in the target contract. The equations
below define punctual lighting, with the declared finite-range window and
numerical guard. D3 (approved 2026-09-22) retains physical local emitter extent
as specified below; it is not an artistic attenuation alternative.

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

### Finite local emitters

Approved EX07A D3 retains `source_radius` as physical size: a sphere for point
lights and a disk for spots, conserving authored total flux. Diffuse and
specular must use the same emitter geometry and angular emission. Radius zero
uses the punctual equations above and is the positive-radius limiting case.
The source radius is independent of the punctual numerical distance guard.

For the equations below, Phi is compensated, untinted flux. Multiply all emitted
RGB by the authored linear tint. Thus actual photometric flux is
`Phi * dot(tint, luminance_weights)` under the existing tint convention; varying
radius does not change that flux. Do not normalize tint behind the caller's back.

Let c be source center, a>0 source radius, x the receiver, y an emitter point,
`d=length(x-y)`, and `l=(y-x)/d`. Evaluate the common BRDF `f(l,v)` below. Define
`W(d)=saturate(1-(d/range)^4)^2` and `A(d)=W(d)/max(d*d,0.001^2)`.
Range is finite and nonnegative. Zero range has zero influence and returns zero
before division or shadow preparation; it is never floored to 1 mm.
Use the same independent 1 mm guard for each differential source contribution;
it does not set emitter size. This preserves the regularized punctual limit,
including receiver distances below 1 mm. A zero-length sample contributes zero
before vector normalization. The range window remains a declared finite-support
approximation to propagation, not a modification of emitted source power.

**Point sphere:** use uniform outward radiance
`Le = Phi/(4*pi*pi*a*a)`. For an emitter point with outward normal n_y,
the unoccluded contribution is

```text
Lo = integral_sphere Le * f(l,v) * max(dot(N,l),0)
                     * max(dot(n_y,-l),0) * A(d) dA_y
```

An interior receiver or one on the sphere surface receives no inward emission.
Changing variables to the unit sphere cancels a*a analytically: use the factor
`Phi/(4*pi*pi)` and `y=c+a*n_y`. Avoid separately forming a potentially huge
radiance and tiny area. For radius zero use the punctual branch; the positive
radius limit gives `Phi/(4*pi) * f(l,v) * NdotL * A(d)`.

**Spot disk:** the disk is perpendicular to the emitted-ray axis e. Its angular
intensity distribution remains `I_peak*w(theta)`, with the previously normalized
Omega and `I_peak=Phi/Omega`. Define directional intensity per unit disk area as
`I_peak*w(theta)/(pi*a*a)`, with `cos(theta)=dot(e,-l)`. Equivalently, for positive
cos(theta), its outgoing radiance is that density divided by cos(theta).
This is an angularly shaped disk, not an unqualified Lambertian emitter. The
cosine in the area-to-solid-angle conversion cancels analytically:

```text
Lo = I_peak/(pi*a*a) * integral_disk
       f(l,v) * max(dot(N,l),0) * w(theta) * A(d) dA_y
```

Evaluate on the unit disk with factor `I_peak/pi`, avoiding division by a*a or
cos(theta) in shader arithmetic. The disk emits into its forward half-space;
receivers on or behind its plane receive zero. Its radius-zero limit is the
punctual spot. Integrating emission before the distance window gives Phi for
both shapes. Conventional visibility is the existing center-source shadow/contact
approximation applied once; no radius-dependent penumbra is claimed.

Range is the support distance from each differential emitter point. Both shapes
therefore fit inside the conservative center sphere of radius `range+a`.
For spots, tighter rejection must enclose the disk swept by the finite cone
support; the old center-apex cone is insufficient. All direct consumers, proxy
bounds and shadow receiver/caster coverage use this same support. Radius changes
invalidate those derived products. Never reject a finite source solely because
its center lies below the receiver's shading-normal horizon.

Approved D5 permits `0 <= inner < outer <= pi/2` and the hard-cone extension
`0 < inner == outer < pi/2`. The stored float32 half-pi endpoint means exactly
90 degrees; its derived cosine is zero. Do not silently narrow it for a shadow
camera. The 90-degree soft-cone profile has a finite zero-radiance grazing limit;
the equal-angle 90-degree disk profile is excluded. A single projected shadow
must cover the full supported influence or use the existing conventional cube/
multiple-face representation; a clamped projection is not valid support.

Use stable squared-half-angle GPU cone parameters:

```text
ti = sin(inner/2)^2; to = sin(outer/2)^2
t  = length(emitted_direction_to_receiver - e)^2 / 4
w  = saturate((to-t)/(to-ti))^2
Omega = 4*pi*(ti+(to-ti)/3)
```

Equal angles use `w=(t<=to)` and `Omega=4*pi*to`. These equations are equivalent
to the cosine profile, while avoiding subtraction of nearly equal values near
one. Convert/validate in double precision before checked float32 transport;
reject unrepresentable positive cone support instead of widening it with an
epsilon. A glTF candela intensity becomes `Phi=I_peak*Omega` using **both** cone
angles; the current outer-angle-only conversion must be repaired.

Production approximations require independent numerical integration, separate
diffuse/specular/total-response comparisons and predeclared approximation
budgets. Validate flux and zero-radius/far-field limits. Source integration is
qualified separately from the existing fixed-PCF/contact-ray visibility
approximation; no radius-dependent penumbra is promised by this decision.

### Common surface BRDF

Approved EX07A D4 selects height-correlated Smith GGX with Schlick Fresnel,
normalized diffuse and multiple-scattering energy compensation. Forward and
deferred, punctual and finite-source evaluation use this same model. Retain the
existing metallic-roughness/specular authoring and dielectric F0 mapping; no
new authored BRDF selector or compatibility model is introduced.

Use the following one-sided reflection model after resolving the existing
material normal/two-sided rules. N, l and v are unit vectors pointing out of the
surface; `mu_l=NdotL`, `mu_v=NdotV`, and `h=normalize(l+v)`. Return zero when
either cosine is nonpositive. Invalid material/normal data is handled by its
owning input contract, not an arbitrary BRDF fallback direction. Ambient occlusion is
an indirect-light approximation, not a multiplier on each unoccluded direct
contribution; conventional/contact shadows own direct visibility.

Perspective uses the direction from receiver to camera position. Orthographic
uses the constant world-space direction opposite camera forward, derived from
the resolved inverse view; it must not normalize camera-position minus receiver.
This applies equally to forward, deferred and diagnostic BRDF evaluation.

Independently integrate the selected model to qualify reciprocity, finite and
nonnegative response, combined reflected-energy bounds and white-furnace
preservation for unit-reflectance conductors over roughness/view angle. Check
dielectrics, metals and mixtures, with separate lobe and combined-response
measurements. Compensation cannot add energy on top of an unreconciled diffuse
term. Existing BRDF integration/LUT/IBL consumers must match the revised shared
model where applicable, without adding a new GI/IBL family.

#### Shared equations and numerical domain

Preserve authored perceptual roughness r in [0,1]; derived evaluation uses
`r_eff=max(r,0.045)` and `alpha=r_eff*r_eff`. Clamp in this domain once, not
again by applying 0.045 to alpha. F0 and diffuse reflectance operands are in
[0,1]. Preserve `F0=lerp(0.08*specular,base_color,metallic)` and
`rho=base_color*(1-metallic)` for valid standard material inputs. HDR light,
emissive and unlit values are not reflectance operands and retain their domains.

```text
D(h) = alpha^2 / (pi * ((1-NdotH^2) + alpha^2*NdotH^2)^2)
V(l,v) = 0.5 / (mu_l*sqrt(mu_v^2*(1-alpha^2)+alpha^2)
             + mu_v*sqrt(mu_l^2*(1-alpha^2)+alpha^2))
F(h,v) = F0 + (1-F0)*(1-VdotH)^5
f_ss(l,v) = D(h)*V(l,v)*F(h,v)
```

The visibility expression already includes the specular denominator; do not
divide by `4*NdotL*NdotV` again. Evaluate the small positive term `1-NdotH^2`
stably, e.g. from `length(cross(N,h))^2`, without clipping the GGX peak with an
absolute denominator floor. Schlick's argument is bounded to [0,1] against
roundoff after valid normalization. Apply incident illumination and NdotL once.

Define directional integrals at fixed r and mu_v:

```text
E(mu_v) = integral_hemisphere D*V * mu_l dOmega_l
B(mu_v) = integral_hemisphere D*V * (1-VdotH)^5 * mu_l dOmega_l
E_avg = 2*integral_0^1 E(mu)*mu dmu
B_avg = 2*integral_0^1 B(mu)*mu dmu
F_avg = F0 + (1-F0)/21
K = F_avg^2 * E_avg / (1-F_avg*(1-E_avg))
f_ms(l,v) = K*(1-E(mu_l))*(1-E(mu_v)) / (pi*(1-E_avg))
f_spec = f_ss + f_ms
```

This is the symmetric energy-compensation lobe described by
[Kulla and Conty](https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf),
also derived in [Filament](https://google.github.io/filament/main/filament.html).
Do not substitute a view-only scale of f_ss: it would not preserve reciprocity.
The zero-loss limit has f_ms=0. Store loss `1-E` directly for stable near-smooth
evaluation, rather than subtracting a rounded near-one table entry.

Couple the normalized Lambertian base to that specular response using its
directional reflected-energy fraction R and transmission fraction T:

```text
R(mu) = F0*E(mu) + (1-F0)*B(mu) + K*(1-E(mu))
R_avg = F0*E_avg + (1-F0)*B_avg + K*(1-E_avg)
T(mu) = 1-R(mu); T_avg = 1-R_avg
f_diff(l,v) = (rho/pi) * T(mu_l)*T(mu_v) / (1-rho*R_avg)
f(l,v) = f_spec(l,v) + f_diff(l,v)
```

All operations involving RGB are componentwise. The diffuse denominator sums
repeated base/specular-layer reflections as a geometric series; this is Oxygen's
explicit coupling of the selected specular model to its existing diffuse base.
Compute it stably as `(1-rho)+rho*T_avg`; the zero-transmission/zero-denominator
limit contributes zero diffuse. This changes no authored material parameters.

Both lobes are reciprocal. Integrating the result for direction v gives
`R(v) + rho*T(v)*T_avg/(1-rho*R_avg) <= 1` for valid reflectance inputs. Unit F0
has K=1, R(v)=1 and zero diffuse, independently of roughness. This supplies an
explicit energy proof and furnace oracle; roughness does not remove dielectric
specular. Constant-environment consumers use the matching integrated response;
this does not introduce an absent specular-IBL feature under the ambient bridge.

#### Integration data and qualification

LightingService owns one immutable model-versioned moment product shared by
all views and consuming families: RG32Float `loss(mu,r),B(mu,r)` and RG32Float
`loss_avg(r),B_avg(r)`. Texture dimensions may be chosen to meet the certified
error bound, not to change the model. Sample mu through `sqrt(mu)` and r_eff
linearly over [0.045,1], with endpoint nodes and matching texel-center mapping.
Mean terms are integrals of the same functions, not separately fitted shading
knobs. Include the version/hash and numerical certificate in generated data;
missing/mismatched data invalidates required lighting. No camera exposure is
baked into these tables. Code and shader model revision is 1 for this contract.

Independent reference uncertainty must be <=1e-5 absolute for moments; table
and interpolation error <=2e-4 absolute, subject also to the final BRDF/image
budget. Check `0<=B<=E<=1`; certification includes grazing and smooth endpoints.
Furnace reflected-energy error is <=2e-3 absolute, with no energy gain beyond
that numerical allowance. Reciprocity uses 2e-5 relative + 2e-7 absolute.
Finite-source approximation error must fit 1% relative + 5e-6 absolute per lobe
and the existing overall 2% + 2e-5 material/physical budget. Do not widen these
budgets after a failing candidate. Endpoint limits, integer ABI and invalid-input
behavior retain their exact checks rather than being hidden by image tolerance.

The [EX07A CPU check](../vortex/plan/EX07A-contract-review.md#verification-obligations-and-current-evidence)
checks model identities and a known analytic case; it is not the complete moment
certificate, a production LUT, a GPU test or EX07B reference qualification.

## Qualification

[LightBench](lightbench.md) owns seven reproducible experiments and visual
presentation. Validate fixed gain, trajectories, reset, zero target, light units
and HDR-domain invariance with independent oracles. Native MultiView compares
each view alone and in a family before composition. FPS-dependent adaptation
and broad 10-20% brightness tolerances are not acceptance criteria.
