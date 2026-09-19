"""Compare isolated layout views and their required producers with a full family.

Uses the exposure acceptance tolerances: 1/512 EV, 0.5% + 2e-5 scene RGB,
and one mapped output code. Requires Pillow.
"""

import argparse
import importlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops

compare_images = importlib.import_module("Assert-MultiViewExposureEquivalence").compare_images


def validate_case_targets(family_targets, views, selected, producers):
    if selected not in family_targets:
        raise AssertionError(f"Unknown selected target {selected}")
    if len(producers) != len(set(producers)) or selected in producers:
        raise AssertionError("Producer targets must be unique and exclude the selection")
    expected = {selected, *producers}
    if not expected <= family_targets:
        raise AssertionError("Unknown required producer")
    actual = [view["target_name"] for view in views]
    if len(actual) != len(set(actual)):
        raise AssertionError("Duplicate captured targets")
    if set(actual) != expected:
        raise AssertionError(
            f"Isolation targets differ: expected {sorted(expected)}, got {sorted(actual)}"
        )


def compare_view(reference, actual):
    identity = ("target_name", "width", "height", "frame", "write_rectangle")
    if any(reference[key] != actual[key] for key in identity):
        raise AssertionError("Isolated and family view identities differ")
    diagnostic = bool(reference["frame_flags"] & 4)
    if bool(actual["frame_flags"] & 4) != diagnostic:
        raise AssertionError("Diagnostic domain changed in isolation")
    for view in (reference, actual):
        if not math.isfinite(view["gain"]) or view["gain"] <= 0:
            raise AssertionError("Expected finite positive scene exposure")
        if diagnostic:
            if view["gain"] != 1:
                raise AssertionError("Diagnostic view must retain unit gain")
        else:
            if not view["state_flags"] & 4 or view["state_flags"] & (16 | 128):
                raise AssertionError("Expected an independent valid scene meter")
            if abs(math.log2(view["gain"] / view["target_gain"])) > 1 / 512:
                raise AssertionError("Static scene has not reached its target")
    gain_error = abs(math.log2(actual["gain"] / reference["gain"]))
    meter_error = abs(actual["raw_ev"] - reference["raw_ev"])
    if gain_error > 1 / 512 or meter_error > 1 / 512:
        raise AssertionError(f"Gain/meter changed: {gain_error}, {meter_error} EV")
    if len(reference["probes"]) != len(actual["probes"]):
        raise AssertionError("Opaque probe coverage changed")
    for ref, got in zip(reference["probes"], actual["probes"]):
        if (ref["x"], ref["y"]) != (got["x"], got["y"]):
            raise AssertionError("Probe positions changed")
        for r, g in zip(ref["scene"][:3], got["scene"][:3]):
            r /= reference["pre_exposure"]
            g /= actual["pre_exposure"]
            if not math.isfinite(g) or abs(g - r) > .005 * abs(r) + 2e-5:
                raise AssertionError(f"Scene-referred RGB changed: {r} vs {g}")
    return {
        "gain_error_ev": gain_error,
        "meter_error_ev": meter_error,
        "mapped_max_error_codes": compare_images(reference["image"], actual["image"]),
        "precomposition_max_error_codes": compare_images(
            reference["precomposition_image"], actual["precomposition_image"]
        ),
    }


def verify_auxiliary_copy(views, copy):
    producer = views[copy["producer"]]
    consumer = views[copy["consumer"]]
    width, height = copy["extent"]
    if not (0 < width <= min(producer["width"], consumer["width"])
            and 0 < height <= min(producer["height"], consumer["height"])):
        raise AssertionError("Invalid auxiliary copy extent")
    region = (0, 0, width, height)
    with Image.open(producer["precomposition_image"]) as source, \
            Image.open(consumer["precomposition_image"]) as target:
        difference = ImageChops.difference(
            source.convert("RGBA").crop(region), target.convert("RGBA").crop(region)
        )
        maximum = max(hi for _, hi in difference.getextrema())
    if maximum != 0:
        raise AssertionError(f"Auxiliary mapped copy differs by {maximum} codes")
    return {**copy, "max_error_codes": maximum}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True,
                        help="JSON cases with report, selected_target and required_producers")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    load = lambda path: json.loads(path.read_text(encoding="utf-8"))
    family = load(args.family)
    if not family["views"]:
        raise AssertionError("Family contains no mapped views")
    expected = {view["target_name"]: view for view in family["views"]}
    if len(expected) != len(family["views"]):
        raise AssertionError("Family target names must be unique")
    checked = set()
    results = []
    cases = load(args.cases)
    copies = [verify_auxiliary_copy(expected, copy)
              for copy in cases.get("auxiliary_copies", [])]
    for case in cases["cases"]:
        path = Path(case["report"])
        if not path.is_absolute():
            path = args.cases.parent / path
        selected = case["selected_target"]
        producers = case["required_producers"]
        isolated = load(path)
        validate_case_targets(set(expected), isolated["views"], selected, producers)
        if selected in checked:
            raise AssertionError(f"Duplicate selected target {selected}")
        for view in isolated["views"]:
            name = view["target_name"]
            if name not in expected:
                raise AssertionError(f"Unknown isolated target {name}")
            results.append({"report": str(path), "target": name,
                            **compare_view(expected[name], view)})
        checked.add(selected)
    if checked != set(expected):
        raise AssertionError(f"Missing isolated views: {sorted(set(expected) - checked)}")
    result = {"verdict": "pass", "family": str(args.family),
              "comparisons": results, "auxiliary_copies": copies}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
