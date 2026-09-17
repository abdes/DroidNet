"""Compare static MultiView proof captures (requires Pillow).

Tolerances: 1/512 EV for gain/meter; 0.5% + 2e-5 scene RGB; one output code.
"""

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops


def compare_images(reference, actual, *, composite=False):
    with Image.open(reference) as ref, Image.open(actual) as got:
        if ref.size != got.size:
            raise AssertionError(f"Image extent mismatch: {ref.size} vs {got.size}")
        ref, got = ref.convert("RGB"), got.convert("RGB")
        if composite:
            # Exclude the toolbar/gizmo and RenderDoc's physical-FPS overlay.
            # The entire main/PiP composition to their right remains compared.
            region = (128, 32, ref.width, ref.height)
            ref, got = ref.crop(region), got.crop(region)
        difference = ImageChops.difference(ref, got)
        maximum = max(hi for _, hi in difference.getextrema())
        if maximum > 1:
            raise AssertionError(f"Image error {maximum} codes exceeds 1: {actual}")
        return maximum


def compare_view(reference, actual):
    if (reference["width"], reference["height"], reference["frame"]) != (
        actual["width"], actual["height"], actual["frame"]
    ):
        raise AssertionError("Views do not share extent and captured frame")
    for view in (reference, actual):
        values = (view["gain"], view["target_gain"], view["pre_exposure"], view["raw_luminance"], view["raw_ev"])
        if not all(math.isfinite(value) for value in values) or min(values[:4]) <= 0:
            raise AssertionError("Invalid exposure or metering values")
        if abs(math.log2(view["gain"] / view["target_gain"])) > 1 / 512:
            raise AssertionError("Paused remeter has not reached its target")
        if view["requested_generation"] == 0 or view["requested_generation"] != view["applied_generation"]:
            raise AssertionError("Proof remeter did not complete")
        if view["state_flags"] & (16 | 128) or not view["state_flags"] & 4:
            raise AssertionError("Expected independent ordinary metering")
    gain_error = abs(math.log2(actual["gain"] / reference["gain"]))
    meter_error = abs(actual["raw_ev"] - reference["raw_ev"])
    if gain_error > 1 / 512 or meter_error > 1 / 512:
        raise AssertionError(f"Gain/meter mismatch: {gain_error}, {meter_error} EV")
    if len(reference["probes"]) != len(actual["probes"]):
        raise AssertionError("Opaque probe coverage differs")
    for ref, got in zip(reference["probes"], actual["probes"]):
        if (ref["x"], ref["y"]) != (got["x"], got["y"]):
            raise AssertionError("Probe positions differ")
        for r, g in zip(ref["scene"][:3], got["scene"][:3]):
            r /= reference["pre_exposure"]
            g /= actual["pre_exposure"]
            if not math.isfinite(g) or abs(g - r) > 0.005 * abs(r) + 2e-5:
                raise AssertionError(f"Scene-referred probe mismatch: {r} vs {g}")
    return {
        "gain_error_ev": gain_error,
        "meter_error_ev": meter_error,
        "mapped_max_error_codes": compare_images(reference["image"], actual["image"]),
        "precomposition_max_error_codes": compare_images(
            reference["precomposition_image"], actual["precomposition_image"]
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("family", "main", "pip", "reordered", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    load = lambda path: json.loads(path.read_text(encoding="utf-8"))
    family, main_view, pip_view, reordered = (
        load(args.family), load(args.main), load(args.pip), load(args.reordered)
    )
    if [len(item["views"]) for item in (family, main_view, pip_view, reordered)] != [2, 1, 1, 2]:
        raise AssertionError("Expected a two-view family, two isolated views and reordered family")
    by_extent = lambda item: {(v["width"], v["height"]): v for v in item["views"]}
    expected = by_extent(family)
    changed_order = by_extent(reordered)
    if len(expected) != 2 or expected.keys() != changed_order.keys():
        raise AssertionError("Proof views require distinct matching extents")
    isolated_keys = [
        (item["views"][0]["width"], item["views"][0]["height"])
        for item in (main_view, pip_view)
    ]
    if len(set(isolated_keys)) != 2 or set(isolated_keys) != expected.keys():
        raise AssertionError("Isolated reports must cover both distinct family views")
    results = {}
    for label, item in (("main", main_view), ("pip", pip_view)):
        view = item["views"][0]
        extent = (view["width"], view["height"])
        results[f"isolated_{label}"] = compare_view(expected[extent], view)
        results[f"reordered_{label}"] = compare_view(expected[extent], changed_order[extent])
    results["reordered_composite_max_error_codes"] = compare_images(
        family["composite_image"], reordered["composite_image"], composite=True
    )
    results["verdict"] = "pass"
    args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
