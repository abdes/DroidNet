"""Validate compatible PiP extent/scissor changes (requires Pillow)."""

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops


def image_error(left, right):
    with Image.open(left) as a, Image.open(right) as b:
        if a.size != b.size:
            raise AssertionError("Compared image extents differ")
        error = max(hi for _, hi in ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getextrema())
        if error > 1:
            raise AssertionError(f"Image changed by {error} codes: {right}")
        return error


def gain_error(left, right):
    if not all(math.isfinite(value) and value > 0 for value in (left, right)):
        raise AssertionError("Invalid exposure gain")
    error = abs(math.log2(left / right))
    if error > 1 / 512:
        raise AssertionError(f"Compatible viewport change reset gain by {error} EV")
    return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    captures = []
    for capture_frame in (42, 43, 47, 51):
        stem = args.directory / f"{args.prefix}-{capture_frame}"
        data = json.loads(Path(str(stem) + ".exposure.json").read_text(encoding="utf-8"))
        meters = json.loads(Path(str(stem) + ".meter.json").read_text(encoding="utf-8"))
        if len(data["views"]) != 2 or len(meters) != 2:
            raise AssertionError("Both views and both meters are required")
        views = sorted(data["views"], key=lambda view: view["width"] * view["height"], reverse=True)
        if any(view["frame"] != capture_frame + 1 for view in views):
            raise AssertionError("Unexpected captured GPU frame")
        captures.append((views, meters))
    base_main, base_pip = captures[0][0]
    results = []
    for phase, (views, meters) in enumerate(captures):
        main_view, pip = views
        resized = phase in (1, 2)
        expected_size = (math.floor(base_main["width"] * .30), math.floor(base_main["height"] * .60)) if resized else (base_pip["width"], base_pip["height"])
        if (pip["width"], pip["height"]) != expected_size:
            raise AssertionError("PiP extent did not follow the scripted resize")
        if (main_view["width"], main_view["height"]) != (base_main["width"], base_main["height"]):
            raise AssertionError("PiP resize changed main's extent")
        for before, after in ((base_main, main_view), (base_pip, pip)):
            for field in ("settings_revision", "requested_generation", "applied_generation"):
                if before[field] != after[field]:
                    raise AssertionError(f"Compatible resize changed {field}")
            if before["applied_generation"] == 0:
                raise AssertionError("Initial remeter was not applied")
        main_meter = [m for m in meters if (m["source_width"], m["source_height"]) == (main_view["width"], main_view["height"])]
        pip_meter = [m for m in meters if (m["source_width"], m["source_height"]) == expected_size]
        if len(main_meter) != 1 or len(pip_meter) != 1:
            raise AssertionError("Meter/source association is ambiguous")
        inset = 96 if phase == 2 else 0
        expected_rect = [inset, inset, expected_size[0] - inset * 2, expected_size[1] - inset * 2]
        if pip_meter[0]["rectangle"] != expected_rect:
            raise AssertionError(f"PiP metered outside its content rectangle: {pip_meter[0]['rectangle']}")
        if pip["write_rectangle"] != expected_rect:
            raise AssertionError("Raster and meter rectangles disagree")
        if main_meter[0]["rectangle"] != [0, 0, main_view["width"], main_view["height"]]:
            raise AssertionError("PiP rectangle contaminated main's meter")
        if abs(main_view["raw_ev"] - base_main["raw_ev"]) > 1 / 512:
            raise AssertionError("PiP changes affected main's meter")
        results.append({
            "frame": pip["frame"], "rectangle": expected_rect,
            "main_gain_error_ev": gain_error(main_view["gain"], base_main["gain"]),
            "pip_gain_error_ev": gain_error(pip["gain"], base_pip["gain"]),
            "main_mapped_error_codes": image_error(base_main["image"], main_view["image"]),
            "main_precomposition_error_codes": image_error(base_main["precomposition_image"], main_view["precomposition_image"]),
        })
    restored = captures[-1][0][1]
    resized = captures[1][0][1]
    scissored = captures[2][0][1]
    with Image.open(resized["precomposition_image"]) as before, Image.open(scissored["precomposition_image"]) as after:
        outside = ImageChops.difference(before.convert("RGB"), after.convert("RGB"))
        outside.paste((0, 0, 0), (96, 96, before.width - 96, before.height - 96))
        outside_error = max(hi for _, hi in outside.getextrema())
        if outside_error > 1:
            raise AssertionError("A scissored draw modified pixels outside its write rectangle")
    report = {"phases": results,
              "outside_scissor_error_codes": outside_error,
              "restored_pip_error_codes": image_error(base_pip["precomposition_image"], restored["precomposition_image"]),
              "verdict": "pass"}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
