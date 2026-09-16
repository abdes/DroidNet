"""Independent, high precision references for native exposure acceptance.

No engine math is imported. Histogram CDF integration uses exact rational
weights; temporal trajectories evaluate total elapsed time analytically with
Decimal, instead of replaying the production per-frame update.
"""

from decimal import Decimal, localcontext
from fractions import Fraction
import math
import unittest

BIN_COUNT = 256
MAX_GRID = 512
MAX_WEIGHT = 4095
MAX_MASS = 1073479680


def half_up(value):
    value = Fraction(value)
    return (2 * value.numerator + value.denominator) // (2 * value.denominator)


def scatter(log_luminance, weight=1, window=(-12, 25)):
    """Return one sample's exact conserved two-bin integer distribution."""
    position = min(Fraction(255), max(Fraction(0),
        (Fraction(log_luminance) - window[0]) * 255 / window[1]))
    lower = position.numerator // position.denominator
    quantized = half_up(Fraction(weight) * MAX_WEIGHT)
    upper_mass = half_up(quantized * (position - lower))
    bins = [0] * BIN_COUNT
    bins[lower] += quantized - upper_mass
    bins[min(lower + 1, 255)] += upper_mass
    return bins


def integrate_histogram(bins, low=0, high=1, window=(-12, 25)):
    """Exact fractional percentile integration, including subnormal inputs."""
    total = sum(bins)
    left, right = Fraction(low) * total, Fraction(high) * total
    if not 0 <= low < high <= 1 or total == 0:
        raise ValueError("invalid percentile interval or empty histogram")
    cursor = mass = moment = Fraction(0)
    for index, count in enumerate(bins):
        retained = max(Fraction(0), min(cursor + count, right) - max(cursor, left))
        mass += retained
        moment += retained * (Fraction(window[0]) + Fraction(index, 255) * window[1])
        cursor += count
    if not mass:
        raise ValueError("empty retained histogram")
    return moment / mass


def hybrid_trajectory(initial, target, elapsed, speed_up, speed_down, distance=1.5):
    """Closed form from the initial condition at total elapsed game time."""
    with localcontext() as context:
        context.prec = 60
        q, qt, t, up, down, d = map(Decimal,
            map(str, (initial, target, elapsed, speed_up, speed_down, distance)))
        if not all(x.is_finite() for x in (q, qt, t, up, down, d)) or min(t, up, down) < 0 or d <= 0:
            raise ValueError("invalid trajectory inputs")
        speed = up if qt < q else down
        radius = abs(qt - q)
        if speed == 0 or t == 0 or radius == 0:
            return q
        crossing_time = max(Decimal(0), (radius - d) / speed)
        if t <= crossing_time:
            remaining = radius - speed * t
        else:
            remaining = min(radius, d) * (-(t - crossing_time) * speed / d).exp()
        return qt + (remaining if q > qt else -remaining)


class ExposureReferenceTests(unittest.TestCase):
    def test_weight_conservation(self):
        for q in (0, 1, 17, 2048, 4095):
            for log_l in (Fraction(-24), Fraction(-7, 3), Fraction(0), Fraction(32)):
                self.assertEqual(sum(scatter(log_l, Fraction(q, 4095))), q)

    def test_half_weight_ties_round_up(self):
        self.assertEqual(half_up(Fraction(1, 2)), 1)
        self.assertEqual(half_up(Fraction(5, 2)), 3)

    def test_known_two_bin_distribution(self):
        bins = scatter(-2)
        self.assertEqual([(i, v) for i, v in enumerate(bins) if v], [(102, 4095)])
        bins = scatter(Fraction(-39, 20))
        self.assertEqual(sum(bins), 4095)
        self.assertEqual([(i, v) for i, v in enumerate(bins) if v], [(102, 2007), (103, 2088)])
        self.assertLess(abs(float(integrate_histogram(bins)) + 1.95), 2e-5)

    def test_partial_percentile_boundaries(self):
        bins = [0] * 256
        bins[0], bins[255] = 3, 1
        self.assertEqual(integrate_histogram(bins, Fraction(1, 2), Fraction(7, 8)), Fraction(-11, 3))

    def test_tiny_percentile_interval(self):
        bins = [0] * 256
        bins[32], bins[240] = MAX_MASS - 1, 1
        self.assertEqual(integrate_histogram(bins, 2**-149, 2**-148), Fraction(-12) + Fraction(32 * 25, 255))

    def test_maximum_mass(self):
        self.assertEqual(MAX_GRID**2 * MAX_WEIGHT, MAX_MASS)
        self.assertLess(MAX_MASS, 2**30)

    def test_invalid_measurement(self):
        with self.assertRaises(ValueError):
            integrate_histogram([0] * 256)

    def test_linear_segment(self):
        self.assertEqual(hybrid_trajectory(8, -8, 2, 3, 1), Decimal(2))

    def test_crossing_and_exponential_tail(self):
        expected = -8 + 1.5 * math.exp(-2)
        self.assertAlmostEqual(float(hybrid_trajectory(8, -8, Decimal(35)/6, 3, 1)), expected, places=12)

    def test_zero_time_and_speed(self):
        self.assertEqual(hybrid_trajectory(8, -8, 0, 3, 1), Decimal(8))
        self.assertEqual(hybrid_trajectory(8, -8, 10, 0, 1), Decimal(8))
        self.assertEqual(hybrid_trajectory(-8, 8, 10, 3, 0), Decimal(-8))

    def test_schedule_invariance(self):
        expected = hybrid_trajectory(-8, 8, 20, 3, 1)
        for schedule in ([Decimal(1)/30] * 600, [Decimal(1)/60] * 1200,
                         [Decimal(1)/120] * 2400, [Decimal('.125'), Decimal('.375')] * 40):
            value = Decimal(-8)
            for dt in schedule:
                value = hybrid_trajectory(value, 8, dt, 3, 1)
            self.assertLess(abs(value - expected), Decimal('1e-24'))

    def test_huge_finite_rate_and_distance(self):
        expected = -8 + 16 * math.exp(-2)
        self.assertAlmostEqual(float(hybrid_trajectory(8, -8, 2, 2**127, 1, 2**127)), expected, places=12)

    def test_tiny_rate_with_huge_delta(self):
        self.assertLess(abs(hybrid_trajectory(0, -8, 2**127, Decimal(2)**-140, 1) + Decimal(2)**-13), Decimal("1e-30"))

    def test_long_time_no_overshoot(self):
        self.assertEqual(hybrid_trajectory(32, -32, 1e30, 3, 1), Decimal(-32))


if __name__ == "__main__":
    unittest.main()
