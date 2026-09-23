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
B still requires complete material evaluation and RGB light-tint transport,
known-input GPU probes, deterministic matched-image fixtures and bounded
instrumentation. Production tables additionally require their own interpolation
certificate. No generated LUT or renderer change may claim those gates from
the current endpoint/foundation tests alone.
