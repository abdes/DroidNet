"""Compare a CSM-5 capture analysis package against the locked CSM-2 baseline."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


SHADOW_DRAW_RECORDS_PATTERN = re.compile(
    r"conventional shadow draw records=(\d+)"
)
PREPARED_SHADOW_BOUNDS_PATTERN = re.compile(
    r"prepared shadow bounds collected=(\d+) retained=(\d+) cast_items=(\d+) "
    r"receive_items=(\d+) visible_items=(\d+)"
)
DIRECTIONAL_SUMMARY_PATTERN = re.compile(
    r"directional light summary total=(\d+).*shadowed_total=(\d+).*shadowed_sun=(\d+)"
)


def parse_key_value_report(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key] = value
    return result


def load_report(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    return parse_key_value_report(path)


def parse_float(report: dict[str, str], key: str) -> float:
    try:
        return float(report.get(key, "0").strip())
    except ValueError:
        return 0.0


def parse_int(report: dict[str, str], key: str) -> int:
    try:
        return int(report.get(key, "0").strip())
    except ValueError:
        return 0


def summary_line(name: str, value) -> str:
    return f"{name}={value}"


def safe_delta_percent(current: float, baseline: float) -> float:
    if baseline <= 0.0:
        return 0.0
    return ((current - baseline) / baseline) * 100.0


def parse_last_match(path: Path, pattern: re.Pattern[str]) -> tuple[int, ...]:
    if not path.exists():
        return tuple()

    last_match: tuple[int, ...] = tuple()
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.search(line)
        if match is not None:
            last_match = tuple(int(group) for group in match.groups())
    return last_match


def parse_last_shadow_draw_record_count(path: Path) -> int:
    match = parse_last_match(path, SHADOW_DRAW_RECORDS_PATTERN)
    return match[0] if match else 0


def parse_last_prepared_shadow_bounds(path: Path) -> tuple[int, int, int, int, int]:
    match = parse_last_match(path, PREPARED_SHADOW_BOUNDS_PATTERN)
    if not match:
        return (0, 0, 0, 0, 0)
    return match  # type: ignore[return-value]


def parse_last_directional_summary(path: Path) -> tuple[int, int, int]:
    match = parse_last_match(path, DIRECTIONAL_SUMMARY_PATTERN)
    if not match:
        return (0, 0, 0)
    return match  # type: ignore[return-value]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-stem", required=True)
    parser.add_argument("--current-stem", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    baseline_stem = Path(args.baseline_stem)
    current_stem = Path(args.current_stem)
    output_path = Path(args.output)

    baseline_shadow = load_report(baseline_stem.with_suffix(".shadow_timing.txt"))
    baseline_shader = load_report(baseline_stem.with_suffix(".shader_timing.txt"))
    baseline_hzb = load_report(baseline_stem.with_suffix(".screen_hzb_timing.txt"))
    baseline_receiver_timing = load_report(
        baseline_stem.with_suffix(".receiver_analysis_timing.txt")
    )
    baseline_receiver = load_report(
        baseline_stem.with_suffix(".receiver_analysis_report.txt")
    )

    current_shadow = load_report(current_stem.with_suffix(".shadow_timing.txt"))
    current_shader = load_report(current_stem.with_suffix(".shader_timing.txt"))
    current_hzb = load_report(current_stem.with_suffix(".screen_hzb_timing.txt"))
    current_receiver_timing = load_report(
        current_stem.with_suffix(".receiver_analysis_timing.txt")
    )
    current_mask_timing = load_report(
        current_stem.with_suffix(".receiver_mask_timing.txt")
    )
    current_culling_timing = load_report(
        current_stem.with_suffix(".caster_culling_timing.txt")
    )
    current_culling = load_report(
        current_stem.with_suffix(".caster_culling_report.txt")
    )
    current_raster_indirect = load_report(
        current_stem.with_suffix(".raster_indirect_report.txt")
    )

    baseline_benchmark_log = baseline_stem.with_suffix(".benchmark.log")
    current_benchmark_log = current_stem.with_suffix(".benchmark.log")

    baseline_shadow_ms = parse_float(
        baseline_shadow, "authoritative_scope_gpu_duration_ms"
    )
    current_shadow_ms = parse_float(
        current_shadow, "authoritative_scope_gpu_duration_ms"
    )
    baseline_shader_ms = parse_float(
        baseline_shader, "authoritative_scope_gpu_duration_ms"
    )
    current_shader_ms = parse_float(
        current_shader, "authoritative_scope_gpu_duration_ms"
    )
    baseline_hzb_ms = parse_float(
        baseline_hzb, "authoritative_scope_gpu_duration_ms"
    )
    current_hzb_ms = parse_float(current_hzb, "authoritative_scope_gpu_duration_ms")
    baseline_receiver_ms = parse_float(
        baseline_receiver_timing, "authoritative_scope_gpu_duration_ms"
    )
    current_receiver_ms = parse_float(
        current_receiver_timing, "authoritative_scope_gpu_duration_ms"
    )
    current_mask_ms = parse_float(
        current_mask_timing, "authoritative_scope_gpu_duration_ms"
    )
    current_culling_ms = parse_float(
        current_culling_timing, "authoritative_scope_gpu_duration_ms"
    )

    baseline_shadow_work = parse_int(baseline_shadow, "work_event_count")
    current_shadow_work = parse_int(current_shadow, "work_event_count")
    baseline_shader_work = parse_int(baseline_shader, "work_event_count")
    current_shader_work = parse_int(current_shader, "work_event_count")

    baseline_job_count = parse_int(baseline_receiver, "job_count")
    current_job_count = parse_int(current_culling, "job_count")
    current_partition_count = parse_int(current_culling, "partition_count")
    current_total_emitted_draw_count = parse_int(
        current_culling, "total_emitted_draw_count"
    )
    current_total_rejected_draw_count = parse_int(
        current_culling, "total_rejected_draw_count"
    )
    current_aggregate_rejection_ratio = parse_float(
        current_culling, "aggregate_rejection_ratio"
    )

    raster_job_scope_count = parse_int(current_raster_indirect, "job_scope_count")
    raster_marker_event_count = parse_int(
        current_raster_indirect, "indirect_marker_event_count"
    )
    raster_draw_work_event_count = parse_int(
        current_raster_indirect, "draw_work_event_count"
    )
    raster_indirect_draw_work_event_count = parse_int(
        current_raster_indirect, "indirect_draw_work_event_count"
    )
    raster_clear_work_event_count = parse_int(
        current_raster_indirect, "clear_work_event_count"
    )
    raster_total_work_event_count = parse_int(
        current_raster_indirect, "total_work_event_count"
    )

    baseline_shadow_draw_record_count = parse_last_shadow_draw_record_count(
        baseline_benchmark_log
    )
    current_benchmark_shadow_draw_record_count = parse_last_shadow_draw_record_count(
        current_benchmark_log
    )
    baseline_prepared_bounds = parse_last_prepared_shadow_bounds(
        baseline_benchmark_log
    )
    current_prepared_bounds = parse_last_prepared_shadow_bounds(current_benchmark_log)
    baseline_directional_summary = parse_last_directional_summary(
        baseline_benchmark_log
    )
    current_directional_summary = parse_last_directional_summary(current_benchmark_log)

    baseline_draw_work_event_count = max(baseline_shadow_work - baseline_job_count, 0)
    current_draw_work_event_count = max(current_shadow_work - current_job_count, 0)
    baseline_total_shadow_path_ms = (
        baseline_shadow_ms + baseline_hzb_ms + baseline_receiver_ms
    )
    current_total_shadow_path_ms = (
        current_shadow_ms
        + current_hzb_ms
        + current_receiver_ms
        + current_mask_ms
        + current_culling_ms
    )

    lines = [
        summary_line("comparison_profile", "conventional_shadow_csm5"),
        summary_line("baseline_stem", baseline_stem),
        summary_line("current_stem", current_stem),
        summary_line("baseline_shadow_draw_record_count", baseline_shadow_draw_record_count),
        summary_line(
            "current_benchmark_shadow_draw_record_count",
            current_benchmark_shadow_draw_record_count,
        ),
        summary_line("baseline_job_count", baseline_job_count),
        summary_line("current_job_count", current_job_count),
        summary_line("current_partition_count", current_partition_count),
        summary_line("current_total_emitted_draw_count", current_total_emitted_draw_count),
        summary_line("current_total_rejected_draw_count", current_total_rejected_draw_count),
        summary_line(
            "current_aggregate_rejection_ratio",
            f"{current_aggregate_rejection_ratio:.6f}",
        ),
        summary_line("raster_job_scope_count", raster_job_scope_count),
        summary_line("raster_indirect_marker_event_count", raster_marker_event_count),
        summary_line("raster_draw_work_event_count", raster_draw_work_event_count),
        summary_line(
            "raster_indirect_draw_work_event_count",
            raster_indirect_draw_work_event_count,
        ),
        summary_line("raster_clear_work_event_count", raster_clear_work_event_count),
        summary_line("raster_total_work_event_count", raster_total_work_event_count),
        summary_line("baseline_shadow_work_event_count", baseline_shadow_work),
        summary_line("current_shadow_work_event_count", current_shadow_work),
        summary_line(
            "baseline_shadow_draw_work_event_count", baseline_draw_work_event_count
        ),
        summary_line(
            "current_shadow_draw_work_event_count", current_draw_work_event_count
        ),
        summary_line(
            "draw_work_event_count_matches_emitted_draw_count",
            str(current_draw_work_event_count == current_total_emitted_draw_count).lower(),
        ),
        summary_line(
            "all_draw_work_events_are_indirect",
            str(raster_draw_work_event_count == raster_indirect_draw_work_event_count).lower(),
        ),
        summary_line(
            "shadow_draw_work_event_reduction_vs_baseline",
            baseline_draw_work_event_count - current_draw_work_event_count,
        ),
        summary_line(
            "shadow_draw_work_event_ratio_vs_baseline",
            f"{(float(current_draw_work_event_count) / float(baseline_draw_work_event_count)) if baseline_draw_work_event_count > 0 else 0.0:.6f}",
        ),
        summary_line("baseline_shadow_gpu_duration_ms", f"{baseline_shadow_ms:.6f}"),
        summary_line("current_shadow_gpu_duration_ms", f"{current_shadow_ms:.6f}"),
        summary_line(
            "shadow_gpu_duration_delta_ms",
            f"{current_shadow_ms - baseline_shadow_ms:.6f}",
        ),
        summary_line(
            "shadow_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_shadow_ms, baseline_shadow_ms):.6f}",
        ),
        summary_line("baseline_shader_gpu_duration_ms", f"{baseline_shader_ms:.6f}"),
        summary_line("current_shader_gpu_duration_ms", f"{current_shader_ms:.6f}"),
        summary_line(
            "shader_gpu_duration_delta_ms",
            f"{current_shader_ms - baseline_shader_ms:.6f}",
        ),
        summary_line(
            "shader_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_shader_ms, baseline_shader_ms):.6f}",
        ),
        summary_line(
            "baseline_total_shadow_path_gpu_duration_ms",
            f"{baseline_total_shadow_path_ms:.6f}",
        ),
        summary_line(
            "current_total_shadow_path_gpu_duration_ms",
            f"{current_total_shadow_path_ms:.6f}",
        ),
        summary_line(
            "total_shadow_path_gpu_duration_delta_ms",
            f"{current_total_shadow_path_ms - baseline_total_shadow_path_ms:.6f}",
        ),
        summary_line(
            "total_shadow_path_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_total_shadow_path_ms, baseline_total_shadow_path_ms):.6f}",
        ),
        summary_line(
            "prepared_shadow_bounds_match",
            str(current_prepared_bounds == baseline_prepared_bounds).lower(),
        ),
        summary_line(
            "directional_summary_match",
            str(current_directional_summary == baseline_directional_summary).lower(),
        ),
    ]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
