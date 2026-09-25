"""Summarize the isolated IBL update workload's native GPU/CPU timings."""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

from SummarizeManyLightBaseline import statistics, union_duration


def summarize(directory: Path) -> dict:
    run = json.loads((directory / "run.json").read_text())
    gpu = json.loads((directory / "gpu.json").read_text())
    if not gpu["complete"] or not gpu["timing_valid"]:
        raise ValueError("Incomplete GPU recording")
    with (directory / "updates.csv").open(newline="") as stream:
        cpu = list(csv.DictReader(stream))
    sequences = list(range(run["warmup"] + 1, run["warmup"] + run["samples"] + 1))
    if [int(row["frame_seq"]) for row in cpu] != sequences:
        raise ValueError("Missing CPU samples")
    if [frame["frame_seq"] for frame in gpu["frames"]] != sequences:
        raise ValueError("CPU/GPU frame mismatch")
    update_names = {
        "Vortex.Environment.DistantSkyLightLut",
        "Vortex.Environment.AtmosphereSkyViewLut",
        "Vortex.Environment.IBL.Process",
    }
    stages = defaultdict(list)
    updates = []
    for frame in gpu["frames"]:
        if frame["overflowed"]:
            raise ValueError("GPU query capacity exceeded")
        seen = set()
        intervals = []
        for scope in frame["scopes"]:
            name = scope["name"]
            if not scope["valid"] or name in seen:
                raise ValueError("Invalid or duplicate GPU scope")
            seen.add(name)
            stages[name].append(scope["duration_ms"])
            if scope["end_ms"] < scope["start_ms"]:
                raise ValueError("Reversed GPU interval")
            if name in update_names:
                intervals.append((scope["start_ms"], scope["end_ms"]))
        if seen != update_names | {"Vortex.Frame"}:
            raise ValueError("Unexpected GPU scope population")
        updates.append(union_duration(intervals))
    return {
        "scope": "Isolated immediate update; full-scene acceptance remains separate",
        "update_gpu_union_ms": statistics(updates),
        "scopes_ms": {name: statistics(values) for name, values in stages.items()},
        "cpu_record_ms": statistics([float(row["cpu_record_ms"]) for row in cpu]),
        "storage_creations": sorted({int(row["storage_creations"]) for row in cpu}),
        "allocated_slots": sorted({int(row["allocated_slots"]) for row in cpu}),
        "run": run,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    summary = summarize(args.directory)
    (args.directory / "summary.json").write_text(
        json.dumps(summary, indent=2, allow_nan=False) + "\n",
        encoding="utf-8", newline="\n")
    print(json.dumps(summary["update_gpu_union_ms"], indent=2))


if __name__ == "__main__":
    main()
