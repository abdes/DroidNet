"""RenderDoc UI analyzer for the VTX-M06A multi-view proof capture."""

import builtins
import re
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

    raise RuntimeError("Unable to locate tools/shadows from the RenderDoc script path.")


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


REPORT_SUFFIX = "_vortex_multiview_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
COPY_NAME = "ID3D12GraphicsCommandList::CopyTextureRegion()"

STAGE3_SCOPE_NAME = "Vortex.Stage3.DepthPrepass"
STAGE9_SCOPE_NAME = "Vortex.Stage9.BasePass"
STAGE12_SCOPE_NAMES = (
    "Vortex.Stage12.DeferredLighting",
    "Vortex.Stage12.DebugVisualization",
)
STAGE22_SCOPE_NAME = "Vortex.PostProcess.Tonemap"
COMPOSITING_SCOPE_PREFIX = "Vortex.CompositingTask[label=Composite"
AUX_CONSUME_SCOPE_PREFIX = "Vortex.AuxView.Consume"
VIEW_ID_RE = re.compile(r"View\s+(\d+)")
EXPECTED_PROOF_VIEW_COUNT = 4
EXPECTED_PROOF_SCENE_PASS_VIEW_COUNT = 3


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_with_name_prefix(action_records, prefix):
    return [record for record in action_records if record.name.startswith(prefix)]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def count_work_records(records, name):
    return sum(1 for record in records if record.name == name and is_work_action(record.flags))


def format_bool(value):
    return "true" if value else "false"


def extract_composited_view_ids(compositing_scopes):
    ids = []
    for scope in compositing_scopes:
        match = VIEW_ID_RE.search(scope.name)
        if match:
            ids.append(match.group(1))
    return ids


def append_order_check(report, label, earlier_records, later_records):
    if not earlier_records or not later_records:
        ordered = False
    else:
        ordered = max(record.event_id for record in earlier_records) < min(
            record.event_id for record in later_records
        )
    report.append(f"{label}={format_bool(ordered)}")
    return ordered


def append_any_earlier_check(report, label, earlier_records, later_records):
    if not earlier_records or not later_records:
        ordered = False
    else:
        ordered = min(record.event_id for record in earlier_records) < min(
            record.event_id for record in later_records
        )
    report.append(f"{label}={format_bool(ordered)}")
    return ordered


def append_interleaved_order_check(report, label, earlier_records, later_records):
    earlier_events = sorted(record.event_id for record in earlier_records)
    later_events = sorted(record.event_id for record in later_records)
    matched_pairs = 0
    previous_later_event = -1

    for later_event in later_events:
        matching_earlier = [
            event
            for event in earlier_events
            if previous_later_event < event < later_event
        ]
        if matching_earlier:
            matched_pairs += 1
            previous_later_event = later_event

    expected_pairs = min(len(earlier_events), len(later_events))
    ordered = expected_pairs > 0 and matched_pairs == expected_pairs
    report.append(f"{label}={format_bool(ordered)}")
    report.append(f"{label}_matched_pairs={matched_pairs}")
    report.append(f"{label}_expected_pairs={expected_pairs}")
    return ordered


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    action_records = collect_action_records(controller)
    stage3_scopes = records_with_name(action_records, STAGE3_SCOPE_NAME)
    stage9_scopes = records_with_name(action_records, STAGE9_SCOPE_NAME)
    stage12_scopes = []
    for name in STAGE12_SCOPE_NAMES:
        stage12_scopes.extend(records_with_name(action_records, name))
    stage22_scopes = records_with_name(action_records, STAGE22_SCOPE_NAME)
    compositing_scopes = records_with_name_prefix(action_records, COMPOSITING_SCOPE_PREFIX)
    aux_consume_scopes = records_with_name_prefix(action_records, AUX_CONSUME_SCOPE_PREFIX)

    stage3_records = records_under_prefix(action_records, STAGE3_SCOPE_NAME)
    stage9_records = records_under_prefix(action_records, STAGE9_SCOPE_NAME)
    compositing_records = []
    for scope in compositing_scopes:
        compositing_records.extend(records_under_prefix(action_records, scope.name))
    aux_consume_records = []
    for scope in aux_consume_scopes:
        aux_consume_records.extend(records_under_prefix(action_records, scope.name))

    composited_view_ids = extract_composited_view_ids(compositing_scopes)
    unique_composited_view_ids = sorted(set(composited_view_ids))

    stage3_view_count_match = len(stage3_scopes) >= EXPECTED_PROOF_SCENE_PASS_VIEW_COUNT
    stage9_view_count_match = len(stage9_scopes) >= EXPECTED_PROOF_VIEW_COUNT
    stage12_view_count_match = len(stage12_scopes) >= EXPECTED_PROOF_SCENE_PASS_VIEW_COUNT
    composition_view_count_match = len(unique_composited_view_ids) >= EXPECTED_PROOF_VIEW_COUNT
    distinct_composition_outputs = len(composited_view_ids) == len(unique_composited_view_ids)
    composition_work_count = count_work_records(compositing_records, DRAW_NAME) + count_work_records(
        compositing_records, COPY_NAME
    )
    aux_consume_work_count = count_work_records(aux_consume_records, COPY_NAME)
    aux_consume_scope_match = len(aux_consume_scopes) >= 1
    aux_consume_work_match = aux_consume_work_count >= 1

    report.append("analysis_profile=vortex_multiview")
    report.append(f"capture_path={capture_path}")
    report.append(f"report_path={report_path}")
    report.append(f"total_actions={len(action_records)}")
    report.append(f"expected_proof_view_count={EXPECTED_PROOF_VIEW_COUNT}")
    report.append(
        f"expected_proof_scene_pass_view_count={EXPECTED_PROOF_SCENE_PASS_VIEW_COUNT}"
    )
    report.append(f"stage3_scope_count={len(stage3_scopes)}")
    report.append(f"stage3_view_count_match={format_bool(stage3_view_count_match)}")
    report.append(f"stage9_scope_count={len(stage9_scopes)}")
    report.append(f"stage9_view_count_match={format_bool(stage9_view_count_match)}")
    report.append(f"stage12_scope_count={len(stage12_scopes)}")
    report.append(f"stage12_view_count_match={format_bool(stage12_view_count_match)}")
    report.append(f"stage22_scope_count={len(stage22_scopes)}")
    report.append(f"composition_scope_count={len(compositing_scopes)}")
    report.append(f"composition_view_count_match={format_bool(composition_view_count_match)}")
    report.append(f"composition_view_ids={','.join(unique_composited_view_ids)}")
    report.append(f"distinct_composition_outputs={format_bool(distinct_composition_outputs)}")
    report.append(f"composition_work_count={composition_work_count}")
    report.append(f"aux_consume_scope_count={len(aux_consume_scopes)}")
    report.append(f"aux_consume_scope_match={format_bool(aux_consume_scope_match)}")
    report.append(f"aux_consume_copy_count={aux_consume_work_count}")
    report.append(f"aux_consume_work_match={format_bool(aux_consume_work_match)}")
    report.append(f"stage3_draw_count={count_work_records(stage3_records, DRAW_NAME)}")
    report.append(f"stage9_draw_count={count_work_records(stage9_records, DRAW_NAME)}")

    stage3_before_stage9 = append_interleaved_order_check(
        report, "stage3_before_stage9", stage3_scopes, stage9_scopes
    )
    stage9_before_composition = append_order_check(
        report, "stage9_before_composition", stage9_scopes, compositing_scopes
    )
    if stage22_scopes:
        stage22_before_composition = append_order_check(
            report, "stage22_before_composition", stage22_scopes, compositing_scopes
        )
    else:
        stage22_before_composition = True
        report.append("stage22_before_composition=not_applicable")
    stage9_before_aux_consume = append_any_earlier_check(
        report, "stage9_before_aux_consume", stage9_scopes, aux_consume_scopes
    )
    aux_consume_before_composition = append_order_check(
        report, "aux_consume_before_composition", aux_consume_scopes, compositing_scopes
    )

    overall = (
        stage3_view_count_match
        and stage9_view_count_match
        and stage12_view_count_match
        and composition_view_count_match
        and distinct_composition_outputs
        and composition_work_count >= 4
        and aux_consume_scope_match
        and aux_consume_work_match
        and stage3_before_stage9
        and stage9_before_composition
        and stage22_before_composition
        and stage9_before_aux_consume
        and aux_consume_before_composition
    )
    report.append(f"overall_verdict={format_bool(overall)}")


run_ui_script(REPORT_SUFFIX, build_report)
