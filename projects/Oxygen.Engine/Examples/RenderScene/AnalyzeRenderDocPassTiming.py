r"""Deep-dive GPU timing for a specific RenderDoc pass.

Run only inside RenderDoc UI:

PowerShell:
    $env:OXYGEN_RENDERDOC_PASS_NAME = 'VsmStaticDynamicMergePass'
    $env:OXYGEN_RENDERDOC_REPORT_PATH = 'H:/path/pass_timing.txt'
    & 'C:/Program Files/RenderDoc/qrenderdoc.exe' --ui-python `
        'H:/projects/DroidNet/projects/Oxygen.Engine/Examples/RenderScene/AnalyzeRenderDocPassTiming.py' `
        'H:/path/capture.rdc'
"""

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
    PASS_NAME_ENV,
    ReportWriter,
    collect_action_records,
    find_pass_work_records,
    find_scope_records,
    get_required_env,
    renderdoc_module,
    run_ui_script,
    summarize_event_ids,
)


REPORT_SUFFIX = "_pass_timing_report.txt"


def build_report(
    controller, report: ReportWriter, capture_path: Path, report_path: Path
) -> None:
    rd = renderdoc_module()
    pass_name = get_required_env(PASS_NAME_ENV, "the target pass name")
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, pass_name)
    work_records = find_pass_work_records(action_records, pass_name)
    counter_results = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    durations = {result.eventId: result.value.d for result in counter_results}

    total_ms = sum(durations.get(action.event_id, 0.0) for action in work_records)
    total_ms *= 1000.0

    report.append("analysis_profile=pass_timing")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(pass_name))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_event_count={}".format(len(work_records)))
    report.append("total_gpu_duration_ms={:.6f}".format(total_ms))
    report.blank()
    report.append("work_events:")
    for action in work_records:
        duration_ms = durations.get(action.event_id, 0.0) * 1000.0
        report.append(
            "{}|{:.6f}|{}|{}".format(
                action.event_id, duration_ms, action.name, action.path
            )
        )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
