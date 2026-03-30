r"""Deep-dive pass inspection for RenderScene RenderDoc captures.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_PASS_NAME = 'VsmProjectionPass'
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/pass_focus.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocPassFocus.py' `
        'H:/path/capture.rdc'

Environment variables
---------------------
- `OXYGEN_RENDERDOC_REPORT_PATH`: optional explicit report path.
- `OXYGEN_RENDERDOC_CAPTURE_PATH`: optional explicit capture path.
- `OXYGEN_RENDERDOC_PASS_NAME`: required pass name, for example
  `VsmProjectionPass`.
- `OXYGEN_RENDERDOC_PASS_EVENT_LIMIT`: optional number of work events to inspect.
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
    DEFAULT_PASS_EVENT_LIMIT,
    PASS_EVENT_LIMIT_ENV,
    PASS_NAME_ENV,
    RESOURCE_NAME_ENV,
    ReportWriter,
    categorized_resource_lines,
    collect_action_records,
    format_flags,
    get_required_env,
    inspect_action_resources,
    parse_int_env,
    resource_id_to_name,
    run_ui_script,
    summarize_event_ids,
    find_pass_work_records,
    find_scope_records,
)


REPORT_SUFFIX = "_pass_focus_report.txt"


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    pass_name = get_required_env(PASS_NAME_ENV, "the target pass name")
    event_limit = parse_int_env(
        PASS_EVENT_LIMIT_ENV, DEFAULT_PASS_EVENT_LIMIT
    )
    resource_filter = os.environ.get(RESOURCE_NAME_ENV, "").strip() or None

    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, pass_name)
    work_records = find_pass_work_records(action_records, pass_name)
    resource_names = resource_id_to_name(controller)

    report.append("analysis_profile=pass_focus")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(pass_name))
    report.append("resource_filter={}".format(resource_filter or "<none>"))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))
    report.append("inspected_work_limit={}".format(event_limit))
    report.blank()

    if not scope_records:
        report.append("No pass scope marker matched {}".format(pass_name))

    if not work_records:
        report.append("No work events were found under {}".format(pass_name))
        report.flush()
        return

    report.append("Pass work inspection")
    for action in work_records[:event_limit]:
        details = inspect_action_resources(controller, resource_names, action)
        report.append(
            "event {} [{}] {}".format(
                action.event_id,
                format_flags(action.flags),
                action.path,
            )
        )
        filtered_lines = categorized_resource_lines(details, resource_filter)
        if filtered_lines:
            report.extend("  " + line for line in filtered_lines)
        else:
            report.append("  <no matching resource bindings>")
        report.blank()

    if len(work_records) > event_limit:
        report.append(
            "truncated_after_events={}".format(event_limit)
        )

    report.flush()


os._exit(run_ui_script(REPORT_SUFFIX, build_report))
