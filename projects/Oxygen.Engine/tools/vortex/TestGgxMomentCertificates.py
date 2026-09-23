"""Safety checks for the rigorous offline GGX certifier."""

import unittest

from flint import acb, arb, ctx

from GenerateGgxMomentCertificates import (
    PRECISION_BITS,
    certify,
    energy_integrand,
    encode_document,
    export_ball,
)


class GgxCertificateTests(unittest.TestCase):
    def setUp(self):
        previous_precision = ctx.prec
        self.addCleanup(setattr, ctx, "prec", previous_precision)
        ctx.prec = PRECISION_BITS

    def test_analytic_callback_rejects_square_root_branch_cut(self):
        # For v=1, alpha^2=1/2, u=3+i, the discriminant is -1/4.
        # Point evaluation is finite, but it cannot certify holomorphicity.
        self.assertTrue(energy_integrand(acb(3, 1), acb(1), acb(0.5), False).is_finite())
        self.assertFalse(energy_integrand(acb(3, 1), acb(1), acb(0.5), True).is_finite())

    def test_export_radius_includes_binary64_rounding(self):
        original = acb(arb("0.12345678901234567890123456789 +/- 1e-20"))
        midpoint, radius = export_ball(original)
        self.assertTrue(arb(midpoint, radius).contains(original.real))
        self.assertGreater(radius, 1e-20)

    def test_wide_nonreal_and_invalid_results_are_rejected(self):
        for value in (acb(arb("1 +/- 1e-4")), acb(1, 1), acb("nan")):
            with self.subTest(value=value):
                with self.assertRaises(RuntimeError):
                    export_ball(value)

    def test_interior_energy_contains_independent_analytic_value(self):
        energy, bias = certify(1.0, 0.5)
        analytic = 1 - arb(0.5)*arb(3).log()
        self.assertTrue(energy.real.contains(analytic))
        export_ball(energy)
        export_ball(bias)

    def test_json_formatting_normalizes_only_numeric_exponents(self):
        data = encode_document({"value": 1e-8, "text": "keep 1e-08 unchanged"}).decode()
        self.assertIn('"value": 1e-8', data)
        self.assertIn('"text": "keep 1e-08 unchanged"', data)
        self.assertNotIn("\r", data)

    def test_invalid_inputs_are_rejected(self):
        for roughness, mu in ((-1, 0.5), (0.5, 2), (float("nan"), 0.5)):
            with self.subTest(roughness=roughness, mu=mu):
                with self.assertRaises(ValueError):
                    certify(roughness, mu)


if __name__ == "__main__":
    unittest.main()
