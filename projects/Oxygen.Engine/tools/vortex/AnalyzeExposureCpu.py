"""Intersect existing exposure-owner QPC intervals with ETW scheduled time."""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import hashlib
import json
import math
from pathlib import Path


def reference(path: Path) -> dict:
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def merge(spans):
    result = []
    for start, end in sorted(spans):
        assert end >= start
        if result and start <= result[-1][1]:
            result[-1] = (result[-1][0], max(end, result[-1][1]))
        else:
            result.append((start, end))
    return result


def intersection(spans, timeline, ends):
    total = 0
    for start, end in spans:
        index = bisect.bisect_right(ends, start)
        while index < len(timeline) and timeline[index][0] < end:
            left, right = timeline[index]
            total += max(0, min(end, right) - max(start, left))
            index += 1
    return total


def schedule(rows, thread):
    running = []
    waiting = []
    active = None
    off = None
    for row in sorted(rows, key=lambda r: int(r["qpc"])):
        stamp = int(row["qpc"])
        cpu = int(row["cpu"])
        if int(row["old_thread"]) == thread:
            if active is not None:
                assert active[1] == cpu, "Thread switched out on a different CPU"
                running.append((active[0], stamp))
                active = None
            off = (stamp, int(row["old_state"]))
        if int(row["new_thread"]) == thread:
            assert active is None, "Overlapping scheduled intervals"
            active = (stamp, cpu)
            if off is not None and off[1] == 5:  # Windows Waiting state.
                waiting.append((off[0], stamp))
            off = None
    assert running, "No scheduled intervals for the benchmark thread"
    return merge(running), merge(waiting)


def distribution(values):
    values = sorted(values)
    return {name: values[max(0, math.ceil(len(values) * fraction) - 1)]
            for name, fraction in [("p50", .50), ("p95", .95),
                                   ("p99", .99), ("max", 1)]}


def analyze(manifest_path, schedule_path, metadata_path, output_path):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    owner = manifest["cpu_owner_timing"]
    assert owner["complete"]
    metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
    assert metadata["status"] == 0 and metadata["clock_type"] == 1
    assert metadata["events_lost"] == metadata["buffers_lost"] == 0
    assert metadata["qpc_frequency"] == owner["qpc_frequency"]
    source = manifest_path.parent / owner["path"]
    rows = list(csv.DictReader(source.open()))
    assert len(rows) == owner["records"]
    scheduler = list(csv.DictReader(schedule_path.open()))
    assert len(scheduler) == metadata["records"]
    running, waiting = schedule(scheduler, owner["thread_id"])
    running_ends = [end for _, end in running]
    waiting_ends = [end for _, end in waiting]
    frames = collections.defaultdict(lambda: collections.defaultdict(list))
    labels = collections.Counter()
    for row in rows:
        assert int(row["thread_id"]) == owner["thread_id"]
        start, end = int(row["start_qpc"]), int(row["end_qpc"])
        assert running[0][0] <= start <= end <= running[-1][1], "Incomplete scheduler coverage"
        frames[int(row["frame_seq"])][row["kind"]].append((start, end))
        labels[row["label"]] += 1
    assert sorted(frames) == list(range(manifest["first_frame_seq"], manifest["last_frame_seq"] + 1))
    assert len(frames) == manifest["sample_count"]
    samples = []
    factor = 1000 / owner["qpc_frequency"]
    for frame, spans in sorted(frames.items()):
        exposure = merge(spans["exposure"])
        elapsed = sum(end - start for start, end in exposure)
        active = intersection(exposure, running, running_ends)
        blocked = intersection(exposure, waiting, waiting_ends)
        assert active + blocked <= elapsed
        samples.append({"frame_seq": frame, "active_ms": active * factor,
                        "elapsed_ms": elapsed * factor, "blocked_ms": blocked * factor,
                        "descheduled_ms": (elapsed - active - blocked) * factor,
                        "fence_wait_ms": sum(b - a for a, b in merge(spans["fence_wait"])) * factor})
    metrics = {key: distribution([row[key] for row in samples]) for key in samples[0] if key != "frame_seq"}
    limits = (.1, .2) if manifest["view_count"] == 1 else (.15, .3)
    result = {"case": manifest["workload"], "width": manifest["width"],
              "samples": len(samples), "metrics_ms": metrics, "scope_counts": labels,
              "cpu_1080_pass": None if manifest["width"] != 1920 else
                  metrics["active_ms"]["p95"] <= limits[0] and metrics["active_ms"]["p99"] <= limits[1],
              "inputs": [reference(p) for p in [manifest_path, source, schedule_path, metadata_path]]}
    if output_path.exists():
        raise FileExistsError(output_path)
    samples_path = output_path.with_suffix(".frames.csv")
    if samples_path.exists():
        raise FileExistsError(samples_path)
    with samples_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(samples[0]))
        writer.writeheader()
        writer.writerows(samples)
    result["derived_frames"] = reference(samples_path)
    output_path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["manifest", "schedule", "metadata", "output"]:
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    analyze(args.manifest, args.schedule, args.metadata, args.output)
