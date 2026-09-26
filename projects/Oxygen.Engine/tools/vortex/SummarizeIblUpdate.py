"""Summarize the isolated IBL update workload's native GPU/CPU timings."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
from collections import defaultdict
from pathlib import Path

from SummarizeManyLightBaseline import statistics, union_duration


def isolated_scope_name(name: str) -> str:
    # The isolated workload has no visible-sky consumers. Group both generations
    # of capture labels so retained pre-split recordings remain reproducible.
    return name.replace(".IBL.Atmosphere", ".Atmosphere").replace(
        ".IBL.DistantSkyLightLut", ".DistantSkyLightLut")


def summarize(directory: Path) -> dict:
    run = json.loads((directory / "run.json").read_text())
    specified = run.get("specified_face_size", 0)
    scheduled = run.get("scheduled", False)
    authoring = run.get("authoring", False)
    recording = directory / "gpu.json"
    gpu = json.loads(recording.read_text() if recording.exists()
                     else gzip.decompress(recording.with_suffix(".json.gz").read_bytes()))
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
    if specified:
        update_names = {"Vortex.Environment.IBL.Process"}
    if scheduled:
        update_names |= {"Vortex.Environment.AtmosphereTransmittanceLut",
                         "Vortex.Environment.AtmosphereMultiScatteringLut"}
    stages = defaultdict(list)
    updates = []
    for frame in gpu["frames"]:
        if frame["overflowed"]:
            raise ValueError("GPU query capacity exceeded")
        seen = set()
        frame_stages = defaultdict(float)
        intervals = []
        for scope in frame["scopes"]:
            name = isolated_scope_name(scope["name"])
            if not scope["valid"] or (name in seen and not scheduled):
                raise ValueError("Invalid or duplicate GPU scope")
            seen.add(name)
            frame_stages[name] += scope["duration_ms"]
            if scope["end_ms"] < scope["start_ms"]:
                raise ValueError("Reversed GPU interval")
            if name in update_names:
                intervals.append((scope["start_ms"], scope["end_ms"]))
        if scheduled:
            allowed = update_names | {"Vortex.Frame", "IblInitializeCS", "IblPrepareCS",
                "IblCapturePrepareCS", "IblRangeCS", "IblNormalizeCS", "IblMipCS",
                "IblShCS", "IblShReduceCS", "IblPrefilterCS", "IblNarrowCS",
                "IblPrecisionRangeCS", "IblPrecisionReduceCS", "IblCompleteCS"}
            allowed |= {"IBL.Batch." + name for name in allowed if name.startswith("Ibl")}
            if not {"Vortex.Frame", "Vortex.Environment.IBL.Process"} <= seen or not seen <= allowed:
                raise ValueError(f"Unexpected scheduled scope population: {seen}")
        elif seen != update_names | {"Vortex.Frame"}:
            raise ValueError("Unexpected GPU scope population")
        for name, value in frame_stages.items():
            stages[name].append(value)
        updates.append(union_duration(intervals))
    result = {
        "scope": "Isolated specified-cube processing; size scaling report" if specified else "Isolated immediate authoring; full-scene acceptance remains separate" if authoring else "Isolated scheduled update; full-scene acceptance remains separate" if scheduled else "Isolated immediate update; full-scene acceptance remains separate",
        "update_gpu_union_ms": statistics(updates),
        "scopes_ms": {name: statistics(values) for name, values in stages.items()},
        "cpu_record_ms": statistics([float(row["cpu_record_ms"]) for row in cpu]),
        "run": run,
    }
    if scheduled:
        latency = [int(row["completion_frames"]) for row in cpu if int(row["completion_frames"]) > 0]
        if not latency:
            raise ValueError("No candidate published in the measured window")
        result["maximum_completion_frames"] = max(latency)
        result["maximum_source_age"] = max(int(row["source_age"]) for row in cpu)
        result["feedback_samples"] = int(cpu[-1]["feedback_samples"])
        result["publications"] = len(latency)
        result["gates"] = {
            "gpu_p95": result["update_gpu_union_ms"]["p95"] <= (2.0 if authoring else 0.5),
            "gpu_p99": result["update_gpu_union_ms"]["p99"] <= (4.0 if authoring else 1.0),
            "completion": max(latency) <= (1 if authoring else 4),
            "source_age": result["maximum_source_age"] <= (0 if authoring else 8),
        }
        if authoring and len(latency) != run['samples']:
            raise ValueError("Authoring did not publish every measured frame")
    else:
        result["storage_creations"] = sorted({int(row["storage_creations"]) for row in cpu})
        result["allocated_slots"] = sorted({int(row["allocated_slots"]) for row in cpu})
    if specified:
        before, after = run["memory"]
        def product_bytes(snapshot):
            return sum(item["placement_bytes"] for kind in ("textures", "buffers")
                       for item in snapshot["resources"][kind]
                       if item["name"].startswith("IBL.")
                       and item["name"] not in {"IBL.ScalingSource", "IBL.ScalingUpload"})
        if (before["frame_seq"] != run["warmup"]
                or after["frame_seq"] != run["warmup"] + run["samples"]
                or before["storage_creations"] != after["storage_creations"]
                or before["registered_resources"] != after["registered_resources"]
                or product_bytes(before) != product_bytes(after)):
            raise ValueError("Specified-cube warm storage changed")
        result["specified_scaling"] = {
            "face_size": specified,
            "product_placement_bytes": product_bytes(after),
            "storage_creations": after["storage_creations"],
            "allocated_slots": after["allocated_slots"],
            "registered_resources": after["registered_resources"],
            "stable_warm_storage": True,
            "scope": "Two retained processing slots including BRDF and scratch; excludes source and upload",
        }
    first_path = directory / "first-use-gpu.json"
    if first_path.exists() or first_path.with_suffix('.json.gz').exists():
        first = json.loads(first_path.read_text() if first_path.exists()
                           else gzip.decompress(first_path.with_suffix('.json.gz').read_bytes()))
        if not first['complete'] or not first['timing_valid'] or len(first['frames']) != 1:
            raise ValueError('Incomplete first-use GPU recording')
        frame = first['frames'][0]
        if frame['frame_seq'] != 1 or frame['overflowed'] or any(not s['valid'] for s in frame['scopes']):
            raise ValueError('Invalid first-use GPU frame')
        charged = update_names | {'Vortex.Environment.IBL.BrdfUpload'}
        if sum(s['name'] == 'Vortex.Environment.IBL.BrdfUpload' for s in frame['scopes']) != 1:
            raise ValueError('First use must include exactly one BRDF upload')
        spans = [s['duration_ms'] for s in frame['scopes'] if s['name'] == 'Vortex.Frame']
        if len(spans) != 1:
            raise ValueError('Missing first-use queue span')
        result['first_use'] = {
            'wall_ms': run['first_use_wall_ms'],
            'gpu_work_union_ms': union_duration([(s['start_ms'], s['end_ms']) for s in frame['scopes'] if isolated_scope_name(s['name']) in charged]),
            'gpu_queue_span_ms': spans[0],
            'scope': 'First-use producer work includes BRDF upload and capture LUTs; queue span also includes submission gaps',
        }
    return result


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
