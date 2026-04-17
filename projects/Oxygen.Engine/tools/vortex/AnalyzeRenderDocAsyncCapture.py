"""RenderDoc UI structural analyzer for the Async runtime capture."""

import builtins
import sys
from pathlib import Path


def resolve_script_dir():
    script_candidates = []
    current_file = globals().get("__file__")
    if current_file:
        script_candidates.append(Path(current_file))

    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.suffix.lower() == ".py":
            script_candidates.append(candidate)

    candidates = []
    for script_candidate in script_candidates:
        resolved = script_candidate.resolve()
        candidates.append(resolved.parent)
        candidates.extend(resolved.parents)

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        candidate = candidate_root / "tools" / "shadows"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

    raise RuntimeError(
        "Unable to locate tools/shadows from the RenderDoc script path. "
        "Launch qrenderdoc with the repository script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_action_records,
    is_work_action,
    run_ui_script,
)


REPORT_SUFFIX = "_async_capture_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"

STAGE3_SCOPE_NAME = "Vortex.Stage3.DepthPrepass"
STAGE8_SCOPE_NAME = "Vortex.Stage8.ShadowDepths"
STAGE12_SCOPE_NAME = "Vortex.Stage12.DeferredLighting"
STAGE12_DIRECTIONAL_SCOPE_NAME = "Vortex.Stage12.DirectionalLight"
STAGE12_SPOT_SCOPE_NAME = "Vortex.Stage12.SpotLight"
STAGE15_SKY_SCOPE_NAME = "Vortex.Stage15.Sky"
STAGE15_ATMOSPHERE_SCOPE_NAME = "Vortex.Stage15.Atmosphere"
STAGE15_FOG_SCOPE_NAME = "Vortex.Stage15.Fog"
STAGE22_TONEMAP_SCOPE_NAME = "Vortex.PostProcess.Tonemap"
COMPOSITING_SCOPE_PREFIX = "Vortex.CompositingTask[label=Composite Copy View "


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_with_name_prefix(action_records, prefix):
    return [record for record in action_records if record.name.startswith(prefix)]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [
        record
        for record in action_records
        if record.path.startswith(prefix_with_sep)
    ]


def count_named_records(action_records, name):
    return sum(1 for record in action_records if record.name == name)


def append_presence_check(report, label, count):
    present = count > 0
    report.append("{}_count={}".format(label, count))
    report.append("{}_present={}".format(label, "true" if present else "false"))
    if not present:
        raise RuntimeError("{} missing from Async capture".format(label))


def append_positive_count_check(report, label, count):
    present = count > 0
    report.append("{}={}".format(label, count))
    report.append("{}_present={}".format(label, "true" if present else "false"))
    if not present:
        raise RuntimeError("{} expected at least one draw".format(label))


def append_order_check(report, label, ordered_event_ids):
    is_ordered = all(
        ordered_event_ids[index] < ordered_event_ids[index + 1]
        for index in range(len(ordered_event_ids) - 1)
    )
    report.append("{}_event_ids={}".format(label, ",".join(str(v) for v in ordered_event_ids)))
    report.append("{}_valid={}".format(label, "true" if is_ordered else "false"))
    if not is_ordered:
        raise RuntimeError(
            "{} mismatch: event ids are not strictly increasing ({})".format(
                label, ordered_event_ids
            )
        )


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    action_records = collect_action_records(controller)

    stage3_scope = records_with_name(action_records, STAGE3_SCOPE_NAME)
    stage8_scope = records_with_name(action_records, STAGE8_SCOPE_NAME)
    stage12_scope = records_with_name(action_records, STAGE12_SCOPE_NAME)
    stage12_directional_scope = records_with_name(
        action_records, STAGE12_DIRECTIONAL_SCOPE_NAME
    )
    stage12_spot_scope = records_with_name(action_records, STAGE12_SPOT_SCOPE_NAME)
    stage15_sky_scope = records_with_name(action_records, STAGE15_SKY_SCOPE_NAME)
    stage15_atmosphere_scope = records_with_name(
        action_records, STAGE15_ATMOSPHERE_SCOPE_NAME
    )
    stage15_fog_scope = records_with_name(action_records, STAGE15_FOG_SCOPE_NAME)
    stage22_tonemap_scope = records_with_name(
        action_records, STAGE22_TONEMAP_SCOPE_NAME
    )
    compositing_scope = records_with_name_prefix(
        action_records, COMPOSITING_SCOPE_PREFIX
    )

    stage3_records = records_under_prefix(action_records, STAGE3_SCOPE_NAME)
    stage8_records = records_under_prefix(action_records, STAGE8_SCOPE_NAME)
    stage12_directional_records = records_under_prefix(
        action_records, STAGE12_SCOPE_NAME + " > " + STAGE12_DIRECTIONAL_SCOPE_NAME
    )
    stage12_spot_records = records_under_prefix(
        action_records, STAGE12_SCOPE_NAME + " > " + STAGE12_SPOT_SCOPE_NAME
    )
    stage15_sky_records = records_under_prefix(action_records, STAGE15_SKY_SCOPE_NAME)
    stage15_atmosphere_records = records_under_prefix(
        action_records, STAGE15_ATMOSPHERE_SCOPE_NAME
    )
    stage15_fog_records = records_under_prefix(action_records, STAGE15_FOG_SCOPE_NAME)
    stage22_tonemap_records = records_under_prefix(
        action_records, STAGE22_TONEMAP_SCOPE_NAME
    )
    compositing_records = []
    for record in compositing_scope:
        compositing_records.extend(records_under_prefix(action_records, record.name))

    post_compositing_draws = [
        record
        for record in action_records
        if record.event_id > compositing_scope[-1].event_id
        and record.name == DRAW_NAME
        and is_work_action(record.flags)
    ] if compositing_scope else []
    stage22_fallback_draws = [
        record
        for record in action_records
        if record.name == DRAW_NAME
        and is_work_action(record.flags)
        and stage15_fog_scope[0].event_id < record.event_id < compositing_scope[0].event_id
    ] if stage15_fog_scope and compositing_scope else []
    stage22_anchor_event_id = (
        stage22_tonemap_scope[0].event_id
        if stage22_tonemap_scope
        else (stage22_fallback_draws[0].event_id if stage22_fallback_draws else 0)
    )

    report.append("analysis_profile=async_runtime_capture")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("total_actions={}".format(len(action_records)))

    append_presence_check(report, "stage3_scope", len(stage3_scope))
    append_presence_check(report, "stage8_scope", len(stage8_scope))
    append_presence_check(report, "stage12_scope", len(stage12_scope))
    append_presence_check(report, "stage12_directional_scope", len(stage12_directional_scope))
    append_presence_check(report, "stage12_spot_scope", len(stage12_spot_scope))
    append_presence_check(report, "stage15_sky_scope", len(stage15_sky_scope))
    append_presence_check(
        report, "stage15_atmosphere_scope", len(stage15_atmosphere_scope)
    )
    append_presence_check(report, "stage15_fog_scope", len(stage15_fog_scope))
    report.append(
        "stage22_tonemap_scope_mode={}".format(
            "explicit" if stage22_tonemap_scope else "fallback_unscoped"
        )
    )
    append_presence_check(
        report, "stage22_tonemap_scope", len(stage22_tonemap_scope) or len(stage22_fallback_draws)
    )
    append_presence_check(report, "compositing_scope", len(compositing_scope))

    append_positive_count_check(
        report, "stage3_draw_count", count_named_records(stage3_records, DRAW_NAME)
    )
    append_positive_count_check(
        report, "stage8_draw_count", count_named_records(stage8_records, DRAW_NAME)
    )
    append_positive_count_check(
        report,
        "stage12_directional_draw_count",
        count_named_records(stage12_directional_records, DRAW_NAME),
    )
    append_positive_count_check(
        report,
        "stage12_spot_draw_count",
        count_named_records(stage12_spot_records, DRAW_NAME),
    )
    append_positive_count_check(
        report,
        "stage15_sky_draw_count",
        count_named_records(stage15_sky_records, DRAW_NAME),
    )
    append_positive_count_check(
        report,
        "stage15_atmosphere_draw_count",
        count_named_records(stage15_atmosphere_records, DRAW_NAME),
    )
    append_positive_count_check(
        report,
        "stage15_fog_draw_count",
        count_named_records(stage15_fog_records, DRAW_NAME),
    )
    append_positive_count_check(
        report,
        "stage22_tonemap_draw_count",
        count_named_records(stage22_tonemap_records, DRAW_NAME)
        or len(stage22_fallback_draws),
    )
    append_positive_count_check(
        report,
        "compositing_draw_count",
        count_named_records(compositing_records, DRAW_NAME),
    )

    report.append(
        "post_compositing_draw_count={}".format(len(post_compositing_draws))
    )
    report.append(
        "post_compositing_draw_present={}".format(
            "true" if len(post_compositing_draws) > 0 else "false"
        )
    )

    append_order_check(
        report,
        "async_runtime_stage_order",
        [
            stage3_scope[0].event_id,
            stage8_scope[0].event_id,
            stage12_scope[0].event_id,
            stage15_sky_scope[0].event_id,
            stage22_anchor_event_id,
            compositing_scope[0].event_id,
        ],
    )

    interesting_keywords = (
        "vortex.stage3",
        "vortex.stage8",
        "vortex.stage12",
        "vortex.stage15",
        "tonemap",
        "composit",
        "imgui",
    )
    interesting = []
    for record in action_records:
        haystack = "{} {}".format(record.name, record.path).lower()
        if any(keyword in haystack for keyword in interesting_keywords):
            interesting.append(record)

    for index, record in enumerate(interesting[:120], start=1):
        report.append(
            "action_{:03d}=event:{} flags:{} name:{} path:{}".format(
                index, record.event_id, record.flags, record.name, record.path
            )
        )

    report.append("overall_verdict=pass")


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
