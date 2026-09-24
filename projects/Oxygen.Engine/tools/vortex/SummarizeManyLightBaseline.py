"""Summarize EX07D native frame, CPU, GPU, memory and image evidence."""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

import numpy as np


def statistics(values) -> dict:
    data = np.asarray(values, dtype=float)
    if data.size == 0 or not np.isfinite(data).all():
        raise ValueError("Empty or nonfinite timing population")
    return {"count": int(data.size), "mean": float(data.mean()),
            **{f"p{p}": float(np.percentile(data, p)) for p in (50, 95, 99)},
            "max": float(data.max())}


def union_duration(intervals) -> float:
    end = -float("inf")
    total = 0.0
    for left, right in sorted(intervals):
        total += max(0.0, right - max(left, end))
        end = max(end, right)
    return total


def summarize(directory: Path) -> dict:
    manifest = json.loads((directory / "manifest.json").read_text())
    comparison = json.loads((directory / "image-comparison.json").read_text())
    if not (manifest["complete"] and manifest["gpu_complete"]
            and manifest["gpu_timing_valid"] and comparison["passed"]):
        raise ValueError(f"Incomplete qualification: {directory}")
    with (directory / "frames.csv").open() as stream:
        frames = list(csv.DictReader(stream))
    sequences = [int(row["frame_seq"]) for row in frames]
    if len(sequences) != manifest["sample_frames"] or sequences != list(range(sequences[0], sequences[-1] + 1)):
        raise ValueError("Missing native CPU frame intervals")
    gpu = json.loads((directory / "gpu.json").read_text())
    if not gpu["complete"] or not gpu["timing_valid"]:
        raise ValueError("Incomplete GPU capture")
    gpu_frames = {row["frame_seq"]: row for row in gpu["frames"]}
    if set(gpu_frames) != set(sequences):
        raise ValueError("CPU/GPU frame sequence mismatch")
    stages = defaultdict(list)
    all_names = {scope["name"] for frame in gpu_frames.values() for scope in frame["scopes"]}
    for sequence in sequences:
        totals = defaultdict(float)
        for scope in gpu_frames[sequence]["scopes"]:
            if not scope["valid"]:
                raise ValueError("Invalid GPU interval")
            totals[scope["name"]] += scope["duration_ms"]
        for name in all_names:
            stages[name].append(totals[name])
    frequency = manifest["cpu"]["qpc_frequency"]
    by_frame = defaultdict(list)
    labels = defaultdict(lambda: defaultdict(float))
    with (directory / "cpu-scopes.csv").open() as stream:
        for row in csv.DictReader(stream):
            sequence = int(row["frame_seq"])
            if sequence not in gpu_frames:
                raise ValueError("CPU scope outside timed window")
            left = int(row["start_qpc"]) * 1000 / frequency
            right = int(row["end_qpc"]) * 1000 / frequency
            labels[row["label"]][sequence] += right - left
            if row["kind"] == "lighting":
                by_frame[sequence].append((left, right))
    wall = np.array([float(row["wall_ms"]) for row in frames])
    block_size = 240 if manifest["request"]["moving"] else 60
    block_means = [float(block.mean()) for block in np.array_split(wall, len(wall) // block_size)]
    before, after = manifest["memory"]
    return {"case": manifest["request"]["case"], "family": manifest["request"]["family"],
            "directory": str(directory), "frames": len(frames),
            "frame_ms": statistics(wall), "gpu_frame_ms": statistics(stages["Vortex.Frame"]),
            "cpu_frame_start_ms": statistics([float(row["frame_start_ms"]) for row in frames]),
            "cpu_scene_update_ms": statistics([float(row["scene_update_ms"]) for row in frames]),
            "cpu_submission_ms": statistics([float(row["submission_ms"]) for row in frames]),
            "cpu_lighting_union_ms": statistics([union_duration(by_frame[s]) for s in sequences]),
            "cpu_scopes_ms": {name: statistics([rows[s] for s in sequences]) for name, rows in labels.items()},
            "gpu_scopes_ms": {name: statistics(values) for name, values in sorted(stages.items())},
            "noise": {"block_frames": block_size, "block_means_ms": block_means,
                      "block_mean_range_percent": 100 * (max(block_means) - min(block_means)) / wall.mean(),
                      "scope": "Within-capture block variability; no between-run confidence claim"},
            "memory_before": before, "memory_after": after,
            "steady_buffer_creations": after["created_buffers"] - before["created_buffers"],
            "steady_texture_creations": after["created_textures"] - before["created_textures"],
            "lighting_staging_bytes_per_frame": (after["lighting_staging_bytes"] - before["lighting_staging_bytes"]) / len(frames),
            "shared_staging_bytes_per_frame": (after["shared_staging_bytes"] - before["shared_staging_bytes"]) / len(frames),
            "images": manifest["images"], "image_comparison": comparison}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    rows = [summarize(path.parent) for path in sorted(root.glob("*/baseline/manifest.json"))]
    if not rows:
        raise ValueError("No completed baseline rows")
    (root / "summary.json").write_text(json.dumps(rows, indent=2, allow_nan=False) + "\n", encoding="utf-8", newline="\n")
    lines = ["# EX07D many-light baseline", "",
             "Deferred and forward rendering, native offscreen Release. Timed windows retain all frames; image readbacks and resource inventories are untimed. Deferred scaling is part of this baseline.", "",
             "| Scene | Path | Frame mean / p95 / p99 / max (ms) | GPU frame mean (ms) | Lighting / whole allocator (MiB) | Buffer / texture creations |",
             "| --- | --- | --- | ---: | ---: | ---: |"]
    for row in rows:
        frame = row["frame_ms"]
        budget = row["memory_after"]["lighting_budget"]["allocated_bytes"] / (1024 ** 2)
        allocated = row["memory_after"]["allocator"]["samples"][0]["local"]["allocation_bytes"] / (1024 ** 2)
        values = " / ".join(f"{frame[name]:.3f}" for name in ("mean", "p95", "p99", "max"))
        lines.append(f"| {row['case']} | {row['family']} | {values} | {row['gpu_frame_ms']['mean']:.3f} | {budget:.2f} / {allocated:.2f} | {row['steady_buffer_creations']} / {row['steady_texture_creations']} |")
    lines += ["", "CPU scope totals use interval unions; nested scopes are reported separately in summary.json. GPU scope costs are summed per frame before computing percentiles, with names kept separate to avoid double counting nested passes.", "",
              "Noise is measured from consecutive fixed-size blocks (full motion cycles for dynamic rows). These captures do not establish between-run confidence intervals. Compare changes smaller than observed variability as inconclusive.", "",
              "Resource inventories include unique native placement requirements, allocator committed/slack bytes, DXGI process usage/budget and the lighting allocation domain. Resource names identify retained shadow/cache allocations; the public allocator does not expose a separate pending-retirement byte category.", "",
              "## Primary scene images", ""]
    for family in ("forward", "deferred"):
        image = root / f"sparse-1024-{family}/baseline/phase-0-view-0.png"
        if image.exists():
            lines += [f"{family.capitalize()}, fixed inspection transform x/(1+x), gamma 2.2:", f"![{family}]({image.as_posix()})", ""]
    (root / "REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"Summarized {len(rows)} rows: {root / 'REPORT.md'}")


if __name__ == "__main__":
    main()
