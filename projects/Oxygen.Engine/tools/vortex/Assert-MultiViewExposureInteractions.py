"""Check the authored MultiView interaction timeline against captured GPU state.

The transition analyzer supplies independent per-frame meter/response checks.
This checks view identity, ownership, event isolation, geometry and continuity.
"""

import argparse
import json
import math
from pathlib import Path


MAIN = "DemoRuntime.CompositeColor.1000"
PIP = "DemoRuntime.CompositeColor.1001"
NEW_PIP = "DemoRuntime.CompositeColor.1007"
OFFSCREEN = {"M06B.OffscreenPreview.Deferred.Color", "M06B.OffscreenCapture.Forward.Color"}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def same_gain(a, b):
    return a == b == 0 or a > 0 and b > 0 and abs(math.log2(a / b)) <= 1 / 512


def check(reports):
    views, solves = {}, {}
    for report in reports:
        require(report["response_verdict"] == "pass", "Incomplete response qualification")
        for key, output in (("views", views), ("solves", solves)):
            for row in report[key]:
                identity = row["frame"], row["target_name"]
                require(identity not in output, f"Duplicate {key} {identity}")
                output[identity] = row
    require(views.keys() == solves.keys(), "View/solve coverage differs")
    required = {39, 40, 43, 44, 45, 46, 51, 52, 55, 56, 59, 60, 61, 62,
                63, 64, 65, 67, 68, 69, 71, 72, 75, 76, 79, 80, 83, 84, 85,
                88, 89, 95, 96, 97, 99, 100, 103, 104, 107, 108, 109,
                111, 112, 113, 115, 116, 117, 119, 120, 121,
                127, 128, 129, 132, 136, 137}
    frames = {frame for frame, _ in views}
    require(required <= frames, f"Missing event frames {sorted(required - frames)}")

    def pip(frame):
        return NEW_PIP if frame >= 108 else PIP

    for frame in frames:
        expected = OFFSCREEN | ({MAIN} if not 120 <= frame < 128 else set())
        if not 100 <= frame < 104:
            expected.add(pip(frame))
        actual = {name for f, name in views if f == frame}
        require(actual == expected, f"Frame {frame} targets: {actual} != {expected}")
        for name in actual:
            v, s = views[frame, name], solves[frame, name]
            require(math.isclose(s["speed_up"], 3, rel_tol=2e-7)
                    and s["speed_down"] == 1, f"Recipe rates changed: {frame}, {name}")
            require(v["probes"] and any(max(p["mapped"][:3]) > .02 for p in v["probes"]),
                    f"Unexpected black lit view: {frame}, {name}")
            if name == pip(frame):
                require(v["shading_path"] == ("forward" if 56 <= frame < 96 else "deferred"),
                        f"PiP shading path at {frame}")

    # Fixed mode, continuity on Auto entry, and explicit events affect PiP only.
    for frame in (52, 55, 56, 59):
        require(solves[frame, PIP]["method"] == "fixed"
                and same_gain(views[frame, PIP]["gain"], 2 ** -14.5), f"Manual at {frame}")
    require(solves[60, PIP]["method"] == "mode_entry"
            and same_gain(views[60, PIP]["gain"], views[59, PIP]["gain"]), "Auto entry jumped")
    for frame, method in ((64, "seed"), (68, "remeter")):
        require(solves[frame, PIP]["method"] == method, f"Missing PiP {method}")
        for name in OFFSCREEN | {MAIN}:
            require(solves[frame, name]["method"] == "hybrid", f"PiP event reset {name}")
    require(same_gain(views[64, PIP]["gain"], 2 ** -15), "Seed gain differs")

    # The scripted intent order reverses, while the engine's z-order keeps
    # main before PiP. Submission order must not change their composition order.
    for frame in (71, 72, 75, 76, 79, 80):
        require(views[frame, MAIN]["event"] < views[frame, PIP]["event"],
                f"Stable view ordering at {frame}")
    extent = lambda frame, name: (views[frame, name]["width"], views[frame, name]["height"])
    require(extent(75, PIP) != extent(76, PIP), "PiP resize did not change extent")
    require(extent(75, PIP) == extent(84, PIP), "PiP extent not restored")
    require(extent(75, MAIN) == extent(76, MAIN) == extent(84, MAIN), "PiP resize changed main")
    for frame in (80, 83):
        v = views[frame, PIP]
        require(v["write_rectangle"] == [96, 96, v["width"] - 192, v["height"] - 192],
                f"Local scissor at {frame}")
    require(extent(89, MAIN) == (1280, 800), "Window resize not applied")
    require(extent(97, MAIN) == extent(85, MAIN), "Window extent not restored")
    for name in OFFSCREEN:
        require(extent(85, name) == extent(89, name) == extent(97, name), "Window resize changed offscreen")

    require(solves[104, PIP]["lifetime"] == solves[99, PIP]["lifetime"], "Reopen lost retained lifetime")
    require(solves[104, PIP]["previous"]["frame"] == 99, "Reopen did not use last active history")
    require(solves[108, NEW_PIP]["method"] == "initialization", "New PiP inherited old exposure")
    require(solves[108, NEW_PIP]["lifetime"] != solves[107, PIP]["lifetime"], "PiP lifetime reused")

    # The consumer copies prior owner output, including a seed one frame later.
    for frame in (112, 113, 116, 117, 120):
        s = solves[frame, NEW_PIP]
        if frame != 120:
            prior = frame - 1
            require(s["method"] == "borrowed", f"Missing borrowing at {frame}")
            require(same_gain(views[frame, NEW_PIP]["gain"], views[prior, MAIN]["gain"]),
                    f"Prior-owner latency at {frame}")
            require(not s["current"]["flags"] & (4 | 8 | 512), "Borrower retained a local meter")
        else:
            require(s["method"] == "source_loss", "Source removal did not preserve continuity")
            require(same_gain(views[120, NEW_PIP]["gain"], views[119, NEW_PIP]["gain"]),
                    "Source loss changed displayed gain")
    require(solves[116, MAIN]["method"] == "seed", "Owner seed missing")
    require(solves[121, NEW_PIP]["method"] == "hybrid", "Detached consumer did not adapt")
    require(solves[128, MAIN]["method"] == "initialization", "Recreated main inherited stale history")
    for name in OFFSCREEN | {MAIN, NEW_PIP}:
        require(solves[137, name]["delta_seconds"] == 0, "Paused frame has nonzero dt")
        require(same_gain(solves[137, name]["current"]["gain"],
                          solves[137, name]["previous"]["gain"]), "Paused gain changed")
    return {"verdict": "pass", "frames": len(frames), "view_checks": len(views),
            "max_response_error_ev": max(s["error_ev"] for s in solves.values())}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", nargs="+", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = check([json.loads(p.read_text()) for p in args.reports])
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
