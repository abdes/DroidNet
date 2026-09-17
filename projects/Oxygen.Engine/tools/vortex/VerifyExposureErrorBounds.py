"""Independent exact-arithmetic checks for exposure quantization propagation.

This verifies mathematical bounds and counterexamples, not GPU implementation.
It deliberately does not import renderer code or change acceptance tolerances.
"""

from argparse import ArgumentParser
from dataclasses import dataclass
from fractions import Fraction as F
from itertools import product
import json
import math
from pathlib import Path
import random
import struct


def f32(value):
    return F.from_float(struct.unpack("<f", struct.pack("<f", float(value)))[0])


def half(value):
    return F.from_float(struct.unpack("<e", struct.pack("<e", float(value)))[0])


@dataclass(frozen=True)
class Bound:
    """For nonnegative reference x: abs(observed - x) <= relative*x + absolute."""

    relative: F = F(0)
    absolute: F = F(0)

    def __post_init__(self):
        if self.relative < 0 or self.absolute < 0:
            raise ValueError("Error coefficients must be nonnegative")

    def error(self, reference):
        if reference < 0:
            raise ValueError("This bound requires nonnegative reference radiance")
        return self.relative * reference + self.absolute

    def add(self, other):
        return Bound(max(self.relative, other.relative), self.absolute + other.absolute)

    def scale(self, gain):
        if gain < 0:
            raise ValueError("Gain must be nonnegative")
        return Bound(self.relative, gain * self.absolute)

    def convex(self, other, weight):
        if not 0 <= weight <= 1:
            raise ValueError("Interpolation weight must be in [0, 1]")
        return Bound(max(self.relative, other.relative),
                     (1 - weight) * self.absolute + weight * other.absolute)

    def attenuate(self, transmittance, maximum_radiance):
        if maximum_radiance < 0:
            raise ValueError("A justified nonnegative source maximum is required")
        r, a = self.relative, self.absolute
        rt, at = transmittance.relative, transmittance.absolute
        return Bound((1 + r) * (1 + rt) - 1,
                     a * (1 + rt) + maximum_radiance * at * (1 + r) + a * at)

    def reference_interval(self, observed):
        if observed < 0 or self.relative >= 1:
            raise ValueError("Inversion requires observed >= 0 and relative < 1")
        return (max(F(0), (observed - self.absolute) / (1 + self.relative)),
                (observed + self.absolute) / (1 - self.relative))


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_algebra():
    rng = random.Random(0x45585035)
    coefficients = [Bound(), Bound(F(1, 2048)), Bound(F(1, 1024), F(1, 2**24)),
                    Bound(F(1, 100), F(1, 100000))]
    cases = 0
    for _ in range(2000):
        x, y = F(rng.randrange(0, 65537), 16), F(rng.randrange(0, 65537), 16)
        t, w = F(rng.randrange(0, 65), 64), F(rng.randrange(0, 65), 64)
        gain = F(rng.randrange(0, 4097), 16)
        bx, by = rng.choice(coefficients), rng.choice(coefficients)
        for sx, sy in product((-1, 1), repeat=2):
            hx = max(F(0), x + sx * bx.error(x))
            hy = max(F(0), y + sy * by.error(y))
            ht = min(F(1), max(F(0), t + sy * by.error(t)))
            require(abs(hx + hy - x - y) <= bx.add(by).error(x + y), "addition")
            require(abs(gain * hx - gain * x) <= bx.scale(gain).error(gain * x), "gain")
            mixed, ref = (1 - w) * hx + w * hy, (1 - w) * x + w * y
            require(abs(mixed - ref) <= bx.convex(by, w).error(ref), "convex interpolation")
            require(abs(hx * ht - x * t) <= bx.attenuate(by, x + 1).error(x * t), "transmittance product")
            lo, hi = bx.reference_interval(hx)
            require(lo <= x <= hi, "inverse interval")
            # A monotone quantizer maps a nonnegative interval into the interval
            # between its quantized endpoints. Keep this check below overflow.
            require(half(lo) <= half(x) <= half(hi), "half interval enclosure")
            cases += 6
    return cases


def image_budget(reference, share=F(1)):
    # Frozen half-budget admission margin: .0025 relative + 1e-5 absolute.
    return share * (abs(reference) * F(25, 10000) + F(1, 100000))


def amplification_counterexample():
    source, strength = f32(1e-8), F(1000000)
    narrowed_source = half(source)
    reference = source * strength
    independently_narrowed_scene = half(reference)
    actual = narrowed_source * strength
    require(abs(narrowed_source - source) <= image_budget(source, F(1, 4)), "local AP check should pass")
    require(abs(independently_narrowed_scene - reference) <= image_budget(reference, F(1, 4)), "independent scene check should pass")
    require(abs(actual - reference) > image_budget(reference), "composed amplification must fail admission")
    propagated = Bound(absolute=abs(narrowed_source - source)).scale(strength)
    require(abs(actual - reference) <= propagated.error(reference), "gain propagation must cover loss")
    return {"source": float(source), "strength": float(strength),
            "reference": float(reference), "actual": float(actual),
            "local_checks_pass": True, "composed_image_pass": False}


def is_dark(luminance, cutoff):
    # Independent implementation of the contract: equality belongs to dark.
    return luminance <= cutoff


def classification_counterexample():
    cutoff = F(1, 4096)
    transmittance = F(1, 2) + F(1, 4096) + F(1, 2**22)
    require(is_dark(cutoff, cutoff), "equality must remain dark")
    require(not is_dark(half(cutoff + F(1, 2**22)), cutoff), "next half value must be non-dark")

    # Negative control: the original one-factor construction ends exactly at
    # the cutoff, so it must never be reported as a classification change.
    old_background = cutoff * (1 - F(35, 100000)) / transmittance
    old_reference = old_background * transmittance
    old_composed = half(old_background * half(transmittance))
    require(old_composed == cutoff, "one-factor boundary control")
    require(all(is_dark(x, cutoff) for x in
                (old_reference, half(old_reference), old_composed)), "one-factor case is entirely dark")

    # Two independently stored attenuation factors correspond to AP followed
    # by fog. Their accumulated upward error crosses strictly above the cutoff.
    background = cutoff * (1 - F(35, 100000)) / (transmittance * transmittance)
    reference = background * transmittance * transmittance
    independent = half(reference)
    actual = half(background * half(transmittance) * half(transmittance))
    for label in ("AP", "fog"):
        require(abs(half(transmittance) - transmittance) <= image_budget(transmittance, F(1, 4)),
                f"local {label} transmittance check")
    require(abs(independent - reference) <= image_budget(reference, F(1, 4)), "independent scene image check")
    require(is_dark(reference, cutoff) == is_dark(independent, cutoff), "independent scene must preserve classification")
    require(is_dark(reference, cutoff) != is_dark(actual, cutoff), "composition must change classification")
    local_ev = abs(math.log2(float(independent / reference)))
    require(local_ev < 1 / 1024, "independent scene meter check should pass")
    trans_error = Bound(absolute=abs(half(transmittance) - transmittance))
    composed_bound = Bound().attenuate(trans_error, background).attenuate(
        trans_error, background * transmittance)
    error = composed_bound.error(reference)
    lo, hi = half(max(F(0), reference - error)), half(reference + error)
    require(lo <= cutoff < hi, "interval must include dark and strictly non-dark values")
    return {"predicate": "luminance <= dark_cutoff", "attenuation_factors": 2,
            "dark_cutoff": float(cutoff), "reference": float(reference),
            "independent_scene_half": float(independent), "composed_half": float(actual),
            "dark_classifications": {"reference": is_dark(reference, cutoff),
                                     "independent": is_dark(independent, cutoff),
                                     "composed": is_dark(actual, cutoff)},
            "one_factor_negative_control_all_dark": True,
            "independent_meter_error_ev": local_ev, "local_checks_pass": True,
            "composed_classification_pass": False}


def temporal_counterexample():
    # Use the production coefficient as its binary32 value and binary32 fresh
    # inputs. Reference recurrence is exact; actual recurrence rounds its blend
    # once to binary32 and then stores binary16.
    weight = f32(.9)
    desired_pre_store = 1 + F(1, 2048) + F(1, 2**22)
    actual = reference = F(1)
    bound = F(0)
    maximum_local_relative = F(0)
    for _ in range(64):
        fresh = f32((desired_pre_store - weight * actual) / (1 - weight))
        require(fresh >= 0, "nonnegative temporal source")
        pre_exact = (1 - weight) * fresh + weight * actual
        pre = f32(pre_exact)
        next_actual = half(pre)
        local = abs(next_actual - pre)
        require(local <= image_budget(pre, F(1, 4)), "every local store must pass")
        require(abs(math.log2(float(next_actual / pre))) < 1 / 1024, "local meter margin")
        reference = (1 - weight) * fresh + weight * reference
        require(abs(half(reference) - reference) <= image_budget(reference, F(1, 4)), "independent scene image check")
        require(abs(math.log2(float(half(reference) / reference))) < 1 / 1024, "independent scene meter check")
        bound = weight * bound + abs(pre - pre_exact) + local
        actual = next_actual
        require(abs(actual - reference) <= bound, "temporal recurrence bound")
        maximum_local_relative = max(maximum_local_relative, local / pre)
    error = abs(actual - reference)
    require(error > image_budget(reference), "cumulative error must exceed admission budget")
    require(abs(math.log2(float(actual / reference))) > 1 / 512, "cumulative meter error must exceed full budget")
    return {"frames": 64, "history_weight": float(weight), "reference": float(reference),
            "actual": float(actual), "maximum_local_relative_error": float(maximum_local_relative),
            "absolute_error": float(error), "propagated_absolute_bound": float(bound),
            "admission_budget": float(image_budget(reference)), "every_local_store_passes": True,
            "cumulative_image_admission_pass": False, "cumulative_meter_pass": False}


@dataclass(frozen=True)
class Interval:
    """Exact nonnegative enclosure; GPU arithmetic/filtering error is separate."""

    low: F
    high: F

    def __post_init__(self):
        if not 0 <= self.low <= self.high:
            raise ValueError("Expected an ordered nonnegative interval")

    def add(self, other):
        return Interval(self.low + other.low, self.high + other.high)

    def multiply(self, other):
        return Interval(self.low * other.low, self.high * other.high)

    def complement(self):
        if self.high > 1:
            raise ValueError("Complement requires a unit interval")
        return Interval(1 - self.high, 1 - self.low)

    def contains(self, value):
        return self.low <= value <= self.high


def ap_composition_interval(background, inscatter, transmittance):
    # Both forward AP and deferred One/InvSrcAlpha blending implement this
    # transfer. Inscatter does not depend on an opacity threshold or division.
    return background.multiply(transmittance).add(inscatter)


def fog_composition_interval(background, height_rgb, height_t, volume_rgb, volume_t):
    # Fog.hlsl followed by the pass's One/InvSrcAlpha RGB blend.
    return volume_rgb.add(height_rgb.multiply(volume_t)).add(
        background.multiply(height_t.multiply(volume_t)))


def coverage_interval(destination, transmittance):
    # Both environment passes blend alpha with One/InvSrcAlpha.
    # Keep the two occurrences of T correlated: 1-T+A*T = 1-(1-A)*T.
    return destination.complement().multiply(transmittance).complement()


def quantize_weight(value):
    return math.floor(min(F(1), max(F(0), value)) * 4095 + F(1, 2))


def meter_normalization_interval(rgb, coverage, profile_mask):
    low_weight = quantize_weight(profile_mask * coverage.low)
    high_weight = quantize_weight(profile_mask * coverage.high)
    if low_weight != high_weight:
        return "unstable_weight", None
    if high_weight == 0:
        return "unweighted", None
    # Histogram normalization has no denominator floor. A nonzero quantized
    # weight proves positive coverage; preserve that gate before division.
    require(coverage.low > 0, "weighted samples need positive coverage")
    return "weighted", Interval(rgb.low / coverage.high, rgb.high / coverage.low)


def check_consumer_intervals():
    rng = random.Random(0x41504647)
    checks = 0
    for _ in range(500):
        def radiance():
            a, b = sorted((F(rng.randrange(32769), 1024), F(rng.randrange(32769), 1024)))
            return Interval(a, b)

        def unit():
            a, b = sorted((F(rng.randrange(1025), 1024), F(rng.randrange(1025), 1024)))
            return Interval(a, b)

        bg, scatter, height = radiance(), radiance(), radiance()
        t, ht, coverage = unit(), unit(), unit()
        fog = fog_composition_interval(bg, height, ht, scatter, t)
        for x, h, th, v, tv in product(
                (bg.low, bg.high), (height.low, height.high),
                (ht.low, ht.high), (scatter.low, scatter.high), (t.low, t.high)):
            require(fog.contains(v + h * tv + x * th * tv), "fog consumer enclosure")
            checks += 1
        for background_scale in (F(0), F(1)):
            source = bg.multiply(Interval(background_scale, background_scale))
            ap = ap_composition_interval(source, scatter, t)
            for x, v, tv in product((bg.low, bg.high), (scatter.low, scatter.high), (t.low, t.high)):
                require(ap.contains(background_scale * x * tv + v), "AP consumer enclosure")
                checks += 1
        out_coverage = coverage_interval(coverage, t)
        for a, tv in product((coverage.low, coverage.high), (t.low, t.high)):
            require(out_coverage.contains(1 - tv + a * tv), "coverage blend enclosure")
            checks += 1

    # The removed branch must not suppress inscatter below or at its threshold.
    threshold = f32(1e-5)
    branch_t = Interval(1 - 2 * threshold, F(1))
    branch = ap_composition_interval(Interval(F(0), F(0)), Interval(F(1), F(2)), branch_t)
    require(branch == Interval(F(1), F(2)), "inscatter survives across the old threshold")
    equality = ap_composition_interval(Interval(F(0), F(0)), Interval(F(1), F(2)),
                                       Interval(1 - threshold, 1 - threshold))
    require(equality == Interval(F(1), F(2)), "inscatter survives equality at the old threshold")

    rgb = Interval(F(1, 100), F(1, 50))
    require(meter_normalization_interval(rgb, Interval(F(0), F(0)), F(1))[0] == "unweighted",
            "zero coverage must skip division")
    mass_boundary = F(1, 8190)
    require(meter_normalization_interval(rgb, Interval(F(0), mass_boundary), F(1))[0]
            == "unstable_weight", "half-up weight boundary must reject")
    status, normalized = meter_normalization_interval(rgb, Interval(mass_boundary, mass_boundary), F(1))
    require(status == "weighted" and normalized == Interval(rgb.low / mass_boundary, rgb.high / mass_boundary),
            "positive weight uses actual coverage")
    require(meter_normalization_interval(rgb, Interval(F(0), F(1)), F(0))[0] == "unweighted",
            "zero profile/mask must skip division")
    return checks + 6


def deferred_ap_branch_regression():
    transmittance, inscatter = f32(1 - 2**-16), f32(.01)
    narrowed_t, narrowed_rgb = half(transmittance), half(inscatter)
    require(1 - transmittance > f32(1e-5) and narrowed_t == 1, "half store crosses AP branch")
    require(abs(narrowed_t - transmittance) <= image_budget(transmittance, F(1, 4)), "local T budget")
    require(abs(narrowed_rgb - inscatter) <= image_budget(inscatter, F(1, 4)), "local RGB budget")
    actual, reference = F(0), inscatter
    require(abs(actual - reference) > image_budget(reference), "branch loss exceeds final image budget")
    # A smooth x*T+I model reports only the small RGB rounding error at x=0.
    require(abs(narrowed_rgb - reference) <= image_budget(reference), "smooth model misses branch loss")
    # R036 removes that lossy operation. Keep the old failure as a regression,
    # not as an ongoing restriction on the corrected consumer.
    corrected = ap_composition_interval(Interval(F(0), F(0)),
        Interval(narrowed_rgb, narrowed_rgb), Interval(narrowed_t, narrowed_t))
    require(corrected.low == narrowed_rgb and corrected.high == narrowed_rgb,
            "corrected deferred agrees with forward")
    require(abs(corrected.low - reference) <= image_budget(reference), "corrected image admission")
    return {"reference_transmittance": float(transmittance), "half_transmittance": float(narrowed_t),
            "reference_rgb": float(reference), "legacy_half_deferred_rgb": float(actual),
            "corrected_half_deferred_rgb": float(corrected.low), "forward_half_rgb": float(narrowed_rgb),
            "local_checks_pass": True, "legacy_image_admission_pass": False,
            "corrected_image_admission_pass": True}


def linear_clamp_sample(values, shape, coordinate):
    """Exact tensor interpolation in texel-center coordinates, including edges."""
    axes = []
    for size, value in zip(shape, coordinate):
        value = min(F(size - 1), max(F(0), value))
        left = math.floor(value)
        fraction = value - left
        axes.append(((left, 1 - fraction), (min(left + 1, size - 1), fraction)))
    result = F(0)
    for x, y, z in product(*axes):
        result += values[(z[0] * shape[1] + y[0]) * shape[0] + x[0]] * x[1] * y[1] * z[1]
    return result


def gradient_limits(values, shape, retained=Bound()):
    """Enclose reference neighbor differences using retained per-texel intervals."""
    intervals = [retained.reference_interval(value) for value in values]
    maxima = [F(0), F(0), F(0)]
    for z, y, x in product(range(shape[2]), range(shape[1]), range(shape[0])):
        coordinate = (x, y, z)
        here = intervals[(z * shape[1] + y) * shape[0] + x]
        for axis, stride in enumerate((1, shape[0], shape[0] * shape[1])):
            if coordinate[axis] + 1 < shape[axis]:
                there = intervals[(z * shape[1] + y) * shape[0] + x + stride]
                maxima[axis] = max(maxima[axis], here[1] - there[0], there[1] - here[0])
    return maxima


def legal_fixed8_coordinates(value):
    """D3D float-to-fixed tolerance: .6 of one 8-bit fractional step."""
    lower = math.ceil(value * 256 - F(3, 5))
    upper = math.floor(value * 256 + F(3, 5))
    return tuple(F(index, 256) for index in range(lower, upper + 1))


def filter_test_values(size, pattern, rng):
    if pattern == 0:
        return [F(1)] * size
    if pattern == 1:
        return [F(0)] * size
    if pattern == 2:
        return [F((index % 2) * 8192) for index in range(size)]
    if pattern == 3:
        return [f32(rng.randrange(16385) / 16384) for _ in range(size)]
    if pattern == 4:
        return [f32(index / 17) for index in range(size)]
    if pattern == 5:
        return [f32(math.ldexp(1 + rng.randrange(1024) / 1024,
                              rng.randrange(-40, 14))) for _ in range(size)]
    return [f32(rng.randrange(65537) / 17) for _ in range(size)]


def check_filter_coordinates():
    rng = random.Random(0x46494c54)
    storage = Bound(F(1, 2048), F(1, 2**25))
    checks = 0
    for shape in ((1, 1, 1), (3, 4, 1), (3, 4, 2), (2, 2, 2)):
        size = math.prod(shape)
        for pattern in range(8):
            reference = filter_test_values(size, pattern, rng)
            stored = [half(value) for value in reference]
            exact_gradients = gradient_limits(reference, shape)
            enclosed_gradients = gradient_limits(stored, shape, storage)
            for exact, enclosed in zip(exact_gradients, enclosed_gradients):
                require(exact <= enclosed, "retained reference gradient")
                checks += 1
            for _ in range(12):
                # Half-step ties admit two legal fixed-point coordinates.
                # Include clamp edges and crossings of integer cell boundaries.
                nominal = tuple(F(rng.choice((-1, 0, 255, 256, extent * 256 - 1)), 256)
                                + F(1, 512) for extent in shape)
                choices = [legal_fixed8_coordinates(value) for value in nominal]
                first = tuple(values[0] for values in choices)
                baseline = linear_clamp_sample(reference, shape, first)
                for second in product(*choices):
                    reference_at_second = linear_clamp_sample(reference, shape, second)
                    observed = linear_clamp_sample(stored, shape, second)
                    displacement = [abs(a - b) for a, b in zip(first, second)]
                    coordinate_error = sum(g * d for g, d in zip(exact_gradients, displacement))
                    enclosed_error = sum(g * d for g, d in zip(enclosed_gradients, displacement))
                    require(abs(reference_at_second - baseline) <= coordinate_error,
                            "linear-clamp Lipschitz bound across cell/edge boundaries")
                    require(abs(observed - reference_at_second) <= storage.error(reference_at_second),
                            "common-weight storage bound")
                    allowance = storage.error(baseline) + (1 + storage.relative) * enclosed_error
                    require(abs(observed - baseline) <= allowance, "combined storage/coordinate bound")
                    checks += 3
    return checks


def filter_coordinate_counterexample():
    # u=257/1024 is exactly binary32 for a two-texel texture. u*2-.5=1/512.
    # Both adjacent 16.8 coordinates are legal at the half-step tie. The texels
    # 0 and 1 themselves are exactly representable in both resource formats.
    uv = F(257, 1024)
    require(f32(uv) == uv, "counterexample normalized coordinate is binary32")
    positions = legal_fixed8_coordinates(uv * 2 - F(1, 2))
    require(positions == (F(0), F(1, 256)), "legal fixed8 tie alternatives")
    values = [F(0), F(1)]
    require([half(v) for v in values] == values, "no texel store error")
    baseline = linear_clamp_sample(values, (2, 1, 1), (positions[0], F(0), F(0)))
    other = linear_clamp_sample(values, (2, 1, 1), (positions[1], F(0), F(0)))
    require(abs(other - baseline) > image_budget(baseline), "coordinate error exceeds image budget")
    require(is_dark(baseline, F(1, 4096)) and not is_dark(other, F(1, 4096)),
            "coordinate error changes dark classification")
    relative = F(1, 1024)
    retained = linear_clamp_sample([F(0), 1 + relative], (2, 1, 1),
                                   (positions[1], F(0), F(0)))
    require(abs(retained - baseline) > relative * baseline + (other - baseline),
            "omitting the relative/displacement cross term must fail")
    require(abs(retained - baseline) == relative * baseline + (1 + relative) * (other - baseline),
            "relative/displacement cross term encloses the tight case")
    return {"texels": [0, 1], "normalized_coordinate": float(uv),
            "legal_texel_coordinates": [float(p) for p in positions],
            "filtered_results": [float(baseline), float(other)],
            "store_error": 0, "texel_checks_alone_suffice": False,
            "retained_relative_cross_term_required": True,
            "hardware_observation": False,
            "scope": "Allowed coordinate-rounding counterexample, not a claim about a measured device"}


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    # An independently rendered 8192-radiance sample fixes the existing global
    # candidate selector at P=1; the counterexamples do not depend on choosing
    # an arbitrary scale that the renderer would never select.
    require(2 ** math.floor(math.log2(16376 / 8192)) == 1, "candidate scale anchor")
    report = {"candidate_scale_context": {"maximum_scene_rgb": 8192, "candidate_pre_exposure": 1},
              "scope": "Exact scalar/componentwise algebra and binary16 counterexamples; not GPU qualification",
              "algebra_checks": check_algebra(),
              "consumer_interval_checks": check_consumer_intervals(),
              "filter_coordinate_checks": check_filter_coordinates(),
              "counterexamples": {"AP strength": amplification_counterexample(),
                                  "dark classification": classification_counterexample(),
                                  "temporal history": temporal_counterexample(),
                                  "filter coordinate rounding": filter_coordinate_counterexample()},
              "regressions": {"removed deferred AP branch": deferred_ap_branch_regression()},
              "remaining": ["Justify runtime source/consumer bounds and carry certificate lifetimes",
                            "Implement outward-safe GPU arithmetic, coverage and filtering rules",
                            "Qualify actual GPU composition and temporal behavior before format switching"],
              "verdict": "pass"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"PASS: {report['algebra_checks']} algebra checks, {report['consumer_interval_checks']} consumer checks, "
          f"{report['filter_coordinate_checks']} filtering checks "
          f"and {len(report['counterexamples'])} per-product acceptance counterexamples: {args.output}")


if __name__ == "__main__":
    main()
