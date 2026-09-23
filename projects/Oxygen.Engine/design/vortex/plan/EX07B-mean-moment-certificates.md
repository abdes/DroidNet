# EX07B mean-moment certificates

Status: **validated for the six queried roughness values; interpolation and full BRDF/image gates remain separate.**

The [reference validation owner](EX07B-reference-validation.md) separates this
gate from production interpolation, BRDF/image qualification and the rest of B.
The cosine-weighted means below belong to the independent reciprocal reference.
[Production model 2](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2)
uses directional E/B only and has no runtime mean texture.

## Independent integration

`tools/vortex/GenerateGgxMeanMomentCertificates.py` reuses the rigorous
[incoming-direction certifier](EX07B-moment-certificates.md), independently of
the C++ half-vector quadrature. It integrates the mean in logarithmic view cosine
with FLINT/Arb, packing E and B into the real/imaginary components of one integral:

```text
v = exp(t)
integrand(t) = 2*v^2 * (E(v) + i*B(v))
```

Directional calculations have tighter requested tolerances than the outer
integral: E 1e-14, B 1e-13, its inner azimuth integral 1e-16, outer mean 1e-12.
Every published mean must independently pass the final 1e-8 radius limit.
An integration request's tolerance is not assumed to equal its returned radius.
In particular, [FLINT's integration documentation](https://flintlib.org/doc/acb_calc.html)
defines its tolerance per subinterval and warns that cumulative error can be
larger. The export gate checks the resulting enclosure, including all nested
uncertainty and endpoint contributions.

The intervals below `v=1e-4` and above `v=1-1e-10` are explicitly enclosed using
`0<=B<=E<=1`. Each omitted contribution lies in `[0, lower^2+1-upper^2]`.
The midpoint and an approximately 5.1e-9 radius enter the result; these intervals
are not discarded or treated as exact zero. Binary64 export rounding is included
using the pointwise generator's outward-radius check.

## Moving-contour bound

[Arb's analytic callback](https://python-flint.readthedocs.io/en/latest/acb.html)
requires an enclosure and proof of analyticity over its complex domain. Real
samples or a quadrature-refinement difference cannot replace that requirement.
Keeping the incoming cosine fixed while the view becomes complex makes the
microfacet poles unnecessarily restrictive for the outer bound. Instead split
the incoming integral at the view cosine:

```text
lower piece: u = s*v,          du = v*ds
upper piece: u = v+s*(1-v),    du = (1-v)*ds,   s in [0,1]
```

For real v, these are exactly the original two parts of `[0,1]`. Their complex
extensions move with v. Let `v0` be the real midpoint, `delta>=|v-v0|` and
`eta=delta/min(v0,1-v0)`. This bounds the relative variations of u, 1-u, u+v,
u-v and the two Jacobians; zero endpoint factors are interpreted by their
continuous factored limits. The same bound conservatively covers 1+u.

Write `a=alpha^2`, `s_m=sqrt(1-m^2)` and
`R_m=sqrt(a+(1-a)*m^2)`. In the incoming-direction integrand,
`q=1+u*v+s_u*s_v*cos(phi)` and the NDF denominator has the stable decomposition:

```text
d = (u+v)^2/2 * (a + (u-v)^2/(s_u+s_v)^2)
    + s_u*s_v*(1+cos(phi))
q = d with a=1
```

Every real-reference summand is nonnegative. Define:

```text
e2 = 2*eta+eta^2
es = e2/(1+sqrt(1-e2))
ei = (2*es+es^2)/(1-es)^2
ed = (1+e2)^2*(1+ei)-1
ev = (1+eta)*(1+es)-1
C  = (1+eta)^2/(1-ev) * ((1+ed)/(1-ed))^2
```

Require `e2<1`, `ed<1` and `ev<1`. The normalized square-root arguments then
avoid their branch cuts; d, q and visibility denominators stay nonzero. The
positive-summand decomposition bounds d/q relative errors by ed, and the
visibility denominator's error by ev. The numerator/Jacobian contributes the
first `(1+eta)^2` factor. These bounds do not shrink with alpha.

The complex energy density is bounded by C times the real energy density at v0.
For the Schlick factor, real `q0/2=c^2` has `0<c<=1` and `|q/q0-1|<1`.
Rationalizing the square-root difference bounds
`|sqrt(q/q0)-1|` by a number below one, so
`|1-c*sqrt(q/q0)| <= (1-c)+c*|1-sqrt(q/q0)| <= 1`.
The same energy-density majorant therefore also bounds B.

Finally E(v0)<=1: under the normalized visible-normal distribution, its
single-scattering integral is the accepted-reflection probability weighted by
`G2/G1(v0)<=1`. The visible-normal formulation is described by
[Heitz](https://www.jcgt.org/published/0007/04/01/paper.pdf).
Thus `4*|v|^2*C` bounds the packed logarithmic-cosine integrand. Domains failing
any guard return a nonfinite ball for subdivision, not an unsupported analytic
claim.

## Wider sector bound

The implementation also uses a sector bound when it is valid and tighter.
With `eta<1/4` and `theta=asin(eta)`, each normalized affine factor lies in the
sector `|arg|<=theta` with modulus between `1-eta` and `1+eta`.
The factored forms of `s_u` and `s_v` retain the same sector and modulus bounds.
Their sum has modulus at least `cos(theta)*(1-eta)` times its real-reference
sum. The three positive-reference terms in d and q therefore lie in the sector
`|arg|<=6*theta<pi/2`, with common relative modulus bounds

```text
L = (1-eta)^4/(1+eta)^2
U = (1+eta)^4/(cos(theta)^2*(1-eta)^2)
Re(d) >= cos(6*theta)*L*d0
|q| <= U*q0
```

For visibility, `R_u` and `R_v` have phase at most theta and modulus at least
`sqrt(cos(2*theta))*(1-eta)` times their real-reference values. Thus

```text
Re(u*R_v+v*R_u) >= cos(2*theta)^(3/2)*(1-eta)^2*(u0*R_v0+v0*R_u0)
C_sector = ((1+eta)/(1-eta))^2 / cos(2*theta)^(3/2)
           * (U/(cos(6*theta)*L))^2
```

These positive-real-part bounds establish holomorphicity without requiring a
small relative perturbation of the complete denominator. For Fresnel,
`sqrt(q/q0)` has modulus at most `sqrt(U)` and phase at most `3*theta`.
Convexity first in `c=sqrt(q0/2)` and then in modulus gives
`|1-sqrt(q/2)|^2 <= max(1,1+U-2*sqrt(U)*cos(3*theta))`.
Raise that bound to 5/2 and multiply C_sector; this bounds B as well as E.
The smaller upper endpoint of the two valid bounds is used. Exact polynomial
identities evaluate cos(2*theta), cos(3*theta) and cos(6*theta) in interval
arithmetic. A nonfinite/unsupported domain still returns a nonfinite enclosure.

## Validation evidence

Seven new Python tests check endpoint/branch-domain rejection, finite complex
enclosures, both moving pieces' energy and Schlick densities against their
majorant across roughness/grazing cases, endpoint inclusion, rejection of the
wrong unweighted measure, invalid roughness and tighter nested precision.
All seven pass, as do the six existing pointwise-generator tests. The pointwise API now exposes nested
tolerances; regenerating its 55 certificates changes only the source hash.

The generated matrix covers roughness 0.045, 0.1, 0.25, 0.5, 0.75 and 1.
Every exported radius is below **5.13e-9**. The roughness-one enclosure contains
the independent analytic means `4/3*(1-ln(2))` and `111/35-(32/7)*ln(2)`.

The C++ comparison passes in Debug and Release. Its maximum distance-to-truth
bounds are **5.228e-9 for E_avg** and **5.229e-9 for B_avg**, below the frozen
1e-5 budget. The unweighted-mean negative control is rejected. Debug also passes
the unchanged 36-test reference suite, giving 38 passing cases across the two
runs. The new C++ file is oxytidy-clean, and the generated JSON passes the
repository formatter byte-unchanged.

Full byte-for-byte regeneration passes. The generator also rejects source
changes during a run rather than publishing results under a mismatched hash.
These are certificates for the queried means. Additional reference queries must
pass the same export gate; this matrix is not a production interpolation table.

```powershell
uv run --no-project --with python-flint==0.9.0 python tools/vortex/GenerateGgxMeanMomentCertificates.py --jobs 6
uv run --no-project --with python-flint==0.9.0 python tools/vortex/GenerateGgxMeanMomentCertificates.py --jobs 6 --check
uv run --no-project --with python-flint==0.9.0 python -m unittest discover -s tools/vortex -p 'TestGgx*Certificates.py' -v
```

This is an offline qualification command; the first full six-case generation
took about 32 minutes on the reference machine. It is not a build-time or frame
rendering operation. C++ tests consume the versioned enclosures.

Evidence under `out/build-ninja/analysis/vortex/exposure-lightbench/ex07b`:
`mean-certificate-{generation,reproduction,generator-tests}.log`,
`mean-certificate-existing-debug.json`, `mean-certificate-{debug,release}.json`,
`mean-certificate-tidy-final/` and `mean-certificate-checkpoint.json`.
