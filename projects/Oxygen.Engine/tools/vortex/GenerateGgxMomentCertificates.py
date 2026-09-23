"""Generate bounded GGX moment certificates with python-flint==0.9.0.

This incoming-direction formulation is independent of the C++ reference's
half-vector quadrature. Every published radius includes binary64 export error.
"""

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

import flint
from flint import acb, arb, ctx


PRECISION_BITS = 160
RADIUS_LIMIT = 1e-8
ROUGHNESSES = (0.045, 0.1, 0.25, 0.5, 0.75, 1.0)
VIEW_COSINES = (0.0, 0.0001, 0.01, 0.1, 0.5, 0.9, 1.0)
DEFAULT_OUTPUT = (Path(__file__).resolve().parents[2]
                  / "src/Oxygen/Vortex/Test/Lighting/Reference/GgxMomentCertificates.json")


def integrate(function, end=1, *, tolerance="1e-12"):
    return acb.integral(function, 0, end, abs_tol=arb(tolerance),
                        rel_tol=arb(tolerance), eval_limit=100000,
                        depth_limit=60)


def energy_integrand(u, view, alpha2, analytic):
    """Exact azimuth reduction; only the incoming cosine remains to integrate."""
    total = u + view
    root_squared = ((u - view)**2
                    + 2 * alpha2 * ((1 - u*u) + (1 - view*view))
                    + alpha2**2 * total**2)
    root = root_squared.sqrt(analytic=analytic)
    middle = ((1 - u*u) + (1 - view*view) + alpha2 * total**2) / 2
    loss = 1 - alpha2
    azimuth = 1 + 2 * loss * total / root + 2 * loss**2 * total * middle / root**3
    view_root = (view**2 + alpha2 * (1 - view**2)).sqrt()
    light_root = (u**2 + alpha2 * (1 - u**2)).sqrt(analytic=analytic)
    return alpha2 * u * azimuth / (u * view_root + view * light_root)


def grazing_bias(alpha):
    cosine_integrals = (acb(1), acb.pi()/4, acb(2)/3,
                        3*acb.pi()/16, acb(8)/15, 5*acb.pi()/32)
    coefficients = (1, -5, 10, -10, 5, -1)

    def integrand(q, analytic):
        sine, cosine = q.sin(), q.cos()
        sin_h = alpha * sine / (cosine**2 + alpha**2 * sine**2).sqrt(analytic=analytic)
        azimuth = sum(coefficients[k] * sin_h**k * cosine_integrals[k] for k in range(6))
        return 4 / acb.pi() * sine**2 * azimuth

    return integrate(integrand, arb.pi()/2)


def schlick_moment(view, alpha2, uniform_ndf):
    view_sine = (1 - view**2).sqrt()
    view_root = (view**2 + alpha2 * (1 - view**2)).sqrt()
    point_radius = arb(2)**-80

    def outer(u, analytic):
        # Define a zero extension outside the real integration domain. Arb's
        # rounded interval radii may extend slightly beyond an endpoint.
        inside = u.real > 0 and u.real < 1
        if analytic and not inside:
            return acb("nan")
        if not analytic:
            if not u.imag.is_zero():
                return acb("nan")
            if u.real.upper() < 0 or u.real.lower() > 1:
                return acb(0)
            if not (u.real >= 0 and u.real <= 1):
                # Evaluate the albedo enclosure on the rounded superset; it
                # bounds the valid [0,1] portion. Zero bounds the extension.
                energy = energy_integrand(u, view, alpha2, False)
                if energy.is_finite():
                    upper = energy.real.abs_upper()
                else:
                    # D<=1/(pi*alpha^2), V*u<=1/(2*alpha): universal bound.
                    upper = (1/(alpha2.real * alpha2.real.sqrt())).abs_upper()
                return acb(arb(upper/2, upper/2))
        angular = (1 - u*u).sqrt(analytic=analytic) * view_sine
        total = u + view
        square = total**2
        plus = 1 + u*view + angular
        middle = ((1 - u*u) + (1 - view*view) + alpha2*square) / 2
        denominator_plus = middle + angular
        stable_discriminant = ((u - view)**2
                               + 2*alpha2*((1 - u*u) + (1 - view*view))
                               + alpha2**2*square)
        denominator_minus = square*stable_discriminant / (4*denominator_plus)
        q_minus = square / plus
        light_root = (u*u + alpha2*(1 - u*u)).sqrt(analytic=analytic)
        # Includes the factor-two azimuth symmetry and V's factor one-half.
        prefactor = alpha2*u / (arb.pi() * (u*view_root + view*light_root))
        if not (angular + prefactor + denominator_minus + q_minus).is_finite():
            return acb("nan")

        def value(t, branch_check):
            q = q_minus + 2*angular*t
            denominator = denominator_minus + 2*angular*t
            ratio = 1 if uniform_ndf else (q/denominator)**2
            return prefactor * ratio * (1 - (q/2).sqrt(analytic=branch_check))**5

        if analytic:
            # Prove branches on the complete real azimuth path for this complex
            # u domain. An analytic callback needs an enclosure, not a point fit.
            # Prove Re(q)>0 coefficientwise. Combining the entire azimuth
            # interval in one ball can lose a small positive lower endpoint.
            if not q_minus.real.lower() > 0 or not angular.real.lower() >= 0:
                return acb("nan")
            q_bound = abs(q_minus).abs_upper() + 2*abs(angular).abs_upper()
            fresnel_bound = (1 + (q_bound/2).sqrt())**5
            if uniform_ndf:
                majorant = arb.pi() * abs(prefactor).abs_upper() * fresnel_bound
            else:
                d0 = denominator_minus.real.lower()
                d1 = 2 * angular.real.lower()
                if not d0 > 0 or not d1 > 0:
                    return acb("nan")
                q0 = abs(q_minus).abs_upper()
                q1 = 2 * abs(angular).abs_upper()
                constant = q1/d1
                remainder = abs(q0 - constant*d0).abs_upper()
                discriminant = d0*(d0+d1)
                root = discriminant.sqrt()
                first = arb.pi()/root
                second = arb.pi()*(2*d0+d1)/(2*root**3)
                majorant = (abs(prefactor).abs_upper() * fresnel_bound
                            * (arb.pi()*constant**2
                               + 2*constant*remainder*first + remainder**2*second))
            bound = majorant.abs_upper()
            # Both components enclose the complex-valued integral on the domain.
            return acb(arb(0, bound), arb(0, bound))

        if u.real.rad() > point_radius:
            # On a real interval, 0 <= Fresnel factor <= 1 supplies an enclosure.
            energy = energy_integrand(u, view, alpha2, False)
            if not energy.is_finite():
                return acb("nan")
            upper = energy.real.abs_upper()
            return acb(arb(upper/2, upper/2))
        return integrate(lambda phi, check: value((phi/2).cos()**2, check), arb.pi())

    return integrate(outer, tolerance="1e-10")


def certify(roughness, mu):
    if not math.isfinite(roughness) or not 0 <= roughness <= 1:
        raise ValueError("Roughness must be finite and in [0,1]")
    if not math.isfinite(mu) or not 0 <= mu <= 1:
        raise ValueError("View cosine must be finite and in [0,1]")
    # Float constructors preserve the query's binary64 values. The model's
    # floor is the exact decimal constant, before any binary64 approximation.
    effective = acb("0.045") if roughness <= 0.045 else acb(roughness)
    alpha = effective**2
    alpha2 = alpha**2
    view = acb(mu)
    if mu == 0:
        return acb(1), grazing_bias(alpha)
    energy = integrate(lambda u, flag: energy_integrand(u, view, alpha2, flag))
    if mu == 1:
        bias = integrate(lambda u, flag: energy_integrand(u, view, alpha2, flag)
                         * (1 - ((1+u)/2).sqrt(analytic=flag))**5)
    else:
        bias = schlick_moment(view, alpha2, roughness == 1)
    return energy, bias


def export_ball(value):
    if not value.is_finite() or not value.imag.contains(0):
        raise RuntimeError(f"Unqualified integral: {value}")
    midpoint = float(value.real.mid())
    radius_bound = (value.real - arb(midpoint)).abs_upper()
    radius = 0.0 if radius_bound == 0 else math.nextafter(float(radius_bound), math.inf)
    if not arb(radius) >= radius_bound:
        raise RuntimeError("Exported radius does not enclose binary64 rounding")
    if radius > RADIUS_LIMIT or not value.imag.abs_upper() <= arb(RADIUS_LIMIT):
        raise RuntimeError(f"Certificate exceeds {RADIUS_LIMIT}: {value}")
    return midpoint, radius


def generate():
    if flint.__version__ != "0.9.0" or flint.__FLINT_VERSION__ != "3.6.0":
        raise RuntimeError("Use python-flint==0.9.0 with FLINT 3.6.0")
    ctx.prec = PRECISION_BITS
    cases = []
    endpoint_path = DEFAULT_OUTPUT.with_name("GgxEndpointReference.json")
    endpoints = json.loads(endpoint_path.read_text())
    anchors = {(row["roughness"], row["view_cosine"]): row for row in endpoints["cases"]}
    for roughness in ROUGHNESSES:
        alpha = roughness * roughness
        views = sorted(set(VIEW_COSINES) | {factor*alpha for factor in (0.5, 1, 2) if factor*alpha <= 1})
        for mu in views:
            energy, bias = certify(roughness, mu)
            if (roughness, mu) in anchors:
                anchor = anchors[(roughness, mu)]
                if not energy.real.contains(arb(anchor["directional_albedo"])) or not bias.real.contains(arb(anchor["schlick_moment"])):
                    raise RuntimeError("Certificate disagrees with independent endpoint data")
            if roughness == 1:
                view = arb(mu)
                analytic_energy = arb(1) if mu == 0 else 1 - view*(1 + 1/view).log()
                if not energy.real.contains(analytic_energy):
                    raise RuntimeError("Certificate disagrees with analytic rough-surface albedo")
            e_mid, e_radius = export_ball(energy)
            b_mid, b_radius = export_ball(bias)
            cases.append({"roughness": roughness, "view_cosine": mu,
                          "E_midpoint": e_mid, "E_radius": e_radius,
                          "B_midpoint": b_mid, "B_radius": b_radius})
            print(f"certified r={roughness} mu={mu}: radius={max(e_radius, b_radius):.3e}", flush=True)
    return {"generated_by": "tools/vortex/GenerateGgxMomentCertificates.py",
            "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "model_revision": 1, "python_flint_version": flint.__version__,
            "endpoint_anchor_sha256": hashlib.sha256(endpoint_path.read_bytes()).hexdigest(),
            "flint_version": flint.__FLINT_VERSION__,
            "input_encoding": "exact binary64 inputs",
            "roughness_floor": "0.045",
            "working_precision_bits": PRECISION_BITS,
            "radius_limit": RADIUS_LIMIT,
            "scope": "Pointwise E/B enclosures; not an interpolation or full BRDF certificate",
            "cases": cases}


def encode_document(data):
    """Match repository JSON exponent formatting without changing strings."""
    tokens = re.compile(r'"(?:[^"\\]|\\.)*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?')

    def canonical_token(match):
        token = match.group(0)
        if token.startswith('"'):
            return token
        return re.sub(r'([eE][+-]?)0+(\d+)', r'\1\2', token)

    text = tokens.sub(canonical_token, json.dumps(data, indent=2))
    return (text + "\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = generate()
    content = encode_document(data)
    if args.check:
        if not args.output.exists() or args.output.read_bytes() != content:
            raise RuntimeError("Certificate data is stale; rerun its generator")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(content)
    print(f"Verified {len(data['cases'])} GGX certificates: {args.output}")


if __name__ == "__main__":
    main()
