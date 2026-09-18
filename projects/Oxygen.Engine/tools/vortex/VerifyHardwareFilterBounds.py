"""Independent rational enclosure checks for mip-zero hardware linear filtering.

Models legal address choices and differently rounded positive weighted sums.
It does not import production helpers or claim to exhaust real GPU behavior.
"""

from argparse import ArgumentParser
from fractions import Fraction as F
from itertools import product
import json
from pathlib import Path
import random
import struct


def rounded(value, half=False):
    if half:
        value = max(F(0), min(F(65504), value))
    result = F(struct.unpack("<e" if half else "<f",
                             struct.pack("<e" if half else "<f", float(value)))[0])
    return F(0) if not half and abs(result) < F(2) ** -126 else result


def arithmetic_parameters(half):
    unit = F(2) ** (-10 if half else -23)
    tiny = F(2) ** (-24 if half else -126)
    growth = (1 + unit) ** 16
    return growth - 1, 64 * tiny * growth


def inverse(value, relative, absolute):
    return max(F(0), (value - absolute) / (1 + relative)), (value + absolute) / (1 - relative)


def coefficients(bound, gradients, shape, half):
    r, a = bound
    if not half and not r and not a:
        return (F(0), F(0)), (F(0), F(0))
    diameter = sum(g for g, n in zip(gradients, shape) if n > 1)
    displacement = sum(g * (F(6, 5 * 256) + F(8, 2**23) * (n + F(1, 2)))
                       for g, n in zip(gradients, shape) if n > 1)
    q, eta = arithmetic_parameters(half)
    qr, etar = arithmetic_parameters(False)
    observed_r = r + (1 + r) * (q + eta)
    reference_r = qr + etar
    reference_a = etar * (diameter + 1)
    observed_a = (1 + q) * (a + (1 + r) * displacement) + eta * (1 + a + (1 + r) * (diameter + displacement))
    relative = (observed_r + reference_r) / (1 - reference_r)
    absolute = observed_a + (1 + relative) * reference_a
    return (relative, absolute), (r, a + (1 + r) * (2 * diameter + 2 * F(2) ** -126))


def intervals(observed, bound, gradients, shape, half):
    arithmetic, hull = coefficients(bound, gradients, shape, half)
    arithmetic = inverse(observed, *arithmetic)
    hull = inverse(observed, *hull)
    return max(arithmetic[0], hull[0]), min(arithmetic[1], hull[1])


def at(values, shape, xyz):
    x, y, z = (max(0, min(n - 1, i)) for n, i in zip(shape, xyz))
    return values[(z * shape[1] + y) * shape[0] + x]


def gradients(values, shape):
    result = [F(0)] * 3
    for xyz in product(*(range(n) for n in shape)):
        for axis in range(3):
            other = list(xyz)
            other[axis] += 1
            result[axis] = max(result[axis], abs(at(values, shape, xyz) - at(values, shape, other)))
    return result


def address(uv, n, upward):
    scaled = rounded(rounded(uv * n) - F(1, 2))
    floor = (scaled * 256).__floor__()
    choices = [F(i, 256) for i in (floor, floor + 1)
               if abs(F(i, 256) - scaled) <= F(3, 5 * 256)]
    return max(choices) if upward else min(choices)


def filtered(values, shape, coordinates, half, upward, reverse):
    addresses = [address(uv, n, upward) for uv, n in zip(coordinates, shape)]
    base = [p.__floor__() for p in addresses]
    weights = [p - b for p, b in zip(addresses, base)]
    terms, taps = [], []
    for corner in product((0, 1), repeat=3):
        value = at(values, shape, [b + c for b, c in zip(base, corner)])
        taps.append(value)
        for w, side in zip(weights, corner):
            converted = rounded(w, half)
            coefficient = converted if side else rounded(1 - converted, half)
            value = rounded(value * coefficient, half)
        terms.append(value)
    total = F(0)
    for value in reversed(terms) if reverse else terms:
        total = rounded(total + value, half)
    # D3D's independent min/max invariant also applies to floating filters.
    return min(max(total, min(taps)), max(taps))


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rng = random.Random(0x524f3435)
    checks = 0
    constant_checks = 0
    native_regression = {
        "observed": F(0.5419921875), "reference": F(0.5417330265045166),
        "store_only_allowance": F(0.00019880134481466832),
    }
    assert abs(native_regression["observed"] - native_regression["reference"]) > native_regression["store_only_allowance"]
    for shape in ((1, 1, 1), (1, 1, 32), (3, 4, 1), (3, 2, 3)):
        count = shape[0] * shape[1] * shape[2]
        for case in range(128):
            scale = F(2) ** rng.choice((-24, -14, -4, 0, 4, 12))
            source = [rounded(scale * F(rng.randrange(1025), 1024)) for _ in range(count)]
            if case % 16 == 0:
                source = [scale] * count
            coordinates = [F(rng.randrange(-32, 1057), 1024) for _ in range(3)]
            if case % 8 == 0:
                coordinates[0] = F(257, 1024)
            for half in (False, True):
                # Include retained error even in FP32 to exercise recovery.
                retained = F(0) if case % 3 == 0 else scale / 4096
                observed = [rounded(max(F(0), v + retained), half) for v in source]
                r = F(1, 2048) if half else F(0)
                a = max([F(0)] + [abs(o - s) - r * s for o, s in zip(observed, source)])
                grad = gradients(source, shape)
                for upper, reverse in product((False, True), repeat=2):
                    sample = filtered(observed, shape, coordinates, half, upper, reverse)
                    reference = filtered(source, shape, coordinates, False, not upper, not reverse)
                    # The zero-error FP32 shortcut relies on identical sampling
                    # execution, unlike the deliberately differing model above.
                    if not half and r == 0 and a == 0:
                        reference = sample
                    low, high = intervals(sample, (r, a), grad, shape, half)
                    if not low <= reference <= high:
                        raise AssertionError((shape, case, half, float(low), float(reference), float(high)))
                    checks += 1
                    constant_checks += case % 16 == 0
    report = {"verdict": "pass", "rational_filter_checks": checks,
              "constant_field_checks": constant_checks,
              "formats": ["FP16", "FP32 with retained error"],
              "scope": "positive weighted expansions, legal fixed address choices, hull intersection; not native qualification",
              "old_store_only_regression_reproduced": True}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
