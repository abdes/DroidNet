"""Compare a CSM-4 capture analysis package against the locked CSM-2 baseline."""

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
    current_receiver = load_report(
        current_stem.with_suffix(".receiver_analysis_report.txt")
    )
    current_mask_timing = load_report(
        current_stem.with_suffix(".receiver_mask_timing.txt")
    )
    current_mask = load_report(current_stem.with_suffix(".receiver_mask_report.txt"))
    current_culling_timing = load_report(
        current_stem.with_suffix(".caster_culling_timing.txt")
    )
    current_culling = load_report(
        current_stem.with_suffix(".caster_culling_report.txt")
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
    baseline_hzb_work = parse_int(baseline_hzb, "work_event_count")
    current_hzb_work = parse_int(current_hzb, "work_event_count")
    baseline_receiver_work = parse_int(
        baseline_receiver_timing, "work_event_count"
    )
    current_receiver_work = parse_int(
        current_receiver_timing, "work_event_count"
    )
    current_mask_work = parse_int(current_mask_timing, "work_event_count")
    current_culling_work = parse_int(current_culling_timing, "work_event_count")

    baseline_aux_ms = baseline_hzb_ms + baseline_receiver_ms
    current_aux_ms = (
        current_hzb_ms + current_receiver_ms + current_mask_ms + current_culling_ms
    )

    baseline_job_count = parse_int(baseline_receiver, "job_count")
    baseline_sampled_job_count = parse_int(baseline_receiver, "sampled_job_count")
    current_job_count = parse_int(current_culling, "job_count")
    current_partition_count = parse_int(current_culling, "partition_count")
    current_valid_job_count = parse_int(current_culling, "valid_job_count")
    current_sampled_job_count = parse_int(current_culling, "sampled_job_count")
    current_input_draw_record_count = parse_int(
        current_culling, "input_draw_record_count"
    )
    current_total_emitted_draw_count = parse_int(
        current_culling, "total_emitted_draw_count"
    )
    current_total_rejected_draw_count = parse_int(
        current_culling, "total_rejected_draw_count"
    )
    current_full_input_eligibility_job_count = parse_int(
        current_culling, "full_input_eligibility_job_count"
    )
    current_rejected_job_count = parse_int(current_culling, "rejected_job_count")
    current_average_eligible_draws_per_job = parse_float(
        current_culling, "average_eligible_draws_per_job"
    )
    current_average_eligible_draws_per_sampled_job = parse_float(
        current_culling, "average_eligible_draws_per_sampled_job"
    )
    current_min_eligible_draw_count = parse_int(
        current_culling, "min_eligible_draw_count"
    )
    current_max_eligible_draw_count = parse_int(
        current_culling, "max_eligible_draw_count"
    )
    current_aggregate_rejection_ratio = parse_float(
        current_culling, "aggregate_rejection_ratio"
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

    lines = [
        summary_line("comparison_profile", "conventional_shadow_csm4"),
        summary_line("baseline_stem", baseline_stem),
        summary_line("current_stem", current_stem),
        summary_line("baseline_shadow_draw_record_count", baseline_shadow_draw_record_count),
        summary_line(
            "current_benchmark_shadow_draw_record_count",
            current_benchmark_shadow_draw_record_count,
        ),
        summary_line("current_input_draw_record_count", current_input_draw_record_count),
        summary_line(
            "input_draw_record_count_matches_benchmark",
            str(current_input_draw_record_count == current_benchmark_shadow_draw_record_count).lower(),
        ),
        summary_line(
            "input_draw_record_count_matches_locked_baseline",
            str(current_input_draw_record_count == baseline_shadow_draw_record_count).lower(),
        ),
        summary_line("baseline_job_count", baseline_job_count),
        summary_line("baseline_sampled_job_count", baseline_sampled_job_count),
        summary_line("current_job_count", current_job_count),
        summary_line("current_partition_count", current_partition_count),
        summary_line("current_valid_job_count", current_valid_job_count),
        summary_line("current_sampled_job_count", current_sampled_job_count),
        summary_line("current_total_emitted_draw_count", current_total_emitted_draw_count),
        summary_line(
            "current_total_rejected_draw_count", current_total_rejected_draw_count
        ),
        summary_line(
            "current_aggregate_rejection_ratio",
            f"{current_aggregate_rejection_ratio:.6f}",
        ),
        summary_line(
            "current_average_eligible_draws_per_job",
            f"{current_average_eligible_draws_per_job:.6f}",
        ),
        summary_line(
            "current_average_eligible_draws_per_sampled_job",
            f"{current_average_eligible_draws_per_sampled_job:.6f}",
        ),
        summary_line(
            "current_average_eligible_draws_per_job_ratio_vs_baseline_input",
            f"{(current_average_eligible_draws_per_job / baseline_shadow_draw_record_count) if baseline_shadow_draw_record_count > 0 else 0.0:.6f}",
        ),
        summary_line(
            "current_average_eligible_draws_per_sampled_job_ratio_vs_baseline_input",
            f"{(current_average_eligible_draws_per_sampled_job / baseline_shadow_draw_record_count) if baseline_shadow_draw_record_count > 0 else 0.0:.6f}",
        ),
        summary_line(
            "current_min_eligible_draw_count", current_min_eligible_draw_count
        ),
        summary_line(
            "current_max_eligible_draw_count", current_max_eligible_draw_count
        ),
        summary_line(
            "current_max_eligible_draw_count_ratio_vs_baseline_input",
            f"{(float(current_max_eligible_draw_count) / float(baseline_shadow_draw_record_count)) if baseline_shadow_draw_record_count > 0 else 0.0:.6f}",
        ),
        summary_line(
            "current_full_input_eligibility_job_count",
            current_full_input_eligibility_job_count,
        ),
        summary_line("current_rejected_job_count", current_rejected_job_count),
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
        summary_line(
            "current_receiver_mask_work_event_count", current_mask_work
        ),
        summary_line(
            "current_caster_culling_gpu_duration_ms", f"{current_culling_ms:.6f}"
        ),
        summary_line(
            "current_caster_culling_work_event_count", current_culling_work
        ),
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
        summary_line(
            "baseline_prepared_shadow_bounds",
            "{},{},{},{},{}".format(*baseline_prepared_bounds),
        ),
        summary_line(
            "current_prepared_shadow_bounds",
            "{},{},{},{},{}".format(*current_prepared_bounds),
        ),
        summary_line(
            "prepared_shadow_bounds_match",
            str(baseline_prepared_bounds == current_prepared_bounds).lower(),
        ),
        summary_line(
            "baseline_directional_summary",
            "{},{},{}".format(*baseline_directional_summary),
        ),
        summary_line(
            "current_directional_summary",
            "{},{},{}".format(*current_directional_summary),
        ),
        summary_line(
            "directional_summary_match",
            str(baseline_directional_summary == current_directional_summary).lower(),
        ),
    ]

    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
