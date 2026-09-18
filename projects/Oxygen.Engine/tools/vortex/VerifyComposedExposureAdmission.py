"""Independent rational oracle for whole-scene exposure admission intervals.

The inputs are certificates, not renderer helpers. This checks the final
decision; it does not establish that a producer supplied a valid certificate.
"""

from fractions import Fraction as F
from itertools import product
import math
import struct


def binary16(bits):
    exponent, mantissa = bits >> 10, bits & 1023
    return F(mantissa, 2**24) if exponent == 0 else F(1024 + mantissa) * F(2) ** (exponent - 25)


def half(value):
    """Exact positive rational -> nearest-even binary16, without double rounding."""
    value = F(value)
    if value < 0 or value > binary16(0x7bff):
        raise ValueError("Outside finite nonnegative half domain")
    lo, hi = 0, 0x7bff
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if binary16(mid) <= value:
            lo = mid
        else:
            hi = mid - 1
    if lo == 0x7bff or binary16(lo) == value:
        return binary16(lo)
    delta_low, delta_high = value - binary16(lo), binary16(lo + 1) - value
    return binary16(lo if delta_low < delta_high or (delta_low == delta_high and lo % 2 == 0) else lo + 1)


def reference_interval(observed, relative, absolute):
    observed, relative, absolute = map(F, (observed, relative, absolute))
    if observed < 0 or not 0 <= relative < 1 or absolute < 0:
        raise ValueError("Invalid reference certificate")
    return max(F(0), (observed - absolute) / (1 + relative)), (observed + absolute) / (1 - relative)


def candidate_interval(reference, relative, absolute):
    relative, absolute = F(relative), F(absolute)
    if not 0 <= relative < 1 or absolute < 0:
        raise ValueError("Invalid candidate certificate")
    return max(F(0), reference[0] * (1 - relative) - absolute), reference[1] * (1 + relative) + absolute


def scene_intervals(pixel, current, candidate, p=F(1), coverage=True, current_store=False):
    reference, narrowed = [], []
    for channel, observed in enumerate(map(F, pixel)):
        start = 2 if channel == 3 else 0
        interval = reference_interval(observed, *current[start:start + 2])
        proposed = (observed, observed) if current_store else candidate_interval(interval, *candidate[start:start + 2])
        scale = F(p)
        if channel == 3:
            interval = tuple(min(F(1), v) for v in interval)
            proposed = tuple(min(F(1), v) for v in proposed)
            if not coverage:
                interval = proposed = (F(1), F(1))
            scale = F(1)
        reference.append(interval)
        narrowed.append(tuple(half(v * scale) / scale for v in proposed))
    return reference, narrowed


def normalize(intervals, floor=F(0)):
    low_alpha, high_alpha = (max(floor, v) for v in intervals[3])
    if low_alpha <= 0:
        raise ValueError("Positive-weight normalization has zero coverage")
    return [(low / high_alpha, high / low_alpha) for low, high in intervals[:3]]


def weight(value):
    value = min(F(1), max(F(0), value)) * 4095 + F(1, 2)
    return value.numerator // value.denominator


def evaluate(reference, candidate, *, gain=F(1), profile_mask=F(1), cutoff=F(1, 4096), black_influence=F(0), meter=True):
    failure = 0
    ref_image, cand_image = normalize(reference, F(1, 1000000)), normalize(candidate, F(1, 1000000))
    for ref_component, cand_component in zip(ref_image, cand_image):
        for x, y in product(ref_component, cand_component):
            if abs(x - y) > F(1, 400) * x + F(1, 100000) / max(F(1), gain):
                failure |= 4
    for ref_component, cand_component in zip(reference[:3], candidate[:3]):
        for x, y in product(ref_component, cand_component):
            if abs(x - y) > F(1, 400) * x + F(1, 100000) / max(F(1), gain):
                failure |= 4
    if not meter:
        return failure
    weights = [weight(profile_mask * alpha) for alpha in (*reference[3], *candidate[3])]
    if len(set(weights)) != 1:
        return failure | 8
    if not weights[0]:
        return failure
    try:
        ref_meter, cand_meter = normalize(reference), normalize(candidate)
    except ValueError:
        return failure | 8
    coefficients = (F(2126, 10000), F(7152, 10000), F(722, 10000))
    luminance = [sum(c * components[i][end] for i, c in enumerate(coefficients))
                 for components in (ref_meter, cand_meter) for end in (0, 1)]
    dark = [value <= cutoff for value in luminance]
    if len(set(dark)) != 1:
        return failure | 8
    mass = int(F(weights[0]) * black_influence + F(1, 2)) if dark[0] else weights[0]
    if mass == 0:
        return failure
    zeros = [value == 0 for value in luminance]
    if len(set(zeros)) != 1:
        return failure | 8
    if zeros[0]:
        return failure
    # The rational ratio is exact; log2 is only used for this well-separated
    # fixture verdict, not to generate an outward GPU certificate.
    ev = max(abs(math.log2(float(luminance[3] / luminance[0]))),
             abs(math.log2(float(luminance[1] / luminance[2]))))
    return failure | (8 if ev > 1 / 1024 else 0)


def display_color(pixel, gain, mapper, gamma, background, position):
    """Independent double-precision tone/composition/dither reference."""
    def scalar32(value):
        return struct.unpack('<f', struct.pack('<f', value))[0]
    def clamp(value):
        return min(1.0, max(0.0, value))
    def matrix(rows, color):
        return [sum(scalar32(k) * v for k, v in zip(row, color)) for row in rows]
    def filmic(x):
        a, b, c, d, e, f = map(scalar32, (.15, .50, .10, .20, .02, .30))
        return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f
    alpha = float(pixel[3]) if background is not None else 1.0
    color = [float(c) * float(gain) / max(alpha, 1e-6) for c in pixel[:3]]
    if mapper == 1:
        color = matrix(((.59719, .35458, .04823), (.07600, .90834, .01566),
                        (.02840, .13383, .83777)), color)
        color = [(x * (x + scalar32(.0245786)) - scalar32(.000090537))
                 / (x * (scalar32(.983729) * x + scalar32(.432951)) + scalar32(.238081)) for x in color]
        color = [clamp(x) for x in matrix(((1.60475, -.53108, -.07367),
                                          (-.10208, 1.10813, -.00605),
                                          (-.00327, -.07276, 1.07602)), color)]
    elif mapper == 2:
        color = [filmic(2*x) / filmic(scalar32(11.2)) for x in color]
    elif mapper == 3:
        color = [x / (x + 1) for x in color]
    else:
        color = [clamp(x) for x in color]
    color = [max(0.0, x) ** (1 / max(float(gamma), 1e-4)) for x in color]
    if background is not None:
        def srgb_to_linear(x):
            x = clamp(x)
            return x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4
        def linear_to_srgb(x):
            x = max(0.0, x)
            return clamp(12.92*x if x <= .0031308 else 1.055*x**(1/2.4)-.055)
        color = [linear_to_srgb(srgb_to_linear(x) * alpha + clamp(bg) * (1-alpha))
                 for x, bg in zip(color, background)]
    bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
    dither = (bayer[(position[0] & 3) | ((position[1] & 3) << 2)] / 16 - .5) / 255
    return tuple(clamp(x + dither) for x in color)


def sampled_display_error(reference, candidate, **controls):
    """Corner counterexamples; GPU intervals must also cover the interiors."""
    refs = [display_color(point, **controls) for point in set(product(*reference))]
    candidates = [display_color(point, **controls) for point in set(product(*candidate))]
    return max(abs(x-y) for ref in refs for cand in candidates for x, y in zip(ref, cand))


def verify_half():
    for bits in range(0x7bff):
        lower, upper = binary16(bits), binary16(bits + 1)
        midpoint = (lower + upper) / 2
        assert half(midpoint) == (upper if bits & 1 else lower)
        assert half(midpoint - (upper - lower) / 1024) == lower
        assert half(midpoint + (upper - lower) / 1024) == upper
        assert F(struct.unpack('<e', struct.pack('<H', bits))[0]) == lower
    return 4 * 0x7bff


if __name__ == '__main__':
    print(f'Exact half rounding checks: {verify_half()}')
