r"""Generic RenderDoc UI pass timing analysis.

Runs only inside RenderDoc UI via:
    qrenderdoc.exe --ui-python tools/shadows/AnalyzeRenderDocPassTiming.py <capture.rdc>
"""

import builtins
import sys
from pathlib import Path


def _script_path():
    current_file = globals().get("__file__")
    if current_file:
        return Path(current_file).resolve()

    # RenderDoc UI Python may not define `__file__` for entry scripts.
    # Do not "simplify" this to an unconditional __file__ access unless
    # qrenderdoc has been verified to provide it.
    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.name == "AnalyzeRenderDocPassTiming.py":
            return candidate.resolve()

    fallback = Path.cwd() / "tools" / "shadows" / "AnalyzeRenderDocPassTiming.py"
    if fallback.exists():
        return fallback.resolve()
    raise RuntimeError("Unable to resolve AnalyzeRenderDocPassTiming.py")


SCRIPT_DIR = _script_path().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc

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
PASS_NAME = get_required_env(PASS_NAME_ENV, "RenderDoc pass name")


def gpu_duration_lookup(controller):
    rd = renderdoc_module()
    counter_results = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    return dict(
        (result.eventId, result.value.d * 1000.0) for result in counter_results
    )


def build_report(controller, report, capture_path, report_path):
    action_records = collect_action_records(controller)
    scope_records = find_scope_records(action_records, PASS_NAME)
    work_records = find_pass_work_records(action_records, PASS_NAME)
    durations = gpu_duration_lookup(controller)

    report.append("analysis_profile=generic_pass_timing")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("pass_name={}".format(PASS_NAME))
    report.append("scope_events={}".format(summarize_event_ids(scope_records)))
    report.append("work_events={}".format(summarize_event_ids(work_records)))

    if not work_records:
        raise RuntimeError("No work events found for {}".format(PASS_NAME))

    total_gpu_duration_ms = 0.0
    for action in work_records:
        total_gpu_duration_ms += durations.get(action.event_id, 0.0)

    report.append("work_event_count={}".format(len(work_records)))
    report.append("total_gpu_duration_ms={:.6f}".format(total_gpu_duration_ms))
    report.blank()
    report.append("work_events_detail:")
    for action in work_records:
        report.append(
            "{}|{:.6f}|{}|{}".format(
                action.event_id,
                durations.get(action.event_id, 0.0),
                action.name,
                action.path,
            )
        )
    report.flush()


RUN_EXIT_CODE = run_ui_script(REPORT_SUFFIX, build_report)
