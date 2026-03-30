r"""Deep-dive single-event inspection for RenderScene RenderDoc captures.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_EVENT_ID = '14967'
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/event_focus.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocEventFocus.py' `
        'H:/path/capture.rdc'

Environment variables
---------------------
- `OXYGEN_RENDERDOC_REPORT_PATH`: optional explicit report path.
- `OXYGEN_RENDERDOC_CAPTURE_PATH`: optional explicit capture path.
- `OXYGEN_RENDERDOC_EVENT_ID`: required event id.
- `OXYGEN_RENDERDOC_RESOURCE_NAME`: optional substring filter applied to
  reported resource bindings.
"""

import os
import sys
import builtins
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
    EVENT_ID_ENV,
    RESOURCE_NAME_ENV,
    ReportWriter,
    categorized_resource_lines,
    collect_action_records,
    find_event_record,
    format_flags,
    get_required_env,
    inspect_action_resources,
    resource_id_to_name,
    run_ui_script,
)


REPORT_SUFFIX = "_event_focus_report.txt"


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    event_id = int(get_required_env(EVENT_ID_ENV, "the target event id"))
    resource_filter = os.environ.get(RESOURCE_NAME_ENV, "").strip() or None

    action_records = collect_action_records(controller)
    event_record = find_event_record(action_records, event_id)

    report.append("analysis_profile=event_focus")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("event_id={}".format(event_id))
    report.append("resource_filter={}".format(resource_filter or "<none>"))
    report.blank()

    if event_record is None:
        report.append("event_status=not_found")
        report.flush()
        return

    resource_names = resource_id_to_name(controller)
    details = inspect_action_resources(controller, resource_names, event_record)

    report.append("event_status=found")
    report.append("event_name={}".format(event_record.name))
    report.append("pass_name={}".format(event_record.pass_name or "<none>"))
    report.append("flags={}".format(format_flags(event_record.flags)))
    report.append("path={}".format(event_record.path))
    report.blank()
    report.append("Event resource bindings")

    filtered_lines = categorized_resource_lines(details, resource_filter)
    if filtered_lines:
        report.extend(filtered_lines)
    else:
        report.append("<no matching resource bindings>")
    report.flush()


os._exit(run_ui_script(REPORT_SUFFIX, build_report))
