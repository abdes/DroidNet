"""Qualify the mixed fixture's real HDR material contributions (requires NumPy)."""

import argparse
import json
from pathlib import Path

import numpy as np


def check(report):
    results = []
    for view in report["views"]:
        width, height, p = view["width"], view["height"], view["pre_exposure"]
        evidence = view["material_evidence"]

        def floats(name):
            pixels = np.fromfile(evidence[name], dtype="<f4").reshape(height, width, 4)
            if not np.isfinite(pixels).all():
                raise AssertionError(f"Nonfinite {name}")
            return pixels[..., :3] / p

        base = floats("base_scene")
        before, after = floats("before_translucency"), floats("after_translucency")
        changes = np.max(np.abs(after - before), axis=2)
        affected = int(np.count_nonzero(changes > .005 * np.max(np.abs(before), axis=2) + 2e-5))
        if affected < 32:
            raise AssertionError(f"Missing visible translucent contribution: {view['target_name']}")
        row = {"target": view["target_name"], "translucent_pixels": affected}
        if view["shading_path"] == "deferred":
            colors = np.fromfile(evidence["base_color"], dtype=np.uint8).reshape(height, width, 4)
            coverage = np.fromfile(evidence["masked_coverage"], dtype=np.uint8).reshape(height, width, 4)
            for label, rgb in (("opaque", [.2, .7, .3]), ("masked", [.9, .4, .4])):
                linear = np.array(rgb)
                encoded = np.rint((1.055 * linear ** (1 / 2.4) - .055) * 255)
                mask = np.max(np.abs(colors[..., :3].astype(float) - encoded), axis=2) <= 1
                if np.count_nonzero(mask) < 32:
                    raise AssertionError(f"Missing {label} base-color coverage")
                row[label + "_pixels"] = int(np.count_nonzero(mask))
                if label == "masked":
                    if not np.all(coverage[mask, 0] == 255) or not np.all(np.abs(coverage[mask, 1].astype(int) - 204) <= 1):
                        raise AssertionError("Masked material did not execute the alpha-test path")
            # Source authoring stores the emissive factors as IEEE binary16.
            expected = np.array([.7, .65, .5]) * 4096
            expected = expected.astype(np.float16).astype(np.float32)
            emitted = np.all(np.abs(base - expected) <= .005 * expected + 2e-5, axis=2)
            row["emissive_pixels"] = int(np.count_nonzero(emitted))
            if row["emissive_pixels"] < 32:
                raise AssertionError("Missing scene-referred emissive base-pass contribution")
        else:
            # Lighting is already accumulated in forward base-pass output.
            green = (base[..., 1] > 1.2 * base[..., 0]) & (base[..., 1] > 1.2 * base[..., 2])
            red = (base[..., 0] > 1.2 * base[..., 1]) & (base[..., 0] > 1.2 * base[..., 2])
            row.update(opaque_green_pixels=int(np.count_nonzero(green)), masked_red_pixels=int(np.count_nonzero(red)))
            if min(row["opaque_green_pixels"], row["masked_red_pixels"]) < 32:
                raise AssertionError("Forward base pass lost a colored material")
        results.append(row)
    if len(results) != 4:
        raise AssertionError("Expected main, PiP and both offscreen products")
    return {"verdict": "pass", "views": results}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = check(json.loads(args.report.read_text()))
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
