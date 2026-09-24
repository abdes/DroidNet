# EX07B pointwise moment certificates

Status: **validated for the recorded 55 parameter pairs.** Subsequent
[B completion](EX07B-completion-audit.md) and [F acceptance](EX07F-acceptance-report.md)
close their respective gates; this record preserves the pointwise checkpoint.

The [independent moment implementation](EX07B-reference-validation.md#independent-moment-implementation)
defines E/B; the reference uncertainty budget here is <=1e-5. This checkpoint adds
an independent, bounded-error oracle for E/B queries. It does not qualify a
production LUT's interpolation or the complete BRDF/finite-source renderer.

## Method and independence

[`GenerateGgxMomentCertificates.py`](../../../tools/vortex/GenerateGgxMomentCertificates.py)
uses python-flint 0.9.0 / FLINT 3.6.0 at 160-bit precision. Inputs are exact
binary64 values; the model's 0.045 roughness floor is retained as an exact
decimal constant. It evaluates incoming-direction integrals, independently of
the C++ reference's half-vector quadrature.

[FLINT's integration contract](https://flintlib.org/doc/acb_calc.html)
provides rigorous enclosures using interval arithmetic and quadrature error
bounds. Its analytic callback must prove holomorphicity on the supplied complex
domain. The [Python API](https://python-flint.readthedocs.io/en/latest/acb.html#flint.acb.integral)
specifically requires branch checks for square roots. The generator preserves
those checks and rejects nonfinite or excessively wide results.

For incoming cosine u, view cosine v, a=alpha^2, z=u+v and k=1-a, define:

```text
P = (1-u^2) + (1-v^2)
Q = (u-v)^2 + 2*a*P + a^2*z^2
M = (P + a*z^2)/2
R = sqrt(Q)
```

Analytically integrating the GGX distribution over azimuth gives the albedo
integrand:

```text
a*u / (u*sqrt(v^2+a*(1-v^2)) + v*sqrt(u^2+a*(1-u^2)))
  * (1 + 2*k*z/R + 2*k^2*z*M/R^3)
```

Its integral over u in [0,1] is E(v). This positive-term discriminant avoids
subtracting nearly equal azimuth terms. Grazing E is the exact limit one.

B retains an inner azimuth integral. Let b=sqrt((1-u^2)*(1-v^2)),
q=1+u*v+b*cos(phi), C=k*z^2/2 and d=q-C. Its integrand on phi in [0,pi] is
`a*u*(q/d)^2*(1-sqrt(q/2))^5 / (pi*denominator)`, where denominator is the
visibility denominator above. Azimuth symmetry is already included.

The outer analytic callback uses a conservative complex-domain majorant rather
than a sampled fit. Coefficientwise positive real parts prove that q avoids the
square-root cut and d avoids zero over the complete azimuth path. If
`|q|<=q0+q1*t` and `Re(d)>=d0+d1*t`, with t=cos(phi/2)^2 and positive d0/d1,
then `|q/d|<=c0+c1/(d0+d1*t)` for
`c0=q1/d1`, `c1=|q0-c0*d0|`. Its squared integral is bounded using:

```text
J1 = pi/sqrt(d0*(d0+d1))
J2 = pi*(2*d0+d1)/(2*(d0*(d0+d1))^(3/2))
integrated ratio bound = pi*c0^2 + 2*c0*c1*J1 + c1^2*J2
```

Triangle bounds cover the remaining Fresnel/prefactor terms. Domains that fail
the proof return nonfinite bounds for further subdivision. Real base intervals
use 0<=B_integrand<=E_integrand. A zero extension outside [0,1], with conservative
bounds on rounded endpoint intervals, handles interval-radius overshoot without
changing the target integral. Normal and grazing endpoints have separate
one-dimensional reductions and are checked against the independent endpoint
data; roughness-one E is checked against its analytic expression at every view.

Optional directional/azimuth tolerance parameters support the nested
[mean certifier](EX07B-mean-moment-certificates.md). The pointwise defaults and
all 55 numerical enclosures remain unchanged after that API extension.

## Results and reproduction

The [generated certificates](../../../src/Oxygen/Vortex/Test/Lighting/Reference/GgxMomentCertificates.json)
cover six roughness values, seven base view cosines and additional samples around
mu=alpha/2, alpha and 2*alpha. Distinct binary64 values are retained. Each result
exports a midpoint and an outward-rounded radius, including midpoint conversion
error; both real and imaginary uncertainty must fit the fixed 1e-8 radius limit.
The largest recorded radius is **1.454e-9**.

The C++ reference is compared directly against those enclosures. Its stopping
estimate does not serve as the certificate. Across the matrix, the maximum
distance-to-truth bounds are **2.260e-11 for E** and **1.710e-9 for B**, below the
frozen 1e-5 budget. C++ bound arithmetic rounds outward. A double-azimuth negative
control is rejected.

Ten C++ tests pass in Debug and Release. Six generator tests cover analytic
branch rejection, binary64 export rounding, invalid/wide-result rejection,
the rough-surface analytic value, invalid inputs and token-safe JSON formatting.
The new C++ file is oxytidy-clean. Formatting and generation checks leave the
generated file byte-identical:

```powershell
uv run --no-project --with python-flint==0.9.0 python tools/vortex/GenerateGgxMomentCertificates.py
uv run --no-project --with python-flint==0.9.0 python tools/vortex/GenerateGgxMomentCertificates.py --check
uv run --no-project --with python-flint==0.9.0 python -m unittest discover -s tools/vortex -p TestGgxMomentCertificates.py -v
```

Evidence under `out/build-ninja/analysis/vortex/exposure-lightbench/ex07b`:
`moment-certificate-{debug,release}.json`, `moment-certificate-generator-tests.log`,
`moment-certificate-format-check.log`, `moment-certificate-tidy-verified/` and
`moment-certificate-checkpoint.json`.

## Remaining qualification

These are pointwise certificates. An arbitrary C++ quadrature result is still
an estimate until checked; additional requested reference nodes must pass the
certifier's radius gate. Do not interpolate the 55-point validation matrix as
production data. The [reference validation owner](EX07B-reference-validation.md)
records the subsequent mean, coupled-BRDF, finite-source, photometric and
material-decoding implementations and their actual qualification boundaries.
Broader numerical/image fixtures and instrumentation remain required in B.
The production table additionally needs its interpolation and final-image gates.
