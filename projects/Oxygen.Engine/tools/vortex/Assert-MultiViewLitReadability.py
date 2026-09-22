"""Check sunlit original materials without accepting emissive-card substitutes."""

import argparse
import json
from pathlib import Path

import numpy as np


def check_view(view, *, force_white=False):
    count = view["width"] * view["height"]
    base = np.fromfile(view["base"], np.uint8).reshape(count, 4)
    before = np.fromfile(view["before_light"], np.float32).reshape(count, 4)
    hdr = np.fromfile(view["scene"], np.float32).reshape(count, 4)
    mapped = np.fromfile(view["mapped"], np.uint8).reshape(count, 4)[:, :3]
    if force_white:
        mapped = np.full_like(mapped, 255)
    geometry = base[:, 3] > 127
    assert geometry.sum() > 1000, "Missing original mesh coverage"
    assert np.isfinite(hdr).all(), "Nonfinite scene input"
    assert np.max(np.abs(before[geometry, :3])) <= 1e-6, "Unexpected emissive input"
    assert view["first_light_type"] == 0, "Expected the authored directional sun"
    assert view["first_light_rgb_lux"] == [110000, 110000, 110000], "Incorrect authored sun"
    assert view["metered_luminance"] > 0, "Auto meter did not produce a measurement"
    assert view["solve"]["mode"] == 2, "Expected Auto exposure"
    assert view["gain"] == view["target_gain"], "Adaptation is still pending"
    assert view["requested_generation"] == view["applied_generation"] != 0, "Remeter was not applied"
    histogram = np.asarray(view["meter"]["words"][:256], dtype=np.float64)
    low, high = view["solve"]["percentiles"]
    total = histogram.sum()
    ends = histogram.cumsum()
    retained = np.maximum(0, np.minimum(ends, high * total)
                          - np.maximum(ends - histogram, low * total))
    minimum, span = view["solve"]["log_window"]
    reference_luminance = 2 ** (np.dot(retained, minimum + np.arange(256) * span / 255)
                               / retained.sum())
    assert abs(reference_luminance / view["metered_luminance"] - 1) < 1e-5, "Histogram solve mismatch"
    # This fixture uses key 12.5, target luminance .18, compensation zero.
    expected_gain = .18 / reference_luminance
    assert abs(view["gain"] / expected_gain - 1) < .001, "Exposure did not reach its measured target"
    results = []
    # The original green, blue and red materials must each remain visible.
    # GBuffer RGB is stored in sRGB; the source colors are linear asset values.
    for name, linear in (("green", (.2, .7, .3)), ("blue", (.4, .4, .9)), ("red", (.9, .4, .4))):
        linear = np.asarray(linear)
        encoded = np.round(np.where(linear <= .0031308, linear * 12.92,
                                    1.055 * linear ** (1 / 2.4) - .055) * 255)
        mask = geometry & np.all(np.abs(base[:, :3].astype(float) - encoded) <= 1, axis=1)
        pixels = int(mask.sum())
        assert pixels >= 100, f"Missing original {name} material"
        white = float(np.all(mapped[mask] >= 250, axis=1).mean())
        colored = float((np.ptp(mapped[mask], axis=1) > 8).mean())
        assert white < .01, f"{name} washed out: {white}"
        assert colored > .05, f"{name} lost color: {colored}"
        results.append({"material": name, "pixels": pixels,
                        "near_white_fraction": white, "colored_fraction": colored})
    return {"view": view["index"], "gain": view["gain"],
            "metered_luminance": view["metered_luminance"], "materials": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", required=True, type=Path)
    parser.add_argument("--average", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    views = json.loads(args.inputs.read_text())["views"]
    assert len(views) == 2, "Expected main and lit PiP"
    average = json.loads(args.average.read_text())["views"]
    assert len(average) == 2, "Expected two Average views"
    for before, after in zip(average, views):
        assert before["meter"]["profile"] == 0 and after["meter"]["profile"] == 2, "Incorrect profile transition"
        assert before["P"] == after["P"], "Pre-exposure changed"
        for key in ("base", "before_light", "first_light", "scene"):
            assert Path(before[key]).read_bytes() == Path(after[key]).read_bytes(), f"Profile test changed {key}"
        assert before["mapper"] == after["mapper"] and before["gamma"] == after["gamma"], "Display mapping changed"
        assert before["gain"] == before["target_gain"], "Average adaptation is pending"
        assert before["solve"]["mode"] == 2, "Average did not use Auto"
    try:
        check_view(average[0])
    except AssertionError as error:
        assert "washed out" in str(error), str(error)
    else:
        raise AssertionError("Average control did not reproduce washout")
    results = [check_view(v) for v in views]
    for view in views:
        try:
            check_view(view, force_white=True)
        except AssertionError as error:
            assert "washed out" in str(error), str(error)
        else:
            raise AssertionError("Checker accepted synthetic all-white output")
    args.output.write_text(json.dumps({"views": results, "white_negative_controls": 2,
                                     "average_main_readability": "failed as expected",
                                     "hdr_inputs_exact": True}, indent=2) + "\n")
    print("Original lit-material readability passed in both views; white controls rejected")


if __name__ == "__main__":
    main()
