# EX07B reference validation

Status: **in_progress — independent physical references and native material decoding implemented; B is not qualified.**

The [EX07 plan](EX07-lighting-correctness-and-scalability.md) owns the full
reference/instrument gate. The [PBR specification](../../renderer-core/physically-based-rendering.md#shared-equations-and-numerical-domain)
owns the model and tolerances. This checkpoint changes no production shader or
renderer behavior.

## Independent moment implementation

`src/Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.{h,cpp}` evaluates the
correlated-GGX directional albedo E and Schlick moment B in double precision.
The separate `Oxygen.Vortex.LightingReference.Tests` target depends on Testing
and base utilities, without linking Vortex or importing production lighting,
photometry or HLSL helpers. Roughness and view cosine have distinct `NamedType`
wrappers so the two normalized inputs cannot be swapped implicitly.

The integrator changes variables from incoming direction l to half-vector h.
With the view in the x/z plane, let `mu=N.v`, `s=sqrt(1-mu^2)`,
`t=tan(theta_h)` and `b=s*cos(phi_h)`. The incoming-hemisphere condition is
`mu*(1-t^2)+2*b*t>0`. For positive mu its upper root is
`t_max=(b+sqrt(b*b+mu*mu))/mu`; the negative-b branch uses the rationalized form
`mu/(sqrt(b*b+mu*mu)-b)`. At mu=0, only positive azimuth projection contributes.

Integrate `4*(v.h)*D(h)*V(l,v)*(N.l)*sin(theta_h)` over the resulting bounded
half-vector angles. Multiplying by `(1-v.h)^5` gives B. Reflection supplies the
Jacobian `dOmega_l=4*(v.h)*dOmega_h`. Azimuth symmetry and splitting at pi/2 make
the hemisphere boundary explicit; no sampled visibility step defines it.
The GGX denominator uses `sin(theta_h)^2 + alpha^2*cos(theta_h)^2` to retain its
small positive term. Legendre rules are generated independently, accumulated
with compensated summation and refined in powers of two.

The directional-albedo/Schlick decomposition and reflection weight are also
documented in [Filament's pre-integration discussion](https://google.github.io/filament/main/filament.html#lighting/imagebasedlights/importanceSamplingForTheIBL).
[Heitz's visible-normal paper](https://www.jcgt.org/published/0007/04/01/paper.pdf)
provides a useful independent sampling formulation for later cross-checks.
Oxygen's approved reciprocal compensation lobe and diffuse coupling remain the
target; this reference introduces no new material model.

## Checks and current evidence

Six tests pass in **Debug and Release**:

- At roughness one, `E(mu)=1-mu*log((1+mu)/mu)`, with grazing limit one.
- The rough grazing Schlick moment is `B(0)=1/21`.
- The rough normal-view Schlick moment matches a separate polynomial/logarithmic
  antiderivative of its incoming-direction integral.
- Authored roughness below 0.045 matches the explicitly supplied minimum.
- Refinement agrees across the tested roughness/view-angle matrix, including
  smooth and grazing cases; E/B retain their physical bounds within roundoff.
- Invalid inputs and exhausted integration work return failure with diagnostics,
  rather than an unqualified successful estimate.

All three added C++ files are oxytidy-clean, without suppressions. Reproduce with:

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.LightingReference.Tests --parallel 6
./out/build-ninja/bin/Debug/Oxygen.Vortex.LightingReference.Tests.exe -v=-1 --gtest_output=json:out/build-ninja/analysis/vortex/exposure-lightbench/ex07b/reference-foundation-debug.json
```

Use the existing VS developer environment for the build. The Release target and
command use the same target name and test matrix. Logs, results, lint output and
the source-hash checkpoint live under
`out/build-ninja/analysis/vortex/exposure-lightbench/ex07b`.

## Independent high-precision endpoint anchors

The versioned [generator](../../../tools/vortex/GenerateGgxEndpointReference.py)
uses mpmath 1.3.0 tanh-sinh integration at 60 and 90 decimal digits. Its
[generated data](../../../src/Oxygen/Vortex/Test/Lighting/Reference/GgxEndpointReference.json)
contains normal-view and grazing-view E/B anchors for roughness
0.045, 0.1, 0.25, 0.5, 0.75 and 1. It records its source hash, model revision,
precision disagreement and the quadrature's reported error estimates.

At normal view the reference integrates incoming cosine directly, using
`(N.h)^2=(1+N.l)/2` and the analytic azimuth factor 2*pi. At grazing view E=1;
the azimuth integral of the Schlick term is evaluated analytically as a
polynomial using the integrals of cos(phi)^1 through cos(phi)^6, leaving one
radial integral. These coordinates and reductions differ from the C++ reference's
two-dimensional half-vector quadrature.

[mpmath's quadrature documentation](https://mpmath.org/doc/current/calculus/integration.html)
describes its returned error as an estimate. The generator therefore records
precision agreement separately and does not label either quantity a rigorous
whole-domain bound. It refuses output if either exceeds 1e-40 at an anchor.

Debug and Release each pass **eight reference tests**. Across the 12 endpoint
anchors, the maximum C++ absolute difference is **1.7763568394002505e-15**.
The generator's maximum precision disagreement is **1.2230165e-59**; maximum
reported quadrature error is **1.1023e-62**. The added negative control rejects
applying the 0.045 floor to alpha after squaring roughness. This provides an
independent check that the earlier minimum-roughness equivalence test alone
could not establish.

Regenerate the data through its owner, never by hand:

```powershell
uv run --no-project --with mpmath==1.3.0 python tools/vortex/GenerateGgxEndpointReference.py
uv run --no-project --with mpmath==1.3.0 python tools/vortex/GenerateGgxEndpointReference.py --check
```

The check reproduces the file byte-for-byte, and the generated JSON passes the
repository formatter without modification. The new C++ endpoint test is
oxytidy-clean. Evidence under `ex07b`: `endpoint-reference-{debug,release}.json`,
`endpoint-reference-tidy-final/` and `endpoint-reference-checkpoint.json`.

## Certified interior queries

The [pointwise moment certificate](EX07B-moment-certificates.md) now supplies
rigorous incoming-direction E/B enclosures using FLINT/Arb, independently of the
C++ quadrature. Fifty-five parameter pairs include endpoints, interior views and
roughness-dependent grazing transitions. The maximum exported radius is 1.454e-9;
the C++ distance bounds are 2.260e-11 for E and 1.710e-9 for B. Ten C++ tests pass
in both configurations, six generator safety checks pass, and generated data is
formatter-stable and reproducible. This is not a production interpolation table.

## Mean moments and coupled BRDF

`IntegrateGgxMeanMoments` integrates the cosine-weighted means
`2*integral_0^1 mu*E(mu) dmu` and `2*integral_0^1 mu*B(mu) dmu`. Logarithmic
cosine coordinates resolve the narrow smooth/grazing transition. Below
`mu=1e-4`, `0<=B<=E<=1` encloses either tail integral in `[0,1e-8]`.
The implementation carries the midpoint and a separately exposed, outward-rounded
radius of approximately `5e-9`; it does not silently discard that interval.
Two consecutive outer refinements must converge, and an inner integration
failure propagates its cause and the failed view cosine. The tail radius bounds
only that contribution; the other quadrature errors remain estimates.

At roughness one, independent exact means are
`E_avg=(4/3)*(1-ln(2))` and `B_avg=111/35-(32/7)*ln(2)`.
For the latter, integrating the projected pair of directions at fixed
`c=v.h` gives the scalar density
`g(c)=2*(c-(1-c*c)*atanh(c))`. Integrating `g(c)` on `[0,1]` gives E_avg;
integrating `g(c)*(1-c)^5` gives B_avg. These expressions independently check
the nested half-vector/outer-cosine quadrature and its cosine measure.
Refinement tests additionally cover roughness 0.045, 0.25 and 0.6.

`Reference/GgxBrdf.{h,cpp}` evaluates the three approved lobes separately:
correlated-GGX single scattering, symmetric multiple-scattering compensation
and the coupled Lambertian base. Light cosine, view cosine and relative azimuth
have distinct types. The evaluator accepts matching reference moments and one
reflectance channel; RGB follows by componentwise evaluation. Incident power,
receiver cosine and exposure are deliberately outside this BRDF API.

Transmission uses the nonnegative rearrangement
`T=(1-K)*(1-E)+(1-F0)*(E-B)`, including its mean counterpart. The diffuse
denominator is `(1-rho)+rho*T_avg`; zero transmission contributes zero diffuse.
Invalid reflectances/moments fail explicitly, back-facing directions return
zero, and nonfinite representability failures cannot become successful results.

The reference tests swap incident/outgoing directions and inspect each lobe for
roughness 0.045, 0.25 and 1, three cosine pairs (including 0.001 grazing), three
azimuths and dielectric, conductor and mixed reflectances. A separate incoming
direction furnace quadrature uses 128 midpoint cosine samples and 256 azimuth
samples, independently of the moment integrator's half-vector coordinates.
Its roughness 0.6/1 and view-cosine 0.05/0.5/1 matrix checks seven material
combinations, individual lobe integrals, combined energy and unit-reflectance
preservation. Omitting compensation is a failing furnace negative control.
The roughness-one furnace uses the analytic means directly.

Debug and Release each pass all **16 reference tests**. Maximum combined furnace
error `6.400e-5` against the `2e-3` budget and maximum relative reciprocity
difference `6.553e-16` pass the specified limits. These are measured errors at the specified samples,
not a whole-domain uncertainty certificate or a production shader result.
All six changed C++ files, including headers and tests, are oxytidy-clean with
no new suppressions. Evidence under `ex07b`: `coupled-reference-{debug,release}.json`,
`coupled-reference-tidy-verified/` and `coupled-reference-checkpoint.json`.

## Independent punctual photometry

`Reference/Photometry.{h,cpp}` resolves point lumens to candela, spot lumens to
peak candela, imported spot candela back to lumens and compensated directional
lux. Distinct types separate these units, source EV, the two cone half-angles,
off-axis angle, receiver distance and influence range. Camera exposure, RGB
tint, material decoding and receiver cosine remain separate operations.

The reference uses the physical cosine-profile definition. It evaluates
`cos(a)-cos(b)` as `2*sin((a+b)/2)*sin((b-a)/2)` and `1-cos(a)` as
`2*sin(a/2)^2`, preserving narrow-cone support without copying the packed GPU
half-angle representation. Hard cones include their boundary; soft cones end at
zero intensity, including the exact physical 90-degree endpoint. The rejected
hard 90-degree cone and zero solid angle do not gain an epsilon fallback.
Interpreting a stored float32 half-pi as the exact physical endpoint belongs to
the separate wire/authoring decoder, not this double-precision angular API.

Independent Simpson integration of `2*pi*I(theta)*sin(theta)` splits at the
inner angle and verifies emitted flux for eight cone pairs: hard, broad/narrow
soft, hemispherical soft and nearly equal angles. A zero-inner soft cone has
three times the peak intensity of the same-outer-angle hard cone. This negative
control detects the legacy outer-angle-only conversion error. The candela/flux
round trip uses both authored angles.

Source compensation scales a decomposed binary quotient. This avoids overflowing
`2^EV`, or underflowing the uncompensated quotient, when the final double result
is representable. Nonfinite inputs, true positive-result overflow/underflow and
unrepresentable cone support return explicit failures. A zero source remains
zero for any finite EV. This is an independent reference, not validation of
production float32 transport or a relaxation of its representability contract.

Distance evaluation implements the specified squared quartic range window and
1 mm inverse-square guard, using a factored window near the range boundary.
Zero separation, zero range and at/beyond-range receivers return zero. Tests
include exact rational values, the guarded regime, inverse-square ratios and
the last representable distance below the range boundary.

The owning Debug suite passes **21/21 tests**; focused Release passes all five
new photometry tests. Maximum emitted-flux relative error is `8.025e-15` in both.
The three new C++ files, including the header, are oxytidy-clean without
suppressions. These tests qualify punctual photometry; finite geometry has its
separate reference below. Decoding/GPU/image gates remain open, and no production
lighting behavior changes here.
Evidence under `ex07b`: `photometry-reference-{debug,release}.json`,
`photometry-reference-tidy-verified/` and `photometry-reference-checkpoint.json`.

## Finite sphere/disk reference

`Reference/FiniteEmitter.{h,cpp}` integrates the approved unoccluded emitter
equations in a receiver-local orthonormal frame with `N=(0,0,1)`. Position in
metres and unit directions have separate structures; radius, range and flux
retain their unit types. An incident-direction callback supplies the BRDF's
three lobes. The integrator applies source compensation, geometry, angular
emission, the receiver cosine and range/guard exactly once. Tint, pre-exposure,
shadow visibility and output transforms remain separate consumers.

For a sphere of radius a at center distance R>a, integrate its apparent cap with
`sin(theta)^2=(a/R)^2*u`. Its projected geometry changes the unit-sphere area
integral into
`I/(2*pi*R^2) * integral f(l,v)*(N.l)/cos(theta)*W(d)*d^2/max(d^2,g^2) du dphi`,
where `I=Phi/(4*pi)` and `g=0.001 m`. The `a^2` factors cancel analytically.
The near ray/sphere intersection uses the rationalized expression
`d=(R-a)*(1+a/R)/(cos(theta)+(a/R)*sqrt(1-u))`.
Clip radial support at the shading horizon and finite range, then clip each
azimuth interval against positive `N.l`. Interior/on-surface receivers get zero
outward emission. Radius zero takes the explicit punctual path.

For the disk, use unit polar area `rho*d(rho)*d(phi)` with factor `I_peak/pi`.
The finite cone and range restrict source points to a circle in the emitter
plane. At perpendicular receiver distance h its radius is
`min(sqrt(range^2-h^2),h*tan(outer))`; the hemispherical endpoint uses only the
range bound. This avoids `h/cos(outer)` rounding to h for a narrow valid cone.
Intersect its radial support with the physical disk, and each radial sample's
azimuth with the receiver horizon and this circle. The circle intersection uses
`(r-p)^2+4*r*p*sin(delta/2)^2<b^2`, retaining small support without subtracting
large squared distances. A collapsed numerical interval with known positive
overlap returns a representability failure, never successful zero light.
Evaluate the spot angle using `atan2(length(cross(e,l)),-dot(e,l))`, retaining
narrow angular support. Receivers on/behind the emitted plane get zero. No
center-ray cone or center-below-horizon rejection substitutes for finite support.

The original Gauss-Legendre rule and compensated sum now live in the shared
test-only `ReferenceQuadrature` helper, with their arithmetic unchanged.
Power-of-two refinement is bounded at 2048 per dimension, with the default
maximum still 256. Two successive rules
must meet the absolute-plus-relative tolerance independently for every lobe.
Callback failures preserve their cause; exhausted work preserves the last
estimate. A refinement without any sampled geometric support cannot establish
convergence to zero. Successive-rule changes remain estimates, not rigorous uncertainty
certificates for arbitrary BRDF callbacks or every source configuration.

The finite-emitter matrix contains eleven tests:

- A perpendicular Lambertian receiver matches the sphere's analytic projected
  solid angle across radii from zero through 0.999 of center distance.
- On-axis hard-cone and hemispherical-soft disks match separate analytic
  integrals. The latter includes the squared cosine emission profile.
- Positive-radius sphere/disk responses converge to the punctual branch above
  and below the independent 1 mm distance guard.
- Sources whose centers lie below the horizon or beyond range retain valid
  finite contributions. Off-axis disks with 0.01 and 1e-8-radian outer cones
  contribute even though their center rays are outside their cones. The latter
  regression fails under the initial cosine-distance cutoff and passes with
  direct plane support. Numerically collapsed positive overlap fails explicitly.
- Interior/on-surface spheres, on/behind-plane disks and zero range skip BRDF
  evaluation; invalid input, callback errors and work exhaustion fail explicitly.
- All three coupled-GGX disk lobes match a separate receiver-angle Simpson
  integral at roughness one. A surface-area Simpson sphere integral separately
  checks the range window and distance guard against the cap formulation.

Debug and Release each pass all **32 reference tests**. The maximum absolute
difference against the separate sphere surface integral is `1.134e-7` in the
tested range/guard cases, within their `1e-10 + 1e-8*reference` comparison limit.
All six changed C++ files,
including headers, are oxytidy-clean without new suppressions. These checks
qualify the listed numerical cases; broader source/BRDF sampling and uncertainty
qualification remain open before this oracle can qualify production approximations.
No renderer behavior or shadow technique changes in this checkpoint.
Evidence under `ex07b`: `finite-reference-{debug,release}.json`,
`finite-reference-narrow-tidy/`, `finite-narrow-negative.log` and
`finite-reference-checkpoint.json`.

### Off-axis and source-edge qualification

`FiniteEmitterOffAxis_test.cpp` compares 18 disk and 18 sphere cases using
roughness 0.045/0.25/1, view cosines 0.5/1 and three source offsets. The last
offset places the mirror direction just beyond the source rim. These controlled
kernels carry a GGX single-scattering lobe and a separate Lambertian response;
they isolate geometry/transport accuracy without pretending that a rough PBR
material is Lambertian. The cases also exercise source compensation and the
specified range window.

Uniform polar refinement reached its work ceiling for a narrow off-axis GGX
highlight. `EmitterIntegrationSettings::peak_direction` now permits a known
unit lobe direction to partition the radial and azimuth domains. The GGX mirror
direction supplies it in these fixtures. A peak outside the source can still
split azimuth to resolve the bright rim; radial splitting is confined to the
actual integration interval. This only changes quadrature partitions, not
emission, geometry, BRDF or visibility. It is an offline sampling hint, not an
authored material/light property. Flat-response checks show partition invariance,
and invalid directions fail explicitly. The optional work ceiling is 2048;
the ordinary default remains 256.

The independent disk calculation integrates Cartesian unit-area coordinates
`x=sin(theta), y=cos(theta)*sin(psi)` with Jacobian
`cos(theta)^2*cos(psi)`. The independent sphere calculation integrates emitting
surface normals over the visible cap and retains the emission cosine explicitly.
Both use 256/512-order comparisons and split at the mirror direction where
applicable. Their coordinates and Jacobians differ from the oracle's polar disk
and apparent sphere cap. All comparisons retain `1e-7 + 1e-5*reference` limits.

The Release finite-source suites pass **14/14 tests**, and the owning Debug
suite passes **43/43**. Maximum relative
differences are **1.838e-11 for disks** and **2.740e-6 for spheres**. The owning
Debug comparisons match these results; the three changed C++ files are oxytidy-clean.
The subsequent tilted/grazing matrix is below. Full coupled-RGB finite-source
fixture qualification remains open. This checkpoint changes no production
shader or runtime rendering cost.
Evidence under `ex07b`: `finite-offaxis-{debug,release}.json`,
`finite-offaxis-limit-negative.log`, `finite-offaxis-tidy-verified/` and
`finite-offaxis-checkpoint.json`.

### Tilted and grazing source geometry

The independent Cartesian disk oracle now accepts a general emitter axis.
It solves the receiver horizon and cone/range circle in emitter-plane coordinates
and clips each integration row before sampling. Thirty-six cases combine four
tilted/horizon-crossing geometries, roughness 0.045/0.25/1 and view cosines
0.01/0.5/1. The sphere matrix also includes 0.01 views, giving 27 sphere cases
alongside the 18 aligned disk cases.

The sharpest grazing sphere case exposed poor convergence near the apparent-cap
boundary. The sphere integral now regularizes `sqrt(1-u)` using a unit parameter
s and a rationalized interval span:

```text
t_lo = sqrt(1-u_max); t_hi = sqrt(1-u_min)
dt = (u_max-u_min)/(t_hi+t_lo)
t = t_lo+dt*s
u = u_max-dt*s*(2*t_lo+dt*s)
abs(du/ds) = 2*dt*t
```

This retains narrow clipped-cap area and the existing ray-distance equation.
For a supplied peak direction, both source integrals additionally split each
azimuth row at `(N cross peak).l=0`. This follows the narrow grazing ridge even
when the unconstrained mirror direction lies beyond the source. It does not
alter source emission or visibility. The optional work ceiling is 2048, while
the default remains 256. The independent sphere surface oracle partitions its
own rows at the receiver view plane; it retains its separate area Jacobian and
emission cosine.

The Release finite-source run passes all 15 cases, with independent
refinement pairs up to 1024/2048 for the hardest grazing inputs. The tilted disk
matrix uses at most 2.546e-5 of its unchanged `1e-7 + 1e-5*reference` error
budget; maximum sphere relative difference is 7.852e-8. The owning Debug suite
passes **44/44 tests** and reproduces these comparison results. Both changed
C++ files are oxytidy-clean.
Full coupled-RGB finite-source validation remains separate.
Evidence under `ex07b`: `finite-tilted-{debug,release}.json`,
`finite-grazing-limit-negative.log`, `finite-tilted-tidy-verified/` and
`finite-tilted-checkpoint.json`.

## Packed material decoding and native format probe

`Reference/MaterialDecode.{h,cpp}` decodes exact stored texel words independently
of production C++/HLSL helpers. Distinct packed-word types prevent mixing normal,
material and color inputs. The formats come from `SceneTextures::GBufferFormat`:

| Product    | Native format     | Interpretation                                                               |
| ---------- | ----------------- | ---------------------------------------------------------------------------- |
| Normal     | R10G10B10A2_UNORM | R/G hold the octahedral normal; decode/unfold/normalize in double precision. |
| Material   | RGBA8_UNORM       | Metallic, specular, roughness, raw shading-model code.                       |
| Base color | RGBA8_UNORM_SRGB  | Ideal sRGB-to-linear RGB; alpha remains linear ambient occlusion.            |

The decoder consumes stored codes rather than predicting implementation-dependent
raster rounding. It preserves decoded roughness; the 0.045 floor remains owned
by BRDF evaluation. Default specular stored as code 128 becomes `128/255`, giving
dielectric F0 `0.04015686274509804`, not exactly 0.04. Resolve each RGB channel with
the approved `F0=lerp(0.08*specular,base,metallic)` and `rho=base*(1-metallic)`.
Invalid reflectance operands fail instead of silently clamping.

Four CPU tests check independent sRGB anchors on both sides of the transfer
threshold, linear AO, upper/lower octahedral hemispheres, unused normal lanes,
quantized roughness/specular and dielectric/metal/mixed reflectance channels.
The native `NativeGBufferFormatsMatchIndependentMaterialDecoder` test uploads
256 known texels into actual textures in all three formats and reads them through
D3D12 SRVs. It executes production `DecodeGBufferNormal`, `DecodeGBufferMaterial`,
`DecodeGBufferBaseColor` and `ComputeMetallicF0`. It covers every 8-bit code,
normal landmarks around the fold and all shading-code bytes. Preserving a raw
shading code does not authorize that model for a lighting path.

The [Direct3D functional specification, section 3.2.3.7](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
permits SRGB-to-FLOAT conversion error of half an encoded integer code, measured
by ideal inverse conversion; endpoints must be exact. The probe applies that
format contract and reports actual linear-space error separately. For F0/rho,
it propagates the measured base-color conversion difference through their linear
coefficients, then allows 1e-6 for shader arithmetic. Normal components use an
absolute 2e-6 limit; UNORM scalars use 1e-7. These format/decode checks do not
widen the frozen end-to-end packed-material/BRDF budget of `2% + 2e-5`.

Debug and Release each pass all **25 native tests**, with no warning/error log
entries. Both report maximum normal-component difference 2.006e-7, maximum
sRGB-code error 0.429605 and maximum linear-color difference 0.00374875.
The owning Debug CPU suite passes **36/36 tests**; focused Release passes all
four new CPU material tests. All six changed C++ files,
including fixture headers and tests, are oxytidy-clean without new suppressions.
This qualifies decoding of supplied texels and reflectance operands. Texture
sampling/factor composition, normal mapping, surface eligibility, actual base-pass
writes and complete forward/deferred lighting images require their own fixtures.
No production material or rendering behavior changes here.
Evidence under `ex07b`: `material-native-{debug,release}.json`,
`material-reference-{debug,release}.json`, `material-reference-tidy-verified/`,
`material-normal-landmarks-tidy/` and `material-reference-checkpoint.json`.

## Smooth/grazing furnace matrix

`GgxFurnaceReference_test.cpp` expands furnace validation to the 49 positive-view
queries in the certified directional-moment matrix, across roughness 0.045,
0.1, 0.25, 0.5, 0.75 and 1. The exact zero-view boundary remains governed by the
BRDF's zero-response rule; the moment table stores its grazing limit.
Eight reflectance pairs cover dielectrics, conductors, mixtures, a white base,
zero diffuse, zero transmission and near-unit F0: **392 furnace combinations**.

The reference exposes the single-scattering lobe independently of E/B inputs,
sharing its implementation with the complete three-lobe evaluator. Its arithmetic
and roughness policy are unchanged. A normal-incidence anchor checks
`f_ss=F0/(4*pi*alpha^2)`, including authored roughness below the 0.045 floor.
Invalid-input and backface tests cover this entry point as well.

Single-scattering integration uses `tan(theta_h)=alpha*tan(psi)`, resolving the
NDF peak in coordinates different from the original half-angle integrator and
the Arb incoming-direction certificates. Clip at the reflected hemisphere
boundary and apply the full reflection/change-of-variable Jacobian. Refine in
bounded powers of two through 4096, then compare the actual lobe integral
against the independent E/B enclosures with outward-rounded distance arithmetic.
The certificate comparison, not the successive-rule estimate, establishes its
1e-5 accuracy requirement.

Multiple scattering and diffuse are integrated from their actual evaluator
outputs using 128 logarithmic-cosine nodes. Their specified azimuth independence
is checked at three azimuths before applying the analytic 2*pi angular measure.
The same directional-moment settings and cosine quadrature underlie the separately
certified C++ means. Mean and view operands come from their certified query data.
An explicit bound covers the unsampled `mu<1e-4` interval using `0<=E,B,T<=1`.
The tests check nonnegative lobes, separate integrated responses, the combined
energy ceiling and unit-reflectance preservation. Omitting compensation loses
energy; adding uncoupled Lambertian diffuse exceeds the unit-energy ceiling.

The Debug and Release matrices pass: maximum single-scattering certificate distance
is **1.283e-6**, and maximum combined furnace comparison error including the
grazing-tail bound is **5.854e-5**, below **2e-3**. The largest tail bound is
**3.933e-5**. The owning Debug suite passes **40/40 tests**, and the affected
Release BRDF/furnace/finite-source suites pass **16/16**. All four changed C++
files are oxytidy-clean without suppressions.
This qualifies the specified CPU reference matrix, not production shaders or
every arbitrary material/view query. Production numerical/image comparisons
must still meet their frozen budgets on the final implementation.
Evidence under `ex07b`: `furnace-reference-{debug,release}.json`,
`furnace-reference-tidy-verified-final/` and `furnace-reference-checkpoint.json`.

## Coupled RGB finite-source composition

The independent Cartesian-disk and emitting-sphere-surface integrals now accept
the complete incident BRDF and accumulate all three lobes. Existing smooth,
grazing and horizon cases use the same helpers without compatibility overloads.
The added RGB case combines base color `(0.8,0.4,0.2)`, metallic `0.25`,
specular `0.5`, roughness `1`, view cosine `0.5`, source EV `+1` and linear
light tint `(0.5,1.25,2)`. It checks an off-axis sphere and a tilted disk with
a soft 90-degree outer half-angle. Material reflectance remains bounded;
light tint is a radiometric scale and may exceed one.

The apparent-cap/polar integrators receive flux scaled by each tint channel;
the independent area integrals instead scale their final responses. All
**18 channel/shape/lobe comparisons** have positive response and pass
`1e-7 + 1e-5*reference`, with maximum error below **1.75e-10** of that budget.
Independent area orders 32/64 also agree within the same tolerance. Directional
moments are integrated at 36,544 distinct cosine queries, with exact-key caching
across channels and no interpolation; mean moments use the analytic roughness-one
values. This is a composition and geometric-integration check, not an independent
certificate for every sampled moment or a production RGB shader qualification.

Debug and Release pass all **16 finite-source tests**. The final Release binary
also passes the new case after the lint fixes. The changed C++ file is
oxytidy-clean without suppressions. Evidence under `ex07b`:
`finite-rgb-{debug,release}.json`, `finite-rgb-final-release.json`,
`finite-rgb-tidy-verified/` and `finite-rgb-checkpoint.json`.

## Native punctual photometry probe and physical admission

`PhotometryGpu_test.cpp` uploads production CPU-resolved point/spot intensities
and cone parameters, then executes the actual shared HLSL distance and angular
attenuation helpers through probe mode 18. The independent double-precision
photometry module supplies expectations. The matrix contains **2,160 inputs**:
point, hard spot, soft spot, hemispherical soft spot and a `1e-5`-radian narrow
spot; EV `-2/0/+3`; linear tint `(0,0.5,2)`; zero/guard/interior/range-boundary
distances; and zero, millimetre and ten-metre ranges. Distance and direction are
controlled separately to isolate the two helper contracts. The hard-cone exact
edge is excluded because quantized direction can lie on either side; its axis
and just-outside support are checked. Soft-cone edges remain in the matrix.

The instrument independently checks CPU intensity conversion, GPU attenuation
factors, finite/nonnegative results, exact zero products and RGB float products.
It also records every illumination result outside `2% + 2e-5`, with probe/lane,
expected/measured values and tolerance. Interior rows must satisfy that physical
budget in the native test. Boundary rows retain their measured residuals for C;
a passing instrument test does **not** establish physical renderer admission.

`tools/vortex/AssertLightingPhysicalProbe.py` is the separate admission gate:
it rejects missing, skipped, partial, duplicate or failed native evidence and
requires **zero** physical-budget failures. Six safety tests cover these rules,
including a successful instrument run with failing physical measurements.
Run it after the native suite's GoogleTest JSON export:

```powershell
python tools/vortex/AssertLightingPhysicalProbe.py out/build-ninja/analysis/vortex/exposure-lightbench/ex07b/brdf-native-release.json
```

The current renderer **fails this admission gate**: 23 narrow-cone boundary
channel results exceed the physical budget, with maximum budget fraction
**49.2744**. The largest missing contribution is **0.06790593 lux**, from a
100-lumen source with EV +3 and blue tint 2 near its quantized outer edge.
Near-range cancellation also exceeds the probe's tighter exploratory tolerance
in high-intensity cases, but remains within the frozen physical budget.
Do not widen that budget or omit the rows to close C. Resolve the cone parameter /
direction precision issue and establish the final supported boundary behavior
before admitting this workload to timing. This checkpoint changes no shipping
lighting implementation.

Debug and Release each pass **26/26 native instrument tests**, with identical
23-channel physical-admission failures and no warning/error log entries.
Maximum attenuation-factor error is **0.007617** of its diagnostic budget.
The changed C++ file is oxytidy-clean without suppressions. The admission
command exits **1**, as required for these unqualified physical results;
all six admission-tool safety tests pass.

Evidence under `ex07b`: `photometry-native-{debug,release}.json` and `.log`,
`photometry-native-tidy-verified/`, `photometry-physical-admission.log`,
`photometry-admission-tests.log` and `photometry-native-checkpoint.json`.

## Native direct-BRDF probe

`BrdfGpu_test.cpp` compares **108 inputs / 648 channel responses** against the
independent complete compensated-GGX oracle. Authored roughness `0/0.25/1`,
four normal/grazing incident-view pairs, three azimuths and dielectric/mixed/metal
materials cover both current forward and deferred paths. Incident RGB is
`(0.5,1.25,2)`; the output includes receiver cosine exactly once. The oracle
uses the certified mean midpoints at effective roughness `0.045/0.25/1` and
numerically integrates directional moments at the uploaded float cosines.
This does not certify arbitrary moment interpolation or texture/material ingress.

The probe calls the production deferred `EvaluateCookTorranceLighting` with
unit AO and the production forward direct-BRDF function. Forward's duplicate
directional/local arithmetic is extracted into `EvaluateForwardDirectBrdf`
without changing operations or wrapper scaling. DXC `ps_6_6`, `-O3`, HLSL 2021
produces **byte-identical stripped DXIL** before/after for translucent, opaque
and opaque-with-complete-depth `ForwardMesh_PS` variants. Artifacts are in
`ex07b/brdf-extraction/`. The probe's forward input represents the local direct
response before distance/cone/shadow/exposure multiplication; this does not
qualify directional wrapper normalization or full images.

The instrument checks RGB F0, finite/nonnegative outputs and the independent
`D=1/pi` anchor at roughness one. It records every complete-BRDF physical residual
without requiring preservation of the current incorrect model. The shared
admission checker now requires **both** photometry and BRDF reports, rejects a
missing matrix, and prints both sets of failures. Seven safety tests cover its
complete, incomplete and physically failing result paths.

The current renderer fails **565/648** BRDF channel comparisons: **298 forward**
and **267 deferred**, with maximum error **310.316** times the frozen
`2% + 2e-5` budget. These are C repair obligations for the already-approved
correlated visibility, roughness handling, normalized diffuse and compensated
three-lobe model. The worst recorded case predicts `0.000368555` and measures
`0.008862232` in deferred. Do not treat the successful instrument run as a
renderer correctness pass or relax its physical admission gate.

Debug and Release each pass **27/27 native instrument tests** with identical
physical failure counts; the final Release binary also passes the new BRDF case
after lint-only fixes. No warning/error log entries occur. The changed C++ file
is oxytidy-clean. The admission checker exits **1** and reports both the 23
photometry and 565 BRDF channel failures. All seven admission-tool tests pass.

Evidence under `ex07b`: `brdf-native-{debug,release}.json` and `.log`,
`brdf-native-final-release.json`, `brdf-native-tidy-verified/`, `brdf-physical-admission.log`,
`brdf-admission-tests.log` and `brdf-native-checkpoint.json`.

## Shared bounded CPU capture foundation

`Test/Support/CpuTimingCapture.{h,cpp}` replaces the exposure-only observer.
`Oxygen.Vortex.Test.Support` (`oxygen::vortex-test-support`) compiles it once per configuration; the
lighting instrumentation tests and exposure benchmarks both depend on that
test-owned library. Tests do not compile or include benchmark implementation
files. No shipping target links the collector, and this checkpoint adds no
production profiling hooks or collection overhead.
Its target uses Oxygen's module declaration and hierarchy helpers, standard
compiler flags/C++23 requirements, header file sets, IDE arrangement and coverage
configuration. The support module is static and has no install/export rule.
Debug/Release support consumers build and all eight collector tests pass after
this CMake correction. `ex07b/support-module-checkpoint.json` records target
ownership and common compiler flags from all three generated configurations;
`support-module-{debug,release}.json` records the executed test results.

Record capacity is an explicit strong type. Storage is reserved before capture;
accepted callbacks copy labels into fixed storage and append only within the
declared limit. The scope stack is fixed at 128 entries. Overflow, invalid labels,
unbalanced scopes, nonmonotonic frames and wrong-thread use invalidate the
capture; export refuses partial or empty evidence. Observer failures cannot
escape into rendering. Formatting and file I/O happen after capture. The existing
exposure attribution and CSV format are preserved without an API compatibility
wrapper. Lighting selection recognizes `Vortex.Lighting.*` and `Vortex.Shadows.*`.
Compact mode records outer owners and explicit fence waits; detailed mode also
records nested phases and Graphics/D3D12 work. QPC intervals represent elapsed
scope time; active CPU work still requires scheduler correlation.

`Oxygen.Vortex.LightingInstrumentation.Tests` checks capture boundaries, nested
attribution, waits, QPC frequency and timestamps bracketed by independent counter
reads, capacity/depth overflow and invalid-state rejection. Both exposure
benchmark executables are built and retain their eight opt-in workload names.
Timing workloads are not rerun merely to qualify the support-library migration.

Debug and Release pass **8/8 collector tests**, and both exposure benchmark
targets build. The compile database confirms one collector object per
configuration, owned solely by the support library. Oxytidy covers every changed
C++ file/header: the collector and tests have no findings; 39 existing
`readability-magic-numbers` findings remain on unchanged exposure recipe lines.
The baseline comparison is recorded rather than disabling their diagnostics.
The only new suppression is scoped to `<stdlib.h>` for MSVC's nonstandard
`_dupenv_s` declaration; `<cstdlib>` still supplies the standard library API.

At that foundation checkpoint, lighting phase integration, bounded
GPU/resource collection and a native on/off overhead measurement remain required
before B's instrumentation gate closes. The user requires normal profiling/probe
overhead only; do not add benchmark collection, formatting, allocation or I/O to
the production frame path.

Evidence under `ex07b`: `cpu-timing-{debug,release}.json`,
`cpu-timing-{debug,release}-build.log`, exposure workload discovery logs,
`cpu-timing-support-tidy/`, `cpu-timing-lint-baseline.json` and
`cpu-timing-checkpoint.json`.

## Native serial-image accumulation fixture

`Oxygen.Vortex.LightingImageReference.Tests` uses the existing native D3D12
lighting fixture at **96x64**, including a partial 64-pixel grid tile. It creates
33 deterministic, visibly contributing sources: 17 points and 16 soft spots,
with varied positions, range, flux, RGB tint and source EV. Shadowing is disabled.
The camera, geometry, material, simulation time and manual pre-exposure remain
fixed within each comparison. Scene capacity is explicit in the shared fixture
so this larger recipe does not trigger resource-table growth.

For forward and deferred pipelines, across opaque, accepted masked and
alpha-blended surfaces, the test renders a no-light baseline and each light
separately. Double-precision accumulation sums the individual HDR differences
above the common baseline. The 31/32/33-light frames must match those serial
images within **0.5% relative + 2e-5 absolute**. Reversing physical light
assignments across node identities must preserve the full image, and disabling
all sources must restore the baseline. Negative controls omit or duplicate the
strongest individual contribution and must fail the same image tolerance.
All measurements use scene color before output mapping; alpha is not summed.

Debug and Release pass **331,776 combined-image channel comparisons** plus
**110,592 permutation channel comparisons**, and the zero-light recovery and
negative controls. Maximum error is below **0.000106** of the allowed image
budget. The smallest individual-source peak across all recipes is **0.01200072**,
so zero-contribution lights cannot satisfy the fixture. Both runs are free of
warning/error messages after reserving the scene capacity. Every changed C++
file/header is oxytidy-clean. The public `NamedType` facade now marks its two
implementation includes as IWYU exports; callers retain the public include
instead of depending on private headers.

The fixture can capture one opaque forward 33-light frame through its existing
`OXYGEN_EXPOSURE_CAPTURE` environment control. The final Debug capture's replay
passes `AnalyzeRenderDocLightingReference.py`: all 33 distinct selection indices,
17 point/16 spot kinds and all **64 complete-list cells** are present in the
actual consumed descriptors. The output is finite RGBA32 float scene color,
with peak channel value **16.789215**. Replay exports the raw HDR image and
closes its replay handles successfully.

This establishes accumulation, permutation, mutation and current complete-list
publication for the stated fixture. It **does not** establish physical BRDF
correctness, shadows, texture/normal-map evaluation, multiview or independent
spatial-culler rejection. The serial passes still use the renderer's selection
path. An independently forced unculled reference remains necessary before this
fixture can qualify a future spatial culler; do not silently reuse a culling
defect in both compared images.

Evidence under `ex07b`: `image-reference-{debug,release}.json` and `.log`,
`image-reference-forward-final_capture.rdc`, `image-reference-renderdoc-final.txt`
and `.rgba32f`, `image-reference-tidy-verified/`,
`image-reference-final-clean/` and `image-reference-checkpoint.json`.

## Reference-test runtime improvement

The same **45 tests**, cases, tolerances, refinement requirements and independent
certificates pass before and after the optimization. Serialized Ninja runs on
the same machine measure:

| Configuration |    Before |     After | Speedup |
| ------------- | --------: | --------: | ------: |
| Debug         | 532.604 s | 128.955 s |   4.13x |
| Release       | 277.866 s |  45.626 s |   6.09x |

The moment integrator retains its half-vector angle coordinates and equations.
It partitions the same domain around the NDF width `atan(alpha)` and the
hemisphere-edge visibility scale, instead of resolving both with one very large
Gaussian rule. At that edge, `abs(d(N.l)/d(theta_h))=2*hypot(projection,mu)`;
the visibility transition occurs at `N.l≈mu*alpha/view_root`. These determine
partition sizes, not approximations to the integrand. Broad features stay
unsplit. Both consecutive-refinement checks and all existing work-limit tests
remain active. Small, bounded `StaticVector` workspaces replace temporary heap
storage; monotone cuts avoid sorting each sample row.

At alpha one, the exact constant NDF and Smith roots avoid redundant arithmetic.
Schlick's fifth power uses multiplication; half-vector normalization uses a
direct norm only where its squared length is normal, retaining `hypot` for tiny
values. Independent sphere-surface integration reuses each azimuth's sine/cosine.
No compiler flags, quadrature-rule generator, generated certificates, test
matrix or acceptance threshold changed. No computed integral is cached between
tests. The finite-source tests remain the principal residual cost.

Certificate reports now retain the measured values and evaluation counts.
Maximum before/after changes over their queried directional and mean values
are **4.108e-15** and **1.666e-15**, respectively. Release also passes all
**27 native instrument tests**; their existing 23 photometry and 565 BRDF
physical-admission failures remain unchanged C obligations. All five changed
C++ files are oxytidy-clean.

Evidence under `ex07b`: `reference-speed-before-{debug,release}.json`,
`reference-speed-final-{debug,release}.json`, `reference-speed-native-release.json`,
`reference-speed-final-tidy/` and `reference-speed-checkpoint.json`. The checkpoint
records source/binary hashes, matching test names, per-test times and numerical
deltas. The earlier `reference-speed-static-debug.json` run used an old executable
during relinking and concurrent compiler load; it is explicitly excluded.

## Default material evaluation reference and native UV probe

`Reference/MaterialEvaluation.{h,cpp}` evaluates the default metallic/roughness
surface in double precision without renderer or shader helpers. Inputs separate
authored factors, resolved texture samples and the tangent frame. It covers
base-color/alpha multiplication, separate scalar maps, packed ORM with dedicated
AO override, HDR emission, normal scale, Gram-Schmidt tangent reconstruction,
handedness, degenerate-frame fallback, two-sided normals and alpha-cutoff
eligibility. Reflectance is bounded; nonfinite inputs and emission overflow fail
explicitly. Disabled texture sampling ignores samples, including invalid unused
values. The material roughness floor remains the BRDF evaluator's responsibility.

Samples are already transfer-decoded and format-expanded; a normal sample has
three encoded XYZ channels. This interface does not substitute for texture
format reconstruction, filtering, UV-set selection, actual G-buffer writes or
raster eligibility checks. Extended/procedural materials are outside this
default-material reference. Their absence does not qualify an unsupported
material for physical lighting.

The independent UV helper applies scale, counterclockwise rotation about the
origin and translation in that order. Native probe mode 20 executes production
`ApplyMaterialUv` for **36 UV0 cases**, including negative coordinates, mirrored
scale, tiling and rotation. Debug and Release report the same maximum absolute
error, **2.448e-6**, within the probe's coordinate-relative float tolerance.

Six new CPU checks take under a millisecond. Both configurations pass all
**10 focused material tests** and **28 native instrument tests**. The other
45-reference checkpoint remains recorded above; no slow integration rerun is
needed for these independent additions. All four added C++ files/headers are
oxytidy-clean. Actual sampled/raster material comparisons remain open in B.

Evidence under `ex07b`: `material-evaluation-{debug,release}.json`,
`material-evaluation-native-{debug,release}.json` and `.log`,
`material-evaluation-tidy-fixed/` and `material-evaluation-checkpoint.json`.

## Native sampled material and G-buffer producer checks

`MaterialRasterReference_test.cpp` renders real one-texel RGBA32-float maps
through the shared surface evaluator and deferred base pass, then reads the
actual R10G10B10A2 normal, RGBA8 scalar and RGBA8-sRGB base-color targets.
The independent material reference predicts the samples and packing; authored
UNORM16 factors are decoded independently from their stored integer values.
Scene-color readback checks HDR emission with fixed manual pre-exposure one.

The **144-case** matrix combines four texture layouts (separate maps, packed
ORM, packed ORM with separate AO, and sampling disabled), six surface cases
(front, two-sided back, culled back, accepted mask, discarded mask and tilted),
and normal scales `0/0.5/1/2/4/10`. Tilted cases exercise the octahedral fold.
There are **96 stored-material cases** and **48 rejected-surface cases**.
The renderer's existing normal-scale admission range is `[0,10]`; exploratory
negative-scale rows selected the material fallback and are not qualified as
renderable material inputs. This checkpoint changes no admission policy.

Stored codes use the [Direct3D float-to-UNORM and float-to-sRGB conversion rules,
sections 3.2.3.6 and 3.2.3.8](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm):
0.6 stored-code error plus 0.001 for float material arithmetic in this bounded
fixture. This is a producer-format check and does not widen the final
`2% + 2e-5` material/BRDF budget. Debug and Release both report maximum stored-code
error **0.501945526**, and emission meets its absolute `1e-5` check. Both tests
in the image-reference executable pass in both configurations; the final Release
binary also passes the material case after a trailing-comma formatting fix.
The added C++ file is oxytidy-clean. This adds only a few seconds of native test
work and no production instrumentation.

These results qualify the stated constant, three-channel float-map matrix and
actual G-buffer writes. They do not qualify compressed normal reconstruction,
nonconstant filtering/mips, alternate UV sets, extended materials or final
physical lighting. Those boundaries remain distinct from the already qualified
UV arithmetic and supplied-texel decoder probes.

Evidence under `ex07b`: `material-raster-{debug,release}.json` and `.log`,
`material-raster-final-release.json`, `material-raster-tidy-verified/` and
`material-raster-checkpoint.json`.

## Independent full-list GPU image reference

`UnculledLightingFixture` supplies a test-only compute pass with lights built
directly from the authored recipe and the independent CPU photometry reference.
It receives no cluster ranges, selection indices or renderer lighting publication;
the bound view constants explicitly contain invalid lighting routing. The pass
reconstructs the controlled planar receiver and evaluates every supplied light.
Deferred comparisons read the actual G-buffer material, while forward comparisons
use the fixture's known white, rough material. The no-light image supplies common
color and coverage, including translucent alpha.

The reference deliberately shares production attenuation and BRDF functions.
It qualifies selection, RGB tint/flux/EV transport and accumulation for this
fixture; it does not certify those functions against the physical model. The
independent physical probes and their recorded EX07C residuals remain authoritative.

The existing six forward/deferred and opaque/masked/translucent recipes now compare
31, 32 and 33 independently supplied lights: **331,776 additional RGB comparisons**
at the unchanged `0.5% + 2e-5` budget. A negative control physically removes the
strongest visible light from the renderer while retaining the full reference;
every recipe must detect the missing contribution. Serial-image, permutation
and zero-light recovery checks remain intact. The test shader is compiled only
for the native image-test target; no shipping pass or instrumentation is added.

Debug and Release pass both image tests in **7.638 s / 3.204 s**, respectively,
and the three affected tone-probe tests in each configuration. Both configurations'
largest full-list error consumes **0.006329733** of the image budget.
All eight changed C++/header files are oxytidy-clean across nine compilation
contexts. Both builds and all native runs use the Ninja build tree.

Evidence under `ex07b`: `unculled-{debug,release}.json`, `unculled-tone-{debug,release}.json`,
their logs, and `unculled-tidy-final/`. The prior RenderDoc capture documents the
raster light publication; it does not capture this newly added reference pass.

## Native CPU phase and GPU timeline coverage

The existing `CpuProfileScope` now covers these synchronous lighting owners:

| Scope                                     | Actual work covered                                                                    |
| ----------------------------------------- | -------------------------------------------------------------------------------------- |
| `Vortex.Lighting.GatherSelection`         | Scene traversal, selection and current transform resolution.                           |
| `Vortex.Lighting.BuildGrid`               | Input validation and per-view grid metadata.                                           |
| `Vortex.Lighting.ResolveEvaluation`       | Physical input validation/conversion and evaluation records; nested in `BuildGrid`.    |
| `Vortex.Lighting.PublishFrame`            | Complete-list construction, transient allocations, CPU writes and binding publication. |
| `Vortex.Lighting.BuildDeferredPackets`    | Deferred packet construction.                                                          |
| `Vortex.Lighting.RecordDeferred`          | Deferred command preparation/recording, including driver calls.                        |
| `Vortex.Shadows.RecordDepths`             | Shadow preparation, recording and publication.                                         |
| `Vortex.Lighting.PublishShadowReferences` | Final light-to-shadow binding publication.                                             |

Each owner caches its owning scope description, so steady-state entry does not
construct an allocating label. Scopes surround phases, not individual light
iterations. Collection remains in the existing profiling observer; no benchmark
buffer, file I/O or extra GPU query is added to production rendering. Compact
capture keeps non-overlapping outer owners; detailed capture also keeps nested
work, which must not be added to its parent. Gather/transform and
publication/allocation/write costs are currently combined intervals, not falsely
reported as separately timed subphases.

`Oxygen.Vortex.LightingInstrumentationGpu.Tests` uses the native published-view
fixture with a visible point light. The fixture omits final composition, so the
test explicitly resolves the existing GPU collector after scene submission and
before the next frame. This correctness fixture deliberately waits for the GPU;
its durations do **not** qualify complete-frame timing or collection overhead.
It verifies every expected CPU phase, absence of deferred-only phases in forward,
the exact GPU frame identity, native queue timestamp frequency, finite ordered
timestamps, nested interval containment, query capacity and the actual forward
base/deferred lighting scope. Limits are 256 CPU records and 128 GPU scopes.
Both configurations produce **8 CPU records / 22 GPU scopes** for deferred and
**6 CPU records / 21 GPU scopes** for forward, with no overflow or GPU diagnostic.

Debug and Release pass that native test, all **8 CPU collector tests** and both
image-reference tests. Release also passes the existing **17 timeline tests**
(including disabled collection, overflow, delayed frames, capacity growth,
failed resolves and incomplete recordings) and **2 native timestamp tests**
(independent resolve ranges and capacity rejection). Oxytidy covers all ten
changed C++/header files. Unchanged code retains 97 findings in the scene renderer,
deferred packet/pass implementation and publication test probe; no suppression
was added. The new native test and phase additions have no remaining findings.

Evidence under `ex07b`: `phase-{debug,release}.json`,
`phase-cpu-{debug,release}.json`, `phase-image-{debug,release}.json`,
`phase-profiler-release.json`, `phase-timestamps-release.json`,
`phase-tidy-final/` and `phase-native-tidy-final/`. Resource-accounting
qualification and native collection on/off overhead remain required before B
closes. Shared forward shading and shadow sampling still need matched-control
attribution rather than inventing independent GPU intervals.

## Bounded allocator-memory capture

`graphics::d3d12::Graphics::GetMemoryStatistics()` provides an on-demand snapshot
of D3D12MA's maintained counters and budget estimates. `MemoryStatistics` keeps
local and non-local segments separate and uses `SizeBytes` for byte quantities.
Allocation counts/bytes include resources whose owners still retain them for
deferred release. Block counts/bytes also include allocator-owned heap capacity
not occupied by an allocation. Their difference is **unallocated block capacity**,
not the size of cached textures or an exact process-residency measurement.
Process usage and budget remain explicitly labelled estimates and may include
objects outside D3D12MA. Usage exceeding budget is a valid pressure observation;
budget is neither physical adapter capacity nor the EX07 admission ceiling.

The query does not enumerate resources and is not called by production rendering.
The existing detailed placement probe remains useful for named resource shapes;
it must not substitute for allocator block totals. No allocator policy, memory
ceiling, depth/stencil format or resource lifetime changes in this checkpoint.

`Test/Support/D3D12MemoryCapture` reserves an explicitly typed sample capacity
before collection. Recording copies fixed-size snapshots without formatting,
allocation or I/O. Strong sample IDs distinguish multiple observations of one
frame. Overflow, invalid frame IDs, nonmonotonic samples/frames or allocation
bytes exceeding block bytes invalidate the capture, and report generation
refuses partial evidence. Reporting happens after collection. The caller
serializes collection and chooses meaningful sample boundaries; this is not a
transaction across concurrent allocators or an event log of every allocation.

The native fixture allocates a known device buffer, float texture and upload
buffer. An independent `ID3D12Device::GetResourceAllocationInfo` query predicts
their occupied bytes, and a native architecture query determines segment
placement. Caller references then move to the actual deferred reclaimer; weak
references and counter snapshots verify retention across other frame slots and
release when the owning slot cycles. Debug and Release both record:

| Observation                 | Local allocation bytes | Non-local allocation bytes |
| --------------------------- | ---------------------: | -------------------------: |
| Initial                     |                      0 |                          0 |
| Live and pending retirement |              1,179,648 |                  4,194,304 |
| Owning slot retired         |                      0 |                          0 |

The live local population is two allocations and the upload population one.
After retirement each segment retains an **8,388,608-byte** block with zero
occupied allocations. The recorder preserves this distinction instead of
reporting resource destruction as equivalent to returning all heap memory.
These exact sizes describe the reference RTX 3080 run; the test derives occupied
sizes and UMA placement at runtime rather than hardcoding that adapter result.
The controlled resources need no rendering commands, so this qualifies deferred
ownership/accounting, not a delayed GPU workload's lifetime correctness.

Debug and Release each pass **11 CPU instrumentation tests** and **2 native
instrument tests**, including the prior phase/timeline fixture. The new memory
files are oxytidy-clean. The changed backend implementation has 148 findings on
unchanged code; its added snapshot implementation has none. The broad header
consumer sweep also encounters an existing Clang error in
`PixFrameCaptureController.cpp` (a deduced return used before its definition);
the backend and new consumer compile contexts were checked separately, and both
MSVC Ninja configurations build. No warning suppression is added.

Evidence under `ex07b`: `memory-{debug,release}.json`,
`memory-cpu-{debug,release}.json`, build/run logs, `memory-new-tidy-final/`,
`memory-backend-tidy/` and the broad `memory-tidy/` attempt. The native JSON
contains all five raw segment snapshots. Workload-specific attribution,
allocation churn and between-sample peaks still require the benchmark integration;
live-count deltas alone must not be presented as allocation churn. Native
collection-overhead qualification also remains open.

## Successful factory-call churn

`Test/Support/ResourceCreationCounter` complements the allocator snapshot with
cumulative successful buffer/texture factory calls and requested buffer bytes.
It owns only fixed-size atomic counters, with no resource references, event
list, formatting or I/O. Concurrent writers are supported; exact window totals
are read at quiescent boundaries. Overflow permanently invalidates snapshots
instead of wrapping into plausible smaller counts. Buffer bytes describe the
request, not allocation alignment, heap growth or physical residency.

Only the native **test backend** invokes this counter, behind an opt-in flag;
no production factory or frame path is modified. The shared implementation lives
under `Test/Support`, and the exposure fixture library links that test-owned
support target. Tests do not depend on benchmarks. The detailed resource-shape
tracker remains a separate untimed instrument.

The native negative control creates and immediately destroys **17 buffers** and
**three textures**. Live allocation bytes return to the same values observed
before the calls, but the counter preserves all 20 successful creations and
**69,632 requested buffer bytes**. Disabling collection excludes a subsequent
creation. CPU tests additionally verify four concurrent writers and invalidation
after byte-count overflow. Debug/Release each pass **13 CPU instrument tests**
and **three native instrument tests**. This counts calls through these factories;
it does not infer driver-object allocations or allocator-internal heap operations.

Evidence under `ex07b`: `churn-cpu-{debug,release}.json`,
`churn-{debug,release}.json`, build/run logs and `churn-tidy-final/`.
The overhead benchmark will combine these event counts with the independently
qualified allocator snapshots; changes in live allocation counts are not a
substitute for event counts.

## Qualification boundary and next work

The [mean certificates](EX07B-mean-moment-certificates.md) now supply
enclosures at six roughness values, and Debug/Release C++ comparisons bound mean
error below 5.23e-9. Bound safety checks, formatter stability and full
byte-for-byte reproduction pass.

`estimated_absolute_change` is eight times the difference between successive
quadrature rules. Two consecutive refinements must satisfy the requested
`refinement_tolerance`. Power-of-two orders bound work and cached rules. This is
a convergence estimate, **not a proven absolute-error bound**. Analytic and high-precision endpoint anchors
and the current matrix do not yet certify the complete interior domain.

The independent certifier can qualify additional pointwise moment queries;
the C++ refinement estimator alone cannot. Mean queries likewise require their
own certificate; the six-query matrix cannot qualify arbitrary interpolation.
B still requires remaining material-format/filter qualification, workload
resource attribution/churn and native instrumentation-overhead qualification. The full-list image
reference qualifies the controlled punctual-light matrix above, not arbitrary
materials, receivers or shadowed/finite emitters. Production tables additionally require their own interpolation
certificate. No generated LUT or renderer change may claim those gates from
the current endpoint/foundation tests alone.
