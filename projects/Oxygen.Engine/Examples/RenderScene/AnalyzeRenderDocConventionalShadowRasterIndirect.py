r"""RenderDoc UI proof script for CSM-5 counted-indirect conventional raster.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/raster_indirect_report.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocConventionalShadowRasterIndirect.py' `
        'H:/path/capture.rdc'
"""

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

    capture_context = globals().get("pyrenderdoc")
    get_capture_filename = getattr(capture_context, "GetCaptureFilename", None)
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
    ReportWriter,
    collect_action_records,
    find_pass_work_records,
    find_scope_records,
    renderdoc_module,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_raster_indirect_report.txt"
PASS_NAME = "ConventionalShadowRasterPass"
JOB_SCOPE_PREFIX = "ConventionalShadowRasterPass.Job["
INDIRECT_MARKER_NAME = "ConventionalShadowRasterPass.ExecuteIndirectCounted"
EXECUTE_INDIRECT_PATH_TOKEN = "ExecuteIndirect("


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    pass_scope_records = find_scope_records(action_records, PASS_NAME)
    pass_work_records = find_pass_work_records(action_records, PASS_NAME)

    job_scope_records = [
        record
        for record in action_records
        if record.pass_name == PASS_NAME and record.name.startswith(JOB_SCOPE_PREFIX)
    ]
    marker_records = [
        record
        for record in action_records
        if record.pass_name == PASS_NAME and record.name == INDIRECT_MARKER_NAME
    ]
    clear_records = [
        record for record in pass_work_records if record.flags & rd.ActionFlags.Clear
    ]
    draw_records = [
        record
        for record in pass_work_records
        if record.flags & rd.ActionFlags.Drawcall
    ]
    indirect_draw_records = [
        record
        for record in draw_records
        if EXECUTE_INDIRECT_PATH_TOKEN in record.path
    ]
    execute_indirect_path_records = [
        record
        for record in pass_work_records
        if EXECUTE_INDIRECT_PATH_TOKEN in record.path
    ]

    if not indirect_draw_records:
        raise RuntimeError(
            "No counted-indirect drawcall work events were found for {}".format(
                PASS_NAME
            )
        )
    if len(indirect_draw_records) != len(draw_records):
        raise RuntimeError(
            "Not all drawcall work events for {} were emitted beneath "
            "ExecuteIndirect nodes".format(PASS_NAME)
        )

    report.append("analysis_profile=conventional_shadow_raster_indirect")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(pass_scope_records)))
    report.append("scope_event_count={}".format(len(pass_scope_records)))
    report.append("job_scope_events={}".format(summarize_event_ids(job_scope_records)))
    report.append("job_scope_count={}".format(len(job_scope_records)))
    report.append("indirect_marker_name={}".format(INDIRECT_MARKER_NAME))
    report.append("indirect_marker_events={}".format(summarize_event_ids(marker_records)))
    report.append("indirect_marker_event_count={}".format(len(marker_records)))
    report.append(
        "execute_indirect_path_event_count={}".format(
            len(execute_indirect_path_records)
        )
    )
    report.append("clear_work_event_count={}".format(len(clear_records)))
    report.append("draw_work_event_count={}".format(len(draw_records)))
    report.append(
        "indirect_draw_work_event_count={}".format(len(indirect_draw_records))
    )
    report.append("total_work_event_count={}".format(len(pass_work_records)))
    report.append(
        "all_draw_work_events_are_indirect={}".format(
            str(len(draw_records) == len(indirect_draw_records)).lower()
        )
    )
    report.blank()
    report.append("indirect_marker_detail:")
    for record in marker_records:
        report.append("{}|{}|{}".format(record.event_id, record.name, record.path))
    report.blank()
    report.append("draw_work_detail:")
    for record in draw_records:
        report.append("{}|{}|{}".format(record.event_id, record.name, record.path))
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
