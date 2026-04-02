"""Compare a phase capture analysis package against the canonical baseline."""

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


def load_report(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    return parse_key_value_report(path)


def summary_line(name: str, value) -> str:
    return f"{name}={value}"


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
    current_shadow = load_report(current_stem.with_suffix(".shadow_timing.txt"))
    current_shader = load_report(current_stem.with_suffix(".shader_timing.txt"))
    current_receiver_timing = load_report(
        current_stem.with_suffix(".receiver_analysis_timing.txt")
    )
    current_receiver = load_report(
        current_stem.with_suffix(".receiver_analysis_report.txt")
    )

    baseline_shadow_ms = parse_float(
        baseline_shadow, "authoritative_scope_gpu_duration_ms"
    )
    baseline_shader_ms = parse_float(
        baseline_shader, "authoritative_scope_gpu_duration_ms"
    )
    current_shadow_ms = parse_float(
        current_shadow, "authoritative_scope_gpu_duration_ms"
    )
    current_shader_ms = parse_float(
        current_shader, "authoritative_scope_gpu_duration_ms"
    )
    current_receiver_ms = parse_float(
        current_receiver_timing, "authoritative_scope_gpu_duration_ms"
    )

    baseline_shadow_work = parse_int(baseline_shadow, "work_event_count")
    current_shadow_work = parse_int(current_shadow, "work_event_count")
    baseline_shader_work = parse_int(baseline_shader, "work_event_count")
    current_shader_work = parse_int(current_shader, "work_event_count")

    min_first_three_ratio = parse_float(
        current_receiver, "min_first_three_full_area_ratio"
    )
    min_full_area_ratio = parse_float(current_receiver, "min_full_area_ratio")
    min_full_depth_ratio = parse_float(current_receiver, "min_full_depth_ratio")
    sampled_job_count = parse_int(current_receiver, "sampled_job_count")
    job_count = parse_int(current_receiver, "job_count")

    shadow_delta = current_shadow_ms - baseline_shadow_ms
    shader_delta = current_shader_ms - baseline_shader_ms
    shadow_delta_pct = (
        (shadow_delta / baseline_shadow_ms) * 100.0 if baseline_shadow_ms > 0.0 else 0.0
    )
    shader_delta_pct = (
        (shader_delta / baseline_shader_ms) * 100.0 if baseline_shader_ms > 0.0 else 0.0
    )
    near_cascade_tightened = min_first_three_ratio > 0.0 and min_first_three_ratio < 0.95
    structural_shadow_match = baseline_shadow_work == current_shadow_work
    structural_shader_match = baseline_shader_work == current_shader_work

    lines = [
        summary_line("comparison_profile", "conventional_shadow_csm2"),
        summary_line("baseline_stem", baseline_stem),
        summary_line("current_stem", current_stem),
        summary_line("baseline_shadow_gpu_duration_ms", f"{baseline_shadow_ms:.6f}"),
        summary_line("current_shadow_gpu_duration_ms", f"{current_shadow_ms:.6f}"),
        summary_line("shadow_gpu_duration_delta_ms", f"{shadow_delta:.6f}"),
        summary_line("shadow_gpu_duration_delta_percent", f"{shadow_delta_pct:.6f}"),
        summary_line("baseline_shadow_work_event_count", baseline_shadow_work),
        summary_line("current_shadow_work_event_count", current_shadow_work),
        summary_line("shadow_work_event_match", str(structural_shadow_match).lower()),
        summary_line("baseline_shader_gpu_duration_ms", f"{baseline_shader_ms:.6f}"),
        summary_line("current_shader_gpu_duration_ms", f"{current_shader_ms:.6f}"),
        summary_line("shader_gpu_duration_delta_ms", f"{shader_delta:.6f}"),
        summary_line("shader_gpu_duration_delta_percent", f"{shader_delta_pct:.6f}"),
        summary_line("baseline_shader_work_event_count", baseline_shader_work),
        summary_line("current_shader_work_event_count", current_shader_work),
        summary_line("shader_work_event_match", str(structural_shader_match).lower()),
        summary_line(
            "current_receiver_analysis_gpu_duration_ms", f"{current_receiver_ms:.6f}"
        ),
        summary_line("current_receiver_job_count", job_count),
        summary_line("current_receiver_sampled_job_count", sampled_job_count),
        summary_line(
            "current_min_first_three_full_area_ratio", f"{min_first_three_ratio:.6f}"
        ),
        summary_line("current_min_full_area_ratio", f"{min_full_area_ratio:.6f}"),
        summary_line("current_min_full_depth_ratio", f"{min_full_depth_ratio:.6f}"),
        summary_line("near_cascade_tightened_vs_full_rect", str(near_cascade_tightened).lower()),
    ]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
