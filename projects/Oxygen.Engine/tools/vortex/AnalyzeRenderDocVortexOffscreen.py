"""RenderDoc UI analyzer for the VTX-M06B offscreen proof capture."""

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
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_vortex_offscreen_report.txt"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
COPY_NAME = "ID3D12GraphicsCommandList::CopyTextureRegion()"

STAGE9_SCOPE_NAME = "Vortex.Stage9.BasePass"
FORWARD_SCOPE_NAME = "Vortex.Stage9.BasePass.Forward"
DEFERRED_SCOPE_NAME = "Vortex.Stage9.BasePass.MainPass"
PREVIEW_COMPOSITE_SCOPE = (
    "Vortex.CompositingTask[label=M06B.OffscreenPreview.Deferred.Composite]"
)
CAPTURE_COMPOSITE_SCOPE = (
    "Vortex.CompositingTask[label=M06B.OffscreenCapture.Forward.Composite]"
)
PREVIEW_TEXTURE_NAME = "M06B.OffscreenPreview.Deferred.Color"
CAPTURE_TEXTURE_NAME = "M06B.OffscreenCapture.Forward.Color"


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


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


def float4_values(pixel_value):
    return [float(v) for v in list(pixel_value.floatValue)[:4]]


def vec_max_abs(values):
    return max(abs(v) for v in values[:4]) if values else 0.0


def vec_diff(a_values, b_values):
    return [b - a for a, b in zip(a_values[:4], b_values[:4])]


def is_nonzero_range(min_values, max_values, epsilon=1.0e-6):
    return (
        vec_max_abs(vec_diff(min_values, max_values)) > epsilon
        or vec_max_abs(max_values) > epsilon
    )


def rgb_nonzero(min_values, max_values, epsilon=1.0e-6):
    rgb_delta = [b - a for a, b in zip(min_values[:3], max_values[:3])]
    return (
        max(abs(v) for v in rgb_delta) > epsilon
        or max(abs(v) for v in max_values[:3]) > epsilon
    )


def make_subresource(rd):
    sub = rd.Subresource()
    sub.mip = 0
    sub.slice = 0
    sub.sample = 0
    return sub


def find_texture_by_name(controller, texture_name):
    names = resource_id_to_name(controller)
    for desc in controller.GetTextures():
        resource_id = safe_getattr(desc, "resourceId")
        if resource_id is None:
            continue
        if names.get(str(resource_id), "") == texture_name:
            return desc
    return None


def probe_texture(controller, rd, texture_name):
    desc = find_texture_by_name(controller, texture_name)
    if desc is None:
        return {
            "found": False,
            "nonzero": False,
            "rgb_nonzero": False,
            "min": [0.0, 0.0, 0.0, 0.0],
            "max": [0.0, 0.0, 0.0, 0.0],
        }

    resource_id = safe_getattr(desc, "resourceId")
    sub = make_subresource(rd)
    min_value, max_value = controller.GetMinMax(resource_id, sub, rd.CompType.Typeless)
    min_values = float4_values(min_value)
    max_values = float4_values(max_value)
    return {
        "found": True,
        "nonzero": is_nonzero_range(min_values, max_values),
        "rgb_nonzero": rgb_nonzero(min_values, max_values),
        "min": min_values,
        "max": max_values,
        "width": int(safe_getattr(desc, "width", 0) or 0),
        "height": int(safe_getattr(desc, "height", 0) or 0),
    }


def append_order_check(report, label, earlier_records, later_records):
    if not earlier_records or not later_records:
        ordered = False
    else:
        ordered = max(record.event_id for record in earlier_records) < min(
            record.event_id for record in later_records
        )
    report.append(f"{label}={format_bool(ordered)}")
    return ordered


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    stage9_scopes = records_with_name(action_records, STAGE9_SCOPE_NAME)
    forward_scopes = records_with_name(action_records, FORWARD_SCOPE_NAME)
    deferred_scopes = records_with_name(action_records, DEFERRED_SCOPE_NAME)
    preview_composite_scopes = records_with_name(action_records, PREVIEW_COMPOSITE_SCOPE)
    capture_composite_scopes = records_with_name(action_records, CAPTURE_COMPOSITE_SCOPE)

    stage9_records = records_under_prefix(action_records, STAGE9_SCOPE_NAME)
    forward_records = records_under_prefix(action_records, FORWARD_SCOPE_NAME)
    deferred_records = records_under_prefix(action_records, DEFERRED_SCOPE_NAME)
    preview_composite_records = records_under_prefix(action_records, PREVIEW_COMPOSITE_SCOPE)
    capture_composite_records = records_under_prefix(action_records, CAPTURE_COMPOSITE_SCOPE)

    stage9_draw_count = count_work_records(stage9_records, DRAW_NAME)
    forward_draw_count = count_work_records(forward_records, DRAW_NAME)
    deferred_draw_count = count_work_records(deferred_records, DRAW_NAME)
    preview_composite_work_count = count_work_records(
        preview_composite_records, DRAW_NAME
    ) + count_work_records(preview_composite_records, COPY_NAME)
    capture_composite_work_count = count_work_records(
        capture_composite_records, DRAW_NAME
    ) + count_work_records(capture_composite_records, COPY_NAME)

    forward_before_capture_composite = append_order_check(
        report, "forward_before_capture_composite", forward_scopes, capture_composite_scopes
    )
    deferred_before_preview_composite = append_order_check(
        report, "deferred_before_preview_composite", deferred_scopes, preview_composite_scopes
    )

    has_deferred_preview = len(deferred_scopes) >= 1 and deferred_draw_count >= 1
    has_forward_capture = len(forward_scopes) >= 1 and forward_draw_count >= 1
    has_preview_composite = (
        len(preview_composite_scopes) >= 1 and preview_composite_work_count >= 1
    )
    has_capture_composite = (
        len(capture_composite_scopes) >= 1 and capture_composite_work_count >= 1
    )
    preview_texture = probe_texture(controller, rd, PREVIEW_TEXTURE_NAME)
    capture_texture = probe_texture(controller, rd, CAPTURE_TEXTURE_NAME)

    report.append("analysis_profile=vortex_offscreen")
    report.append(f"capture_path={capture_path}")
    report.append(f"report_path={report_path}")
    report.append(f"total_actions={len(action_records)}")
    report.append(f"stage9_scope_count={len(stage9_scopes)}")
    report.append(f"stage9_draw_count={stage9_draw_count}")
    report.append(f"deferred_scope_count={len(deferred_scopes)}")
    report.append(f"deferred_draw_count={deferred_draw_count}")
    report.append(f"forward_scope_count={len(forward_scopes)}")
    report.append(f"forward_draw_count={forward_draw_count}")
    report.append(f"preview_composite_scope_count={len(preview_composite_scopes)}")
    report.append(f"preview_composite_work_count={preview_composite_work_count}")
    report.append(f"capture_composite_scope_count={len(capture_composite_scopes)}")
    report.append(f"capture_composite_work_count={capture_composite_work_count}")
    report.append(f"has_deferred_preview={format_bool(has_deferred_preview)}")
    report.append(f"has_forward_capture={format_bool(has_forward_capture)}")
    report.append(f"has_preview_composite={format_bool(has_preview_composite)}")
    report.append(f"has_capture_composite={format_bool(has_capture_composite)}")
    report.append(f"preview_texture_found={format_bool(preview_texture['found'])}")
    report.append(f"preview_texture_rgb_nonzero={format_bool(preview_texture['rgb_nonzero'])}")
    report.append(f"preview_texture_min={preview_texture['min']}")
    report.append(f"preview_texture_max={preview_texture['max']}")
    report.append(f"capture_texture_found={format_bool(capture_texture['found'])}")
    report.append(f"capture_texture_rgb_nonzero={format_bool(capture_texture['rgb_nonzero'])}")
    report.append(f"capture_texture_min={capture_texture['min']}")
    report.append(f"capture_texture_max={capture_texture['max']}")

    overall = (
        has_deferred_preview
        and has_forward_capture
        and has_preview_composite
        and has_capture_composite
        and preview_texture["found"]
        and preview_texture["rgb_nonzero"]
        and capture_texture["found"]
        and capture_texture["rgb_nonzero"]
        and forward_before_capture_composite
        and deferred_before_preview_composite
    )
    report.append(f"overall_verdict={format_bool(overall)}")


run_ui_script(REPORT_SUFFIX, build_report)
