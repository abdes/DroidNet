"""Summarize matched VX-IBL-01 native scene recordings without trimming frames."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
from pathlib import Path

from SummarizeManyLightBaseline import statistics, union_duration


IBL_SCOPES = {
    "Vortex.Environment.IBL.Process",
    "Vortex.Environment.IBL.AtmosphereTransmittanceLut",
    "Vortex.Environment.IBL.AtmosphereMultiScatteringLut",
    "Vortex.Environment.IBL.DistantSkyLightLut",
    "Vortex.Environment.IBL.AtmosphereSkyViewLut",
    "Vortex.Environment.IBL.BrdfUpload",
}


def read_json(path: Path) -> dict:
    return json.loads(path.read_bytes() if path.exists()
                      else gzip.decompress(path.with_suffix(path.suffix + ".gz").read_bytes()))


def frame_cost(frame: dict) -> tuple[float, float, int]:
    scopes = frame["scopes"]
    if frame["overflowed"] or any(not s["valid"] for s in scopes):
        raise ValueError("Invalid or overflowed GPU frame")
    if any(s["end_ms"] < s["start_ms"] for s in scopes):
        raise ValueError("Reversed GPU scope")
    spans = [s["duration_ms"] for s in scopes if s["name"] == "Vortex.Frame"]
    if len(spans) != 1:
        raise ValueError("Expected one whole-frame span")
    intervals = [(s["start_ms"], s["end_ms"]) for s in scopes if s["name"] in IBL_SCOPES]
    return union_duration(intervals), spans[0], len(intervals)


def summarize(directory: Path) -> dict:
    run = read_json(directory / "run.json")
    gpu = read_json(directory / "gpu.json")
    first = read_json(directory / "first-use-gpu.json")
    if any(not recording["complete"] or not recording["timing_valid"]
           for recording in (gpu, first)):
        raise ValueError("Incomplete GPU recording")
    if run["mode"] not in ("static", "runtime", "authoring"):
        raise ValueError("Unknown workload")
    if run["samples"] != 1800 or run["warmup"] != 120 or run["hz"] != 60:
        raise ValueError("Unexpected reference window")
    sequences = list(range(run["warmup"] + 1, run["warmup"] + run["samples"] + 1))
    with (directory / "frames.csv").open(newline="") as stream:
        cpu = list(csv.DictReader(stream))
    if ([int(row["frame_seq"]) for row in cpu] != sequences
            or [row["frame_seq"] for row in gpu["frames"]] != sequences):
        raise ValueError("Missing or mismatched frame population")
    if any(int(row["draws"]) == 0 for row in cpu):
        raise ValueError("Scene did not render")
    costs = [frame_cost(frame) for frame in gpu["frames"]]
    required = {"Vortex.Stage9.BasePass.MainPass", "Vortex.Stage13.IndirectLighting",
                "Vortex.Stage15.Atmosphere", "Vortex.Stage15.Fog", "Vortex.Stage18.Translucency"}
    if any(not required <= {scope["name"] for scope in frame["scopes"]}
           for frame in gpu["frames"]):
        raise ValueError("Measured frame omitted a required scene stage")
    updates = statistics([cost[0] for cost in costs])
    completion = [int(row["completion_frames"]) for row in cpu if int(row["completion_frames"]) > 0]
    age = max(int(row["source_age"]) for row in cpu)
    authoring = run["mode"] == "authoring"
    static = run["mode"] == "static"
    if not static and not completion:
        raise ValueError("Animated scene never published a generation")
    before, after = run["memory"]
    def product_bytes(snapshot):
        resources = snapshot["resources"]
        return sum(item["placement_bytes"] for kind in ("textures", "buffers")
                   for item in resources[kind]
                   if item["name"].startswith("IBL.") and item["name"] != "IBL.Scene.Output")
    memory = {
        "before_product_placement_bytes": product_bytes(before),
        "after_product_placement_bytes": product_bytes(after),
        "before_storage_creations": before["storage_creations"],
        "after_storage_creations": after["storage_creations"],
        "before_registered_resources": before["registered_resources"],
        "after_registered_resources": after["registered_resources"],
        "maximum_slots": max(int(row["allocated_slots"]) for row in cpu),
    }
    gates = {
        "gpu_p95": updates["p95"] <= (2.0 if authoring else 0.5),
        "gpu_p99": updates["p99"] <= (4.0 if authoring else 1.0),
        "completion": max(completion, default=0) <= (1 if authoring else 4),
        "source_age": age <= (0 if authoring or static else 8),
        "steady_product_allocations": before["storage_creations"] == after["storage_creations"],
        "steady_product_bytes": product_bytes(before) == product_bytes(after),
        "bounded_slots": memory["maximum_slots"] <= 5,
    }
    if static:
        gates["zero_update_work"] = all(cost[2] == 0 for cost in costs)
        gates["unchanged_generation"] = len({row["revision"] for row in cpu}) == 1
    if authoring:
        gates["same_frame_publications"] = len(completion) == run["samples"]
    if len(first["frames"]) != 1 or first["frames"][0]["frame_seq"] != 1:
        raise ValueError("Missing first-use frame")
    initial = first["frames"][0]
    names = {scope["name"] for scope in initial["scopes"]}
    if not IBL_SCOPES <= names:
        raise ValueError("First-use recording omits required IBL producers")
    first_cost = frame_cost(initial)
    return {
        "mode": run["mode"], "width": run["width"], "height": run["height"],
        "update_gpu_union_ms": updates,
        "gpu_frame_span_ms": statistics([cost[1] for cost in costs]),
        "cpu_frame_ms": statistics([float(row["cpu_frame_ms"]) for row in cpu]),
        "publications": len(completion), "maximum_completion_frames": max(completion, default=0),
        "maximum_source_age": age, "memory": memory, "gates": gates,
        "first_use": {"wall_ms": run["first_use_wall_ms"],
                      "scope": "First submitted IBL-ready frame; geometry uploads may still be pending",
                      "ibl_gpu_work_union_ms": first_cost[0], "gpu_frame_span_ms": first_cost[1]},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    summary = summarize(args.directory)
    (args.directory / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n",
                                                 encoding="utf-8", newline="\n")
    print(json.dumps(summary, indent=2))
    if not all(summary["gates"].values()):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
