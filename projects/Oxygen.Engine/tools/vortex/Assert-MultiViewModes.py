"""Verify immediate per-view exposure mode/lifecycle events (requires Pillow)."""

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops


def gain_error(actual, expected):
    if expected == 0:
        if actual != 0:
            raise AssertionError("Zero target did not produce exact zero gain")
        return 0.0
    if not all(math.isfinite(value) and value > 0 for value in (actual, expected)):
        raise AssertionError("Invalid gain")
    error = abs(math.log2(actual / expected))
    if error > 1 / 512:
        raise AssertionError(f"Gain mismatch: {actual} vs {expected}, error {error} EV")
    return error


def image_error(left, right):
    with Image.open(left) as a, Image.open(right) as b:
        if a.size != b.size:
            raise AssertionError("Unexpected view extent change")
        error = max(hi for _, hi in ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getextrema())
        if error > 1:
            raise AssertionError(f"Unrelated/preserved image changed by {error} codes")
        return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    phases = {}
    for capture_frame in (42, 43, 47, 51, 55, 59, 63, 67, 71, 75):
        data = json.loads((args.directory / f"{args.prefix}-{capture_frame}.exposure.json").read_text(encoding="utf-8"))
        views = sorted(data["views"], key=lambda view: view["width"] * view["height"], reverse=True)
        if len(views) != 2 or any(view["frame"] != capture_frame + 1 for view in views):
            raise AssertionError("Wrong view count or event frame")
        if any(not math.isfinite(view[key]) for view in views
               for key in ("gain", "target_gain", "latent_gain", "latent_target")):
            raise AssertionError("Nonfinite mode state")
        phases[capture_frame + 1] = views
    main_before, pip_before = phases[43]
    results = {}
    expected = {43: pip_before["gain"], 44: 1, 48: pip_before["gain"],
                52: .25, 56: 1, 60: 4 / (11 * 11 * 125), 64: 0,
                68: pip_before["gain"], 72: 4 / 64, 76: pip_before["gain"]}
    for frame, (main_view, pip) in phases.items():
        if (pip["width"], pip["height"]) != (pip_before["width"], pip_before["height"]):
            raise AssertionError("PiP geometry extent changed")
        for key in ("settings_revision", "requested_generation", "applied_generation"):
            if main_view[key] != main_before[key]:
                raise AssertionError("PiP event altered main's state identity")
        results[str(frame)] = {
            "main_gain_error_ev": gain_error(main_view["gain"], main_before["gain"]),
            "main_image_error_codes": image_error(main_before["precomposition_image"], main_view["precomposition_image"]),
            "pip_gain_error_ev": gain_error(pip["gain"], expected[frame]),
        }
    if not phases[44][1]["frame_flags"] & 4:
        raise AssertionError("Wireframe did not use temporary diagnostic exposure")
    restored = phases[48][1]
    for key in ("settings_revision", "requested_generation", "applied_generation"):
        if restored[key] != pip_before[key]:
            raise AssertionError("Diagnostics overwrote the retained Auto history")
    results["diagnostic_restore_image_error_codes"] = image_error(
        pip_before["precomposition_image"], restored["precomposition_image"])
    for frame, mode in ((52, 0), (56, 3), (60, 1), (64, 2), (68, 2)):
        if (phases[frame][1]["state_flags"] >> 10) & 3 != mode:
            raise AssertionError(f"Wrong authored mode at frame {frame}")
    zero = phases[64][1]
    if not zero["state_flags"] & 64 or zero["latent_gain"] <= 0:
        raise AssertionError("Zero target lost its positive latent history")
    for frame in (72, 76):
        view = phases[frame][1]
        prior = phases[68 if frame == 72 else 72][1]
        if view["requested_generation"] <= prior["requested_generation"] or view["applied_generation"] != view["requested_generation"]:
            raise AssertionError("Seed/cut did not apply its new generation on the event frame")
    results["verdict"] = "pass"
    args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
