"""Check source-loss continuity and independent adaptation (1/512 EV budget)."""

import argparse
import json
import math
from pathlib import Path
import re

from exposure_reference import hybrid_trajectory
from PIL import Image, ImageChops


def gain_error(actual, expected):
    if not all(math.isfinite(value) and value > 0 for value in (actual, expected)):
        raise AssertionError("Invalid gain")
    error = abs(math.log2(actual / expected))
    if error > 1 / 512:
        raise AssertionError(f"Gain error {error} EV: {actual} vs {expected}")
    return error


def load_view(path, frame, count):
    views = json.loads(path.read_text(encoding="utf-8"))["views"]
    if len(views) != count or any(view["frame"] != frame for view in views):
        raise AssertionError(f"Wrong view count or frame in {path}")
    return min(views, key=lambda view: view["width"] * view["height"])


def continuity_image_error(before, after, key):
    with Image.open(before[key]) as left, Image.open(after[key]) as right:
        if left.size != right.size:
            raise AssertionError("PiP image extent changed during source loss")
        error = max(hi for _, hi in ImageChops.difference(
            left.convert("RGB"), right.convert("RGB")).getextrema())
        if error > 1:
            raise AssertionError(f"Source loss changed {key} by {error} codes")
        return error


def trajectory(initial, view, log_path):
    entries = re.findall(r"Vortex\.MultiView\.SourceLoss frame=(\d+) delta_seconds=([0-9.eE+\-]+)",
        log_path.read_text(encoding="utf-8"))
    deltas = {}
    for frame, delta in entries:
        frame, delta = int(frame), float(delta)
        if frame in deltas or not math.isfinite(delta) or delta < 0:
            raise AssertionError("Invalid/duplicate game-delta record")
        deltas[frame] = delta
    expected_frames = set(range(43, view["frame"] + 1))
    if not expected_frames.issubset(deltas):
        raise AssertionError("Incomplete game-delta records")
    if any(deltas[frame] != 0 for frame in range(43, 47)):
        raise AssertionError("The source-loss continuity window was not paused")
    elapsed = sum(deltas[frame] for frame in range(47, view["frame"] + 1))
    if elapsed <= 0 or view["state_flags"] & 128 or view["frame_flags"] & 2:
        raise AssertionError("Independent adaptation did not resume")
    if view["fallback_reason"] != 0 or not view["state_flags"] & 4:
        raise AssertionError("Independent meter is unavailable")
    # Canonical fixture settings: target .18, key 12.5, compensation +2,
    # unclipped metered EV, speed-up 3, speed-down 1, transition distance 1.5.
    if not math.isfinite(view["raw_luminance"]) or view["raw_luminance"] <= 0:
        raise AssertionError("Invalid independent luminance")
    target = 0.72 / view["raw_luminance"]
    gain_error(view["target_gain"], target)
    expected = 2 ** float(hybrid_trajectory(
        math.log2(initial), math.log2(target), elapsed, 3, 1, 1.5))
    if not initial < view["gain"] <= target * 2 ** (1 / 512):
        raise AssertionError("Expected a continuous move toward the brighter independent target")
    crossing_time = max(0, abs(math.log2(target / initial)) - 1.5)
    if view["frame"] == 52 and elapsed <= crossing_time:
        raise AssertionError("Later sample did not exercise the exponential branch")
    return {"elapsed_game_seconds": elapsed, "crossing_time_seconds": crossing_time,
            "expected_gain": expected,
            "actual_gain": view["gain"], "error_ev": gain_error(view["gain"], expected)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "continuity", "first", "first-log", "later", "later-log", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    before = load_view(args.before, 43, 2)
    continuity = load_view(args.continuity, 44, 1)
    first = load_view(args.first, 47, 1)
    later = load_view(args.later, 52, 1)
    extent = (before["width"], before["height"])
    if any((view["width"], view["height"]) != extent for view in (continuity, first, later)):
        raise AssertionError("Source-loss reports must follow the same PiP extent")
    if not before["state_flags"] & 128 or not before["frame_flags"] & 2:
        raise AssertionError("PiP did not start as a borrowing view")
    if continuity["fallback_reason"] != 4 or continuity["frame_flags"] & 2:
        raise AssertionError("Missing source-loss continuity disposition")
    if not any(max(probe["scene"][:3]) > 1e-6 for probe in continuity["probes"]):
        raise AssertionError("The surviving PiP has no scene signal")
    results = {
        "continuity_error_ev": gain_error(continuity["gain"], before["gain"]),
        "continuity_mapped_error_codes": continuity_image_error(before, continuity, "image"),
        "continuity_precomposition_error_codes": continuity_image_error(before, continuity, "precomposition_image"),
        "first_independent_frame": trajectory(before["gain"], first, args.first_log),
        "later_independent_frame": trajectory(before["gain"], later, args.later_log),
        "verdict": "pass",
    }
    args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
