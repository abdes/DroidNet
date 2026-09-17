"""Assert the public shared-exposure proof's one-frame latency from GPU state."""

import argparse
import json
import math
from pathlib import Path


def close_gain(actual, expected):
    if not all(math.isfinite(value) and value > 0 for value in (actual, expected)):
        raise AssertionError("Invalid gain")
    error = abs(math.log2(actual / expected))
    if error > 1 / 512:
        raise AssertionError(f"Gain mismatch: {actual} vs {expected} ({error} EV)")
    return error


def load_pair(path, frame):
    data = json.loads(path.read_text(encoding="utf-8"))
    if len(data["views"]) != 2:
        raise AssertionError("Expected the ordinary main/PiP family")
    # This fixture's full-window main view is larger than its PiP.
    owner, consumer = sorted(data["views"], key=lambda view: view["width"] * view["height"], reverse=True)
    for view in (owner, consumer):
        if view["frame"] != frame:
            raise AssertionError(f"Expected GPU frame {frame}, got {view['frame']}")
    if owner["state_flags"] & 128 or owner["frame_flags"] & 2:
        raise AssertionError("Main unexpectedly borrows exposure")
    if not consumer["state_flags"] & 128 or not consumer["frame_flags"] & 2:
        raise AssertionError("PiP did not consume the shared source")
    if owner["requested_generation"] == 0 or owner["applied_generation"] != owner["requested_generation"]:
        raise AssertionError("Owner remeter was not applied")
    return owner, consumer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "step", "after", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    before, old_consumer = load_pair(args.before, 43)
    step, lagging_consumer = load_pair(args.step, 44)
    after, following_consumer = load_pair(args.after, 45)
    if step["requested_generation"] <= before["requested_generation"]:
        raise AssertionError("Owner step did not issue a new generation")
    if after["requested_generation"] != step["requested_generation"]:
        raise AssertionError("Unrequested extra remeter after the step")
    results = {
        "before_shared_error_ev": close_gain(old_consumer["gain"], before["gain"]),
        "owner_step_error_ev": close_gain(step["gain"], before["gain"] * 2),
        "same_frame_prior_gain_error_ev": close_gain(lagging_consumer["gain"], before["gain"]),
        "next_frame_shared_error_ev": close_gain(following_consumer["gain"], step["gain"]),
        "paused_owner_error_ev": close_gain(after["gain"], step["gain"]),
        "verdict": "pass",
    }
    args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
