"""Certify cosine-weighted GGX means with python-flint==0.9.0.

The outer holomorphic bound uses incoming contours split at the view cosine.
Directional integrals retain the independent incoming-direction certifier.
"""

import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import math
from pathlib import Path

import flint
from flint import acb, arb, ctx

import GenerateGgxMomentCertificates as pointwise


DEFAULT_OUTPUT = pointwise.DEFAULT_OUTPUT.with_name("GgxMeanMomentCertificates.json")
LOWER_COSINE = "0.0001"
UPPER_GAP = "1e-10"
OUTER_TOLERANCE = "1e-12"
ENERGY_TOLERANCE = "1e-14"
DIRECTIONAL_TOLERANCE = "1e-13"
AZIMUTH_TOLERANCE = "1e-16"
OUTER_EVALUATION_LIMIT = 500000


def relative_comparison(eta):
    square_error = 2*eta + eta*eta
    if not square_error < 1:
        return None
    root_error = square_error/(1 + (1-square_error).sqrt())
    inverse_square_error = (2*root_error + root_error*root_error)/(1-root_error)**2
    denominator_error = (1+square_error)**2*(1+inverse_square_error) - 1
    visibility_error = (1+eta)*(1+root_error) - 1
    if not denominator_error < 1 or not visibility_error < 1:
        return None
    return ((1+eta)**2/(1-visibility_error)
            * ((1+denominator_error)/(1-denominator_error))**2)


def sector_comparison(eta):
    """A wider holomorphic domain, using positive-real-part sectors."""
    if not eta < arb(1)/4:
        return None
    square = eta*eta
    # theta=asin(eta); 6*theta<pi/2. These polynomial forms are exact identities.
    cos2 = 1-2*square
    cos6 = 4*cos2**3-3*cos2
    lower = (1-eta)**4/(1+eta)**2
    upper = (1+eta)**4/((1-square)*(1-eta)**2)
    comparison = (((1+eta)/(1-eta))**2/(cos2*cos2.sqrt())
                  * (upper/(cos6*lower))**2)
    # For q/q0 in the sector, sqrt(q/q0) has phase <=3*theta and modulus
    # <=sqrt(upper). Convexity in c=sqrt(q0/2) in [0,1] puts the maximum
    # |1-c*sqrt(q/q0)| at an endpoint; then maximize its modulus similarly.
    cos3 = (1-square).sqrt()*(1-4*square)
    fresnel_square = 1+upper-2*upper.sqrt()*cos3
    fresnel = arb(1) if fresnel_square < 1 else fresnel_square.abs_upper().sqrt()**5
    return comparison*fresnel


def moving_contour_comparison(view):
    """Bound |I(view)| relative to a positive real-view energy integrand.

    Split u in [0,1] as u=t*v and u=v+t*(1-v). Relative variations of u,
    1-u, u+v, u-v and both Jacobians are bounded by eta. The decomposition
    d=(u+v)^2*(a+(u-v)^2/(s_u+s_v)^2)/2+s_u*s_v*(1+cos(phi))
    has positive real summands; their relative bounds do not depend on a.
    """
    if not view.is_finite():
        return None
    center = view.real.mid()
    if not center > 0 or not center < 1:
        return None
    delta = abs(view - acb(center)).abs_upper()
    # center is an exact midpoint, so the two gap comparisons are unambiguous.
    gap = min(center, 1-center)
    eta = delta/gap
    relative = relative_comparison(eta)
    sector = sector_comparison(eta)
    if relative is None:
        return sector
    if sector is None or relative.upper() < sector.upper():
        return relative
    return sector


def analytic_mean_enclosure(view):
    comparison = moving_contour_comparison(view)
    if comparison is None:
        return acb("nan")
    # E(v0)<=1. Both comparison bounds already cover the Schlick factor,
    # so each also bounds B using the real unit-Fresnel energy integral.
    # Log-cosine measure is 2*v^2; packing E+i*B contributes another factor 2.
    bound = (4*abs(view).abs_upper()**2*comparison).abs_upper()
    return acb(arb(0, bound), arb(0, bound))


def endpoint_enclosure(lower, upper):
    # 0<=B<=E<=1 gives the exact cosine measure of both endpoint intervals.
    measure = lower*lower + (1-upper)*(1+upper)
    return arb(measure/2, measure/2)


def certify_means(roughness, *, progress=False):
    if not math.isfinite(roughness) or not 0 <= roughness <= 1:
        raise ValueError("Roughness must be finite and in [0,1]")
    ctx.prec = pointwise.PRECISION_BITS
    effective = acb("0.045") if roughness <= 0.045 else acb(roughness)
    alpha2 = effective**4
    lower = arb(LOWER_COSINE)
    upper = 1-arb(UPPER_GAP)
    point_radius = arb(2)**-80
    evaluations = 0

    def outer(log_view, analytic):
        nonlocal evaluations
        view = log_view.exp()
        if analytic:
            return analytic_mean_enclosure(view)
        if not view.imag.is_zero() or not view.real >= 0 or not view.real <= 1:
            return acb("nan")
        if view.real.rad() > point_radius:
            upper_bound = view.real.upper()**2
            return acb(arb(upper_bound, upper_bound), arb(upper_bound, upper_bound))
        evaluations += 1
        if progress and evaluations % 32 == 0:
            print(f"mean r={roughness}: {evaluations} directional queries; "
                  f"mu={float(view.real.mid()):.8g}", flush=True)
        energy = pointwise.integrate(
            lambda u, flag: pointwise.energy_integrand(u, view, alpha2, flag),
            tolerance=ENERGY_TOLERANCE)
        bias = pointwise.schlick_moment(
            view, alpha2, roughness == 1,
            tolerance=DIRECTIONAL_TOLERANCE, angular_tolerance=AZIMUTH_TOLERANCE)
        if not (energy.is_finite() and bias.is_finite()
                and energy.imag.contains(0) and bias.imag.contains(0)):
            raise RuntimeError("Directional integration did not produce a real enclosure")
        # The real physical moments are known to be real; their independent
        # imaginary numerical enclosures have just been checked to contain zero.
        return acb(2*view.real**2*energy.real, 2*view.real**2*bias.real)

    result = acb.integral(
        outer, lower.log(), upper.log(), abs_tol=arb(OUTER_TOLERANCE),
        rel_tol=arb(OUTER_TOLERANCE), eval_limit=OUTER_EVALUATION_LIMIT, depth_limit=60)
    tail = endpoint_enclosure(lower, upper)
    result += acb(tail, tail)
    if not result.is_finite():
        raise RuntimeError("Mean integration exhausted its bounded work")
    return acb(result.real), acb(result.imag), evaluations


def certify_case(roughness):
    energy, bias, evaluations = certify_means(roughness, progress=True)
    if roughness == 1:
        analytic_energy = arb(4)/3*(1-arb(2).log())
        analytic_bias = arb(111)/35-arb(32)/7*arb(2).log()
        if not energy.real.contains(analytic_energy) or not bias.real.contains(analytic_bias):
            raise RuntimeError("Mean certificate disagrees with roughness-one analytic integrals")
    e_midpoint, e_radius = pointwise.export_ball(energy)
    b_midpoint, b_radius = pointwise.export_ball(bias)
    print(f"certified mean r={roughness}: radius={max(e_radius, b_radius):.3e}", flush=True)
    return {"roughness": roughness, "E_avg_midpoint": e_midpoint,
            "E_avg_radius": e_radius, "B_avg_midpoint": b_midpoint,
            "B_avg_radius": b_radius, "directional_queries": evaluations}


def generate(roughnesses, jobs):
    if flint.__version__ != "0.9.0" or flint.__FLINT_VERSION__ != "3.6.0":
        raise RuntimeError("Use python-flint==0.9.0 with FLINT 3.6.0")
    if jobs < 1:
        raise ValueError("At least one worker is required")
    source_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    directional_hash = hashlib.sha256(Path(pointwise.__file__).read_bytes()).hexdigest()
    if jobs == 1:
        cases = [certify_case(roughness) for roughness in roughnesses]
    else:
        with ProcessPoolExecutor(max_workers=jobs) as workers:
            cases = list(workers.map(certify_case, roughnesses))
    if (source_hash != hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
            or directional_hash != hashlib.sha256(Path(pointwise.__file__).read_bytes()).hexdigest()):
        raise RuntimeError("Generator source changed during certification")
    return {
        "generated_by": "tools/vortex/GenerateGgxMeanMomentCertificates.py",
        "generator_sha256": source_hash,
        "directional_generator_sha256": directional_hash,
        "model_revision": 1, "python_flint_version": flint.__version__,
        "flint_version": flint.__FLINT_VERSION__, "input_encoding": "exact binary64 inputs",
        "roughness_floor": "0.045", "working_precision_bits": pointwise.PRECISION_BITS,
        "radius_limit": pointwise.RADIUS_LIMIT, "lower_cosine": LOWER_COSINE,
        "upper_gap": UPPER_GAP, "outer_tolerance": OUTER_TOLERANCE,
        "energy_tolerance": ENERGY_TOLERANCE,
        "directional_tolerance": DIRECTIONAL_TOLERANCE,
        "azimuth_tolerance": AZIMUTH_TOLERANCE,
        "outer_evaluation_limit": OUTER_EVALUATION_LIMIT,
        "scope": "Queried cosine-weighted means; not an interpolation or full BRDF certificate",
        "cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--roughness", type=float, action="append")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = generate(args.roughness or pointwise.ROUGHNESSES, args.jobs)
    content = pointwise.encode_document(data)
    if args.check:
        if not args.output.exists() or args.output.read_bytes() != content:
            raise RuntimeError("Mean certificates are stale; rerun their generator")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(content)
    print(f"Verified {len(data['cases'])} mean certificates: {args.output}")


if __name__ == "__main__":
    main()
