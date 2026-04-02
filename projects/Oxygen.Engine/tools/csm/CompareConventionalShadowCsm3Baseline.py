"""Compare a CSM-3 capture analysis package against the locked CSM-2 baseline."""

from __future__ import annotations

import argparse
from pathlib import Path


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
    value = report.get(key, "0").strip()
    try:
        return float(value)
    except ValueError:
        return 0.0


def parse_int(report: dict[str, str], key: str) -> int:
    value = report.get(key, "0").strip()
    try:
        return int(value)
    except ValueError:
        return 0


def summary_line(name: str, value) -> str:
    return f"{name}={value}"


def safe_delta_percent(current: float, baseline: float) -> float:
    if baseline <= 0.0:
        return 0.0
    return ((current - baseline) / baseline) * 100.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-stem", required=True)
    parser.add_argument("--current-stem", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    baseline_stem = Path(args.baseline_stem)
    current_stem = Path(args.current_stem)
    output_path = Path(args.output)

    baseline_shadow = load_report(
        baseline_stem.with_suffix(".shadow_timing.txt")
    )
    baseline_shader = load_report(
        baseline_stem.with_suffix(".shader_timing.txt")
    )
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
    current_receiver = load_report(
        current_stem.with_suffix(".receiver_analysis_report.txt")
    )
    current_mask_timing = load_report(
        current_stem.with_suffix(".receiver_mask_timing.txt")
    )
    current_mask = load_report(current_stem.with_suffix(".receiver_mask_report.txt"))

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

    baseline_shadow_work = parse_int(baseline_shadow, "work_event_count")
    current_shadow_work = parse_int(current_shadow, "work_event_count")
    baseline_shader_work = parse_int(baseline_shader, "work_event_count")
    current_shader_work = parse_int(current_shader, "work_event_count")
    baseline_hzb_work = parse_int(baseline_hzb, "work_event_count")
    current_hzb_work = parse_int(current_hzb, "work_event_count")
    baseline_receiver_work = parse_int(
        baseline_receiver_timing, "work_event_count"
    )
    current_receiver_work = parse_int(
        current_receiver_timing, "work_event_count"
    )
    current_mask_work = parse_int(current_mask_timing, "work_event_count")

    baseline_aux_ms = baseline_hzb_ms + baseline_receiver_ms
    current_aux_ms = current_hzb_ms + current_receiver_ms + current_mask_ms

    baseline_receiver_job_count = parse_int(baseline_receiver, "job_count")
    baseline_receiver_sampled_job_count = parse_int(
        baseline_receiver, "sampled_job_count"
    )
    current_receiver_job_count = parse_int(current_receiver, "job_count")
    current_receiver_sampled_job_count = parse_int(
        current_receiver, "sampled_job_count"
    )
    current_mask_job_count = parse_int(current_mask, "job_count")
    current_mask_valid_job_count = parse_int(current_mask, "valid_job_count")
    current_mask_sampled_job_count = parse_int(current_mask, "sampled_job_count")
    current_mask_sparse_job_count = parse_int(current_mask, "sparse_job_count")
    current_mask_empty_job_count = parse_int(current_mask, "empty_job_count")
    current_mask_hierarchy_built_job_count = parse_int(
        current_mask, "hierarchy_built_job_count"
    )
    current_total_occupied_tile_count = parse_int(
        current_mask, "total_occupied_tile_count"
    )
    current_total_full_tile_count = parse_int(
        current_mask, "total_full_tile_count"
    )
    current_total_hierarchy_occupied_tile_count = parse_int(
        current_mask, "total_hierarchy_occupied_tile_count"
    )
    current_total_full_hierarchy_tile_count = parse_int(
        current_mask, "total_full_hierarchy_tile_count"
    )
    current_total_sampled_job_occupied_tile_count = parse_int(
        current_mask, "total_sampled_job_occupied_tile_count"
    )
    current_total_sampled_job_full_tile_count = parse_int(
        current_mask, "total_sampled_job_full_tile_count"
    )
    current_aggregate_occupied_tile_ratio = parse_float(
        current_mask, "aggregate_occupied_tile_ratio"
    )
    current_aggregate_hierarchy_occupied_tile_ratio = parse_float(
        current_mask, "aggregate_hierarchy_occupied_tile_ratio"
    )
    current_sampled_aggregate_occupied_tile_ratio = parse_float(
        current_mask, "sampled_aggregate_occupied_tile_ratio"
    )
    current_sampled_aggregate_hierarchy_occupied_tile_ratio = parse_float(
        current_mask, "sampled_aggregate_hierarchy_occupied_tile_ratio"
    )
    current_min_sampled_occupied_tile_ratio = parse_float(
        current_mask, "min_sampled_occupied_tile_ratio"
    )
    current_max_sampled_occupied_tile_ratio = parse_float(
        current_mask, "max_sampled_occupied_tile_ratio"
    )
    current_min_sampled_hierarchy_occupied_tile_ratio = parse_float(
        current_mask, "min_sampled_hierarchy_occupied_tile_ratio"
    )
    current_max_sampled_hierarchy_occupied_tile_ratio = parse_float(
        current_mask, "max_sampled_hierarchy_occupied_tile_ratio"
    )

    lines = [
        summary_line("comparison_profile", "conventional_shadow_csm3"),
        summary_line("baseline_stem", baseline_stem),
        summary_line("current_stem", current_stem),
        summary_line(
            "baseline_shadow_gpu_duration_ms", f"{baseline_shadow_ms:.6f}"
        ),
        summary_line(
            "current_shadow_gpu_duration_ms", f"{current_shadow_ms:.6f}"
        ),
        summary_line(
            "shadow_gpu_duration_delta_ms",
            f"{current_shadow_ms - baseline_shadow_ms:.6f}",
        ),
        summary_line(
            "shadow_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_shadow_ms, baseline_shadow_ms):.6f}",
        ),
        summary_line("baseline_shadow_work_event_count", baseline_shadow_work),
        summary_line("current_shadow_work_event_count", current_shadow_work),
        summary_line(
            "shadow_work_event_match",
            str(baseline_shadow_work == current_shadow_work).lower(),
        ),
        summary_line(
            "baseline_shader_gpu_duration_ms", f"{baseline_shader_ms:.6f}"
        ),
        summary_line(
            "current_shader_gpu_duration_ms", f"{current_shader_ms:.6f}"
        ),
        summary_line(
            "shader_gpu_duration_delta_ms",
            f"{current_shader_ms - baseline_shader_ms:.6f}",
        ),
        summary_line(
            "shader_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_shader_ms, baseline_shader_ms):.6f}",
        ),
        summary_line("baseline_shader_work_event_count", baseline_shader_work),
        summary_line("current_shader_work_event_count", current_shader_work),
        summary_line(
            "shader_work_event_match",
            str(baseline_shader_work == current_shader_work).lower(),
        ),
        summary_line(
            "baseline_screen_hzb_gpu_duration_ms", f"{baseline_hzb_ms:.6f}"
        ),
        summary_line(
            "current_screen_hzb_gpu_duration_ms", f"{current_hzb_ms:.6f}"
        ),
        summary_line(
            "screen_hzb_gpu_duration_delta_ms",
            f"{current_hzb_ms - baseline_hzb_ms:.6f}",
        ),
        summary_line(
            "screen_hzb_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_hzb_ms, baseline_hzb_ms):.6f}",
        ),
        summary_line("baseline_screen_hzb_work_event_count", baseline_hzb_work),
        summary_line("current_screen_hzb_work_event_count", current_hzb_work),
        summary_line(
            "screen_hzb_work_event_match",
            str(baseline_hzb_work == current_hzb_work).lower(),
        ),
        summary_line(
            "baseline_receiver_analysis_gpu_duration_ms",
            f"{baseline_receiver_ms:.6f}",
        ),
        summary_line(
            "current_receiver_analysis_gpu_duration_ms",
            f"{current_receiver_ms:.6f}",
        ),
        summary_line(
            "receiver_analysis_gpu_duration_delta_ms",
            f"{current_receiver_ms - baseline_receiver_ms:.6f}",
        ),
        summary_line(
            "receiver_analysis_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_receiver_ms, baseline_receiver_ms):.6f}",
        ),
        summary_line(
            "baseline_receiver_analysis_work_event_count", baseline_receiver_work
        ),
        summary_line(
            "current_receiver_analysis_work_event_count", current_receiver_work
        ),
        summary_line(
            "receiver_analysis_work_event_match",
            str(baseline_receiver_work == current_receiver_work).lower(),
        ),
        summary_line(
            "current_receiver_mask_gpu_duration_ms", f"{current_mask_ms:.6f}"
        ),
        summary_line("current_receiver_mask_work_event_count", current_mask_work),
        summary_line(
            "baseline_aux_shadow_path_gpu_duration_ms", f"{baseline_aux_ms:.6f}"
        ),
        summary_line(
            "current_aux_shadow_path_gpu_duration_ms", f"{current_aux_ms:.6f}"
        ),
        summary_line(
            "aux_shadow_path_gpu_duration_delta_ms",
            f"{current_aux_ms - baseline_aux_ms:.6f}",
        ),
        summary_line(
            "aux_shadow_path_gpu_duration_delta_percent",
            f"{safe_delta_percent(current_aux_ms, baseline_aux_ms):.6f}",
        ),
        summary_line("baseline_receiver_job_count", baseline_receiver_job_count),
        summary_line(
            "baseline_receiver_sampled_job_count",
            baseline_receiver_sampled_job_count,
        ),
        summary_line("current_receiver_job_count", current_receiver_job_count),
        summary_line(
            "current_receiver_sampled_job_count",
            current_receiver_sampled_job_count,
        ),
        summary_line("current_mask_job_count", current_mask_job_count),
        summary_line(
            "current_mask_valid_job_count", current_mask_valid_job_count
        ),
        summary_line(
            "current_mask_sampled_job_count", current_mask_sampled_job_count
        ),
        summary_line(
            "current_mask_sparse_job_count", current_mask_sparse_job_count
        ),
        summary_line(
            "current_mask_empty_job_count", current_mask_empty_job_count
        ),
        summary_line(
            "current_mask_hierarchy_built_job_count",
            current_mask_hierarchy_built_job_count,
        ),
        summary_line(
            "current_total_occupied_tile_count", current_total_occupied_tile_count
        ),
        summary_line(
            "current_total_full_tile_count", current_total_full_tile_count
        ),
        summary_line(
            "current_aggregate_occupied_tile_ratio",
            f"{current_aggregate_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_total_hierarchy_occupied_tile_count",
            current_total_hierarchy_occupied_tile_count,
        ),
        summary_line(
            "current_total_full_hierarchy_tile_count",
            current_total_full_hierarchy_tile_count,
        ),
        summary_line(
            "current_aggregate_hierarchy_occupied_tile_ratio",
            f"{current_aggregate_hierarchy_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_total_sampled_job_occupied_tile_count",
            current_total_sampled_job_occupied_tile_count,
        ),
        summary_line(
            "current_total_sampled_job_full_tile_count",
            current_total_sampled_job_full_tile_count,
        ),
        summary_line(
            "current_sampled_aggregate_occupied_tile_ratio",
            f"{current_sampled_aggregate_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_sampled_aggregate_hierarchy_occupied_tile_ratio",
            f"{current_sampled_aggregate_hierarchy_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_min_sampled_occupied_tile_ratio",
            f"{current_min_sampled_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_max_sampled_occupied_tile_ratio",
            f"{current_max_sampled_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_min_sampled_hierarchy_occupied_tile_ratio",
            f"{current_min_sampled_hierarchy_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "current_max_sampled_hierarchy_occupied_tile_ratio",
            f"{current_max_sampled_hierarchy_occupied_tile_ratio:.6f}",
        ),
        summary_line(
            "receiver_job_count_match",
            str(baseline_receiver_job_count == current_receiver_job_count).lower(),
        ),
        summary_line(
            "receiver_sampled_job_count_match",
            str(
                baseline_receiver_sampled_job_count
                == current_receiver_sampled_job_count
            ).lower(),
        ),
        summary_line(
            "mask_has_sparse_sampled_job",
            str(current_mask_sparse_job_count > 0).lower(),
        ),
        summary_line(
            "mask_has_nonzero_occupancy",
            str(current_total_occupied_tile_count > 0).lower(),
        ),
        summary_line(
            "mask_is_below_full_coverage",
            str(current_aggregate_occupied_tile_ratio < 1.0).lower(),
        ),
        summary_line(
            "mask_hierarchy_is_below_full_coverage",
            str(current_aggregate_hierarchy_occupied_tile_ratio < 1.0).lower(),
        ),
    ]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
