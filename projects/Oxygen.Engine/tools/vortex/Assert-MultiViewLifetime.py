"""Verify retained-view hiding/reopening and independent recreation (Pillow)."""

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops


def gain_error(actual, expected):
    if not all(math.isfinite(value) and value > 0 for value in (actual, expected)):
        raise AssertionError("Invalid gain")
    error = abs(math.log2(actual / expected))
    if error > 1 / 512:
        raise AssertionError(f"Gain mismatch: {actual} vs {expected}")
    return error


def image_error(left, right):
    with Image.open(left) as a, Image.open(right) as b:
        if a.size != b.size:
            raise AssertionError("View extent changed")
        error = max(hi for _, hi in ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getextrema())
        if error > 1:
            raise AssertionError(f"View image changed by {error} codes")
        return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    captures = []
    for capture_frame, count in ((42, 2), (43, 1), (47, 2), (51, 2)):
        path = args.directory / f"{args.prefix}-{capture_frame}.exposure.json"
        views = json.loads(path.read_text(encoding="utf-8"))["views"]
        if len(views) != count or any(view["frame"] != capture_frame + 1 for view in views):
            raise AssertionError(f"Wrong active views/frame: {path}")
        for view in views:
            if not all(math.isfinite(view[field]) for field in ("gain", "target_gain", "raw_luminance", "raw_ev")):
                raise AssertionError("Nonfinite exposure/meter data")
            if not view["state_flags"] & 4 or view["state_flags"] & (16 | 128):
                raise AssertionError("Expected ordinary independent metering")
        captures.append(sorted(views, key=lambda view: view["width"] * view["height"], reverse=True))
    main_before, pip_before = captures[0]
    main_errors = []
    for views in captures:
        main_view = views[0]
        for field in ("width", "height", "settings_revision", "requested_generation", "applied_generation"):
            if main_view[field] != main_before[field]:
                raise AssertionError(f"PiP lifecycle changed main's {field}")
        if abs(main_view["raw_ev"] - main_before["raw_ev"]) > 1 / 512:
            raise AssertionError("PiP lifecycle changed main's meter")
        main_errors.append({"frame": main_view["frame"],
            "gain_error_ev": gain_error(main_view["gain"], main_before["gain"]),
            "image_error_codes": image_error(main_before["precomposition_image"], main_view["precomposition_image"])})
    reopened = captures[2][1]
    recreated = captures[3][1]
    for view in (reopened, recreated):
        if (view["width"], view["height"]) != (pip_before["width"], pip_before["height"]):
            raise AssertionError("PiP identity comparison has mismatched extents")
        if not view["state_flags"] & 4 or view["state_flags"] & (16 | 128):
            raise AssertionError("Expected an independently metered PiP")
    if pip_before["applied_generation"] == 0 or reopened["applied_generation"] != pip_before["applied_generation"]:
        raise AssertionError("Retained PiP lost its applied transition")
    if reopened["settings_revision"] <= pip_before["settings_revision"]:
        raise AssertionError("Reopened PiP did not accept its new compensation")
    if recreated["requested_generation"] or recreated["applied_generation"]:
        raise AssertionError("Recreated PiP inherited an old transition")
    result = {"main": main_errors,
        "reopen_gain_error_ev": gain_error(reopened["gain"], pip_before["gain"]),
        "reopen_target_error_ev": gain_error(reopened["target_gain"], pip_before["target_gain"] * 2),
        "reopen_image_error_codes": image_error(pip_before["precomposition_image"], reopened["precomposition_image"]),
        "recreated_initialization_error_ev": gain_error(recreated["gain"], recreated["target_gain"]),
        "recreated_target_error_ev": gain_error(recreated["gain"], 1.44 / recreated["raw_luminance"]),
        "verdict": "pass"}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
