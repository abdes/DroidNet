"""Safety and algebra checks for the mean-moment certificate bounds."""

import unittest

from flint import acb, arb, ctx

from GenerateGgxMeanMomentCertificates import (
    analytic_mean_enclosure,
    certify_means,
    endpoint_enclosure,
    moving_contour_comparison,
)
from GenerateGgxMomentCertificates import PRECISION_BITS, schlick_moment


def split_density(view, parameter, azimuth, alpha2, upper_piece, bias):
    if upper_piece:
        light = view + parameter*(1-view)
        jacobian = 1-view
    else:
        light = parameter*view
        jacobian = view
    light_sine = (1-light*light).sqrt(analytic=True)
    view_sine = (1-view*view).sqrt(analytic=True)
    q = 1+light*view+light_sine*view_sine*azimuth.cos()
    d = q-(1-alpha2)*(light+view)**2/2
    light_root = (alpha2+(1-alpha2)*light*light).sqrt(analytic=True)
    view_root = (alpha2+(1-alpha2)*view*view).sqrt(analytic=True)
    value = (alpha2*light*jacobian/(acb.pi()*(light*view_root+view*light_root))
             * (q/d)**2)
    return value*(1-(q/2).sqrt(analytic=True))**5 if bias else value


class GgxMeanCertificateTests(unittest.TestCase):
    def setUp(self):
        previous_precision = ctx.prec
        self.addCleanup(setattr, ctx, "prec", previous_precision)
        ctx.prec = PRECISION_BITS

    def test_crossing_endpoint_or_large_complex_domains_are_rejected(self):
        for domain in (acb(arb("0.01 +/- 0.02")), acb(arb("0.99 +/- 0.02")),
                       acb(0.5, 0.5), acb("nan")):
            with self.subTest(domain=domain):
                self.assertIsNone(moving_contour_comparison(domain))
                self.assertFalse(analytic_mean_enclosure(domain).is_finite())

    def test_moving_contours_bound_complex_energy_and_schlick_densities(self):
        for view in (acb("0.0002", "0.000001"), acb("0.5", "0.01"), acb("0.5", "0.1"),
                     acb("0.99", "0.00001")):
            comparison = moving_contour_comparison(view)
            self.assertIsNotNone(comparison)
            for roughness in ("0.045", "0.5", "1"):
                alpha2 = acb(roughness)**4
                for upper_piece in (False, True):
                    for parameter in (acb("0.125"), acb("0.5"), acb("0.875")):
                        for azimuth in (acb(0), acb.pi()/2, acb.pi()):
                            reference = split_density(acb(view.real.mid()), parameter,
                                                      azimuth, alpha2, upper_piece, False)
                            self.assertTrue(reference.real > 0)
                            for bias in (False, True):
                                value = split_density(view, parameter, azimuth, alpha2,
                                                      upper_piece, bias)
                                self.assertTrue(value.is_finite())
                                self.assertTrue(abs(value).abs_upper()
                                                <= comparison.upper()*reference.real.lower())

    def test_small_rectangular_domains_have_finite_analytic_enclosures(self):
        domain = acb(arb("0.5 +/- 0.01"), arb("0 +/- 0.01"))
        self.assertTrue(analytic_mean_enclosure(domain).is_finite())
        self.assertEqual(moving_contour_comparison(acb("0.5")), arb(1))

    def test_endpoint_contributions_are_enclosed_not_discarded(self):
        lower = arb("1e-4")
        upper = 1-arb("1e-10")
        enclosure = endpoint_enclosure(lower, upper)
        self.assertTrue(enclosure.contains(0))
        self.assertTrue(enclosure.contains(lower*lower+(1-upper)*(1+upper)))
        self.assertTrue(enclosure.rad() < arb("5.11e-9"))

    def test_non_cosine_weighted_endpoint_measure_is_detectably_wrong(self):
        lower = arb("1e-4")
        upper = 1-arb("1e-10")
        self.assertFalse(endpoint_enclosure(lower, upper).contains(lower+1-upper))

    def test_invalid_roughness_is_rejected_before_integration(self):
        for roughness in (-1, 2, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                certify_means(roughness)

    def test_explicit_nested_precision_reduces_directional_uncertainty(self):
        view = acb("0.4")
        alpha2 = acb("0.5")**4
        original = schlick_moment(view, alpha2, False)
        refined = schlick_moment(view, alpha2, False,
                                 tolerance="1e-13", angular_tolerance="1e-16")
        self.assertTrue(original.real.overlaps(refined.real))
        self.assertTrue(refined.real.rad() < original.real.rad()/10)


if __name__ == "__main__":
    unittest.main()
