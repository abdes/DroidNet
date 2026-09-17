"""Verify the replay checker's binary64 shortcut against exact dyadic gradients."""

from argparse import ArgumentParser
from fractions import Fraction as F
import json
from pathlib import Path
import random
import struct

from AnalyzeRenderDocFilterGradients import exact_texel_gradient_upper
from VerifyExposureErrorBounds import gradient_limits


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    rng = random.Random(0x444f5542)
    checks = 0
    for shape in ((1, 1, 1), (3, 4, 1), (3, 4, 2)):
        for case in range(100):
            samples = [tuple(struct.unpack("<f", struct.pack("<I", rng.randrange(0x7f800000)))[0]
                             for _ in range(3)) + (rng.randrange(65537) / 65536,)
                       for _ in range(shape[0] * shape[1] * shape[2])]
            if case == 0:
                samples = [(1., 1., 1., 1.)] * len(samples)
            inverse_p = F(2) ** rng.randrange(-32, 33)
            for wrap in (False, True):
                actual = exact_texel_gradient_upper(samples, shape, inverse_p, True, wrap)
                rgb = [F(0)] * 3
                for channel in range(3):
                    exact = gradient_limits([F(p[channel]) for p in samples], shape, wrap=wrap)
                    rgb = [max(a, b * inverse_p) for a, b in zip(rgb, exact)]
                transmission = gradient_limits([F(p[3]) for p in samples], shape, wrap=wrap)
                if any(a < b for a, b in zip(actual, rgb + transmission)):
                    raise AssertionError(f"Checker underestimated shape={shape} case={case} wrap={wrap}")
                if case == 0 and any(actual):
                    raise AssertionError("Checker inflated a constant texture's zero gradient")
                checks += 6
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"binary64_upper_vs_exact_checks": checks,
                                     "constant_controls": 6, "verdict": "pass"}, indent=2) + "\n")
    print(f"PASS: {checks} binary64 upper bounds enclose exact rational gradients")


if __name__ == "__main__":
    main()
