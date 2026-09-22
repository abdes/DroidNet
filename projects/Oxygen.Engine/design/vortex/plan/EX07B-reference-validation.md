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

## Qualification boundary and next work

`estimated_absolute_change` is eight times the difference between successive
quadrature rules. Two consecutive refinements must satisfy the requested
`refinement_tolerance`. Power-of-two orders bound work and cached rules. This is
a convergence estimate, **not a proven absolute-error bound**. Analytic anchors
and the current matrix do not yet certify the complete roughness/grazing domain.

B still requires independent-coordinate/high-precision cross-checks establishing
moment uncertainty <=1e-5, mean moments, the coupled BRDF and furnace/reciprocity
checks, finite sphere/disk and photometric references, material decoding,
known-input GPU probes, deterministic matched-image fixtures and bounded
instrumentation. Production tables additionally require their own interpolation
certificate. No generated LUT or renderer change may claim those gates from
this six-test foundation alone.
