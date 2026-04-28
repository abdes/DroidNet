"""RenderDoc UI analyzer for the VTX-M06C feature-variant proof capture."""

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


REPORT_SUFFIX = "_vortex_feature_variants_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
COPY_NAME = "ID3D12GraphicsCommandList::CopyTextureRegion()"
DISPATCH_NAME = "ID3D12GraphicsCommandList::Dispatch()"

STAGE3_SCOPE_NAME = "Vortex.Stage3.DepthPrepass"
STAGE8_SCOPE_NAME = "Vortex.Stage8.ShadowDepths"
STAGE9_SCOPE_NAMES = (
    "Vortex.Stage9.BasePass.MainPass",
    "Vortex.Stage9.BasePass.Forward",
)
STAGE12_SCOPE_NAMES = (
    "Vortex.Stage12.DeferredLighting",
    "Vortex.Stage12.DebugVisualization",
)
STAGE14_VOLUMETRIC_SCOPE = "Vortex.Stage14.VolumetricFog"
STAGE14_LOCAL_FOG_SCOPE = "Vortex.Stage14.LocalFogTiledCulling"
STAGE15_SCOPE_NAME = "Vortex.Stage15.SkyAtmosphereFog"
STAGE22_TONEMAP_SCOPE_NAME = "Vortex.PostProcess.Tonemap"
COMPOSITING_SCOPE_PREFIX = "Vortex.CompositingTask[label=Composite"
VIEW_ID_RE = re.compile(r"View\s+(\d+)")

EXPECTED_VARIANT_VIEW_COUNT = 6
EXPECTED_SCENE_LIGHTING_VARIANTS = 3


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_with_name_prefix(action_records, prefix):
    return [record for record in action_records if record.name.startswith(prefix)]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    nested_with_sep = " > " + prefix_with_sep
    return [
        record
        for record in action_records
        if record.path.startswith(prefix_with_sep) or nested_with_sep in record.path
    ]


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


def collect_named_records(action_records, names):
    records = []
    for name in names:
        records.extend(records_with_name(action_records, name))
    return records


def collect_nested_records(action_records, scopes):
    records = []
    for scope in scopes:
        records.extend(records_under_prefix(action_records, scope.name))
    return records


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    action_records = collect_action_records(controller)

    stage3_scopes = records_with_name(action_records, STAGE3_SCOPE_NAME)
    stage8_scopes = records_with_name(action_records, STAGE8_SCOPE_NAME)
    stage9_scopes = collect_named_records(action_records, STAGE9_SCOPE_NAMES)
    stage12_scopes = collect_named_records(action_records, STAGE12_SCOPE_NAMES)
    stage14_volumetric_scopes = records_with_name(action_records, STAGE14_VOLUMETRIC_SCOPE)
    stage14_local_fog_scopes = records_with_name(action_records, STAGE14_LOCAL_FOG_SCOPE)
    stage15_scopes = records_with_name(action_records, STAGE15_SCOPE_NAME)
    stage22_scopes = records_with_name(action_records, STAGE22_TONEMAP_SCOPE_NAME)
    compositing_scopes = records_with_name_prefix(action_records, COMPOSITING_SCOPE_PREFIX)

    stage3_records = collect_nested_records(action_records, stage3_scopes)
    stage8_records = collect_nested_records(action_records, stage8_scopes)
    stage9_records = collect_nested_records(action_records, stage9_scopes)
    stage12_records = collect_nested_records(action_records, stage12_scopes)
    stage14_volumetric_records = collect_nested_records(
        action_records, stage14_volumetric_scopes
    )
    stage14_local_fog_records = collect_nested_records(
        action_records, stage14_local_fog_scopes
    )
    stage15_records = collect_nested_records(action_records, stage15_scopes)
    stage22_records = collect_nested_records(action_records, stage22_scopes)
    compositing_records = collect_nested_records(action_records, compositing_scopes)

    composited_view_ids = extract_composited_view_ids(compositing_scopes)
    unique_composited_view_ids = sorted(set(composited_view_ids))

    stage3_draw_count = count_work_records(stage3_records, DRAW_NAME)
    stage8_draw_count = count_work_records(stage8_records, DRAW_NAME)
    stage9_draw_count = count_work_records(stage9_records, DRAW_NAME)
    stage12_draw_count = count_work_records(stage12_records, DRAW_NAME)
    stage14_volumetric_dispatch_count = count_work_records(
        stage14_volumetric_records, DISPATCH_NAME
    )
    stage14_local_fog_dispatch_count = count_work_records(
        stage14_local_fog_records, DISPATCH_NAME
    )
    stage15_draw_count = count_work_records(stage15_records, DRAW_NAME)
    stage22_draw_count = count_work_records(stage22_records, DRAW_NAME)
    composition_work_count = count_work_records(
        compositing_records, DRAW_NAME
    ) + count_work_records(compositing_records, COPY_NAME)

    composition_view_count_match = (
        len(unique_composited_view_ids) >= EXPECTED_VARIANT_VIEW_COUNT
    )
    distinct_composition_outputs = len(composited_view_ids) == len(
        unique_composited_view_ids
    )
    scene_lighting_scope_count_match = (
        len(stage9_scopes) == EXPECTED_SCENE_LIGHTING_VARIANTS
        and len(stage12_scopes) == EXPECTED_SCENE_LIGHTING_VARIANTS
    )
    reduced_variants_omitted_scene_lighting = len(stage9_scopes) < (
        EXPECTED_VARIANT_VIEW_COUNT
    )
    shadow_products_present = len(stage8_scopes) >= 1 and stage8_draw_count >= 1
    depth_products_present = len(stage3_scopes) >= 1 and stage3_draw_count >= 1
    no_volumetric_overrun = len(stage14_volumetric_scopes) <= 1
    stage22_scene_lighting_only = len(stage22_scopes) == EXPECTED_SCENE_LIGHTING_VARIANTS

    stage3_before_stage9 = append_interleaved_order_check(
        report, "stage3_before_stage9", stage3_scopes, stage9_scopes
    )
    stage9_before_stage12 = append_interleaved_order_check(
        report, "stage9_before_stage12", stage9_scopes, stage12_scopes
    )
    stage12_before_composition = append_order_check(
        report, "stage12_before_composition", stage12_scopes, compositing_scopes
    )

    report.append("analysis_profile=vortex_feature_variants")
    report.append(f"capture_path={capture_path}")
    report.append(f"report_path={report_path}")
    report.append(f"total_actions={len(action_records)}")
    report.append(f"expected_variant_view_count={EXPECTED_VARIANT_VIEW_COUNT}")
    report.append(
        f"expected_scene_lighting_variants={EXPECTED_SCENE_LIGHTING_VARIANTS}"
    )
    report.append(f"stage3_scope_count={len(stage3_scopes)}")
    report.append(f"stage3_draw_count={stage3_draw_count}")
    report.append(f"stage8_scope_count={len(stage8_scopes)}")
    report.append(f"stage8_draw_count={stage8_draw_count}")
    report.append(f"stage9_scope_count={len(stage9_scopes)}")
    report.append(f"stage9_draw_count={stage9_draw_count}")
    report.append(f"stage12_scope_count={len(stage12_scopes)}")
    report.append(f"stage12_draw_count={stage12_draw_count}")
    report.append(f"stage14_volumetric_scope_count={len(stage14_volumetric_scopes)}")
    report.append(f"stage14_volumetric_dispatch_count={stage14_volumetric_dispatch_count}")
    report.append(f"stage14_local_fog_scope_count={len(stage14_local_fog_scopes)}")
    report.append(f"stage14_local_fog_dispatch_count={stage14_local_fog_dispatch_count}")
    report.append(f"stage15_scope_count={len(stage15_scopes)}")
    report.append(f"stage15_draw_count={stage15_draw_count}")
    report.append(f"stage22_scope_count={len(stage22_scopes)}")
    report.append(f"stage22_draw_count={stage22_draw_count}")
    report.append(f"composition_scope_count={len(compositing_scopes)}")
    report.append(f"composition_work_count={composition_work_count}")
    report.append(f"composition_view_ids={','.join(unique_composited_view_ids)}")
    report.append(f"composition_view_count_match={format_bool(composition_view_count_match)}")
    report.append(f"distinct_composition_outputs={format_bool(distinct_composition_outputs)}")
    report.append(f"depth_products_present={format_bool(depth_products_present)}")
    report.append(f"shadow_products_present={format_bool(shadow_products_present)}")
    report.append(
        "scene_lighting_scope_count_match="
        f"{format_bool(scene_lighting_scope_count_match)}"
    )
    report.append(
        "reduced_variants_omitted_scene_lighting="
        f"{format_bool(reduced_variants_omitted_scene_lighting)}"
    )
    report.append(f"no_volumetric_overrun={format_bool(no_volumetric_overrun)}")
    report.append(f"stage22_scene_lighting_only={format_bool(stage22_scene_lighting_only)}")

    overall = (
        composition_view_count_match
        and distinct_composition_outputs
        and composition_work_count >= EXPECTED_VARIANT_VIEW_COUNT
        and depth_products_present
        and shadow_products_present
        and scene_lighting_scope_count_match
        and reduced_variants_omitted_scene_lighting
        and no_volumetric_overrun
        and stage22_scene_lighting_only
        and stage3_before_stage9
        and stage9_before_stage12
        and stage12_before_composition
    )
    report.append(f"overall_verdict={format_bool(overall)}")


run_ui_script(REPORT_SUFFIX, build_report)
