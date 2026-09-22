"""Generate independent, high-precision GGX normal/grazing endpoint anchors.

Run with mpmath==1.3.0. This uses incident-cosine integration at normal view
and an analytically azimuth-integrated expression at grazing view, rather than
the C++ reference's two-dimensional half-vector quadrature.
"""

import argparse
import hashlib
import json
from pathlib import Path

import mpmath
from mpmath import mp


ROUGHNESSES = ("0.045", "0.1", "0.25", "0.5", "0.75", "1")
FIRST_PRECISION = 60
SECOND_PRECISION = 90
DEFAULT_OUTPUT = (
    Path(__file__).resolve().parents[2]
    / "src/Oxygen/Vortex/Test/Lighting/Reference/GgxEndpointReference.json"
)


def endpoint_moments(roughness, grazing, precision):
    with mp.workdps(precision):
        alpha = mp.mpf(roughness) ** 2
        alpha2 = alpha**2
        if not grazing:
            # mu=N.l, N.v=1, (N.h)^2=(1+mu)/2; the azimuth integral is 2*pi.
            def incident_integrand(mu, schlick):
                denominator = (1 - mu) / 2 + alpha2 * (1 + mu) / 2
                distribution = alpha2 / (mp.pi * denominator**2)
                visibility = mp.mpf("0.5") / (
                    mu + mp.sqrt(mu**2 + alpha2 * (1 - mu**2))
                )
                fresnel = (1 - mp.sqrt((1 + mu) / 2)) ** 5 if schlick else 1
                return 2 * mp.pi * distribution * visibility * mu * fresnel

            intervals = [mp.mpf(0)] + [1 - mp.mpf(10) ** -k for k in range(1, 7)] + [mp.mpf(1)]
            energy, energy_error = mp.quad(
                lambda mu: incident_integrand(mu, False), intervals,
                method="tanh-sinh", error=True,
            )
            bias, bias_error = mp.quad(
                lambda mu: incident_integrand(mu, True), intervals,
                method="tanh-sinh", error=True,
            )
            return energy, bias, max(energy_error, bias_error)

        # At grazing view E=1. With GGX CDF u=sin(q)^2 the azimuth integral
        # of cos(phi)*(1-sin(theta_h)*cos(phi))^5 is a degree-five polynomial.
        cosine_integrals = (mp.mpf(1), mp.pi / 4, mp.mpf(2) / 3,
                            3 * mp.pi / 16, mp.mpf(8) / 15, 5 * mp.pi / 32)

        def grazing_integrand(q):
            sin_q, cos_q = mp.sin(q), mp.cos(q)
            sin_h = alpha * sin_q / mp.sqrt(cos_q**2 + alpha2 * sin_q**2)
            azimuth = mp.fsum(
                mp.binomial(5, k) * (-sin_h) ** k * cosine_integrals[k]
                for k in range(6)
            )
            return 4 / mp.pi * sin_q**2 * azimuth

        bias, error = mp.quad(grazing_integrand, [0, mp.pi / 4, mp.pi / 2],
                              method="tanh-sinh", error=True)
        return mp.mpf(1), bias, error


def generate():
    if mpmath.__version__ != "1.3.0":
        raise RuntimeError("Use the pinned mpmath==1.3.0 generator environment")
    cases = []
    with mp.workdps(SECOND_PRECISION):
        for roughness in ROUGHNESSES:
            for grazing in (False, True):
                low = endpoint_moments(roughness, grazing, FIRST_PRECISION)
                high = endpoint_moments(roughness, grazing, SECOND_PRECISION)
                disagreement = max(abs(a - b) for a, b in zip(low[:2], high[:2]))
                estimated_error = max(low[2], high[2])
                if max(disagreement, estimated_error) > mp.mpf("1e-40"):
                    raise RuntimeError(f"Unresolved endpoint: r={roughness}, grazing={grazing}")
                if not 0 <= high[1] <= high[0] <= 1:
                    raise RuntimeError("Endpoint violates its physical moment bounds")
                cases.append({
                    "roughness": float(roughness),
                    "view_cosine": 0 if grazing else 1,
                    "directional_albedo": mp.nstr(high[0], 50),
                    "schlick_moment": mp.nstr(high[1], 50),
                    "precision_disagreement": mp.nstr(disagreement, 8),
                    "reported_quadrature_error": mp.nstr(estimated_error, 8),
                })
    return {
        "generated_by": "tools/vortex/GenerateGgxEndpointReference.py",
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "model_revision": 1,
        "mpmath_version": mpmath.__version__,
        "first_precision_digits": FIRST_PRECISION,
        "second_precision_digits": SECOND_PRECISION,
        "scope": "Endpoint anchors only; precision agreement is not an interior-domain error certificate",
        "cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = (json.dumps(generate(), indent=2) + "\n").encode("utf-8")
    if args.check:
        if not args.output.exists() or args.output.read_bytes() != content:
            raise RuntimeError("Generated endpoint data is stale; rerun the generator")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(content)
    print(f"Verified 12 GGX endpoint anchors: {args.output}")


if __name__ == "__main__":
    main()
