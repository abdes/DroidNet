"""Check standard/auxiliary layout diagnostic routing from native capture reports.

The input report must come from AnalyzeRenderDocMultiViewExposure.py, which
independently checks S/P pixels. Visual framing and shadow availability remain
separate acceptance checks.
"""

import argparse
import json
from pathlib import Path


def check(path):
    report = json.loads(path.read_text(encoding="utf-8"))
    views = report["views"]
    if len(views) != 4:
        raise AssertionError(f"{path}: expected four rendered views")
    diagnostic = [v for v in views if v["frame_flags"] & 4]
    ordinary = [v for v in views if not v["frame_flags"] & 4]
    if len(diagnostic) != 3 or len(ordinary) != 1:
        raise AssertionError(f"{path}: expected one lit and three diagnostic views")
    for view in diagnostic:
        if view["gain"] != 1 or view["applied_generation"] != 0:
            raise AssertionError(f"{path}: diagnostic must use transient unit exposure")
    if ordinary[0]["gain"] <= 0 or not ordinary[0]["state_flags"] & 4:
        raise AssertionError(f"{path}: lit view must retain a valid meter result")
    modes = [mode for view in diagnostic for mode in view["debug_visualizations"]]
    for expected in ("WorldNormals", "DirectionalShadowMask"):
        if sum(f"Vortex.DebugVisualization.{expected} >" in mode for mode in modes) != 1:
            raise AssertionError(f"{path}: expected one {expected} draw")
    if ordinary[0]["debug_visualizations"] or len(modes) != 2:
        raise AssertionError(f"{path}: diagnostic mode leaked to another view")
    return {"report": str(path), "views": len(views), "diagnostic_views": 3,
            "debug_visualizations": modes, "verdict": "pass"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if len({path.resolve() for path in args.reports}) != len(args.reports):
        raise AssertionError("Capture reports must be distinct")
    result = {"scope": "Per-view diagnostic routing and transient exposure",
              "reports": [check(path) for path in args.reports]}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Passed {len(result['reports'])} capture reports")


if __name__ == "__main__":
    main()
