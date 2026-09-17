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
              "counterexamples": {"AP strength": amplification_counterexample(),
                                  "dark classification": classification_counterexample(),
                                  "temporal history": temporal_counterexample()},
              "remaining": ["Justify runtime source/consumer bounds and carry certificate lifetimes",
                            "Implement outward-safe GPU arithmetic, coverage and filtering rules",
                            "Qualify actual GPU composition and temporal behavior before format switching"],
              "verdict": "pass"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"PASS: {report['algebra_checks']} algebra checks and three per-product acceptance counterexamples: {args.output}")


if __name__ == "__main__":
    main()
