r"""Deep-dive Stage 15 mask inspection for RenderScene RenderDoc captures.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/stage15_masks.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocStage15Masks.py' `
        'H:/path/capture.rdc'

Environment variables
---------------------
- `OXYGEN_RENDERDOC_REPORT_PATH`: optional explicit report path.
- `OXYGEN_RENDERDOC_CAPTURE_PATH`: optional explicit capture path.
- `OXYGEN_RENDERDOC_RESOURCE_EVENT_LIMIT`: optional number of ShaderPass work
  events to inspect while searching for Stage 15 mask consumers.
"""

import os
import sys
import builtins
from pathlib import Path
from typing import List


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

    capture_context = globals().get("pyrenderdoc")
    get_capture_filename = getattr(
        capture_context, "GetCaptureFilename", None
    )
    if callable(get_capture_filename):
        try:
            capture_path = Path(get_capture_filename()).resolve()
            candidates.append(capture_path.parent)
            candidates.extend(capture_path.parents)
        except Exception:
            pass

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        if (candidate_root / "renderdoc_ui_analysis.py").exists():
            return candidate_root

        candidate = candidate_root / "Examples" / "RenderScene"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate
    raise RuntimeError(
        "Unable to locate Examples/RenderScene from the RenderDoc script path. "
        "Launch qrenderdoc with the repository script path."
    )


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    DEFAULT_RESOURCE_EVENT_LIMIT,
    RESOURCE_EVENT_LIMIT_ENV,
    ActionRecord,
    ReportWriter,
    categorized_resource_lines,
    collect_action_records,
    collect_resource_records,
    describe_resource_record,
    find_pass_work_records,
    find_resources_by_substring,
    find_scope_records,
    format_flags,
    inspect_action_resources,
    parse_int_env,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_stage15_masks_report.txt"
STAGE15_MASKS = {
    "diagnostic_shadow_mask": "VsmProjectionPass.ShadowMask",
    "directional_shadow_mask": "VsmProjectionPass.DirectionalShadowMask",
}


def summarize_touch_events(
    resource_snippet: str,
    inspections,
    line_selector,
) -> List[ActionRecord]:
    matches = []  # type: List[ActionRecord]
    for action, details in inspections:
        if any(
            resource_snippet.lower() in line.lower()
            for line in line_selector(details)
        ):
            matches.append(action)
    return matches


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    shader_event_limit = parse_int_env(
        RESOURCE_EVENT_LIMIT_ENV, DEFAULT_RESOURCE_EVENT_LIMIT
    )
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records(controller)
    resource_names = resource_id_to_name(controller)

    projection_scopes = find_scope_records(action_records, "VsmProjectionPass")
    projection_work = find_pass_work_records(action_records, "VsmProjectionPass")
    shader_scopes = find_scope_records(action_records, "ShaderPass")
    shader_work = find_pass_work_records(action_records, "ShaderPass")

    projection_inspections = [
        (action, inspect_action_resources(controller, resource_names, action))
        for action in projection_work
    ]
    shader_inspections = [
        (action, inspect_action_resources(controller, resource_names, action))
        for action in shader_work[:shader_event_limit]
    ]

    report.append("analysis_profile=stage15_masks")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append(
        "projection_scope_events={}".format(
            summarize_event_ids(projection_scopes)
        )
    )
    report.append(
        "projection_work_events={}".format(
            summarize_event_ids(projection_work)
        )
    )
    report.append("shader_scope_events={}".format(summarize_event_ids(shader_scopes)))
    report.append("shader_work_events={}".format(summarize_event_ids(shader_work)))
    report.append("inspected_shader_work_limit={}".format(shader_event_limit))
    report.blank()

    for check_name, resource_snippet in STAGE15_MASKS.items():
        matching_resources = find_resources_by_substring(
            resource_records, resource_snippet
        )
        producer_events = summarize_touch_events(
            resource_snippet,
            projection_inspections,
            lambda details: details.writable_lines(),
        )
        consumer_events = summarize_touch_events(
            resource_snippet,
            shader_inspections,
            lambda details: details.pixel_ro + details.compute_ro,
        )

        report.append("{}:".format(check_name))
        if matching_resources:
            report.extend(
                "  resource={}".format(describe_resource_record(resource))
                for resource in matching_resources[:4]
            )
        else:
            report.append("  resource=<none>")
        report.append(
            "  projection_writers={}".format(summarize_event_ids(producer_events))
        )
        report.append(
            "  shader_consumers={}".format(summarize_event_ids(consumer_events))
        )
        report.blank()

    report.append("Projection pass details")
    for action, details in projection_inspections:
        report.append(
            "event {} [{}] {}".format(
                action.event_id,
                format_flags(action.flags),
                action.path,
            )
        )
        lines = categorized_resource_lines(details, "VsmProjectionPass.")
        if lines:
            report.extend("  " + line for line in lines)
        else:
            report.append("  <no Stage 15 bindings>")
        report.blank()

    report.append("Shader pass details")
    for action, details in shader_inspections:
        if not any(
            "VsmProjectionPass." in line
            for line in details.pixel_ro + details.compute_ro
        ):
            continue
        report.append(
            "event {} [{}] {}".format(
                action.event_id,
                format_flags(action.flags),
                action.path,
            )
        )
        lines = categorized_resource_lines(details, "VsmProjectionPass.")
        if lines:
            report.extend("  " + line for line in lines)
        else:
            report.append("  <no Stage 15 bindings>")
        report.blank()

    if len(shader_work) > shader_event_limit:
        report.append(
            "shader_work_truncated_after_events={}".format(shader_event_limit)
        )

    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
