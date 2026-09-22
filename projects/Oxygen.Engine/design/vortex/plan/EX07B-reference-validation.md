# EX07B reference validation

Status: **in_progress — independent GGX moment foundation implemented; B is not qualified.**

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

## Qualification boundary and next work

`estimated_absolute_change` is eight times the difference between successive
quadrature rules. Two consecutive refinements must satisfy the requested
`refinement_tolerance`. Power-of-two orders bound work and cached rules. This is
a convergence estimate, **not a proven absolute-error bound**. Analytic and high-precision endpoint anchors
and the current matrix do not yet certify the complete interior domain.

B still requires interior-domain independent-coordinate/high-precision checks
establishing moment uncertainty <=1e-5, mean moments, the coupled BRDF and furnace/reciprocity
checks, finite sphere/disk and photometric references, material decoding,
known-input GPU probes, deterministic matched-image fixtures and bounded
instrumentation. Production tables additionally require their own interpolation
certificate. No generated LUT or renderer change may claim those gates from
the current endpoint/foundation tests alone.
