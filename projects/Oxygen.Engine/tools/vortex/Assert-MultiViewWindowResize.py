"""Verify exposure history through swapchain resizing/restoration (Pillow)."""

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops


def image_error(left, right):
    with Image.open(left) as a, Image.open(right) as b:
        if a.size != b.size:
            raise AssertionError("Restored image extent differs")
        error = max(hi for _, hi in ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getextrema())
        if error > 1:
            raise AssertionError(f"Restored image differs by {error} codes")
        return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    captures = []
    for capture_frame in (42, 48, 56):
        data = json.loads((args.directory / f"{args.prefix}-{capture_frame}.exposure.json").read_text(encoding="utf-8"))
        if len(data["views"]) != 2 or any(v["frame"] != capture_frame + 1 for v in data["views"]):
            raise AssertionError("Both views must survive the resize at the expected frame")
        views = sorted(data["views"], key=lambda v: v["width"] * v["height"], reverse=True)
        with Image.open(data["composite_image"]) as composite:
            if composite.size != (views[0]["width"], views[0]["height"]):
                raise AssertionError("Swapchain and main-view extents disagree")
        captures.append(views)
    if (captures[1][0]["width"], captures[1][0]["height"]) != (1280, 800):
        raise AssertionError("Window did not reach the requested resized extent")
    if (captures[0][0]["width"], captures[0][0]["height"]) == (1280, 800):
        raise AssertionError("The proof must exercise an actual extent change")
    if (captures[1][1]["width"], captures[1][1]["height"]) != (576, 360):
        raise AssertionError("PiP did not follow the resized window")
    results = []
    for index, before in enumerate(captures[0]):
        errors = []
        for views in captures:
            view = views[index]
            if not all(math.isfinite(view[key]) for key in ("gain", "target_gain", "raw_ev", "raw_luminance")) or view["gain"] <= 0:
                raise AssertionError("Invalid exposure values")
            if not view["state_flags"] & 4 or view["state_flags"] & (16 | 128):
                raise AssertionError("Expected ordinary independent metering")
            for key in ("settings_revision", "requested_generation", "applied_generation"):
                if view[key] != before[key] or before["applied_generation"] == 0:
                    raise AssertionError("Window resizing reset the exposure lifetime")
            error = abs(math.log2(view["gain"] / before["gain"]))
            if error > 1 / 512:
                raise AssertionError("Paused gain changed during resizing")
            errors.append(error)
        restored = captures[-1][index]
        if (restored["width"], restored["height"]) != (before["width"], before["height"]):
            raise AssertionError("View extent was not restored")
        results.append({"role": "main" if index == 0 else "pip", "gain_errors_ev": errors,
            "restored_mapped_error_codes": image_error(before["image"], restored["image"]),
            "restored_precomposition_error_codes": image_error(before["precomposition_image"], restored["precomposition_image"])})
    report = {"views": results, "verdict": "pass"}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
