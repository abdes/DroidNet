"""Independent CPU references for captured exposure inputs."""

from fractions import Fraction


def trimmed_histogram_log_luminance(bins, low, high, minimum, span):
    """Integrate exact percentile mass, then convert the weighted bin center."""
    if len(bins) != 256 or not 0 <= low < high <= 1:
        raise ValueError("Invalid histogram/percentile domain")
    total = sum(bins)
    if total == 0:
        return None
    begin = Fraction.from_float(low) * total
    end = Fraction.from_float(high) * total
    cumulative = 0
    weighted = Fraction(0)
    retained = Fraction(0)
    for index, mass in enumerate(bins):
        overlap = min(cumulative + mass, end) - max(cumulative, begin)
        if overlap > 0:
            weighted += index * overlap
            retained += overlap
        cumulative += mass
    if retained == 0:
        return None
    return minimum + float(weighted / retained) * span / 255


def target_gain(keys, metered_ev):
    """Evaluate authored log-gain knots with clamped endpoints."""
    if not keys or any(a[0] >= b[0] for a, b in zip(keys, keys[1:])):
        raise ValueError("Target knots must be nonempty and strictly ordered")
    if metered_ev <= keys[0][0]:
        return 2 ** keys[0][1]
    for left, right in zip(keys, keys[1:]):
        if metered_ev <= right[0]:
            fraction = (metered_ev - left[0]) / (right[0] - left[0])
            return 2 ** (left[1] + fraction * (right[1] - left[1]))
    return 2 ** keys[-1][1]
